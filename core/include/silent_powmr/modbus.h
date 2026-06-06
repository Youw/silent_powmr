#pragma once
// Minimal, allocation-free Modbus RTU framing. Caller provides all buffers so
// this is safe to use on the ESP32 (no heap) and trivial to unit-test on host.
//
// NOTE on byte order: Modbus puts each 16-bit register on the wire big-endian
// (high byte first). This PowMR additionally needs per-register byte swapping
// for some values (see odya/esphome-powmr register notes) — that interpretation
// lives in the register-decode layer, not here. This module only frames/parses
// and hands back a pointer to the raw register payload. Use reg_be()/reg_le()
// to interpret it.
#include <cstddef>
#include <cstdint>

namespace silent_powmr::modbus {

enum FunctionCode : uint8_t {
  kReadHoldingRegisters = 0x03,
  kWriteSingleRegister = 0x06,
  kWriteMultipleRegisters = 0x10,
};

// Largest possible RTU frame.
constexpr size_t kMaxFrame = 256;

enum class ParseError : uint8_t {
  kOk = 0,
  kTooShort,
  kBadCrc,
  kWrongSlave,
  kException,            // slave replied with an exception (function | 0x80)
  kUnexpectedFunction,
  kLengthMismatch,
  kEchoMismatch,         // write echo did not match what we sent
};

const char* to_string(ParseError e) noexcept;

// ---- Request builders. Return frame length written, or 0 if buffer too small.

size_t build_read_holding(uint8_t* out, size_t out_cap, uint8_t slave,
                          uint16_t start_reg, uint16_t count) noexcept;

size_t build_write_single(uint8_t* out, size_t out_cap, uint8_t slave,
                          uint16_t reg, uint16_t value) noexcept;

// Write `count` registers from `values` (host order) starting at `start_reg`.
size_t build_write_multiple(uint8_t* out, size_t out_cap, uint8_t slave,
                            uint16_t start_reg, const uint16_t* values,
                            uint16_t count) noexcept;

// ---- Response parsers.

struct ReadResponse {
  uint8_t slave = 0;
  uint16_t count = 0;             // number of 16-bit registers in payload
  const uint8_t* payload = nullptr;  // points into `frame`; count*2 bytes
};

// expected_slave / expected_count == 0 means "do not check".
ParseError parse_read_holding(const uint8_t* frame, size_t len,
                              uint8_t expected_slave, uint16_t expected_count,
                              ReadResponse& out) noexcept;

// Validate a write-single-register echo against what was requested.
ParseError parse_write_single(const uint8_t* frame, size_t len,
                              uint8_t expected_slave, uint16_t expected_reg,
                              uint16_t expected_value) noexcept;

// ---- Register interpretation helpers (operate on ReadResponse::payload).

// Standard Modbus big-endian word (high byte first).
inline uint16_t reg_be(const uint8_t* payload, size_t index) noexcept {
  const size_t o = index * 2;
  return static_cast<uint16_t>((static_cast<uint16_t>(payload[o]) << 8) |
                               payload[o + 1]);
}

// Byte-swapped / little-endian word (low byte first) — this unit's quirk.
inline uint16_t reg_le(const uint8_t* payload, size_t index) noexcept {
  const size_t o = index * 2;
  return static_cast<uint16_t>((static_cast<uint16_t>(payload[o + 1]) << 8) |
                               payload[o]);
}

}  // namespace silent_powmr::modbus
