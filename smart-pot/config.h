#pragma once

// --------------------------------------------------------------------------
// ------------------------- CONSTANTS --------------------------------------
// --------------------------------------------------------------------------

// Pins
const byte MOISTURE_PIN = 0;
const byte LDR_PIN = 1;
const byte WATER_PUMP_PIN = 3;
const int DHT_PIN = 4;

// Timing variables
const unsigned long WATERING_DURATION = 5000;            // 5 seconds
const unsigned long WATERING_INTERVAL = 1800000UL;       // 30 minutes (30 * 60 * 1000)
const unsigned long LIGHT_SEND_INTERVAL = 60000;         // 1 minute
const unsigned long DARK_SEND_INTERVAL = 1800000000ULL;  // 30 minutes in microseconds (30 * 60 * 1000 * 1000)
const unsigned long AP_TIMEOUT = 180000UL;               // 3 minutes for AP mode
const unsigned long WIFI_RETRY_INTERVAL = 30000;         // 30 seconds between WiFi connection attempts

// Deep sleep timing - FIXED: Changed from 1 minute to 10 minutes
const unsigned long AWAKE_TIME_MS = 10000;  // 10 seconds awake time

// Thresholds
const int MOISTURE_THRESHOLD = 2000;
const int SUNLIGHT_THRESHOLD = 1500;

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
const char* NTP_SERVER_URL = "pool.ntp.org";

// --------------------------------------------------------------------------
// ------------------------- VARIABLES --------------------------------------
// --------------------------------------------------------------------------

// Default values
float temperature = 0.0;
float humidity = 0.0;
int ldrValue = 0;
int moisture = 0;

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
bool isWatering = false;
bool justWokeUp = false;
bool isDark = false;
unsigned long lastDataSendTime = 0;

// Deep sleep and wake management
unsigned long wakeupTime = 0;
bool tasksCompleted = false;

// RTC memory structure to persist data across deep sleep
RTC_DATA_ATTR struct {
  bool isInitialized = false;
  unsigned long lastWateringTime = 0;
  uint32_t bootCount = 0;
  // ADDED: Track total sleep time to maintain accurate timing
  unsigned long totalSleepTime = 0;
} rtcData;