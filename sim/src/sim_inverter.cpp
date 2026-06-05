#include "silent_powmr/sim_inverter.h"

#include "silent_powmr/crc16.h"
#include "silent_powmr/modbus.h"
#include "silent_powmr/registers.h"

namespace silent_powmr {

namespace {
void put_word(uint8_t* p, uint16_t v, bool little_endian) {
  if (little_endian) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
  } else {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v & 0xFF);
  }
}
size_t finish_with_crc(uint8_t* resp, size_t n) {
  const uint16_t crc = modbus_crc16(resp, n);
  resp[n] = static_cast<uint8_t>(crc & 0xFF);
  resp[n + 1] = static_cast<uint8_t>(crc >> 8);
  return n + 2;
}
}  // namespace

SimInverter::SimInverter(uint8_t slave, bool little_endian)
    : slave_(slave), le_(little_endian) {
  prime_defaults();
}

void SimInverter::prime_defaults() {
  set_register(powmr::reg::kBatteryVoltage, 254);  // 25.4 V
  set_register(powmr::reg::kBatterySoc, 100);
  set_register(powmr::reg::kLoadVoltage, 2300);    // 230 V
  set_register(powmr::reg::kLoadPower, 200);
  set_register(powmr::reg::kLoadVa, 230);
  set_register(powmr::reg::kChargerStatus, 1);
  set_charger_priority(powmr::charger_priority::kSolarAndUtility);
  set_grid(true);
}

void SimInverter::set_register(uint16_t addr, uint16_t value) {
  regs_[addr] = value;
}

uint16_t SimInverter::get_register(uint16_t addr) const {
  const auto it = regs_.find(addr);
  return it == regs_.end() ? uint16_t{0} : it->second;
}

void SimInverter::set_soc(int soc) {
  set_register(powmr::reg::kBatterySoc, static_cast<uint16_t>(soc));
}

void SimInverter::set_charger_priority(uint16_t value) {
  set_register(powmr::reg::kChargerSourcePriority, value);
}

uint16_t SimInverter::charger_priority() const {
  return get_register(powmr::reg::kChargerSourcePriority);
}

void SimInverter::set_fault(uint16_t error_code) {
  set_register(powmr::reg::kErrorCode, error_code);
}

void SimInverter::set_grid(bool present) {
  set_register(powmr::reg::kGridVoltage, present ? 2300 : 0);
  uint16_t flags = get_register(powmr::reg::kStatusFlags);
  if (present) {
    flags &= static_cast<uint16_t>(~powmr::flag::kOnBattery);
  } else {
    flags |= powmr::flag::kOnBattery;
  }
  set_register(powmr::reg::kStatusFlags, flags);
}

size_t SimInverter::transfer(const uint8_t* req, size_t len, uint8_t* resp,
                             size_t cap) {
  if (!online_ || len < 4) return 0;

  // Validate CRC; a real bus would just produce no/garbage response.
  const uint16_t want = modbus_crc16(req, len - 2);
  const uint16_t got = static_cast<uint16_t>(req[len - 2] | (req[len - 1] << 8));
  if (want != got) return 0;
  if (req[0] != slave_) return 0;

  const uint8_t fn = req[1];

  if (fn == modbus::kReadHoldingRegisters) {
    const uint16_t start = static_cast<uint16_t>((req[2] << 8) | req[3]);
    const uint16_t count = static_cast<uint16_t>((req[4] << 8) | req[5]);
    const size_t need = 3 + static_cast<size_t>(count) * 2 + 2;
    if (cap < need) return 0;
    resp[0] = slave_;
    resp[1] = fn;
    resp[2] = static_cast<uint8_t>(count * 2);
    for (uint16_t i = 0; i < count; ++i) {
      const uint16_t addr = static_cast<uint16_t>(start + i);
      // Settings registers (5000+) are big-endian; measurements little-endian.
      put_word(resp + 3 + i * 2, get_register(addr), addr < 5000 ? le_ : false);
    }
    return finish_with_crc(resp, 3 + static_cast<size_t>(count) * 2);
  }

  if (fn == modbus::kWriteSingleRegister) {
    const uint16_t reg = static_cast<uint16_t>((req[2] << 8) | req[3]);
    const uint16_t val = static_cast<uint16_t>((req[4] << 8) | req[5]);
    set_register(reg, val);  // writes are standard big-endian -> logical value
    if (cap < 8) return 0;
    for (int i = 0; i < 6; ++i) resp[i] = req[i];  // echo
    return finish_with_crc(resp, 6);
  }

  if (fn == modbus::kWriteMultipleRegisters) {
    const uint16_t start = static_cast<uint16_t>((req[2] << 8) | req[3]);
    const uint16_t count = static_cast<uint16_t>((req[4] << 8) | req[5]);
    for (uint16_t i = 0; i < count; ++i) {
      const uint16_t v =
          static_cast<uint16_t>((req[7 + i * 2] << 8) | req[8 + i * 2]);
      set_register(static_cast<uint16_t>(start + i), v);
    }
    if (cap < 8) return 0;
    for (int i = 0; i < 6; ++i) resp[i] = req[i];  // echo addr+qty
    return finish_with_crc(resp, 6);
  }

  // Unsupported function -> Modbus exception (illegal function).
  if (cap < 5) return 0;
  resp[0] = slave_;
  resp[1] = static_cast<uint8_t>(fn | 0x80);
  resp[2] = 0x01;
  return finish_with_crc(resp, 3);
}

}  // namespace silent_powmr
