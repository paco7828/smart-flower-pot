#pragma once

// --------------------------------------------------------------------------
// ------------------------- CONSTANTS --------------------------------------
// --------------------------------------------------------------------------

// Pins
const byte MOISTURE_PIN = 0;
const byte LDR_PIN = 1;
const byte WATER_LEVEL_PIN = 2;
const byte WATER_PUMP_PIN = 3;
const byte DHT_PIN = 4;

// Timing variables
const unsigned long WATER_NOTIFICATION_INTERVAL = 86400000UL;  // 24 hours
const unsigned long WATERING_DURATION = 5000;                  // 5 seconds
const unsigned long WATERING_INTERVAL = 1800000UL;             // 30 minutes (30 * 60 * 1000)
const unsigned long LIGHT_SEND_INTERVAL = 60000;               // 1 minute
const unsigned long DARK_SEND_INTERVAL = 600000UL;             // 10 minutes
const unsigned long AP_TIMEOUT = 180000UL;                     // 3 minutes for AP mode
const unsigned long WIFI_RETRY_INTERVAL = 30000;               // 30 seconds between WiFi connection attempts

// Thresholds
const int MOISTURE_THRESHOLD = 2000;
const int SUNLIGHT_THRESHOLD = 3000;
const int WATER_LEVEL_THRESHOLD = 1000;

// Captive portal
const IPAddress localIP(4, 3, 2, 1);
const IPAddress gatewayIP(4, 3, 2, 1);
const IPAddress subnet(255, 255, 255, 0);
const char* AP_SSID = "Smart-flower-pot";

// Default MQTT broker info (will be overridden by saved config)
String MQTT_SERVER_IP = "192.168.31.31";
int MQTT_SERVER_PORT = 1883;
String MQTT_USERNAME = "okos-cserep";
String MQTT_PASSWORD = "okoscserep123";

// NTP server
const char *NTP_SERVER_URL = "pool.ntp.org";

// --------------------------------------------------------------------------
// ------------------------- VARIABLES --------------------------------------
// --------------------------------------------------------------------------

// Default values
float temperature = 0.0;
float humidity = 0.0;
int ldrValue = 0;
int moisture = 0;
bool waterPresent = false;

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

// Helper variables
unsigned long wateringStartTime = 0;
unsigned long lastMQTTSendTime = -60000;
bool waterNotifSent = false;
bool isWatering = false;
bool justWokeUp = false;