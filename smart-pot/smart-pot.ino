#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <esp_wifi.h>
#include "html.h"

// Default MQTT broker info (will be overridden by saved config)
String mqttServerIP = "192.168.31.31";
int mqttServerPort = 1883;
String mqttUsername = "okos-cserep";
String mqttPassword = "okoscserep123";

// NTP server
const char* ntpServerURL = "pool.ntp.org";

// Pins
const byte MOISTURE_PIN = 0;
const byte LDR_PIN = 1;
const byte WATER_LEVEL_PIN = 2;
const byte WATER_PUMP_PIN = 3;
const byte DHT_PIN = 4;

// Thresholds
const int MOISTURE_THRESHOLD = 2000;
const int SUNLIGHT_THRESHOLD = 3000;
const int WATER_LEVEL_THRESHOLD = 1000;

// Timing variables
const unsigned long WATER_NOTIFICATION_INTERVAL = 86400000UL;  // 24 hours
const unsigned long WATERING_DURATION = 5000;                  // 5 seconds
const unsigned long WATERING_INTERVAL = 1800000UL;             // 30 minutes (30 * 60 * 1000)
const unsigned long LIGHT_SEND_INTERVAL = 60000;               // 1 minute
const unsigned long DARK_SEND_INTERVAL = 600000UL;             // 10 minutes
const unsigned long AP_TIMEOUT = 180000UL;                     // 3 minutes for AP mode
const unsigned long WIFI_RETRY_INTERVAL = 30000;               // 30 seconds between WiFi connection attempts

// Default values
float temperature = 0.0;
float humidity = 0.0;
int ldrValue = 0;
int moisture = 0;
bool waterPresent = false;

// Captive portal
const IPAddress localIP(4, 3, 2, 1);
const IPAddress gatewayIP(4, 3, 2, 1);
const IPAddress subnet(255, 255, 255, 0);
const char* AP_SSID = "Smart-flower-pot";

// Connection state management
enum WiFiState {
  WIFI_SETUP_MODE,  // Initial setup with AP
  WIFI_CONNECTING,  // Trying to connect to WiFi
  WIFI_CONNECTED,   // Connected to WiFi
  WIFI_FAILED       // WiFi connection failed
};

// AP & Wifi variables
WiFiState currentWiFiState = WIFI_SETUP_MODE;
unsigned long apStartTime = 0;
unsigned long lastWiFiAttempt = 0;
bool credentialsSaved = false;
bool apModeActive = false;
bool isInitialSetup = true;
String savedSSID = "";
String savedPassword = "";

// Helper variables
unsigned long lastWaterNotificationTime = 0;
unsigned long wateringStartTime = 0;
unsigned long lastWateringTime = 0;
unsigned long lastMQTTSendTime = -60000;
bool waterNotifSent = false;
bool isWatering = false;
bool justWokeUp = false;

// Instances
WiFiClient espClient;
PubSubClient client(espClient);
#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);
struct tm localTime;
Preferences preferences;
DNSServer dnsServer;
AsyncWebServer server(80);

// Function to load WiFi credentials from flash
bool loadWiFiCredentials() {
  preferences.begin("wifi", true);
  savedSSID = preferences.getString("ssid", "");
  savedPassword = preferences.getString("pass", "");
  preferences.end();

  return (savedSSID != "" && savedPassword != "");
}

// Function to load MQTT configuration from flash
bool loadMQTTConfig() {
  preferences.begin("mqtt", true);
  mqttServerIP = preferences.getString("server", "192.168.31.31");
  mqttServerPort = preferences.getInt("port", 1883);
  mqttUsername = preferences.getString("user", "okos-cserep");
  mqttPassword = preferences.getString("pass", "okoscserep123");
  preferences.end();

  return (mqttServerIP != "");
}

// Function to load/save last watering time from/to flash
void loadLastWateringTime() {
  preferences.begin("watering", true);
  lastWateringTime = preferences.getULong("lastTime", 0);
  preferences.end();
}

