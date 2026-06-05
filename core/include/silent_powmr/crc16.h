#pragma once
#include <cstddef>
#include <cstdint>

namespace silent_powmr {

// CRC-16/MODBUS: polynomial 0xA001 (reflected 0x8005), init 0xFFFF,
// reflected in/out, no final XOR. This is the checksum used by Modbus RTU.
//
// On the wire the 16-bit result is appended LOW byte first, then HIGH byte.
// Check value over the ASCII string "123456789" is 0x4B37.
uint16_t modbus_crc16(const uint8_t* data, size_t len) noexcept;

}  // namespace silent_powmr
