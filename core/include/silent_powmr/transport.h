#pragma once
#include <cstddef>
#include <cstdint>

namespace silent_powmr {

// Abstract byte transport for a Modbus master: send a request frame and read the
// response. The host tests use a simulated inverter; the firmware uses UART.
class ModbusTransport {
 public:
  virtual ~ModbusTransport() = default;

  // Send `req_len` bytes, then read a response into `resp` (capacity `resp_cap`).
  // Returns the response length, or 0 on timeout / bus error.
  virtual size_t transfer(const uint8_t* req, size_t req_len, uint8_t* resp,
                          size_t resp_cap) = 0;
};

}  // namespace silent_powmr
