#pragma once
// One iteration of the autonomous control loop: poll the inverter, run the FSM,
// and reconcile the inverter's charger-source-priority with the desired mode.
// The firmware calls this on a timer; the host tests call it against a sim.
#include <cstdint>

#include "silent_powmr/inverter_client.h"
#include "silent_powmr/inverter_status.h"
#include "silent_powmr/quiet_controller.h"

namespace silent_powmr {

struct ControlCycleResult {
  bool polled = false;            // got a valid status read
  InverterStatus status;          // last status (valid only if polled)
  ChargerMode desired = ChargerMode::kCharge;  // FSM output this cycle
  bool read_mode_ok = false;      // could read the inverter's current mode
  ChargerMode current_mode = ChargerMode::kCharge;  // inverter's mode (if read)
  bool wrote = false;             // a mode change was issued
  bool write_ok = false;          // the change was verified
};

// `state` and `cfg` persist across calls; `now_ms` is a monotonic clock.
ControlCycleResult run_control_cycle(InverterClient& client,
                                     const QuietControllerConfig& cfg,
                                     QuietControllerState& state,
                                     uint32_t now_ms);

}  // namespace silent_powmr
