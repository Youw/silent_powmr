#include "diag_probe.hpp"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "silent_powmr/crc16.h"
#include "silent_powmr/modbus.h"
#include "uart_transport.hpp"

namespace diag {
namespace {
constexpr char kTag[] = "probe";
silent_powmr::UartTransport* s_uart = nullptr;
SemaphoreHandle_t s_lock = nullptr;

// Production default the UART is left at after any probe read, so poll() (which
// does not set baud/pins) always runs at the confirmed config.
constexpr int kDefBaud = 2400;
constexpr int kDefTx = 17;
constexpr int kDefRx = 16;

void to_hex(const uint8_t* d, size_t n, char* out, size_t cap, bool spaced) {
  size_t o = 0;
  const char* fmt = spaced ? "%02X " : "%02X";
  for (size_t i = 0; i < n && o + 3 < cap; ++i) {
    o += std::snprintf(out + o, cap - o, fmt, d[i]);
  }
  if (o < cap) out[o] = '\0';
}

// One Modbus read. Returns bytes received (0 = silence). Serialized by s_lock so
// the web endpoints and the serial console never touch UART2 simultaneously.
size_t do_read(uint8_t slave, uint16_t start, uint16_t count, int baud,
               bool swap, uint8_t* resp, size_t cap, char* req_hex,
               size_t req_hex_cap) {
  if (s_uart == nullptr) return 0;
  if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
  const int tx = swap ? 16 : 17;
  const int rx = swap ? 17 : 16;
  s_uart->reconfigure(baud, tx, rx);
  vTaskDelay(pdMS_TO_TICKS(20));
  uint8_t req[16];
  const size_t rl =
      silent_powmr::modbus::build_read_holding(req, sizeof(req), slave, start, count);
  size_t n = 0;
  if (rl != 0) {
    if (req_hex) to_hex(req, rl, req_hex, req_hex_cap, true);
    n = s_uart->transfer(req, rl, resp, cap);
  }
  s_uart->reconfigure(kDefBaud, kDefTx, kDefRx);  // restore default for poll()
  if (s_lock) xSemaphoreGive(s_lock);
  return n;
}

struct Cfg {
  int baud;
  uint8_t slave;
  uint16_t start;
  uint16_t count;
};

// Candidate configs: PowMR WIFI-VM (4501/4546 @2400) and SMG-II (200-range @9600).
const Cfg kConfigs[] = {
    {2400, 5, 4501, 45},  // leodesigner/odya exact (most likely)
    {2400, 5, 4546, 16},
    {2400, 1, 4501, 45},
    {9600, 5, 4501, 45},
    {9600, 1, 200, 30},  // SMG-II family
    {9600, 1, 215, 1},
    {9600, 5, 215, 1},
    {9600, 1, 100, 40},
};

void log_one(uint8_t slave, uint16_t start, uint16_t count, int baud, bool swap) {
  uint8_t resp[300];
  char hx[700];
  char reqhx[48];
  const size_t n =
      do_read(slave, start, count, baud, swap, resp, sizeof(resp), reqhx, sizeof(reqhx));
  if (n > 0) {
    to_hex(resp, n, hx, sizeof(hx), true);
    ESP_LOGW(kTag, "swap=%d baud=%d slave=%d start=%u count=%u req=[%s] -> %u bytes: %s",
             swap ? 1 : 0, baud, slave, start, count, reqhx, (unsigned)n, hx);
  } else {
    ESP_LOGI(kTag, "swap=%d baud=%d slave=%d start=%u count=%u req=[%s] -> NO REPLY",
             swap ? 1 : 0, baud, slave, start, count, reqhx);
  }
}

void sweep_to_log() {
  ESP_LOGW(kTag, "=== SWEEP start ===");
  const bool swaps[] = {false, true};
  for (bool swap : swaps) {
    for (const Cfg& c : kConfigs) log_one(c.slave, c.start, c.count, c.baud, swap);
  }
  ESP_LOGW(kTag, "=== SWEEP done ===");
}

void console_line(char* line) {
  char cmd = 0;
  unsigned slave = 5, start = 4501, count = 45, baud = 2400, swap = 0;
  const int got =
      std::sscanf(line, " %c %u %u %u %u %u", &cmd, &slave, &start, &count, &baud, &swap);
  if ((cmd == 'r' || cmd == 'R') && got >= 4) {
    uint8_t resp[300];
    char hx[700];
    const size_t n = do_read((uint8_t)slave, (uint16_t)start, (uint16_t)count,
                             (int)baud, swap != 0, resp, sizeof(resp), nullptr, 0);
    if (n > 0) {
      to_hex(resp, n, hx, sizeof(hx), true);
      ESP_LOGW(kTag, "RESP %u bytes: %s", (unsigned)n, hx);
    } else {
      ESP_LOGW(kTag, "RESP NO REPLY");
    }
  } else if (cmd == 's' || cmd == 'S') {
    sweep_to_log();
  } else if (line[0] != '\0') {
    ESP_LOGI(kTag, "usage: r <slave> <start> <count> [baud=2400] [swap=0]  |  s");
  }
}

void task(void*) {
  // Config is known; no auto boot sweep (it left the UART mis-configured for
  // poll()). The sweep is still available via the 's' console command / /api/sweep.
  vTaskDelay(pdMS_TO_TICKS(1500));
  char line[80];
  size_t li = 0;
  for (;;) {
    const int c = getchar();
    if (c == EOF) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (c == '\r' || c == '\n') {
      line[li] = '\0';
      if (li > 0) console_line(line);
      li = 0;
    } else if (li < sizeof(line) - 1) {
      line[li++] = static_cast<char>(c);
    }
  }
}
}  // namespace

// ---- Public API used by the web endpoints (no serial needed) ----

size_t diag_probe_read_raw(uint8_t slave, uint16_t start, uint16_t count,
                           int baud, bool swap, uint8_t* resp, size_t cap) {
  return do_read(slave, start, count, baud, swap, resp, cap, nullptr, 0);
}

bool diag_probe_write_reg(uint8_t slave, uint16_t reg, uint16_t value, int baud,
                          bool swap, char* resp_hex, size_t resp_cap) {
  if (s_uart == nullptr) return false;
  if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
  const int tx = swap ? 16 : 17;
  const int rx = swap ? 17 : 16;
  s_uart->reconfigure(baud, tx, rx);
  vTaskDelay(pdMS_TO_TICKS(20));

  uint8_t req[16], resp[32];
  bool ok = false;
  size_t n = 0;
  // The bus occasionally drops a request, so retry the whole write. Each attempt
  // tries function 0x06, then 0x10 (a few settings only accept write-multiple).
  for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
    if (attempt) vTaskDelay(pdMS_TO_TICKS(40));
    size_t rl = silent_powmr::modbus::build_write_single(req, sizeof(req), slave, reg, value);
    n = s_uart->transfer(req, rl, resp, sizeof(resp));
    ok = n > 0 && silent_powmr::modbus::parse_write_single(resp, n, slave, reg, value) ==
                      silent_powmr::modbus::ParseError::kOk;
    if (ok) break;
    vTaskDelay(pdMS_TO_TICKS(30));
    const uint16_t vals[1] = {value};
    rl = silent_powmr::modbus::build_write_multiple(req, sizeof(req), slave, reg, vals, 1);
    n = s_uart->transfer(req, rl, resp, sizeof(resp));
    if (n >= 8) {
      const uint16_t crc = silent_powmr::modbus_crc16(resp, n - 2);
      const uint16_t got = static_cast<uint16_t>(resp[n - 2] | (resp[n - 1] << 8));
      ok = (crc == got) && resp[0] == slave && resp[1] == 0x10;
    }
  }

