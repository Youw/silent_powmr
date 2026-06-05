#pragma once
// ESP-IDF UART implementation of ModbusTransport (RS232 via MAX3232).
#include "driver/uart.h"
#include "silent_powmr/transport.h"

namespace silent_powmr {

class UartTransport : public ModbusTransport {
 public:
  UartTransport(uart_port_t port, int tx_gpio, int rx_gpio, int baud = 2400)
      : port_(port), tx_(tx_gpio), rx_(rx_gpio), baud_(baud) {}

  void begin();
  // Change baud + TX/RX pins on the fly (driver must already be installed).
  void reconfigure(int baud, int tx_gpio, int rx_gpio);
  size_t transfer(const uint8_t* req, size_t req_len, uint8_t* resp,
                  size_t resp_cap) override;

 private:
  uart_port_t port_;
  int tx_;
  int rx_;
  int baud_;
};

}  // namespace silent_powmr
