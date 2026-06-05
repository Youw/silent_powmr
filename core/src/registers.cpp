#include "silent_powmr/registers.h"

#include "silent_powmr/modbus.h"

namespace silent_powmr::powmr {

void payload_to_words(const uint8_t* payload, uint16_t count, uint16_t* out,
                      bool little_endian) {
  for (uint16_t i = 0; i < count; ++i) {
    out[i] = little_endian ? modbus::reg_le(payload, i)
                           : modbus::reg_be(payload, i);
  }
}

InverterStatus decode_status(const RegisterView& v) {
  InverterStatus s;

  auto scaled = [&](uint16_t addr, float scale) -> float {
    return v.has(addr) ? static_cast<float>(v.get(addr)) * scale : 0.0f;
  };
  auto integer = [&](uint16_t addr) -> int {
    return v.has(addr) ? static_cast<int>(v.get(addr)) : 0;
  };

  s.grid_voltage = scaled(reg::kGridVoltage, kVoltageScale);
  s.load_voltage = scaled(reg::kLoadVoltage, kVoltageScale);
  s.battery_voltage = scaled(reg::kBatteryVoltage, kVoltageScale);
  s.battery_soc = integer(reg::kBatterySoc);
  s.charge_current = scaled(reg::kBatteryChargeCurrent, kCurrentScale);
  s.discharge_current = scaled(reg::kBatteryDischargeCurrent, kCurrentScale);
  s.load_power = integer(reg::kLoadPower);
  s.load_va = integer(reg::kLoadVa);
  s.load_percent = integer(reg::kLoadPercent);
  s.error_code = static_cast<uint16_t>(integer(reg::kErrorCode));
  s.output_source_priority =
      static_cast<uint8_t>(integer(reg::kOutputSourcePriorityRead));
  s.status_flags = static_cast<uint16_t>(integer(reg::kStatusFlags));
  s.charger_status = static_cast<uint8_t>(integer(reg::kChargerStatus));
  s.temperature = scaled(reg::kTemperature, kTempScale);

  // Derived quantities.
  s.battery_current = s.charge_current - s.discharge_current;
  s.battery_power = s.battery_current * s.battery_voltage;

  // Apparent load current from VA (preferred) or active power, over AC-out volts.
  if (s.load_voltage > 1.0f) {
    const float va = s.load_va > 0 ? static_cast<float>(s.load_va)
                                   : static_cast<float>(s.load_power);
    s.load_current = va / s.load_voltage;
  }

  // Grid presence is taken from the AC-input voltage (reg 4502). The status-flag
  // bit odya labels "on battery" (0x100) is SET while on grid on this unit, so it
  // is intentionally NOT used. (Confirm 4502 drops to ~0 during an actual outage.)
  s.grid_present = s.grid_voltage >= kGridPresentVolts;

  // Consider the snapshot valid only if the essential registers were present.
  s.valid = v.has(reg::kBatteryVoltage) && v.has(reg::kBatterySoc);
  return s;
}

}  // namespace silent_powmr::powmr