  if (resp_hex && resp_cap) to_hex(resp, n, resp_hex, resp_cap, false);
  s_uart->reconfigure(kDefBaud, kDefTx, kDefRx);
  if (s_lock) xSemaphoreGive(s_lock);
  ESP_LOGW(kTag, "write reg %u = %u -> %s", reg, value, ok ? "ok" : "FAILED");
  return ok;
}

size_t diag_probe_sweep_json(char* out, size_t cap) {
  size_t o = 0;
  o += std::snprintf(out + o, cap - o, "{\"results\":[");
  bool first = true;
  const bool swaps[] = {false, true};
  for (bool swap : swaps) {
    for (const Cfg& c : kConfigs) {
      if (o > cap - 260) break;
      uint8_t resp[300];
      const size_t n =
          do_read(c.slave, c.start, c.count, c.baud, swap, resp, sizeof(resp), nullptr, 0);
      o += std::snprintf(out + o, cap - o,
                         "%s{\"swap\":%d,\"baud\":%d,\"slave\":%d,\"start\":%u,"
                         "\"count\":%u,\"n\":%u",
                         first ? "" : ",", swap ? 1 : 0, c.baud, c.slave, c.start,
                         c.count, (unsigned)n);
      first = false;
      if (n > 0) {
        char hx[160];
        to_hex(resp, n < 48 ? n : 48, hx, sizeof(hx), false);
        o += std::snprintf(out + o, cap - o, ",\"hex\":\"%s\"", hx);
      }
      o += std::snprintf(out + o, cap - o, "}");
    }
  }
  o += std::snprintf(out + o, cap - o, "]}");
  return o;
}

void diag_probe_start(silent_powmr::UartTransport* uart) {
  s_uart = uart;
  s_lock = xSemaphoreCreateMutex();
  xTaskCreate(task, "probe", 4096, nullptr, 5, nullptr);
}

}  // namespace diag
