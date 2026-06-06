#include "wifi_prov.hpp"

#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"

namespace diag {
namespace {
constexpr char kTag[] = "wifi";
constexpr char kHostname[] = "silent-powmr-diag";  // shows up in DHCP leases
constexpr char kApSsid[] = "silent_powmr-setup";
constexpr char kApPass[] = "powmr-setup";  // >= 8 chars for WPA2
constexpr char kNvsNs[] = "wifi";
constexpr int kBootButtonGpio = 0;
constexpr int kStaMaxRetry = 6;
constexpr int kConnectedBit = 1 << 0;
constexpr int kFailBit = 1 << 1;

EventGroupHandle_t s_events = nullptr;
int s_retry = 0;
esp_netif_t* s_sta_netif = nullptr;
esp_netif_t* s_ap_netif = nullptr;

void on_event(void*, esp_event_base_t base, int32_t id, void*) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_netif_set_hostname(s_sta_netif, kHostname);  // (re)apply before DHCP
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry < kStaMaxRetry) {
      ++s_retry;
      esp_wifi_connect();
    } else {
      xEventGroupSetBits(s_events, kFailBit);
    }
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    s_retry = 0;
    xEventGroupSetBits(s_events, kConnectedBit);
  }
}

bool boot_button_held() {
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << kBootButtonGpio;
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&io);
  return gpio_get_level(static_cast<gpio_num_t>(kBootButtonGpio)) == 0;
}

bool load_creds(char* ssid, size_t sl, char* pass, size_t pl) {
  nvs_handle_t h;
  if (nvs_open(kNvsNs, NVS_READONLY, &h) != ESP_OK) return false;
  size_t l1 = sl, l2 = pl;
  const esp_err_t e1 = nvs_get_str(h, "ssid", ssid, &l1);
  if (nvs_get_str(h, "pass", pass, &l2) != ESP_OK) pass[0] = '\0';
  nvs_close(h);
  return e1 == ESP_OK && ssid[0] != '\0';
}

void start_ap() {
  wifi_config_t ap = {};
  std::strncpy(reinterpret_cast<char*>(ap.ap.ssid), kApSsid, sizeof(ap.ap.ssid));
  ap.ap.ssid_len = std::strlen(kApSsid);
  std::strncpy(reinterpret_cast<char*>(ap.ap.password), kApPass, sizeof(ap.ap.password));
  ap.ap.max_connection = 4;
  ap.ap.authmode = std::strlen(kApPass) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  esp_wifi_set_mode(WIFI_MODE_APSTA);  // APSTA so the portal can scan
  esp_wifi_set_config(WIFI_IF_AP, &ap);
  esp_wifi_start();
  ESP_LOGI(kTag, "AP portal '%s' up — connect, then browse http://192.168.4.1/", kApSsid);
}
}  // namespace

const char* wifi_ap_ssid() { return kApSsid; }
const char* wifi_hostname() { return kHostname; }

bool wifi_save_creds(const char* ssid, const char* pass) {
  nvs_handle_t h;
  if (nvs_open(kNvsNs, NVS_READWRITE, &h) != ESP_OK) return false;
  nvs_set_str(h, "ssid", ssid ? ssid : "");
  nvs_set_str(h, "pass", pass ? pass : "");
  nvs_commit(h);
  nvs_close(h);
  return true;
}

void wifi_forget_and_restart() {
  nvs_handle_t h;
  if (nvs_open(kNvsNs, NVS_READWRITE, &h) == ESP_OK) {
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
  }
  esp_restart();
}

int wifi_scan_json(char* out, size_t cap) {
  wifi_scan_config_t sc = {};
  if (esp_wifi_scan_start(&sc, true) != ESP_OK) return std::snprintf(out, cap, "[]");
  uint16_t num = 0;
  esp_wifi_scan_get_ap_num(&num);
  if (num > 20) num = 20;
  static wifi_ap_record_t recs[20];
  uint16_t got = num;
  esp_wifi_scan_get_ap_records(&got, recs);
  int off = std::snprintf(out, cap, "[");
  for (uint16_t i = 0; i < got && off < static_cast<int>(cap) - 64; ++i) {
    off += std::snprintf(out + off, cap - off, "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                         i ? "," : "",
                         reinterpret_cast<char*>(recs[i].ssid), recs[i].rssi);
  }
  off += std::snprintf(out + off, cap - off, "]");
  return off;
}

bool wifi_ip(char* out, size_t cap) {
  esp_netif_t* nif = s_sta_netif ? s_sta_netif : s_ap_netif;
  if (!nif) return false;
  esp_netif_ip_info_t ip;
  if (esp_netif_get_ip_info(nif, &ip) != ESP_OK || ip.ip.addr == 0) return false;
  std::snprintf(out, cap, IPSTR, IP2STR(&ip.ip));
  return true;
}

WifiMode wifi_start() {
  s_events = xEventGroupCreate();
  s_sta_netif = esp_netif_create_default_wifi_sta();
  s_ap_netif = esp_netif_create_default_wifi_ap();
  esp_netif_set_hostname(s_sta_netif, kHostname);

  wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&ic);
  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr, nullptr);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr, nullptr);

  const bool forced_ap = boot_button_held();
  char ssid[33] = {};
  char pass[65] = {};
  const bool have = load_creds(ssid, sizeof(ssid), pass, sizeof(pass));

  if (have && !forced_ap) {
    wifi_config_t wc = {};
    std::strncpy(reinterpret_cast<char*>(wc.sta.ssid), ssid, sizeof(wc.sta.ssid));
    std::strncpy(reinterpret_cast<char*>(wc.sta.password), pass, sizeof(wc.sta.password));
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_start();
    ESP_LOGI(kTag, "connecting to '%s' (hostname '%s')", ssid, kHostname);
    const EventBits_t bits = xEventGroupWaitBits(
        s_events, kConnectedBit | kFailBit, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (bits & kConnectedBit) {
      char ip[16] = "?";
      wifi_ip(ip, sizeof(ip));
      ESP_LOGI(kTag, "================================================");
      ESP_LOGI(kTag, " connected. find me in your router's DHCP leases:");
      ESP_LOGI(kTag, "   hostname: %s", kHostname);
      ESP_LOGI(kTag, "   IP:       %s   ->  http://%s/", ip, ip);
      ESP_LOGI(kTag, "================================================");
      return WifiMode::kStation;
    }
    ESP_LOGW(kTag, "STA connect failed; starting AP portal");
    esp_wifi_stop();
  }

  start_ap();
  return WifiMode::kAccessPoint;
}

}  // namespace diag
