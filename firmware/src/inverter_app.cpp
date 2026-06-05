#include "inverter_app.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "nvs.h"
#include "silent_powmr/control_cycle.h"
#include "silent_powmr/dp_map.h"
#include "silent_powmr/inverter_client.h"

namespace silent_powmr {

namespace {
constexpr char kTag[] = "inverter_app";
constexpr char kNvsNamespace[] = "spowmr";
constexpr uint32_t kPollIntervalMs = 5000;

// CONFIRM ON HARDWARE: Modbus slave id and the read byte order for this unit.
constexpr uint8_t kSlaveId = 5;
constexpr bool kReadLittleEndian = true;

uint32_t now_ms() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}
}  // namespace

InverterApp g_app;

void InverterApp::begin() {
  lock_ = xSemaphoreCreateMutex();
  load_config();
  uart_.begin();
  xTaskCreate(&InverterApp::task_trampoline, "inverter", 4096, this, 5, nullptr);
}

void InverterApp::task_trampoline(void* arg) {
  static_cast<InverterApp*>(arg)->run();
}

void InverterApp::run() {
  InverterClient client(uart_, kSlaveId, kReadLittleEndian);
  for (;;) {
    QuietControllerConfig local_cfg;
    xSemaphoreTake(lock_, portMAX_DELAY);
    local_cfg = cfg_;
    xSemaphoreGive(lock_);

    const ControlCycleResult r =
        run_control_cycle(client, local_cfg, state_, now_ms());

    xSemaphoreTake(lock_, portMAX_DELAY);
    if (r.polled) {
      snap_.status = r.status;
      snap_.online = true;
      last_good_ms_ = now_ms();
    } else {
      snap_.online = false;
    }
    snap_.mode = r.desired;
    if (r.wrote) snap_.last_write_ok = r.write_ok;
    xSemaphoreGive(lock_);

    if (r.wrote) {
      ESP_LOGI(kTag, "switch -> %s (%s)", to_string(r.desired),
               r.write_ok ? "ok" : "FAILED");
    }
    vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
  }
}

AppSnapshot InverterApp::snapshot() {
  xSemaphoreTake(lock_, portMAX_DELAY);
  AppSnapshot s = snap_;
  s.age_ms = s.online ? (now_ms() - last_good_ms_) : 0;
  xSemaphoreGive(lock_);
  return s;
}

QuietControllerConfig InverterApp::config() {
  xSemaphoreTake(lock_, portMAX_DELAY);
  QuietControllerConfig c = cfg_;
  xSemaphoreGive(lock_);
  return c;
}

bool InverterApp::apply_control_dp(uint8_t dp_id, int32_t value) {
  xSemaphoreTake(lock_, portMAX_DELAY);
  dp::ControlUpdate update;
  const bool handled = dp::apply_control(dp_id, value, cfg_, update);
  const bool changed = update.any();
  xSemaphoreGive(lock_);
  if (changed) save_config();
  if (handled) ESP_LOGI(kTag, "control dp %u = %ld", dp_id, (long)value);
  return handled;
}

void InverterApp::load_config() {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return;
  uint8_t auto_en = cfg_.auto_enabled ? 1 : 0;
  uint8_t manual = cfg_.manual_mode == ChargerMode::kQuiet ? 1 : 0;
  int32_t recharge = cfg_.recharge_soc;
  nvs_get_u8(h, "auto", &auto_en);
  nvs_get_u8(h, "manual", &manual);
  nvs_get_i32(h, "recharge", &recharge);
  cfg_.auto_enabled = auto_en != 0;
  cfg_.manual_mode = manual ? ChargerMode::kQuiet : ChargerMode::kCharge;
  cfg_.recharge_soc = recharge;
  nvs_close(h);
}

void InverterApp::save_config() {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  xSemaphoreTake(lock_, portMAX_DELAY);
  const uint8_t auto_en = cfg_.auto_enabled ? 1 : 0;
  const uint8_t manual = cfg_.manual_mode == ChargerMode::kQuiet ? 1 : 0;
  const int32_t recharge = cfg_.recharge_soc;
  xSemaphoreGive(lock_);
  nvs_set_u8(h, "auto", auto_en);
  nvs_set_u8(h, "manual", manual);
  nvs_set_i32(h, "recharge", recharge);
  nvs_commit(h);
  nvs_close(h);
}

}  // namespace silent_powmr
