#pragma once
// The fan-silencing decision logic, as a pure, deterministic state machine.
//
// Two charger-priority intents:
//   CHARGE = Solar + Utility  (charges the battery from the grid; fans may run)
//   QUIET  = Solar only       (no grid charging; the inverter runs quiet)
//
// Rules when automatic mode is enabled:
//   * QUIET  -> CHARGE  if grid is lost OR battery SoC falls to recharge_soc.
//   * CHARGE -> QUIET   if grid is present AND battery SoC reaches quiet_soc.
//   * Fail-safe: invalid status or a fault forces CHARGE — a UPS must never be
//     left uncharged.
// Hysteresis (recharge_soc < quiet_soc), a grid-flicker debounce, and a minimum
// dwell time prevent rapid toggling. Switching toward CHARGE (the safe
// direction) is immediate; switching toward QUIET respects the dwell time.
//
// The clock is injected (now_ms, a monotonic millisecond counter) so the whole
// thing is testable with no real time.
#include <cstdint>

namespace silent_powmr {

enum class ChargerMode : uint8_t {
  kCharge = 0,  // Solar + Utility
  kQuiet = 1,   // Solar only
};

const char* to_string(ChargerMode mode) noexcept;

struct QuietControllerConfig {
  bool auto_enabled = true;
  ChargerMode manual_mode = ChargerMode::kCharge;  // used when auto disabled
  int recharge_soc = 89;                 // SoC <= this -> CHARGE
  int quiet_soc = 100;                   // SoC >= this (grid up) -> QUIET
  uint32_t min_dwell_ms = 60000;         // min time between switches (to QUIET)
  uint32_t grid_debounce_ms = 10000;     // grid state must hold this long
};

struct QuietControllerState {
  ChargerMode mode = ChargerMode::kCharge;  // current desired mode (safe default)
  bool initialized = false;
  bool switched_once = false;
  bool last_grid_present = false;
  bool debounced_grid = false;
  uint32_t grid_since_ms = 0;
  uint32_t last_switch_ms = 0;
};

struct ControllerInputs {
  bool status_valid = false;  // did we get a fresh, valid reading?
  bool grid_present = false;
  int battery_soc = 0;
  bool fault = false;         // inverter reported a fault
};

// Advance the state machine and return the desired ChargerMode.
ChargerMode quiet_controller_tick(const QuietControllerConfig& cfg,
                                  QuietControllerState& state,
                                  const ControllerInputs& in,
                                  uint32_t now_ms) noexcept;

// Map a desired mode to the PowMR charger-source-priority register (5017) value,
// and back. (Centralized so the magic numbers live only in registers.h.)
uint16_t charger_priority_register_value(ChargerMode mode) noexcept;
ChargerMode charger_mode_from_register(uint16_t reg_value) noexcept;

}  // namespace silent_powmr
