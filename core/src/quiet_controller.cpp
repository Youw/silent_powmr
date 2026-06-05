#include "silent_powmr/quiet_controller.h"

#include "silent_powmr/registers.h"

namespace silent_powmr {

namespace {
void apply_mode(QuietControllerState& st, ChargerMode m, uint32_t now) noexcept {
  if (st.mode != m) {
    st.mode = m;
    st.last_switch_ms = now;
    st.switched_once = true;
  }
}
}  // namespace

const char* to_string(ChargerMode mode) noexcept {
  switch (mode) {
    case ChargerMode::kCharge: return "charge";  // Solar + Utility
    case ChargerMode::kQuiet: return "quiet";    // Solar only
  }
  return "?";
}

uint16_t charger_priority_register_value(ChargerMode mode) noexcept {
  return mode == ChargerMode::kQuiet ? powmr::charger_priority::kSolarOnly
                                     : powmr::charger_priority::kSolarAndUtility;
}

ChargerMode charger_mode_from_register(uint16_t reg_value) noexcept {
  return reg_value == powmr::charger_priority::kSolarOnly ? ChargerMode::kQuiet
                                                          : ChargerMode::kCharge;
}

ChargerMode quiet_controller_tick(const QuietControllerConfig& cfg,
                                  QuietControllerState& st,
                                  const ControllerInputs& in,
                                  uint32_t now_ms) noexcept {
  if (!st.initialized) {
    st.initialized = true;
    st.switched_once = false;
    st.last_grid_present = in.grid_present;
    st.debounced_grid = in.grid_present;
    st.grid_since_ms = now_ms;
    st.last_switch_ms = now_ms;
    st.mode = ChargerMode::kCharge;  // safe default until proven otherwise
  }

  // Maintain the debounced grid signal, but only from readings we trust.
  if (in.status_valid) {
    if (in.grid_present != st.last_grid_present) {
      st.last_grid_present = in.grid_present;
      st.grid_since_ms = now_ms;
    }
    if (static_cast<uint32_t>(now_ms - st.grid_since_ms) >= cfg.grid_debounce_ms) {
      st.debounced_grid = st.last_grid_present;
    }
  }

  // Manual override: honor the user's explicit choice immediately.
  if (!cfg.auto_enabled) {
    apply_mode(st, cfg.manual_mode, now_ms);
    return st.mode;
  }

  // Fail-safe: without a trustworthy reading, or on fault, keep it charging.
  if (!in.status_valid || in.fault) {
    apply_mode(st, ChargerMode::kCharge, now_ms);
    return st.mode;
  }

  // Decide the target mode using hysteresis on the debounced grid + SoC.
  ChargerMode target = st.mode;
  if (st.mode == ChargerMode::kQuiet) {
    if (!st.debounced_grid || in.battery_soc <= cfg.recharge_soc) {
      target = ChargerMode::kCharge;
    }
  } else {  // currently CHARGE
    if (st.debounced_grid && in.battery_soc >= cfg.quiet_soc) {
      target = ChargerMode::kQuiet;
    }
  }

  if (target != st.mode) {
    if (target == ChargerMode::kCharge) {
      apply_mode(st, target, now_ms);  // safe direction: act immediately
    } else {
      const bool dwell_ok =
          !st.switched_once ||
          static_cast<uint32_t>(now_ms - st.last_switch_ms) >= cfg.min_dwell_ms;
      if (dwell_ok) apply_mode(st, target, now_ms);
    }
  }
  return st.mode;
}

}  // namespace silent_powmr
