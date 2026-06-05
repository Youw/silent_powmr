#include "uart_transport.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace silent_powmr {
namespace {
constexpr int kRxBufSize = 512;
constexpr TickType_t kReplyTimeout = pdMS_TO_TICKS(300);
constexpr TickType_t kIdleGap = pdMS_TO_TICKS(20);
}  // namespace

void UartTransport::begin() {
  const uart_config_t cfg = {
      .baud_rate = baud_,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 0,
      .source_clk = UART_SCLK_DEFAULT,
  };
  uart_driver_install(port_, kRxBufSize, 0, 0, nullptr, 0);
  uart_param_config(port_, &cfg);
  uart_set_pin(port_, tx_, rx_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void UartTransport::reconfigure(int baud, int tx, int rx) {
  baud_ = baud;
  tx_ = tx;
  rx_ = rx;
  uart_set_baudrate(port_, baud);
  uart_set_pin(port_, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_flush_input(port_);
}

size_t UartTransport::transfer(const uint8_t* req, size_t req_len, uint8_t* resp,
                               size_t resp_cap) {
  uart_flush_input(port_);
  uart_write_bytes(port_, reinterpret_cast<const char*>(req), req_len);
  uart_wait_tx_done(port_, kReplyTimeout);

  size_t got = 0;
  TickType_t waited = 0;
  while (got < resp_cap && waited < kReplyTimeout) {
    const int n = uart_read_bytes(port_, resp + got, resp_cap - got, kIdleGap);
    if (n > 0) {
      got += static_cast<size_t>(n);
    } else {
      if (got > 0) break;
      waited += kIdleGap;
    }
  }
  return got;
}

}  // namespace silent_powmr
