#pragma once
#include "wifi_prov.hpp"

namespace silent_powmr {
class InverterClient;
}

namespace diag {
// Start the HTTP server. In AP mode it serves the Wi-Fi provisioning portal; in
// STA mode it serves the read-only register diagnostics page.
void start_web(WifiMode mode, silent_powmr::InverterClient* client);
}  // namespace diag