// Function to save last watering time into flash
void saveLastWateringTime() {
  preferences.begin("watering", false);
  preferences.putULong("lastTime", lastWateringTime);
  preferences.end();
}

// Function to load/save last water notification time
void loadLastWaterNotificationTime() {
  preferences.begin("notification", true);
  lastWaterNotificationTime = preferences.getULong("lastNotif", 0);
  preferences.end();
}

// Function to save last water notif time into flash
void saveLastWaterNotificationTime() {
  preferences.begin("notification", false);
  preferences.putULong("lastNotif", lastWaterNotificationTime);
  preferences.end();
}

// Function to save configuration to flash
void saveConfiguration(String ssid, String wifiPass, String mqttServer, int mqttPort, String mqttUser, String mqttPass) {
  // Save WiFi credentials
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", wifiPass);
  preferences.end();

  // Save MQTT configuration
  preferences.begin("mqtt", false);
  preferences.putString("server", mqttServer);
  preferences.putInt("port", mqttPort);
  preferences.putString("user", mqttUser);
  preferences.putString("pass", mqttPass);
  preferences.end();

  // Update current variables
  savedSSID = ssid;
  savedPassword = wifiPass;
  mqttServerIP = mqttServer;
  mqttServerPort = mqttPort;
  mqttUsername = mqttUser;
  mqttPassword = mqttPass;
}

// Function to start Access Point - ONLY during initial setup or when no WiFi credentials
void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(localIP, gatewayIP, subnet);
  WiFi.softAP(AP_SSID);

  dnsServer.start(53, "*", localIP);
  setupWebServer();

  apStartTime = millis();
  apModeActive = true;
}

// Function to stop Access Point
void stopAccessPoint() {
  if (apModeActive) {
    dnsServer.stop();
    server.end();
    WiFi.softAPdisconnect(true);
    apModeActive = false;
  }
}

// Function to attempt WiFi connection
bool attemptWiFiConnection() {
  if (savedSSID == "" || savedPassword == "") {
    return false;
  }

  // Stop AP during connection attempt to save power
  if (apModeActive && !isInitialSetup) {
    stopAccessPoint();
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

  // Wait up to 20 seconds for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    attempts++;

    // Continue processing AP requests during initial setup only
    if (apModeActive && isInitialSetup) {
      dnsServer.processNextRequest();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Initialize MQTT with saved configuration
    client.setServer(mqttServerIP.c_str(), mqttServerPort);
    configTime(3600, 3600, ntpServerURL);
    getLocalTime(&localTime);
    initializeSensors();
    return true;
  }

  return false;
}

// Function to initialize sensors
void initializeSensors() {
  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(WATER_LEVEL_PIN, INPUT);
  pinMode(WATER_PUMP_PIN, OUTPUT);
  digitalWrite(WATER_PUMP_PIN, LOW);
  dht.begin();
}

// Function to setup web server for captive portal
void setupWebServer() {
  server.on("/", HTTP_ANY, [](AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(200, "text/html", index_html);
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);
  });

  server.on("/config", HTTP_POST, [](AsyncWebServerRequest* request) {
    String wifiSSID, wifiPassword, mqttServer, mqttUser, mqttPass;
    int mqttPort = 1883;

    // Get WiFi parameters
    if (request->hasParam("wifi_ssid", true)) wifiSSID = request->getParam("wifi_ssid", true)->value();
    if (request->hasParam("wifi_password", true)) wifiPassword = request->getParam("wifi_password", true)->value();

    // Get MQTT parameters
    if (request->hasParam("mqtt_server", true)) mqttServer = request->getParam("mqtt_server", true)->value();
    if (request->hasParam("mqtt_port", true)) mqttPort = request->getParam("mqtt_port", true)->value().toInt();
    if (request->hasParam("mqtt_username", true)) mqttUser = request->getParam("mqtt_username", true)->value();
    if (request->hasParam("mqtt_password", true)) mqttPass = request->getParam("mqtt_password", true)->value();

    // Validate inputs
    if (wifiSSID.length() == 0 || wifiPassword.length() == 0 || mqttServer.length() == 0 || mqttUser.length() == 0 || mqttPass.length() == 0 || mqttPort < 1 || mqttPort > 65535) {
      request->send(400, "text/plain", "Invalid configuration parameters");
      return;
    }

    // Save all configuration
    saveConfiguration(wifiSSID, wifiPassword, mqttServer, mqttPort, mqttUser, mqttPass);
    credentialsSaved = true;

    // Send success response
    AsyncWebServerResponse* response = request->beginResponse(200, "text/html", success_html);
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);
  });

  // Handle CORS for AJAX requests
  server.on("/config", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(200);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request->send(response);
  });

  server.onNotFound([](AsyncWebServerRequest* request) {
    request->redirect("http://4.3.2.1");
  });

  server.begin();
}

