#pragma once
#include <cstddef>
#include <cstdint>

namespace silent_powmr {
class UartTransport;
}
namespace diag {

// One raw Modbus read at an arbitrary config (used by the /api/raw web endpoint).
// Returns bytes received (0 = silence).
size_t diag_probe_read_raw(uint8_t slave, uint16_t start, uint16_t count,
                           int baud, bool swap, uint8_t* resp, size_t cap);

// Run the full candidate sweep and write a JSON result into `out`. Returns length.
size_t diag_probe_sweep_json(char* out, size_t cap);

// Write a single holding register: tries function 0x06, then 0x10 (some settings
// registers only accept write-multiple). Returns true if the slave acknowledged.
// resp_hex (optional) receives the raw response hex of the last attempt.
bool diag_probe_write_reg(uint8_t slave, uint16_t reg, uint16_t value, int baud,
                          bool swap, char* resp_hex = nullptr, size_t resp_cap = 0);

// Start the serial Modbus probe: a boot-time sweep over candidate
// {baud, slave, start, count, pin-swap} configs (logged to the console), then an
// interactive command loop. Drive it over the USB serial port (UART0).
//   r <slave> <start> <count> [baud=2400] [swap=0]   -> one read, prints raw hex
//   s                                                -> re-run the sweep
void diag_probe_start(silent_powmr::UartTransport* uart);
}  // namespace diag
