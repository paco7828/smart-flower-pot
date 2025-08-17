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
const byte WATER_LEVEL_PIN = 2;  // Now used as digital input for water presence
const byte WATER_PUMP_PIN = 3;
const byte DHT_PIN = 4;

// Thresholds
const int MOISTURE_THRESHOLD = 2000;
const int SUNLIGHT_THRESHOLD = 3000;
// Removed WATER_LEVEL_THRESHOLD since we're using digital read

// Timing
const unsigned long WATER_NOTIFICATION_INTERVAL = 86400000UL;  // 24 hours
const unsigned long WATERING_DURATION = 5000;                  // 5 seconds
const unsigned long WATERING_INTERVAL = 43200000UL;            // 12 hours (12 * 60 * 60 * 1000)
const unsigned long LIGHT_SEND_INTERVAL = 60000;               // 1 minute
const unsigned long DARK_SEND_INTERVAL = 600000UL;             // 10 minutes
const unsigned long AP_TIMEOUT = 180000UL;                     // 3 minutes for AP mode
const unsigned long WIFI_RETRY_INTERVAL = 30000;               // 30 seconds between WiFi connection attempts

// Default values
float temperature = 0.0;
float humidity = 0.0;
int ldrValue = 0;
int moisture = 0;
bool waterPresent = false;  // Changed from int waterLevel to bool waterPresent

// Captive portal
const IPAddress localIP(4, 3, 2, 1);
const IPAddress gatewayIP(4, 3, 2, 1);
const IPAddress subnet(255, 255, 255, 0);
const char* AP_SSID = "Smart-flower-pot";

// Connection state management
enum WiFiState {
  WIFI_AP_ONLY,     // AP mode only (first 3 minutes)
  WIFI_AP_AND_STA,  // AP + trying to connect to saved WiFi
  WIFI_CONNECTED,   // Connected to WiFi (AP may still be running)
  WIFI_FAILED       // WiFi connection failed, running on AP only
};

// AP & Wifi variables
WiFiState currentWiFiState = WIFI_AP_ONLY;
unsigned long apStartTime = 0;
unsigned long lastWiFiAttempt = 0;
bool credentialsSaved = false;
bool apModeActive = false;
String savedSSID = "";
String savedPassword = "";

// Helper variables
unsigned long lastWaterNotificationTime = 0;
unsigned long wateringStartTime = 0;
unsigned long lastWateringTime = 0;  // NEW: Track when last watering occurred
unsigned long lastMQTTSendTime = 0;  // Fixed: Initialize to 0 instead of -60000
bool waterNotifSent = false;
bool isWatering = false;
bool justWokeUp = false;  // NEW: Track if we just woke up from sleep

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

// NEW: Function to load/save last watering time from/to flash
void loadLastWateringTime() {
  preferences.begin("watering", true);
  lastWateringTime = preferences.getULong("lastTime", 0);
  preferences.end();
}

void saveLastWateringTime() {
  preferences.begin("watering", false);
  preferences.putULong("lastTime", lastWateringTime);
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

// Function to start Access Point - ALWAYS CALLED ON STARTUP
void startAccessPoint() {
  // Set to AP+STA mode so we can have both AP and try WiFi connection
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(localIP, gatewayIP, subnet);
  WiFi.softAP(AP_SSID);

  dnsServer.start(53, "*", localIP);
  setupWebServer();

  apStartTime = millis();
  apModeActive = true;
  currentWiFiState = WIFI_AP_ONLY;
}

// Function to stop Access Point after timeout
void stopAccessPoint() {
  if (apModeActive) {
    dnsServer.stop();
    server.end();
    WiFi.softAPdisconnect(true);
    apModeActive = false;

    // Switch to STA mode only if we have WiFi connection
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.mode(WIFI_STA);
    }
  }
}

