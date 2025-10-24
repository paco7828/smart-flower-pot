#pragma once

// --------------------------------------------------------------------------
// ------------------------- CONSTANTS --------------------------------------
// --------------------------------------------------------------------------

// Pins
constexpr uint8_t PUMP_PIN = 0;
constexpr uint8_t BTN_PIN = 1;

// MQTT & WiFi
const char* MQTT_TOPIC_WATER_COMMAND = "okoscserep/water_command";
constexpr int MQTT_RECONNECT_ATTEMPTS = 5;

// Timing variables
const unsigned long AP_TIMEOUT = 120000UL;          // 2 minutes
const unsigned long WIFI_RETRY_INTERVAL = 15000UL;  // 15 seconds
const unsigned long STATUS_LOG_INTERVAL = 10000UL;  // 10 seconds

// Watering
const unsigned long WATERING_DURATION = 5000UL;  // 5 seconds
const char* WATERING_CODE = "1";

// Access point
const IPAddress localIP(192, 168, 4, 1);
const IPAddress gatewayIP(192, 168, 4, 1);
const IPAddress subnet(255, 255, 255, 0);
const char* AP_SSID = "Watering-station";

// Default MQTT broker info (will be overridden by saved config)
String MQTT_SERVER_IP = "192.168.31.32";
int MQTT_SERVER_PORT = 1883;
String MQTT_USERNAME = "smart-pot";
String MQTT_PASSWORD = "smartpot123";

// NTP server
const char* NTP_SERVER_URL = "pool.ntp.org";

// --------------------------------------------------------------------------
// ------------------------- VARIABLES --------------------------------------
// --------------------------------------------------------------------------

// Connection state management
enum WiFiState {
  WIFI_SETUP_MODE,
  WIFI_CONNECTING,
  WIFI_CONNECTED,
  WIFI_FAILED
};

// AP & Wifi variables
WiFiState currentWiFiState = WIFI_SETUP_MODE;
unsigned long lastWiFiAttempt = 0;

// Pump control
bool pumpActive = false;
unsigned long pumpStartTime = 0;