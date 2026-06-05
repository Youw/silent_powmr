#pragma once
// High-level inverter I/O built on a ModbusTransport: poll status, read and set
// the charger-source-priority (the quiet/charge lever). Reused verbatim by the
// firmware (UART transport) and the host tests (simulated transport).
#include <cstdint>

#include "silent_powmr/inverter_status.h"
#include "silent_powmr/quiet_controller.h"
#include "silent_powmr/transport.h"

namespace silent_powmr {

class InverterClient {
 public:
  // read_little_endian: this unit returns register bytes byte-swapped vs the
  // Modbus standard (see registers.h). Default true.
  InverterClient(ModbusTransport& transport, uint8_t slave = 5,
                 bool read_little_endian = true)
      : t_(transport), slave_(slave), le_(read_little_endian) {}

  // Poll the full status block. Returns true and fills `out` on success.
  bool poll(InverterStatus& out);

  // Read the current charger source priority (reg 5017) as a ChargerMode.
  bool read_charger_mode(ChargerMode& out);

  // Write the charger source priority. With verify=true, re-reads and confirms.
  bool set_charger_mode(ChargerMode mode, bool verify = true);

  // Diagnostic: copy `count` holding registers' raw bytes (count*2, wire /
  // big-endian order) starting at `start` into `out`. Returns bytes copied, or
  // 0 on error. Lets a bring-up tool dump arbitrary ranges read-only.
  size_t read_holding_raw(uint16_t start, uint16_t count, uint8_t* out,
                          size_t out_cap);

 private:
  // Read one register window (with a few retries for the occasional miss) and
  // scatter it into the decode span at powmr::kPollBase. Returns true on success.
  bool read_block(uint16_t start, uint16_t count, uint16_t* span_words,
                  int attempts = 3);

  ModbusTransport& t_;
  uint8_t slave_;
  bool le_;
};

}  // namespace silent_powmr