// Function to connect to MQTT
void reconnect() {
  int attempts = 0;
  while (!client.connected() && WiFi.status() == WL_CONNECTED && attempts < 3) {
    if (client.connect("smart_flower_pot", mqttUsername.c_str(), mqttPassword.c_str())) {
      break;
    } else {
      delay(2000);
      attempts++;
    }
  }
}

void setup() {
  // Load existing credentials and configuration
  bool hasCredentials = loadWiFiCredentials();
  loadMQTTConfig();
  loadLastWateringTime();
  loadLastWaterNotificationTime();

  if (!hasCredentials) {
    // No credentials saved - start in setup mode
    startAccessPoint();
    isInitialSetup = true;
    currentWiFiState = WIFI_SETUP_MODE;
  } else {
    // Credentials exist - try to connect directly
    isInitialSetup = false;
    currentWiFiState = WIFI_CONNECTING;

    if (attemptWiFiConnection()) {
      currentWiFiState = WIFI_CONNECTED;
    } else {
      // Connection failed - start AP for reconfiguration
      currentWiFiState = WIFI_FAILED;
      startAccessPoint();
    }
  }
}

void loop() {
  unsigned long currentMillis = millis();

  // Process DNS requests only during initial setup
  if (apModeActive && isInitialSetup) {
    dnsServer.processNextRequest();
  }

  // Handle different WiFi states
  switch (currentWiFiState) {
    case WIFI_SETUP_MODE:
      // Initial setup mode with AP
      if (credentialsSaved) {
        credentialsSaved = false;
        delay(1500);  // Give time for success page to be served

        if (attemptWiFiConnection()) {
          currentWiFiState = WIFI_CONNECTED;
          isInitialSetup = false;
          stopAccessPoint();
        } else {
          currentWiFiState = WIFI_FAILED;
        }
      }

      // Check AP timeout during initial setup
      if (currentMillis - apStartTime >= AP_TIMEOUT) {
        // If we have saved credentials but couldn't connect, try without AP
        if (loadWiFiCredentials()) {
          stopAccessPoint();
          isInitialSetup = false;
          currentWiFiState = WIFI_CONNECTING;
        }
      }
      break;

    case WIFI_CONNECTING:
      if (attemptWiFiConnection()) {
        currentWiFiState = WIFI_CONNECTED;
      } else {
        currentWiFiState = WIFI_FAILED;
        lastWiFiAttempt = currentMillis;
      }
      break;

    case WIFI_CONNECTED:
      // Check if WiFi connection is still alive
      if (WiFi.status() != WL_CONNECTED) {
        currentWiFiState = WIFI_FAILED;
        lastWiFiAttempt = currentMillis;
        break;
      }

      // Handle credential updates
      if (credentialsSaved) {
        credentialsSaved = false;
        delay(1500);
        WiFi.disconnect();
        delay(1000);
        currentWiFiState = WIFI_CONNECTING;
        break;
      }

      // Reconnect MQTT if needed
      if (!client.connected()) {
        reconnect();
      }
      client.loop();

      // Handle sensor operations
      handleSensorOperations(currentMillis);
      break;

    case WIFI_FAILED:
      // Handle credential updates
      if (credentialsSaved) {
        credentialsSaved = false;
        delay(1500);
        currentWiFiState = WIFI_CONNECTING;
      }
      // Retry WiFi connection periodically
      else if (currentMillis - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
        currentWiFiState = WIFI_CONNECTING;
      }
      break;
  }

  delay(100);
}

