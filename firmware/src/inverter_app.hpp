#pragma once
// Owns the autonomous control loop on the ESP32: polls the inverter over UART,
// runs the (host-tested) quiet-mode FSM, reconciles charger priority, and
// publishes a thread-safe snapshot for the Tuya layer and the web UI. Config is
// persisted in NVS and can be changed live from SmartLife.
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "silent_powmr/inverter_status.h"
#include "silent_powmr/quiet_controller.h"
#include "uart_transport.hpp"

namespace silent_powmr {

struct AppSnapshot {
  InverterStatus status;                  // last decoded status
  ChargerMode mode = ChargerMode::kCharge;  // current desired/applied mode
  bool online = false;                    // last poll succeeded
  bool last_write_ok = true;              // last mode write verified
  uint32_t age_ms = 0;                    // ms since last good poll
};

class InverterApp {
 public:
  void begin();                       // load NVS, init UART, start the task
  AppSnapshot snapshot();             // thread-safe copy
  QuietControllerConfig config();     // thread-safe copy

  // Apply a control DP from SmartLife; persists on change. Returns true if the
  // DP id was a recognized control DP.
  bool apply_control_dp(uint8_t dp_id, int32_t value);

 private:
  static void task_trampoline(void* arg);
  void run();
  void load_config();
  void save_config();

  // UART2 by default; pins per the odya wiring (swap if your harness differs).
  UartTransport uart_{UART_NUM_2, /*tx*/17, /*rx*/16, 2400};
  QuietControllerConfig cfg_{};
  QuietControllerState state_{};
  AppSnapshot snap_{};
  SemaphoreHandle_t lock_ = nullptr;
  uint32_t last_good_ms_ = 0;
};

extern InverterApp g_app;

}  // namespace silent_powmr
