#include "silent_powmr/quiet_controller.h"

#include "silent_powmr/registers.h"
#include "test_framework.h"

using namespace silent_powmr;

namespace {
QuietControllerConfig default_cfg() {
  QuietControllerConfig c;
  c.auto_enabled = true;
  c.recharge_soc = 89;
  c.quiet_soc = 100;
  c.min_dwell_ms = 60000;
  c.grid_debounce_ms = 10000;
  return c;
}

ControllerInputs in(bool valid, bool grid, int soc, bool fault = false) {
  ControllerInputs i;
  i.status_valid = valid;
  i.grid_present = grid;
  i.battery_soc = soc;
  i.fault = fault;
  return i;
}
}  // namespace

TEST_CASE(controller_register_value_mapping) {
  CHECK_EQ(charger_priority_register_value(ChargerMode::kQuiet),
           powmr::charger_priority::kSolarOnly);
  CHECK_EQ(charger_priority_register_value(ChargerMode::kCharge),
           powmr::charger_priority::kSolarAndUtility);
  CHECK(charger_mode_from_register(powmr::charger_priority::kSolarOnly) ==
        ChargerMode::kQuiet);
  CHECK(charger_mode_from_register(powmr::charger_priority::kSolarAndUtility) ==
        ChargerMode::kCharge);
}

TEST_CASE(controller_goes_quiet_when_full_and_on_grid) {
  auto cfg = default_cfg();
  QuietControllerState st;
  // Full battery, grid present -> should reach QUIET (first switch is immediate).
  const auto m = quiet_controller_tick(cfg, st, in(true, true, 100), 0);
  CHECK(m == ChargerMode::kQuiet);
}

TEST_CASE(controller_recharges_when_soc_drops) {
  auto cfg = default_cfg();
  QuietControllerState st;
  quiet_controller_tick(cfg, st, in(true, true, 100), 0);      // -> QUIET
  // SoC falls to the recharge threshold -> CHARGE, immediately (protective).
  const auto m = quiet_controller_tick(cfg, st, in(true, true, 89), 5000);
  CHECK(m == ChargerMode::kCharge);
}

TEST_CASE(controller_hysteresis_band_holds_mode) {
  auto cfg = default_cfg();
  QuietControllerState st;
  quiet_controller_tick(cfg, st, in(true, true, 100), 0);  // -> QUIET
  // 95% is inside the 89..100 band: stays QUIET.
  CHECK(quiet_controller_tick(cfg, st, in(true, true, 95), 1000) ==
        ChargerMode::kQuiet);
  // Drop to threshold -> CHARGE.
  quiet_controller_tick(cfg, st, in(true, true, 89), 2000);
  // Back to 95 inside band: stays CHARGE (does not flip until >=100).
  CHECK(quiet_controller_tick(cfg, st, in(true, true, 95), 3000) ==
        ChargerMode::kCharge);
}

TEST_CASE(controller_quiet_switch_respects_dwell) {
  auto cfg = default_cfg();
  QuietControllerState st;
  quiet_controller_tick(cfg, st, in(true, true, 100), 0);     // -> QUIET (t=0)
  quiet_controller_tick(cfg, st, in(true, true, 89), 5000);   // -> CHARGE (t=5s)
  // Conditions to go QUIET again, but only 5s since last switch (< 60s dwell).
  CHECK(quiet_controller_tick(cfg, st, in(true, true, 100), 10000) ==
        ChargerMode::kCharge);
  // After the dwell elapses, it may switch to QUIET.
  CHECK(quiet_controller_tick(cfg, st, in(true, true, 100), 65001) ==
        ChargerMode::kQuiet);
}

TEST_CASE(controller_grid_flicker_is_debounced) {
  auto cfg = default_cfg();
  QuietControllerState st;
  quiet_controller_tick(cfg, st, in(true, true, 100), 0);  // -> QUIET, grid up
  // Brief 2s dropout: debounced grid stays up, no switch.
  quiet_controller_tick(cfg, st, in(true, false, 100), 1000);
  CHECK(quiet_controller_tick(cfg, st, in(true, true, 100), 3000) ==
        ChargerMode::kQuiet);
}

TEST_CASE(controller_sustained_grid_loss_charges) {
  auto cfg = default_cfg();
  QuietControllerState st;
  quiet_controller_tick(cfg, st, in(true, true, 100), 0);  // -> QUIET
  // Grid goes down and stays down past the debounce window.
  quiet_controller_tick(cfg, st, in(true, false, 100), 20000);
  const auto m = quiet_controller_tick(cfg, st, in(true, false, 100), 30001);
  CHECK(m == ChargerMode::kCharge);
}

TEST_CASE(controller_failsafe_on_invalid_status) {
  auto cfg = default_cfg();
  QuietControllerState st;
  quiet_controller_tick(cfg, st, in(true, true, 100), 0);  // -> QUIET
  // Lost comms -> force CHARGE even though last good SoC was 100.
  CHECK(quiet_controller_tick(cfg, st, in(false, true, 100), 5000) ==
        ChargerMode::kCharge);
}

TEST_CASE(controller_failsafe_on_fault) {
  auto cfg = default_cfg();
  QuietControllerState st;
  quiet_controller_tick(cfg, st, in(true, true, 100), 0);  // -> QUIET
  CHECK(quiet_controller_tick(cfg, st, in(true, true, 100, /*fault=*/true), 5000) ==
        ChargerMode::kCharge);
}

TEST_CASE(controller_manual_override_quiet) {
  auto cfg = default_cfg();
  cfg.auto_enabled = false;
  cfg.manual_mode = ChargerMode::kQuiet;
  QuietControllerState st;
  // Manual QUIET honored even with a low battery.
  CHECK(quiet_controller_tick(cfg, st, in(true, true, 40), 0) ==
        ChargerMode::kQuiet);
}

TEST_CASE(controller_manual_override_charge) {
  auto cfg = default_cfg();
  cfg.auto_enabled = false;
  cfg.manual_mode = ChargerMode::kCharge;
  QuietControllerState st;
  CHECK(quiet_controller_tick(cfg, st, in(true, true, 100), 0) ==
        ChargerMode::kCharge);
}

TEST_CASE(controller_custom_recharge_threshold) {
  auto cfg = default_cfg();
  cfg.recharge_soc = 50;  // let it run down further before charging
  QuietControllerState st;
  quiet_controller_tick(cfg, st, in(true, true, 100), 0);  // -> QUIET
  // 80% would trigger recharge at default 89, but here threshold is 50 -> stays QUIET.
  CHECK(quiet_controller_tick(cfg, st, in(true, true, 80), 1000) ==
        ChargerMode::kQuiet);
  CHECK(quiet_controller_tick(cfg, st, in(true, true, 50), 2000) ==
        ChargerMode::kCharge);
}
