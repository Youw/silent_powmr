// End-to-end: the real control loop (InverterClient + FSM + run_control_cycle)
// driven against the simulated inverter. No hardware, no mocks of the codec —
// actual Modbus frames flow through the sim and back.
#include "silent_powmr/control_cycle.h"
#include "silent_powmr/inverter_client.h"
#include "silent_powmr/quiet_controller.h"
#include "silent_powmr/registers.h"
#include "silent_powmr/sim_inverter.h"
#include "test_framework.h"

using namespace silent_powmr;

namespace {
QuietControllerConfig cfg() {
  QuietControllerConfig c;
  c.recharge_soc = 89;
  c.quiet_soc = 100;
  c.min_dwell_ms = 60000;
  c.grid_debounce_ms = 10000;
  return c;
}
}  // namespace

TEST_CASE(integration_client_poll_decodes_sim) {
  SimInverter sim(5, /*little_endian=*/true);
  sim.set_register(powmr::reg::kBatteryVoltage, 254);
  sim.set_register(powmr::reg::kBatterySoc, 96);
  sim.set_grid(true);
  InverterClient client(sim, 5, true);

  InverterStatus s;
  REQUIRE(client.poll(s));
  CHECK_NEAR(s.battery_voltage, 25.4f, 0.05f);
  CHECK_EQ(s.battery_soc, 96);
  CHECK(s.grid_present);
}

TEST_CASE(integration_set_charger_mode_roundtrips) {
  SimInverter sim(5, true);
  InverterClient client(sim, 5, true);
  // Sim starts in Solar+Utility (charge).
  CHECK_EQ(sim.charger_priority(), powmr::charger_priority::kSolarAndUtility);
  REQUIRE(client.set_charger_mode(ChargerMode::kQuiet, /*verify=*/true));
  CHECK_EQ(sim.charger_priority(), powmr::charger_priority::kSolarOnly);
  ChargerMode m;
  REQUIRE(client.read_charger_mode(m));
  CHECK(m == ChargerMode::kQuiet);
}

TEST_CASE(integration_goes_quiet_when_full_on_grid) {
  SimInverter sim(5, true);
  sim.set_grid(true);
  sim.set_soc(100);
  sim.set_charger_priority(powmr::charger_priority::kSolarAndUtility);
  InverterClient client(sim, 5, true);
  QuietControllerState st;
  auto c = cfg();

  const auto r = run_control_cycle(client, c, st, 0);
  CHECK(r.polled);
  CHECK(r.desired == ChargerMode::kQuiet);
  CHECK(r.wrote);
  CHECK(r.write_ok);
  // The inverter was actually switched to Solar-only (quiet).
  CHECK_EQ(sim.charger_priority(), powmr::charger_priority::kSolarOnly);
}

TEST_CASE(integration_recharges_when_soc_drops) {
  SimInverter sim(5, true);
  sim.set_grid(true);
  sim.set_soc(100);
  sim.set_charger_priority(powmr::charger_priority::kSolarOnly);  // already quiet
  InverterClient client(sim, 5, true);
  QuietControllerState st;
  auto c = cfg();

  run_control_cycle(client, c, st, 0);       // stays quiet
  CHECK_EQ(sim.charger_priority(), powmr::charger_priority::kSolarOnly);

  sim.set_soc(89);                            // battery drained to threshold
  const auto r = run_control_cycle(client, c, st, 5000);
  CHECK(r.desired == ChargerMode::kCharge);
  CHECK_EQ(sim.charger_priority(), powmr::charger_priority::kSolarAndUtility);
}

TEST_CASE(integration_grid_loss_forces_charge) {
  SimInverter sim(5, true);
  sim.set_grid(true);
  sim.set_soc(100);
  sim.set_charger_priority(powmr::charger_priority::kSolarOnly);
  InverterClient client(sim, 5, true);
  QuietControllerState st;
  auto c = cfg();

  run_control_cycle(client, c, st, 0);  // quiet
  sim.set_grid(false);
  run_control_cycle(client, c, st, 20000);             // within debounce
  const auto r = run_control_cycle(client, c, st, 31000);  // past debounce
  CHECK(r.desired == ChargerMode::kCharge);
  CHECK_EQ(sim.charger_priority(), powmr::charger_priority::kSolarAndUtility);
}

TEST_CASE(integration_failsafe_when_bus_down) {
  SimInverter sim(5, true);
  sim.set_grid(true);
  sim.set_soc(100);
  InverterClient client(sim, 5, true);
  QuietControllerState st;
  auto c = cfg();

  run_control_cycle(client, c, st, 0);  // quiet
  sim.set_online(false);                // bus goes dark
  const auto r = run_control_cycle(client, c, st, 5000);
  CHECK(!r.polled);
  CHECK(r.desired == ChargerMode::kCharge);  // fail-safe decision
  CHECK(!r.read_mode_ok);                    // couldn't talk to it at all
}

TEST_CASE(integration_no_write_when_already_correct) {
  SimInverter sim(5, true);
  sim.set_grid(true);
  sim.set_soc(100);
  sim.set_charger_priority(powmr::charger_priority::kSolarOnly);  // already quiet
  InverterClient client(sim, 5, true);
  QuietControllerState st;
  // Seed the FSM into quiet so desired matches the inverter's current mode.
  st.initialized = true;
  st.switched_once = true;
  st.debounced_grid = true;
  st.last_grid_present = true;
  st.mode = ChargerMode::kQuiet;
  auto c = cfg();

  const auto r = run_control_cycle(client, c, st, 100000);
  CHECK(r.read_mode_ok);
  CHECK(r.current_mode == ChargerMode::kQuiet);
  CHECK(r.desired == ChargerMode::kQuiet);
  CHECK(!r.wrote);  // no redundant write
}

TEST_CASE(integration_read_holding_raw) {
  SimInverter sim(5, true);  // emits little-endian register bytes
  sim.set_register(powmr::reg::kBatteryVoltage, 254);  // 0x00FE
  InverterClient client(sim, 5, true);
  uint8_t raw[4] = {};
  const size_t n =
      client.read_holding_raw(powmr::reg::kBatteryVoltage, 1, raw, sizeof(raw));
  REQUIRE(n == 2);
  CHECK_EQ(raw[0], static_cast<uint8_t>(0xFE));  // low byte first
  CHECK_EQ(raw[1], static_cast<uint8_t>(0x00));
}

TEST_CASE(integration_fault_forces_charge) {
  SimInverter sim(5, true);
  sim.set_grid(true);
  sim.set_soc(100);
  sim.set_charger_priority(powmr::charger_priority::kSolarOnly);
  sim.set_fault(7);  // non-zero error code
  InverterClient client(sim, 5, true);
  QuietControllerState st;
  auto c = cfg();

  const auto r = run_control_cycle(client, c, st, 0);
  CHECK(r.polled);
  CHECK(r.desired == ChargerMode::kCharge);
  CHECK_EQ(sim.charger_priority(), powmr::charger_priority::kSolarAndUtility);
}
