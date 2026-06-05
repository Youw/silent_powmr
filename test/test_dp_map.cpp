#include "silent_powmr/dp_map.h"

#include "silent_powmr/inverter_status.h"
#include "silent_powmr/quiet_controller.h"
#include "test_framework.h"

using namespace silent_powmr;
namespace dp = silent_powmr::dp;

namespace {
// Find a DP by id in a list; returns nullptr if absent.
const dp::DpValue* find(const dp::DpValue* v, size_t n, uint8_t id) {
  for (size_t i = 0; i < n; ++i)
    if (v[i].id == id) return &v[i];
  return nullptr;
}
}  // namespace

TEST_CASE(dp_build_telemetry_values) {
  InverterStatus s;
  s.battery_voltage = 25.4f;
  s.battery_soc = 96;
  s.load_power = 350;
  s.load_current = 1.74f;
  s.grid_present = true;
  s.battery_power = -127.0f;
  s.battery_current = -5.0f;
  s.temperature = 33.0f;
  s.error_code = 0;

  dp::DpValue out[16];
  const size_t n = dp::build_telemetry(s, ChargerMode::kQuiet, out, 16);
  CHECK(n == 10);

  const auto* bv = find(out, n, dp::kBatteryVoltage);
  REQUIRE(bv != nullptr);
  CHECK_EQ(bv->value, 254);  // 25.4 V * 10
  CHECK(bv->type == dp::DpType::kValue);

  CHECK_EQ(find(out, n, dp::kBatterySoc)->value, 96);
  CHECK_EQ(find(out, n, dp::kLoadPower)->value, 350);
  CHECK_EQ(find(out, n, dp::kLoadCurrent)->value, 17);  // 1.74 A * 10 (rounded)
  CHECK_EQ(find(out, n, dp::kGridPresent)->value, 1);
  CHECK_EQ(find(out, n, dp::kBatteryPower)->value, -127);
  CHECK_EQ(find(out, n, dp::kBatteryCurrent)->value, -50);

  const auto* mode = find(out, n, dp::kChargerMode);
  REQUIRE(mode != nullptr);
  CHECK(mode->type == dp::DpType::kEnum);
  CHECK_EQ(mode->value, dp::kModeQuiet);
}

TEST_CASE(dp_telemetry_respects_capacity) {
  InverterStatus s;
  dp::DpValue out[3];
  const size_t n = dp::build_telemetry(s, ChargerMode::kCharge, out, 3);
  CHECK(n == 3);  // never overflows the buffer
}

TEST_CASE(dp_config_report) {
  QuietControllerConfig cfg;
  cfg.auto_enabled = true;
  cfg.manual_mode = ChargerMode::kCharge;
  cfg.recharge_soc = 89;
  dp::DpValue out[8];
  const size_t n = dp::build_config_report(cfg, out, 8);
  CHECK(n == 3);
  CHECK_EQ(find(out, n, dp::kAutoQuietEnable)->value, 1);
  CHECK_EQ(find(out, n, dp::kManualChargerMode)->value, dp::kModeCharge);
  CHECK_EQ(find(out, n, dp::kRechargeSocThreshold)->value, 89);
}

TEST_CASE(dp_apply_control_auto_toggle) {
  QuietControllerConfig cfg;
  cfg.auto_enabled = true;
  dp::ControlUpdate u;
  CHECK(dp::apply_control(dp::kAutoQuietEnable, 0, cfg, u));
  CHECK(!cfg.auto_enabled);
  CHECK(u.changed_auto);
}

TEST_CASE(dp_apply_control_manual_mode) {
  QuietControllerConfig cfg;
  dp::ControlUpdate u;
  CHECK(dp::apply_control(dp::kManualChargerMode, dp::kModeQuiet, cfg, u));
  CHECK(cfg.manual_mode == ChargerMode::kQuiet);
  CHECK(u.changed_manual);
}

TEST_CASE(dp_apply_control_threshold_clamped) {
  QuietControllerConfig cfg;
  dp::ControlUpdate u;
  // Out-of-range high -> clamped to 99 (must stay below quiet_soc).
  CHECK(dp::apply_control(dp::kRechargeSocThreshold, 150, cfg, u));
  CHECK_EQ(cfg.recharge_soc, 99);
  // Out-of-range low -> clamped to 10.
  dp::apply_control(dp::kRechargeSocThreshold, -5, cfg, u);
  CHECK_EQ(cfg.recharge_soc, 10);
}

TEST_CASE(dp_apply_control_unknown_id) {
  QuietControllerConfig cfg;
  dp::ControlUpdate u;
  CHECK(!dp::apply_control(200, 1, cfg, u));
  CHECK(!u.any());
}
