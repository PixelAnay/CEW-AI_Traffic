#ifndef CONFIG_H
#define CONFIG_H

// ==============================================================================
// SmartFlow NodeMCU (ESP8266) - Network & Hardware Configuration
// ==============================================================================
// Copy this file to "config.h" and replace the placeholder values below with
// your local Wi-Fi credentials and PC's local IP address.
// Note: "config.h" is git-ignored to prevent leaking credentials.
// ==============================================================================

// Wi-Fi Credentials
static const char* WIFI_SSID = "YOUR_WIFI_SSID";
static const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// SmartFlow Server URL (Replace with your computer's local LAN IP address)
// Example: "http://192.168.1.50:8080"
static const char* SERVER_URL = "http://YOUR_PC_IP:8080";

// Serial Debugging (Set to 1 to enable Serial logs; keep 0 if DHT11 is on GPIO3/RX)
#define DEBUG_SERIAL 0

#endif // CONFIG_H
