#include "silent_powmr/crc16.h"

namespace silent_powmr {

uint16_t modbus_crc16(const uint8_t* data, size_t len) noexcept {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]);
    for (int bit = 0; bit < 8; ++bit) {
      if (crc & 0x0001u) {
        crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001u);
      } else {
        crc = static_cast<uint16_t>(crc >> 1);
      }
    }
  }
  return crc;
}

}  // namespace silent_powmr
