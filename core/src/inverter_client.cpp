#include "silent_powmr/inverter_client.h"

#include "silent_powmr/modbus.h"
#include "silent_powmr/registers.h"

namespace silent_powmr {

bool InverterClient::read_block(uint16_t start, uint16_t count, uint16_t* span,
                                int attempts) {
  uint8_t raw[256];
  for (int attempt = 0; attempt < attempts; ++attempt) {
    const size_t n = read_holding_raw(start, count, raw, sizeof(raw));
    if (n == static_cast<size_t>(count) * 2) {
      uint16_t tmp[125];
      powmr::payload_to_words(raw, count, tmp, le_);
      for (uint16_t i = 0; i < count; ++i) {
        const uint16_t addr = static_cast<uint16_t>(start + i);
        if (addr >= powmr::kPollBase &&
            addr < powmr::kPollBase + powmr::kPollCount) {
          span[addr - powmr::kPollBase] = tmp[i];
        }
      }
      return true;
    }
  }
  return false;
}

bool InverterClient::poll(InverterStatus& out) {
  // This unit only answers two specific windows (see registers.h); read both and
  // decode from the merged span. The gap between them stays zero.
  uint16_t words[powmr::kPollCount] = {};
  const bool b1 = read_block(powmr::kBlock1Base, powmr::kBlock1Count, words);
  const bool b2 = read_block(powmr::kBlock2Base, powmr::kBlock2Count, words);
  // The error/fault code (reg 4530) lives in the unreadable gap; try a single
  // best-effort read — it either works or leaves the field at 0.
  read_block(powmr::reg::kErrorCode, 1, words, 1);
  if (!b1 && !b2) return false;
  const powmr::RegisterView view{words, powmr::kPollBase, powmr::kPollCount};
  out = powmr::decode_status(view);
  out.valid = b1;  // battery voltage + SoC come from block 1
  return b1;
}

bool InverterClient::read_charger_mode(ChargerMode& out) {
  uint8_t req[16];
  uint8_t resp[16];
  const size_t n = modbus::build_read_holding(
      req, sizeof(req), slave_, powmr::reg::kChargerSourcePriority, 1);
  const size_t r = t_.transfer(req, n, resp, sizeof(resp));
  if (r == 0) return false;

  modbus::ReadResponse rr;
  if (modbus::parse_read_holding(resp, r, slave_, 1, rr) !=
      modbus::ParseError::kOk) {
    return false;
  }
  // Reg 5017 is a settings register: always big-endian, regardless of the
  // little-endian byte order used for 4500-range measurements.
  const uint16_t value = modbus::reg_be(rr.payload, 0);
  out = charger_mode_from_register(value);
  return true;
}

bool InverterClient::set_charger_mode(ChargerMode mode, bool verify) {
  const uint16_t logical = charger_priority_register_value(mode);
  uint8_t req[16];
  uint8_t resp[16];
  const size_t n = modbus::build_write_single(
      req, sizeof(req), slave_, powmr::reg::kChargerSourcePriority, logical);
  const size_t r = t_.transfer(req, n, resp, sizeof(resp));
  if (r == 0) return false;
  if (modbus::parse_write_single(resp, r, slave_,
                                 powmr::reg::kChargerSourcePriority,
                                 logical) != modbus::ParseError::kOk) {
    return false;
  }
  if (verify) {
    ChargerMode current;
    if (!read_charger_mode(current)) return false;
    return current == mode;
  }
  return true;
}

size_t InverterClient::read_holding_raw(uint16_t start, uint16_t count,
                                        uint8_t* out, size_t out_cap) {
  const size_t want = static_cast<size_t>(count) * 2;
  if (out == nullptr || out_cap < want) return 0;
  uint8_t req[modbus::kMaxFrame];
  uint8_t resp[modbus::kMaxFrame];
  const size_t n =
      modbus::build_read_holding(req, sizeof(req), slave_, start, count);
  if (n == 0) return 0;
  const size_t r = t_.transfer(req, n, resp, sizeof(resp));
  if (r == 0) return 0;
  modbus::ReadResponse rr;
  if (modbus::parse_read_holding(resp, r, slave_, count, rr) !=
      modbus::ParseError::kOk) {
    return 0;
  }
  for (size_t i = 0; i < want; ++i) out[i] = rr.payload[i];
  return want;
}

}  // namespace silent_powmr
