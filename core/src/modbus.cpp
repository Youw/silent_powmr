#include "silent_powmr/modbus.h"

#include "silent_powmr/crc16.h"

namespace silent_powmr::modbus {

namespace {

// Append the Modbus CRC (low byte first) after `n` bytes already in `buf`.
// Returns total length (n + 2).
size_t append_crc(uint8_t* buf, size_t n) noexcept {
  const uint16_t crc = modbus_crc16(buf, n);
  buf[n] = static_cast<uint8_t>(crc & 0xFF);
  buf[n + 1] = static_cast<uint8_t>(crc >> 8);
  return n + 2;
}

// True if the trailing two bytes are a valid CRC over the preceding bytes.
bool crc_ok(const uint8_t* frame, size_t len) noexcept {
  if (len < 2) return false;
  const uint16_t want = modbus_crc16(frame, len - 2);
  const uint16_t got = static_cast<uint16_t>(frame[len - 2] |
                                             (frame[len - 1] << 8));
  return want == got;
}

}  // namespace

const char* to_string(ParseError e) noexcept {
  switch (e) {
    case ParseError::kOk: return "ok";
    case ParseError::kTooShort: return "too-short";
    case ParseError::kBadCrc: return "bad-crc";
    case ParseError::kWrongSlave: return "wrong-slave";
    case ParseError::kException: return "exception";
    case ParseError::kUnexpectedFunction: return "unexpected-function";
    case ParseError::kLengthMismatch: return "length-mismatch";
    case ParseError::kEchoMismatch: return "echo-mismatch";
  }
  return "?";
}

size_t build_read_holding(uint8_t* out, size_t out_cap, uint8_t slave,
                          uint16_t start_reg, uint16_t count) noexcept {
  if (out_cap < 8) return 0;
  out[0] = slave;
  out[1] = kReadHoldingRegisters;
  out[2] = static_cast<uint8_t>(start_reg >> 8);
  out[3] = static_cast<uint8_t>(start_reg & 0xFF);
  out[4] = static_cast<uint8_t>(count >> 8);
  out[5] = static_cast<uint8_t>(count & 0xFF);
  return append_crc(out, 6);
}

size_t build_write_single(uint8_t* out, size_t out_cap, uint8_t slave,
                          uint16_t reg, uint16_t value) noexcept {
  if (out_cap < 8) return 0;
  out[0] = slave;
  out[1] = kWriteSingleRegister;
  out[2] = static_cast<uint8_t>(reg >> 8);
  out[3] = static_cast<uint8_t>(reg & 0xFF);
  out[4] = static_cast<uint8_t>(value >> 8);
  out[5] = static_cast<uint8_t>(value & 0xFF);
  return append_crc(out, 6);
}

size_t build_write_multiple(uint8_t* out, size_t out_cap, uint8_t slave,
                            uint16_t start_reg, const uint16_t* values,
                            uint16_t count) noexcept {
  if (count == 0 || values == nullptr) return 0;
  const size_t body = 7 + static_cast<size_t>(count) * 2;
  if (out_cap < body + 2) return 0;
  out[0] = slave;
  out[1] = kWriteMultipleRegisters;
  out[2] = static_cast<uint8_t>(start_reg >> 8);
  out[3] = static_cast<uint8_t>(start_reg & 0xFF);
  out[4] = static_cast<uint8_t>(count >> 8);
  out[5] = static_cast<uint8_t>(count & 0xFF);
  out[6] = static_cast<uint8_t>(count * 2);
  for (uint16_t i = 0; i < count; ++i) {
    out[7 + i * 2] = static_cast<uint8_t>(values[i] >> 8);
    out[8 + i * 2] = static_cast<uint8_t>(values[i] & 0xFF);
  }
  return append_crc(out, body);
}

ParseError parse_read_holding(const uint8_t* frame, size_t len,
                              uint8_t expected_slave, uint16_t expected_count,
                              ReadResponse& out) noexcept {
  if (len < 5) return ParseError::kTooShort;
  if (!crc_ok(frame, len)) return ParseError::kBadCrc;
  if (expected_slave != 0 && frame[0] != expected_slave) {
    return ParseError::kWrongSlave;
  }
  if (frame[1] == (kReadHoldingRegisters | 0x80)) return ParseError::kException;
  if (frame[1] != kReadHoldingRegisters) return ParseError::kUnexpectedFunction;

  const uint8_t byte_count = frame[2];
  if ((byte_count & 0x01) != 0) return ParseError::kLengthMismatch;
  if (len != static_cast<size_t>(3) + byte_count + 2) {
    return ParseError::kLengthMismatch;
  }
  const uint16_t count = static_cast<uint16_t>(byte_count / 2);
  if (expected_count != 0 && count != expected_count) {
    return ParseError::kLengthMismatch;
  }

  out.slave = frame[0];
  out.count = count;
  out.payload = frame + 3;
  return ParseError::kOk;
}

ParseError parse_write_single(const uint8_t* frame, size_t len,
                              uint8_t expected_slave, uint16_t expected_reg,
                              uint16_t expected_value) noexcept {
  if (len < 5) return ParseError::kTooShort;
  if (!crc_ok(frame, len)) return ParseError::kBadCrc;
  if (expected_slave != 0 && frame[0] != expected_slave) {
    return ParseError::kWrongSlave;
  }
  if (frame[1] == (kWriteSingleRegister | 0x80)) return ParseError::kException;
  if (frame[1] != kWriteSingleRegister) return ParseError::kUnexpectedFunction;
  if (len != 8) return ParseError::kLengthMismatch;

  const uint16_t reg = static_cast<uint16_t>((frame[2] << 8) | frame[3]);
  const uint16_t value = static_cast<uint16_t>((frame[4] << 8) | frame[5]);
  if (reg != expected_reg || value != expected_value) {
    return ParseError::kEchoMismatch;
  }
  return ParseError::kOk;
}

}  // namespace silent_powmr::modbus
