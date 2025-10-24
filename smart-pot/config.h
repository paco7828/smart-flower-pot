#pragma once

// --------------------------------------------------------------------------
// ------------------------- CONSTANTS --------------------------------------
// --------------------------------------------------------------------------

// Pins
const byte MOISTURE_PIN = 0;
const byte DS_TEMP_PIN = 1;
const byte LDR_PIN = 2;
const byte BUZZER_PIN = 3;

// MQTT Topics
const char* MQTT_TOPIC_WATER_COMMAND = "okoscserep/water_command";
const char* MQTT_TOPIC_LAST_WATERING_TIME = "okoscserep/last_watering_time";

// Timing variables
const unsigned long WATERING_COOLDOWN = 1000;  // 5 minutes between watering cycles
const unsigned long LIGHT_SEND_INTERVAL = 60000;         // 1 minute
const unsigned long DARK_SEND_INTERVAL = 1800000000ULL;  // 30 minutes in microseconds (30 * 60 * 1000 * 1000)
const unsigned long AP_TIMEOUT = 60000UL;                // 1 minute for AP mode
const unsigned long WIFI_RETRY_INTERVAL = 15000;         // 15 seconds between WiFi connection attempts

// Buzzer
const unsigned long LOW_MOISTURE_BEEP_INTERVAL = 300000;  // 5 minutes (5 * 60 * 1000)
const unsigned int LOW_MOISTURE_HZ = 3700;

// Thresholds
const int MOISTURE_THRESHOLD = 2900;
const int SUNLIGHT_THRESHOLD = 1500;

// Captive portal - FIXED IP CONFIGURATION
const IPAddress localIP(192, 168, 4, 1);
const IPAddress gatewayIP(192, 168, 4, 1);
const IPAddress subnet(255, 255, 255, 0);
const char* AP_SSID = "Smart-Pot";

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

// Default values
float temperature = 0.0;
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
unsigned long lastMoistureReading = 0;  // Track when we last checked moisture

// Deep sleep and wake management
unsigned long wakeupTime = 0;
bool tasksCompleted = false;

// RTC memory structure to persist data across deep sleep
RTC_DATA_ATTR struct {
  bool isInitialized = false;
  uint32_t bootCount = 0;
  unsigned long totalSleepTime = 0;
  unsigned long lastLowMoistureBeep = 0;  // Track last low moisture beep time
  unsigned long lastWateringTime = 0;     // Track last watering time across sleep cycles
} rtcData;