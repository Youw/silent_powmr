#pragma once
// Mapping between the inverter/controller state and Tuya "Data Points" (DPs).
//
// This layer is independent of the Tuya SDK: it produces/consumes a neutral
// DpValue list, and the firmware translates those into the SDK's calls. That
// keeps the mapping host-testable. The DP IDs below must match the DPs created
// in the Tuya IoT product (documented in the README).
#include <cstddef>
#include <cstdint>

#include "silent_powmr/inverter_status.h"
#include "silent_powmr/quiet_controller.h"

namespace silent_powmr::dp {

// DP IDs (mirror these when defining the Tuya product).
enum DpId : uint8_t {
  // Telemetry (reported by the device).
  kBatteryVoltage = 101,   // value, 0.1 V
  kBatterySoc = 102,       // value, %
  kLoadPower = 103,        // value, W
  kLoadCurrent = 104,      // value, 0.1 A
  kGridPresent = 105,      // bool
  kBatteryPower = 106,     // value, W   (signed: + charging, - discharging)
  kBatteryCurrent = 107,   // value, 0.1 A (signed)
  kChargerMode = 108,      // enum: 0=charge (Solar+Utility), 1=quiet (Solar only)
  kTemperature = 109,      // value, 0.1 C
  kFaultCode = 110,        // value (0 = no fault)

  // Control (written by the app; also reported so the UI reflects state).
  kAutoQuietEnable = 120,       // bool
  kManualChargerMode = 121,     // enum: 0=charge, 1=quiet (used when auto off)
  kRechargeSocThreshold = 122,  // value, %
};

enum class DpType : uint8_t { kBool, kValue, kEnum };

struct DpValue {
  uint8_t id;
  DpType type;
  int32_t value;  // bool: 0/1, enum: index, value: scaled integer
};

// Enum indices for kChargerMode / kManualChargerMode.
constexpr int32_t kModeCharge = 0;
constexpr int32_t kModeQuiet = 1;

// Build the telemetry DP list from a status snapshot + the active mode.
// Returns the number of DpValues written (<= cap).
size_t build_telemetry(const InverterStatus& status, ChargerMode current_mode,
                       DpValue* out, size_t cap) noexcept;

// Report the current control config back as DPs (so the app shows current state).
size_t build_config_report(const QuietControllerConfig& cfg, DpValue* out,
                           size_t cap) noexcept;

struct ControlUpdate {
  bool changed_auto = false;
  bool changed_manual = false;
  bool changed_threshold = false;
  bool any() const { return changed_auto || changed_manual || changed_threshold; }
};

// Apply an incoming control DP write to `cfg`. Returns true if `dp_id` is a
// recognized control DP (value is range-checked/clamped).
bool apply_control(uint8_t dp_id, int32_t value, QuietControllerConfig& cfg,
                   ControlUpdate& update) noexcept;

}  // namespace silent_powmr::dp
