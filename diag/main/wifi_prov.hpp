#pragma once
// Minimal WiFi provisioning: try saved STA credentials; otherwise bring up a
// SoftAP portal so the user can pick a network and enter a password (saved to
// NVS). No credentials are ever compiled in. Hold BOOT at power-on to forget.
#include <cstddef>

namespace diag {

enum class WifiMode { kStation, kAccessPoint };

// Connect as a station with saved creds, or start the AP portal. Returns the
// resulting mode.
WifiMode wifi_start();

// Persist credentials (called by the provisioning page).
bool wifi_save_creds(const char* ssid, const char* pass);

// Erase saved creds and reboot (back into the AP portal).
void wifi_forget_and_restart();

// Scan and emit a JSON array: [{"ssid":"..","rssi":-50}, ...].
int wifi_scan_json(char* out, size_t cap);

// Dotted-quad IP of the active interface; false if none.
bool wifi_ip(char* out, size_t cap);

const char* wifi_ap_ssid();

// The DHCP hostname this device announces (find it in your router's leases).
const char* wifi_hostname();

}  // namespace diag
