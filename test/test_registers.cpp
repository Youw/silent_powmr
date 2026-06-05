#include "silent_powmr/registers.h"

#include <cstdint>
#include <cstring>

#include "silent_powmr/inverter_status.h"
#include "test_framework.h"

namespace pm = silent_powmr::powmr;

namespace {
// Build a register-word array covering the poll span, indexed by absolute addr.
struct PollBuffer {
  uint16_t words[pm::kPollCount] = {};
  pm::RegisterView view() const {
    return pm::RegisterView{words, pm::kPollBase, pm::kPollCount};
  }
  void set(uint16_t addr, uint16_t value) { words[addr - pm::kPollBase] = value; }
};
}  // namespace

TEST_CASE(registers_decode_on_grid) {
  PollBuffer b;
  b.set(pm::reg::kGridVoltage, 2300);             // 230.0 V
  b.set(pm::reg::kBatteryVoltage, 254);           // 25.4 V
  b.set(pm::reg::kBatterySoc, 100);
  b.set(pm::reg::kBatteryChargeCurrent, 0);
  b.set(pm::reg::kBatteryDischargeCurrent, 5);    // 5 A (currents are x1)
  b.set(pm::reg::kLoadVoltage, 2300);
  b.set(pm::reg::kLoadPower, 350);
  b.set(pm::reg::kLoadVa, 400);
  b.set(pm::reg::kLoadPercent, 12);
  b.set(pm::reg::kStatusFlags, 0x0000);           // not on battery
  b.set(pm::reg::kChargerStatus, 1);              // idle
  b.set(pm::reg::kTemperature, 35);

  const silent_powmr::InverterStatus s = pm::decode_status(b.view());
  CHECK(s.valid);
  CHECK_NEAR(s.grid_voltage, 230.0f, 0.01f);
  CHECK_NEAR(s.battery_voltage, 25.4f, 0.01f);
  CHECK_EQ(s.battery_soc, 100);
  CHECK_NEAR(s.discharge_current, 5.0f, 0.01f);
  CHECK_NEAR(s.charge_current, 0.0f, 0.01f);
  CHECK_EQ(s.load_power, 350);
  CHECK_EQ(s.load_percent, 12);
  // Apparent load current = 400 VA / 230 V = 1.74 A.
  CHECK_NEAR(s.load_current, 1.739f, 0.01f);
  CHECK(s.grid_present);
  // Discharging at 5.0 A * 25.4 V = -127 W (negative = leaving the battery).
  CHECK_NEAR(s.battery_current, -5.0f, 0.01f);
  CHECK_NEAR(s.battery_power, -127.0f, 0.5f);
}

TEST_CASE(registers_decode_on_battery_no_grid) {
  PollBuffer b;
  b.set(pm::reg::kGridVoltage, 0);
  b.set(pm::reg::kBatteryVoltage, 240);  // 24.0 V
  b.set(pm::reg::kBatterySoc, 78);
  b.set(pm::reg::kBatteryDischargeCurrent, 12);  // 12 A
  b.set(pm::reg::kStatusFlags, pm::flag::kOnBattery);

  const silent_powmr::InverterStatus s = pm::decode_status(b.view());
  CHECK(s.valid);
  CHECK(!s.grid_present);
  CHECK_EQ(s.battery_soc, 78);
  CHECK(s.battery_power < 0.0f);  // discharging
}

TEST_CASE(registers_decode_charging) {
  PollBuffer b;
  b.set(pm::reg::kGridVoltage, 2300);
  b.set(pm::reg::kBatteryVoltage, 280);          // 28.0 V (bulk charge)
  b.set(pm::reg::kBatterySoc, 90);
  b.set(pm::reg::kBatteryChargeCurrent, 20);     // 20 A
  b.set(pm::reg::kChargerStatus, 2);             // active

  const silent_powmr::InverterStatus s = pm::decode_status(b.view());
  CHECK(s.grid_present);
  CHECK(s.battery_power > 0.0f);  // charging
  CHECK_NEAR(s.battery_current, 20.0f, 0.01f);
}

TEST_CASE(registers_invalid_when_essential_missing) {
  // A view that does not cover the battery registers -> not valid.
  uint16_t words[2] = {0, 0};
  pm::RegisterView v{words, 9000, 2};
  const silent_powmr::InverterStatus s = pm::decode_status(v);
  CHECK(!s.valid);
}

TEST_CASE(registers_payload_to_words_little_endian) {
  // Device sends each register low byte first; reg value 0x1234 -> bytes 34 12.
  const uint8_t payload[] = {0x34, 0x12, 0xFA, 0x00};
  uint16_t words[2] = {};
  pm::payload_to_words(payload, 2, words, /*little_endian=*/true);
  CHECK_EQ(words[0], static_cast<uint16_t>(0x1234));
  CHECK_EQ(words[1], static_cast<uint16_t>(0x00FA));

  // Big-endian interpretation for comparison.
  pm::payload_to_words(payload, 2, words, /*little_endian=*/false);
  CHECK_EQ(words[0], static_cast<uint16_t>(0x3412));
  CHECK_EQ(words[1], static_cast<uint16_t>(0xFA00));
}
