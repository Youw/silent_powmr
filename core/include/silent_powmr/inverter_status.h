#pragma once
#include <cstdint>

namespace silent_powmr {

// Decoded, human-meaningful snapshot of the inverter, produced from a block of
// Modbus registers. All scaling has already been applied.
struct InverterStatus {
  bool valid = false;  // true if the essential registers decoded

  float grid_voltage = 0.0f;      // V (AC input / utility)
  float load_voltage = 0.0f;      // V (AC output)
  float battery_voltage = 0.0f;   // V
  int battery_soc = 0;            // %

  float charge_current = 0.0f;     // A into the battery
  float discharge_current = 0.0f;  // A out of the battery

  int load_power = 0;    // W
  int load_va = 0;       // VA
  int load_percent = 0;  // %
  float load_current = 0.0f;  // A (apparent, derived from VA / AC-output voltage)

  uint16_t status_flags = 0;    // raw bitfield (see powmr::flag)
  uint8_t charger_status = 0;   // 0=off, 1=idle, 2=active
  uint16_t error_code = 0;      // 0 = no fault
  float temperature = 0.0f;     // deg C
  uint8_t output_source_priority = 0;

  // ---- Derived ----
  float battery_current = 0.0f;  // signed: + charging, - discharging (A)
  float battery_power = 0.0f;    // signed: + charging, - discharging (W)
  bool grid_present = false;     // utility power available
};

}  // namespace silent_powmr