// Function to handle sensor operations (only when connected to WiFi)
void handleSensorOperations(unsigned long currentMillis) {
  checkWateringStatus();

  // Determine sending interval based on sunlight detection
  ldrValue = analogRead(LDR_PIN);
  bool isDark = ldrValue <= SUNLIGHT_THRESHOLD;
  unsigned long sendInterval = isDark ? DARK_SEND_INTERVAL : LIGHT_SEND_INTERVAL;

  // Check if we should send data
  bool shouldSend = justWokeUp || (currentMillis - lastMQTTSendTime >= sendInterval);

  if (shouldSend) {
    justWokeUp = false;
    lastMQTTSendTime = currentMillis;

    // Read sensors
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    moisture = analogRead(MOISTURE_PIN);
    waterPresent = (analogRead(WATER_LEVEL_PIN) >= WATER_LEVEL_THRESHOLD);

    // Buffer for MQTT data
    char dataBuffer[10];

    // Temperature
    dtostrf(temperature, 1, 2, dataBuffer);
    client.publish("okoscserep/temperature", dataBuffer);

    // Humidity
    dtostrf(humidity, 1, 2, dataBuffer);
    client.publish("okoscserep/humidity", dataBuffer);

    // Water presence
    sprintf(dataBuffer, "%d", waterPresent ? 1 : 0);
    client.publish("okoscserep/water_level", dataBuffer);

    // Soil moisture
    sprintf(dataBuffer, "%d", moisture);
    client.publish("okoscserep/soil_moisture", dataBuffer);

    // Sunlight presence
    sprintf(dataBuffer, "%d", isDark ? 0 : 1);
    client.publish("okoscserep/sunlight", dataBuffer);

    // Handle automated tasks
    handleAutomation(currentMillis);

    // Go to sleep during dark periods
    if (isDark && !isWatering) {
      delay(500);  // Allow MQTT messages to be sent

      // Properly convert milliseconds to microseconds
      uint64_t sleepTimeUs = (uint64_t)DARK_SEND_INTERVAL * 1000ULL;
      esp_sleep_enable_timer_wakeup(sleepTimeUs);

      // Use light sleep to maintain WiFi connection
      justWokeUp = true;
      esp_light_sleep_start();
    }
  }
}

// Function to check if watering is happening
void checkWateringStatus() {
  if (isWatering && millis() - wateringStartTime >= WATERING_DURATION) {
    isWatering = false;
    digitalWrite(WATER_PUMP_PIN, LOW);
  }
}

// Function to handle automated tasks
void handleAutomation(unsigned long currentMillis) {
  // Water notification logic
  if (!waterPresent) {
    if (!waterNotifSent || currentMillis - lastWaterNotificationTime >= WATER_NOTIFICATION_INTERVAL) {
      sendNotification();
      lastWaterNotificationTime = currentMillis;
      saveLastWaterNotificationTime();
      waterNotifSent = true;
    }
  } else {
    // Reset notification flag when water is present
    waterNotifSent = false;
  }

  // Plant watering logic
  if (moisture >= MOISTURE_THRESHOLD && waterPresent && !isWatering && (currentMillis - lastWateringTime >= WATERING_INTERVAL)) {
    waterPlant();
  }
}

// Plant watering function
void waterPlant() {
  // Update last watering time
  lastWateringTime = millis();
  saveLastWateringTime();

  // Publish watering time to MQTT
  if (getLocalTime(&localTime)) {
    char timeBuffer[9];
    sprintf(timeBuffer, "%02d:%02d:%02d", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
    client.publish("okoscserep/last_watering_time", timeBuffer);
  }

  // Start watering
  isWatering = true;
  digitalWrite(WATER_PUMP_PIN, HIGH);
  wateringStartTime = millis();
}

// Notification sending
void sendNotification() {
  client.publish("smart_flower_pot/notify", "ON");
  delay(1000);
  client.publish("smart_flower_pot/notify", "OFF");
}