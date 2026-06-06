#include "silent_powmr/control_cycle.h"

namespace silent_powmr {

ControlCycleResult run_control_cycle(InverterClient& client,
                                     const QuietControllerConfig& cfg,
                                     QuietControllerState& state,
                                     uint32_t now_ms) {
  ControlCycleResult r;

  ControllerInputs in;
  InverterStatus s;
  if (client.poll(s)) {
    r.polled = true;
    r.status = s;
    in.status_valid = true;
    in.grid_present = s.grid_present;
    in.battery_soc = s.battery_soc;
    in.fault = s.error_code != 0;
  } else {
    in.status_valid = false;  // FSM will fail-safe to CHARGE
  }

  r.desired = quiet_controller_tick(cfg, state, in, now_ms);

  // Reconcile the inverter's actual charger priority with what we want. Reading
  // it each cycle also catches changes made manually on the inverter's panel.
  ChargerMode current;
  if (client.read_charger_mode(current)) {
    r.read_mode_ok = true;
    r.current_mode = current;
    if (current != r.desired) {
      r.wrote = true;
      r.write_ok = client.set_charger_mode(r.desired, /*verify=*/true);
    }
  }
  return r;
}

}  // namespace silent_powmr
