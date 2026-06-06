#include "silent_powmr/modbus.h"

#include <cstdint>
#include <cstring>

#include "silent_powmr/crc16.h"
#include "test_framework.h"

namespace mb = silent_powmr::modbus;
using silent_powmr::modbus_crc16;

namespace {
// Append a valid CRC to a hand-built frame; returns total length.
size_t with_crc(uint8_t* f, size_t n) {
  const uint16_t c = modbus_crc16(f, n);
  f[n] = static_cast<uint8_t>(c & 0xFF);
  f[n + 1] = static_cast<uint8_t>(c >> 8);
  return n + 2;
}
}  // namespace

TEST_CASE(modbus_build_read_known_vector) {
  // slave 1, fn3, addr 0, qty 1 -> 01 03 00 00 00 01 84 0A
  uint8_t out[mb::kMaxFrame];
  const size_t n = mb::build_read_holding(out, sizeof(out), 1, 0x0000, 0x0001);
  REQUIRE(n == 8);
  const uint8_t expect[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
  CHECK(std::memcmp(out, expect, 8) == 0);
}

TEST_CASE(modbus_build_read_powmr_range) {
  // The real poll: slave 5, start 4501, count 45. Validate header + self CRC.
  uint8_t out[mb::kMaxFrame];
  const size_t n = mb::build_read_holding(out, sizeof(out), 5, 4501, 45);
  REQUIRE(n == 8);
  CHECK_EQ(out[0], static_cast<uint8_t>(5));
  CHECK_EQ(out[1], static_cast<uint8_t>(0x03));
  CHECK_EQ(out[2], static_cast<uint8_t>(4501 >> 8));
  CHECK_EQ(out[3], static_cast<uint8_t>(4501 & 0xFF));
  CHECK_EQ(out[4], static_cast<uint8_t>(0));
  CHECK_EQ(out[5], static_cast<uint8_t>(45));
  const uint16_t crc = modbus_crc16(out, 6);
  CHECK_EQ(out[6], static_cast<uint8_t>(crc & 0xFF));
  CHECK_EQ(out[7], static_cast<uint8_t>(crc >> 8));
}

TEST_CASE(modbus_buffer_too_small_returns_zero) {
  uint8_t small[4];
  CHECK_EQ(mb::build_read_holding(small, sizeof(small), 5, 4501, 45),
           static_cast<size_t>(0));
}

TEST_CASE(modbus_parse_read_roundtrip_be_and_le) {
  // Response: slave 5, fn3, 2 registers: 0x1234, 0x00FA.
  uint8_t f[mb::kMaxFrame];
  f[0] = 5; f[1] = 0x03; f[2] = 0x04;
  f[3] = 0x12; f[4] = 0x34;  // reg0
  f[5] = 0x00; f[6] = 0xFA;  // reg1
  const size_t len = with_crc(f, 7);

  mb::ReadResponse r;
  const auto err = mb::parse_read_holding(f, len, 5, 2, r);
  CHECK(err == mb::ParseError::kOk);
  CHECK_EQ(r.count, static_cast<uint16_t>(2));
  CHECK_EQ(mb::reg_be(r.payload, 0), static_cast<uint16_t>(0x1234));
  CHECK_EQ(mb::reg_be(r.payload, 1), static_cast<uint16_t>(0x00FA));
  // Byte-swapped interpretation (this unit's quirk):
  CHECK_EQ(mb::reg_le(r.payload, 0), static_cast<uint16_t>(0x3412));
  CHECK_EQ(mb::reg_le(r.payload, 1), static_cast<uint16_t>(0xFA00));
}

TEST_CASE(modbus_parse_read_bad_crc) {
  uint8_t f[mb::kMaxFrame];
  f[0] = 5; f[1] = 0x03; f[2] = 0x02; f[3] = 0x01; f[4] = 0x02;
  size_t len = with_crc(f, 5);
  f[len - 1] ^= 0xFF;  // corrupt CRC
  mb::ReadResponse r;
  CHECK(mb::parse_read_holding(f, len, 5, 1, r) == mb::ParseError::kBadCrc);
}

TEST_CASE(modbus_parse_read_wrong_slave) {
  uint8_t f[mb::kMaxFrame];
  f[0] = 9; f[1] = 0x03; f[2] = 0x02; f[3] = 0x01; f[4] = 0x02;
  const size_t len = with_crc(f, 5);
  mb::ReadResponse r;
  CHECK(mb::parse_read_holding(f, len, 5, 1, r) == mb::ParseError::kWrongSlave);
}

TEST_CASE(modbus_parse_read_exception) {
  // Exception response: fn | 0x80, code 0x02 (illegal data address).
  uint8_t f[mb::kMaxFrame];
  f[0] = 5; f[1] = 0x83; f[2] = 0x02;
  const size_t len = with_crc(f, 3);
  mb::ReadResponse r;
  CHECK(mb::parse_read_holding(f, len, 5, 0, r) == mb::ParseError::kException);
}

TEST_CASE(modbus_parse_read_length_mismatch) {
  // byte_count says 4 but only 2 data bytes present.
  uint8_t f[mb::kMaxFrame];
  f[0] = 5; f[1] = 0x03; f[2] = 0x04; f[3] = 0x01; f[4] = 0x02;
  const size_t len = with_crc(f, 5);
  mb::ReadResponse r;
  CHECK(mb::parse_read_holding(f, len, 5, 0, r) ==
        mb::ParseError::kLengthMismatch);
}

TEST_CASE(modbus_write_single_build_and_echo) {
  // Set charger source priority (reg 5017) to value 3 (solar-only) on slave 5.
  uint8_t req[mb::kMaxFrame];
  const size_t n = mb::build_write_single(req, sizeof(req), 5, 5017, 3);
  REQUIRE(n == 8);
  CHECK_EQ(req[0], static_cast<uint8_t>(5));
  CHECK_EQ(req[1], static_cast<uint8_t>(0x06));
  CHECK_EQ(req[2], static_cast<uint8_t>(5017 >> 8));
  CHECK_EQ(req[3], static_cast<uint8_t>(5017 & 0xFF));
  CHECK_EQ(req[5], static_cast<uint8_t>(3));
  // A compliant slave echoes the request verbatim.
  CHECK(mb::parse_write_single(req, n, 5, 5017, 3) == mb::ParseError::kOk);
  // Wrong echoed value is detected.
  CHECK(mb::parse_write_single(req, n, 5, 5017, 2) ==
        mb::ParseError::kEchoMismatch);
}

TEST_CASE(modbus_write_single_exception) {
  uint8_t f[mb::kMaxFrame];
  f[0] = 5; f[1] = 0x86; f[2] = 0x04;  // 0x06 | 0x80, code 0x04
  const size_t len = with_crc(f, 3);
  CHECK(mb::parse_write_single(f, len, 5, 5017, 3) ==
        mb::ParseError::kException);
}

TEST_CASE(modbus_write_multiple_frame) {
  uint8_t out[mb::kMaxFrame];
  const uint16_t vals[] = {0x0003};
  const size_t n = mb::build_write_multiple(out, sizeof(out), 5, 5017, vals, 1);
  REQUIRE(n == 11);  // 7 header + 2 data + 2 crc
  CHECK_EQ(out[1], static_cast<uint8_t>(0x10));
  CHECK_EQ(out[6], static_cast<uint8_t>(2));   // byte count
  CHECK_EQ(out[7], static_cast<uint8_t>(0x00));
  CHECK_EQ(out[8], static_cast<uint8_t>(0x03));
  const uint16_t crc = modbus_crc16(out, 9);
  CHECK_EQ(out[9], static_cast<uint8_t>(crc & 0xFF));
  CHECK_EQ(out[10], static_cast<uint8_t>(crc >> 8));
}
