#pragma once

// --------------------------------------------------------------------------
// ------------------------- CONSTANTS --------------------------------------
// --------------------------------------------------------------------------

// Pins
constexpr byte PUMP_PIN = 0;
constexpr byte BTN_PIN = 1;

// MQTT Topics
const char* MQTT_TOPIC_WATER_COMMAND = "okoscserep/water_command";
const char* MQTT_TOPIC_WATER_STATUS = "okoscserep/water_status";

// Timing variables
const unsigned long AP_TIMEOUT = 60000UL;           // 1 minute for AP mode
const unsigned long WIFI_RETRY_INTERVAL = 30000;    // 30 seconds between WiFi connection attempts
const unsigned long PUMP_DURATION = 5000;           // 5 seconds pump runtime

// Captive portal - FIXED IP CONFIGURATION
const IPAddress localIP(192, 168, 4, 1);
const IPAddress gatewayIP(192, 168, 4, 1);
const IPAddress subnet(255, 255, 255, 0);
const char* AP_SSID = "Watering-station";

// Default MQTT broker info (will be overridden by saved config)
String MQTT_SERVER_IP = "192.168.31.32";
int MQTT_SERVER_PORT = 1883;
String MQTT_USERNAME = "okos-cserep";
String MQTT_PASSWORD = "okoscserep123";

// NTP server
const char* NTP_SERVER_URL = "pool.ntp.org";

// --------------------------------------------------------------------------
// ------------------------- VARIABLES --------------------------------------
// --------------------------------------------------------------------------

// Connection state management
enum WiFiState {
  WIFI_SETUP_MODE,  // Initial setup with AP
  WIFI_CONNECTING,  // Trying to connect to WiFi
  WIFI_CONNECTED,   // Connected to WiFi
  WIFI_FAILED       // WiFi connection failed
};

// AP & Wifi variables
WiFiState currentWiFiState = WIFI_SETUP_MODE;
unsigned long lastWiFiAttempt = 0;

// Pump control
bool pumpActive = false;
unsigned long pumpStartTime = 0;