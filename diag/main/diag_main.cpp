// Read-only bring-up firmware: provision Wi-Fi, then serve a register-dump page.
// It only ever issues Modbus *reads* — there is no path that writes to the
// inverter, so it is safe to leave running during calibration.
#include "diag_probe.hpp"
#include "diag_web.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "silent_powmr/inverter_client.h"
#include "uart_transport.hpp"
#include "wifi_prov.hpp"

namespace {
constexpr char kTag[] = "diag";

// CONFIRM ON HARDWARE: UART2 pins (this family is known to have TX2/RX2 swapped,
// so try swapping if there's no reply), Modbus slave id, and read byte order.
constexpr int kUartTx = 17;
constexpr int kUartRx = 16;
constexpr uint8_t kSlaveId = 5;
constexpr bool kReadLittleEndian = true;
}  // namespace

extern "C" void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  esp_netif_init();
  esp_event_loop_create_default();

  const diag::WifiMode mode = diag::wifi_start();

  static silent_powmr::UartTransport uart(UART_NUM_2, kUartTx, kUartRx, 2400);
  uart.begin();
  static silent_powmr::InverterClient client(uart, kSlaveId, kReadLittleEndian);

  diag::start_web(mode, &client);
  diag::diag_probe_start(&uart);  // serial Modbus sweep + console over USB

  // OTA rollback safety: we booted and brought services up, so confirm this
  // image is good. Otherwise the bootloader reverts to the previous one on the
  // next reset — which is what protects you during a remote update.
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK &&
      ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(kTag, "OTA image confirmed valid");
  }

  if (mode == diag::WifiMode::kAccessPoint) {
    ESP_LOGI(kTag, "join Wi-Fi '%s' and open http://192.168.4.1/ to set up",
             diag::wifi_ap_ssid());
  }
}
