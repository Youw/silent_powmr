#include "silent_powmr/dp_map.h"

#include <cmath>

namespace silent_powmr::dp {

namespace {
int32_t scaled10(float v) noexcept {
  return static_cast<int32_t>(std::lroundf(v * 10.0f));
}
int32_t clamp_int(int32_t v, int32_t lo, int32_t hi) noexcept {
  return v < lo ? lo : (v > hi ? hi : v);
}
}  // namespace

size_t build_telemetry(const InverterStatus& s, ChargerMode mode, DpValue* out,
                       size_t cap) noexcept {
  size_t n = 0;
  auto add = [&](uint8_t id, DpType t, int32_t v) {
    if (n < cap) out[n++] = DpValue{id, t, v};
  };
  add(kBatteryVoltage, DpType::kValue, scaled10(s.battery_voltage));
  add(kBatterySoc, DpType::kValue, s.battery_soc);
  add(kLoadPower, DpType::kValue, s.load_power);
  add(kLoadCurrent, DpType::kValue, scaled10(s.load_current));
  add(kGridPresent, DpType::kBool, s.grid_present ? 1 : 0);
  add(kBatteryPower, DpType::kValue, static_cast<int32_t>(std::lroundf(s.battery_power)));
  add(kBatteryCurrent, DpType::kValue, scaled10(s.battery_current));
  add(kChargerMode, DpType::kEnum, mode == ChargerMode::kQuiet ? kModeQuiet : kModeCharge);
  add(kTemperature, DpType::kValue, scaled10(s.temperature));
  add(kFaultCode, DpType::kValue, static_cast<int32_t>(s.error_code));
  return n;
}

size_t build_config_report(const QuietControllerConfig& cfg, DpValue* out,
                           size_t cap) noexcept {
  size_t n = 0;
  auto add = [&](uint8_t id, DpType t, int32_t v) {
    if (n < cap) out[n++] = DpValue{id, t, v};
  };
  add(kAutoQuietEnable, DpType::kBool, cfg.auto_enabled ? 1 : 0);
  add(kManualChargerMode, DpType::kEnum,
      cfg.manual_mode == ChargerMode::kQuiet ? kModeQuiet : kModeCharge);
  add(kRechargeSocThreshold, DpType::kValue, cfg.recharge_soc);
  return n;
}

bool apply_control(uint8_t dp_id, int32_t value, QuietControllerConfig& cfg,
                   ControlUpdate& u) noexcept {
  switch (dp_id) {
    case kAutoQuietEnable: {
      const bool nb = value != 0;
      if (nb != cfg.auto_enabled) {
        cfg.auto_enabled = nb;
        u.changed_auto = true;
      }
      return true;
    }
    case kManualChargerMode: {
      const ChargerMode m =
          value == kModeQuiet ? ChargerMode::kQuiet : ChargerMode::kCharge;
      if (m != cfg.manual_mode) {
        cfg.manual_mode = m;
        u.changed_manual = true;
      }
      return true;
    }
    case kRechargeSocThreshold: {
      // Must stay below quiet_soc (100) and above a sane floor.
      const int nv = static_cast<int>(clamp_int(value, 10, 99));
      if (nv != cfg.recharge_soc) {
        cfg.recharge_soc = nv;
        u.changed_threshold = true;
      }
      return true;
    }
    default:
      return false;
  }
}

}  // namespace silent_powmr::dp
