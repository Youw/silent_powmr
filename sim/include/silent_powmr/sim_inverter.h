#pragma once
// Host-only simulated PowMR Modbus slave. Implements ModbusTransport so the same
// InverterClient / control loop used on the ESP32 can be driven in unit tests.
#include <cstddef>
#include <cstdint>
#include <map>

#include "silent_powmr/transport.h"

namespace silent_powmr {

class SimInverter : public ModbusTransport {
 public:
  explicit SimInverter(uint8_t slave = 5, bool little_endian = true);

  size_t transfer(const uint8_t* req, size_t req_len, uint8_t* resp,
                  size_t resp_cap) override;

  // --- Test controls (logical values; byte order handled internally) ---
  void set_register(uint16_t addr, uint16_t value);
  uint16_t get_register(uint16_t addr) const;
  void set_soc(int soc);
  void set_grid(bool present);
  void set_charger_priority(uint16_t value);
  uint16_t charger_priority() const;
  void set_fault(uint16_t error_code);
  void set_online(bool online) { online_ = online; }  // simulate bus up/down

 private:
  void prime_defaults();

  uint8_t slave_;
  bool le_;
  bool online_ = true;
  std::map<uint16_t, uint16_t> regs_;
};

}  // namespace silent_powmr
