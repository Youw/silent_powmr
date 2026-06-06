#include "silent_powmr/crc16.h"

#include <cstdint>

#include "test_framework.h"

using silent_powmr::modbus_crc16;

TEST_CASE(crc16_standard_check_value) {
  // The canonical CRC-16/MODBUS check value over "123456789" is 0x4B37.
  const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  CHECK_EQ(modbus_crc16(data, sizeof(data)), static_cast<uint16_t>(0x4B37));
}

TEST_CASE(crc16_empty_is_init) {
  CHECK_EQ(modbus_crc16(nullptr, 0), static_cast<uint16_t>(0xFFFF));
}

TEST_CASE(crc16_known_read_frame) {
  // Read Holding Registers: slave 0x01, fn 0x03, addr 0x0000, qty 0x0001.
  // Expected CRC 0x0A84 -> on the wire appended as 0x84 0x0A.
  const uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
  CHECK_EQ(modbus_crc16(frame, sizeof(frame)), static_cast<uint16_t>(0x0A84));
}