// Function to attempt WiFi connection
void attemptWiFiConnection() {
  if (savedSSID == "" || savedPassword == "") {
    return;
  }

  // If AP is still active, use AP+STA mode, otherwise use STA mode
  if (apModeActive) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }

  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

  // Wait up to 20 seconds for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    attempts++;

    // Continue processing AP requests during connection attempt
    if (apModeActive) {
      dnsServer.processNextRequest();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    currentWiFiState = WIFI_CONNECTED;

    // Initialize MQTT with saved configuration
    client.setServer(mqttServerIP.c_str(), mqttServerPort);
    configTime(3600, 3600, ntpServerURL);
    getLocalTime(&localTime);
    initializeSensors();

  } else {
    currentWiFiState = WIFI_FAILED;
    lastWiFiAttempt = millis();
  }
}

// Function to initialize sensors
void initializeSensors() {
  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(WATER_LEVEL_PIN, INPUT);  // Digital input for water presence detection
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

// Function to connect to MQTT in home assistant
void reconnect() {
  while (!client.connected() && WiFi.status() == WL_CONNECTED) {
    if (client.connect("smart_flower_pot", mqttUsername.c_str(), mqttPassword.c_str())) {
      // Successfully connected
      break;
    } else {
      // Connection failed, wait before retry
      delay(5000);
    }
  }
}

void setup() {
  // Load existing credentials and MQTT config
  bool hasCredentials = loadWiFiCredentials();
  loadMQTTConfig();  // Load MQTT configuration
  loadLastWateringTime();  // NEW: Load last watering time

  // ALWAYS start AP mode first on every power-up
  startAccessPoint();

  // If we have credentials, also try to connect to WiFi immediately
  if (hasCredentials) {
    delay(2000);  // Give AP time to fully start
    currentWiFiState = WIFI_AP_AND_STA;
    attemptWiFiConnection();
  }
}

void loop() {
  unsigned long currentMillis = millis();

  // ALWAYS process DNS requests while AP is active
  if (apModeActive) {
    dnsServer.processNextRequest();

    // Check if AP timeout reached (3 minutes)
    if (currentMillis - apStartTime >= AP_TIMEOUT) {
      stopAccessPoint();
    }
  }

  // Handle different WiFi states
  switch (currentWiFiState) {
    case WIFI_AP_ONLY:
      // Only AP mode active, waiting for credentials or timeout

      // If credentials were just saved, attempt connection
      if (credentialsSaved) {
        credentialsSaved = false;
        currentWiFiState = WIFI_AP_AND_STA;
        delay(2000);  // Give time for success page to be served
        attemptWiFiConnection();
      }

      // Check if we have saved credentials and should try connecting
      else if (loadWiFiCredentials()) {
        currentWiFiState = WIFI_AP_AND_STA;
        attemptWiFiConnection();
      }
      break;

    case WIFI_AP_AND_STA:
      // AP is running AND we're trying to connect to WiFi

      // If credentials were just saved, attempt new connection
      if (credentialsSaved) {
        credentialsSaved = false;
        delay(2000);  // Give time for success page to be served

        // Disconnect from current WiFi and try new credentials
        WiFi.disconnect();
        delay(1000);
        attemptWiFiConnection();
      }
      break;

    case WIFI_CONNECTED:
      // Connected to WiFi (AP may still be running for the first 3 minutes)

      // Check if WiFi connection is still alive
      if (WiFi.status() != WL_CONNECTED) {
        currentWiFiState = WIFI_FAILED;
        lastWiFiAttempt = currentMillis;
        break;
      }

      // Handle new credentials even when connected
      if (credentialsSaved) {
        credentialsSaved = false;
        delay(2000);  // Give time for success page to be served

        // Disconnect and try new credentials
        WiFi.disconnect();
        delay(1000);
        currentWiFiState = apModeActive ? WIFI_AP_AND_STA : WIFI_FAILED;
        attemptWiFiConnection();
        break;
      }

      // Reconnect MQTT if needed
      if (!client.connected()) {
        reconnect();
      }
      client.loop();

      // Handle normal sensor operations
      handleSensorOperations(currentMillis);
      break;

    case WIFI_FAILED:
      // WiFi connection failed, retry periodically

      // Handle new credentials
      if (credentialsSaved) {
        credentialsSaved = false;
        delay(2000);  // Give time for success page to be served
        currentWiFiState = apModeActive ? WIFI_AP_AND_STA : WIFI_FAILED;
        attemptWiFiConnection();
      }

      // Retry WiFi connection periodically
      else if (currentMillis - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
        currentWiFiState = apModeActive ? WIFI_AP_AND_STA : WIFI_FAILED;
        attemptWiFiConnection();
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

  // Force immediate send on wake up from sleep, or after normal interval
  bool shouldSend = justWokeUp || (currentMillis - lastMQTTSendTime >= sendInterval);
  
  if (shouldSend) {
    justWokeUp = false;  // Reset wake up flag
    lastMQTTSendTime = currentMillis;

    // Read sensors
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    moisture = analogRead(MOISTURE_PIN);
    waterPresent = digitalRead(WATER_LEVEL_PIN);  // Digital read for water presence

    // Publish to MQTT
    char dataBuffer[10];
    dtostrf(temperature, 1, 2, dataBuffer);
    client.publish("okoscserep/temperature", dataBuffer);
    dtostrf(humidity, 1, 2, dataBuffer);
    client.publish("okoscserep/humidity", dataBuffer);
    sprintf(dataBuffer, "%d", waterPresent ? 1 : 0);  // Publish 1 if water present, 0 if not
    client.publish("okoscserep/water_level", dataBuffer);
    sprintf(dataBuffer, "%d", moisture);
    client.publish("okoscserep/soil_moisture", dataBuffer);
    sprintf(dataBuffer, "%d", isDark);
    client.publish("okoscserep/sunlight", dataBuffer);

    // Handle automated tasks
    handleAutomation(currentMillis);

    // If dark and not watering => go to sleep (FIXED SLEEP LOGIC)
    if (isDark && !isWatering) {
      delay(200);  // Allow MQTT to flush
      
      // FIXED: Proper microsecond conversion for sleep timer
      uint64_t sleepTimeUs = (uint64_t)DARK_SEND_INTERVAL * 1000ULL;  // Convert ms to µs
      esp_sleep_enable_timer_wakeup(sleepTimeUs);
      
      justWokeUp = true;  // Set flag for when we wake up
      esp_light_sleep_start();
      
      // When we wake up, we continue from here
      // The justWokeUp flag will ensure immediate sensor reading on next loop
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
  // If water is not present (LOW signal from sensor)
  if (!waterPresent) {
    // Check if time has passed since the last notification
    if (!waterNotifSent || currentMillis - lastWaterNotificationTime >= WATER_NOTIFICATION_INTERVAL) {
      // Send notification in home assistant once
      sendNotification();
      lastWaterNotificationTime = currentMillis;
      waterNotifSent = true;
    }
  }

  // Only water the plant if:
  // 1. Moisture is higher than threshold (soil is dry)
  // 2. Water is present in the reservoir
  // 3. Not currently watering
  // 4. NEW: At least 12 hours have passed since last watering
  if (moisture >= MOISTURE_THRESHOLD && waterPresent && !isWatering && 
      (currentMillis - lastWateringTime >= WATERING_INTERVAL)) {
    waterPlant();
  }

  // Reset notification flag if interval has passed and water is present again
  if (waterPresent && waterNotifSent) {
    waterNotifSent = false;
  }
}

// Plant watering function
void waterPlant() {
  // Update last watering time
  lastWateringTime = millis();
  saveLastWateringTime();  // NEW: Save to flash memory

  // If time is obtained
  if (getLocalTime(&localTime)) {
    char timeBuffer[9];
    // Format time
    sprintf(timeBuffer, "%02d:%02d:%02d", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
    // Publish to MQTT
    client.publish("okoscserep/last_watering_time", timeBuffer);
  }

  // Actual watering
  isWatering = true;
  digitalWrite(WATER_PUMP_PIN, HIGH);
  wateringStartTime = millis();
}

// Notification sending
void sendNotification() {
  // Turn on for 1 second
  client.publish("smart_flower_pot/notify", "ON");
  delay(1000);
  client.publish("smart_flower_pot/notify", "OFF");
}