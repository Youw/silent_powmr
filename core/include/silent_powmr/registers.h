#pragma once
// PowMR (POW-HVM4.2K-24V-D) Modbus register map and decode.
//
// Addresses come from odya/esphome-powmr-hybrid-inverter (docs/registers-map.md,
// a 2.4K unit) cross-checked with leodesigner/powmr_comm. Addresses are expected
// to match across the HVM family; the SCALING factors and the CHARGER-PRIORITY
// enum values below are best-known defaults and MUST be confirmed against this
// physical unit (see docs bring-up guide). They are centralized here so a single
// edit recalibrates the whole firmware.
#include <cstdint>

#include "silent_powmr/inverter_status.h"

namespace silent_powmr::powmr {

namespace reg {
// --- Read (function 3, holding registers) ---
constexpr uint16_t kGridVoltage = 4502;             // V
constexpr uint16_t kGridFrequency = 4503;           // Hz
constexpr uint16_t kBatteryVoltage = 4506;          // V
constexpr uint16_t kBatterySoc = 4507;              // %
constexpr uint16_t kBatteryChargeCurrent = 4508;    // A
constexpr uint16_t kBatteryDischargeCurrent = 4509; // A
constexpr uint16_t kLoadVoltage = 4510;             // V (AC output)
constexpr uint16_t kLoadPower = 4512;               // W
constexpr uint16_t kLoadVa = 4513;                  // VA
constexpr uint16_t kLoadPercent = 4514;             // %
constexpr uint16_t kErrorCode = 4530;
constexpr uint16_t kOutputSourcePriorityRead = 4537;
constexpr uint16_t kStatusFlags = 4553;
constexpr uint16_t kChargerStatus = 4555;           // 0=off,1=idle,2=active
constexpr uint16_t kTemperature = 4557;             // deg C

// --- Write (holding registers) ---
constexpr uint16_t kChargerSourcePriority = 5017;   // 0..3  <- the quiet/charge lever
constexpr uint16_t kOutputSourcePriority = 5018;    // 0..2
constexpr uint16_t kMaxTotalChargeCurrent = 5022;
constexpr uint16_t kUtilityChargeCurrent = 5024;
}  // namespace reg

// CONFIRMED ON HARDWARE (POW-HVM4.2K-24V-D): this unit only answers two specific
// register windows. Reads starting at 4501, or spanning the 4515..4545 gap, are
// rejected outright (no reply) — which is why a single 61-register read fails.
constexpr uint16_t kBlock1Base = 4502;   // grid V/Hz, PV, battery V, SoC, currents, load
constexpr uint16_t kBlock1Count = 13;    // 4502..4514
constexpr uint16_t kBlock2Base = 4546;   // status flags, charger status, temperature
constexpr uint16_t kBlock2Count = 16;    // 4546..4561
// The decoder addresses into one logical span covering both windows; the unread
// gap (4515..4545) stays zero. (poll() issues the two reads above, not one.)
constexpr uint16_t kPollBase = 4502;
constexpr uint16_t kPollCount = 60;      // span 4502..4561

// ---- Scaling (CONFIRM ON HARDWARE) ----
constexpr float kVoltageScale = 0.1f;  // raw 273 -> 27.3 V (confirmed on hardware)
constexpr float kCurrentScale = 1.0f;  // battery currents are whole amps: raw 9 -> 9 A
                                       // (confirmed: page showed 0.9 A at x0.1 vs ~8 A on BMS)
constexpr float kTempScale = 1.0f;
constexpr float kGridPresentVolts = 180.0f;  // grid considered present above this

// ---- Charger source priority values (reg 5017) — HARDWARE-CONFIRMED ----
// These differ from odya's generic 0..3 map: on this unit the menu reports
// CSO=0, SNU=1, OSO=2 (verified live by switching modes and reading reg 5017).
// IMPORTANT: settings registers (5017/5018) are read and written BIG-ENDIAN,
// unlike the little-endian 4500-range measurement registers.
namespace charger_priority {
constexpr uint16_t kSolarFirst = 0;       // CSO — utility charges when no solar (fans run)
constexpr uint16_t kSolarAndUtility = 1;  // SNU — charges from grid -> CHARGE (fans run)
constexpr uint16_t kSolarOnly = 2;        // OSO — no grid charging  -> QUIET (fans off)
constexpr uint16_t kUtilityFirst = 3;     // CUL — position on this unit unverified
}  // namespace charger_priority

// ---- Status flag bits (reg 4553) ----
namespace flag {
constexpr uint16_t kOnBattery = 0x0100;
constexpr uint16_t kLoadOff = 0x1000;
constexpr uint16_t kLoadEnabled = 0x4000;
}  // namespace flag

// A window into a block of already-byte-order-corrected register words, indexed
// by absolute Modbus address.
struct RegisterView {
  const uint16_t* words = nullptr;
  uint16_t base = 0;
  uint16_t count = 0;

  bool has(uint16_t addr) const {
    return words != nullptr && addr >= base && addr < base + count;
  }
  uint16_t get(uint16_t addr) const { return words[addr - base]; }
};

// Convert a raw Modbus payload (count 16-bit registers) into host words,
// applying this unit's byte order (little_endian = byte-swapped vs standard).
void payload_to_words(const uint8_t* payload, uint16_t count, uint16_t* out,
                      bool little_endian);

// Decode a register window into an InverterStatus (applies scaling + derives
// grid_present / battery power).
InverterStatus decode_status(const RegisterView& view);

}  // namespace silent_powmr::powmr
