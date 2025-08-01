#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <esp_wifi.h>
#include "html.h"

// MQTT broker info
const char* MQTT_SERVER_IP = "192.168.31.31";
const int MQTT_SERVER_PORT = 1883;
const char* MQTT_USERNAME = "okos-cserep";
const char* MQTT_PASSWORD = "okoscserep123";

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
const int WATER_LEVEL_THRESHOLD = 1700;

// Timing
const unsigned long WATER_NOTIFICATION_INTERVAL = 86400000UL;  // 24 hours
const unsigned long WATERING_DURATION = 5000;                  // 5 seconds
const unsigned long LIGHT_SEND_INTERVAL = 60000;               // 1 minute
const unsigned long DARK_SEND_INTERVAL = 600000UL;             // 10 minutes
const unsigned long AP_TIMEOUT = 180000UL;                     // 3 minutes for AP mode
const unsigned long WIFI_RETRY_INTERVAL = 10000;             // 30 seconds between WiFi connection attempts

// Default values
float temperature = 0.0;
float humidity = 0.0;
int ldrValue = 0;
int moisture = 0;
int waterLevel = 0;

// Captive portal
const IPAddress localIP(4, 3, 2, 1);
const IPAddress gatewayIP(4, 3, 2, 1);
const IPAddress subnet(255, 255, 255, 0);
const char* AP_SSID = "Smart-flower-pot";

// Connection state management
enum WiFiState {
  WIFI_AP_MODE,
  WIFI_CONNECTING,
  WIFI_CONNECTED,
  WIFI_FAILED
};

WiFiState currentWiFiState = WIFI_AP_MODE;
unsigned long apStartTime = 0;
unsigned long lastWiFiAttempt = 0;
bool credentialsSaved = false;
String savedSSID = "";
String savedPassword = "";

// Helper variables
unsigned long lastWaterNotificationTime = 0;
unsigned long wateringStartTime = 0;
unsigned long lastMQTTSendTime = -60000;
bool waterNotifSent = false;
bool isWatering = false;

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

// Function to start Access Point
void startAccessPoint() {
  Serial.println("Starting Access Point...");
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAPConfig(localIP, gatewayIP, subnet);
  WiFi.softAP(AP_SSID);

  dnsServer.start(53, "*", localIP);
  setupWebServer();

  apStartTime = millis();
  currentWiFiState = WIFI_AP_MODE;

  Serial.println("Access Point started: " + String(AP_SSID));
  Serial.println("IP address: " + WiFi.softAPIP().toString());
  Serial.println("Connect to WiFi and open http://4.3.2.1");
}

// Function to attempt WiFi connection
void attemptWiFiConnection() {
  if (savedSSID == "" || savedPassword == "") {
    Serial.println("No credentials available for WiFi connection");
    return;
  }

  Serial.println("Attempting to connect to WiFi: " + savedSSID);
  currentWiFiState = WIFI_CONNECTING;

  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

  // Wait up to 20 seconds for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    currentWiFiState = WIFI_CONNECTED;
    Serial.println("\nWiFi connected successfully!");
    Serial.println("IP address: " + WiFi.localIP().toString());

    // Stop AP mode and DNS server if they were running
    dnsServer.stop();
    server.end();

    // Initialize MQTT and sensors
    client.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);
    configTime(3600, 3600, ntpServerURL);
    getLocalTime(&localTime);
    initializeSensors();

  } else {
    currentWiFiState = WIFI_FAILED;
    Serial.println("\nFailed to connect to WiFi");
    lastWiFiAttempt = millis();
  }
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

  server.on("/login", HTTP_POST, [](AsyncWebServerRequest* request) {
    String ssid, password;
    if (request->hasParam("username", true)) ssid = request->getParam("username", true)->value();
    if (request->hasParam("password", true)) password = request->getParam("password", true)->value();

    Serial.println("Received credentials:");
    Serial.println("SSID: " + ssid);
    Serial.println("Password: [hidden]");

    // Validate inputs
    if (ssid.length() == 0 || password.length() == 0) {
      request->send(400, "text/plain", "Invalid credentials");
      return;
    }

    // Save credentials to flash memory
    preferences.begin("wifi", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", password);
    preferences.end();

    // Update saved credentials
    savedSSID = ssid;
    savedPassword = password;
    credentialsSaved = true;

    Serial.println("Credentials saved to flash memory.");

    // Send success response
    AsyncWebServerResponse* response = request->beginResponse(200, "text/html", success_html);
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);

    Serial.println("Success page sent. Will attempt WiFi connection shortly...");
  });

  // Handle CORS for AJAX requests
  server.on("/login", HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
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
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("smart_flower_pot", MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Smart Flower Pot starting...");

  // Load existing credentials
  bool hasCredentials = loadWiFiCredentials();

  if (hasCredentials) {
    Serial.println("Found saved WiFi credentials");
  } else {
    Serial.println("No saved WiFi credentials found");
  }

  // Always start AP mode first
  startAccessPoint();

  // If we have credentials, also try to connect to WiFi
  if (hasCredentials) {
    delay(2000);  // Give AP time to start
    attemptWiFiConnection();
  }
}

void loop() {
  unsigned long currentMillis = millis();

  // Handle different WiFi states
  switch (currentWiFiState) {
    case WIFI_AP_MODE:
      // Process captive portal
      dnsServer.processNextRequest();

      // Check if AP timeout reached
      if (currentMillis - apStartTime >= AP_TIMEOUT && !credentialsSaved) {
        Serial.println("AP timeout reached, checking for saved credentials...");
        if (loadWiFiCredentials()) {
          attemptWiFiConnection();
        } else {
          Serial.println("No credentials found, restarting AP...");
          apStartTime = currentMillis;  // Reset AP timer
        }
      }

      // If credentials were just saved, attempt connection
      if (credentialsSaved) {
        credentialsSaved = false;
        delay(2000);  // Give time for success page to be served
        attemptWiFiConnection();
      }
      break;

    case WIFI_CONNECTING:
      // Connection attempt is handled in attemptWiFiConnection()
      // This state should be brief
      break;

    case WIFI_CONNECTED:
      // Normal operation mode
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi connection lost!");
        currentWiFiState = WIFI_FAILED;
        lastWiFiAttempt = currentMillis;
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
      // Retry WiFi connection periodically
      if (currentMillis - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
        Serial.println("Retrying WiFi connection...");
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

  if (currentMillis - lastMQTTSendTime >= sendInterval) {
    lastMQTTSendTime = currentMillis;

    // Read sensors
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    moisture = analogRead(MOISTURE_PIN);
    waterLevel = analogRead(WATER_LEVEL_PIN);

    // Publish to MQTT
    char dataBuffer[10];
    dtostrf(temperature, 1, 2, dataBuffer);
    client.publish("okoscserep/temperature", dataBuffer);
    dtostrf(humidity, 1, 2, dataBuffer);
    client.publish("okoscserep/humidity", dataBuffer);
    sprintf(dataBuffer, "%d", waterLevel);
    client.publish("okoscserep/water_level", dataBuffer);
    sprintf(dataBuffer, "%d", moisture);
    client.publish("okoscserep/soil_moisture", dataBuffer);
    sprintf(dataBuffer, "%d", isDark);
    client.publish("okoscserep/sunlight", dataBuffer);

    // Handle automated tasks
    handleAutomation(currentMillis);

    // If dark and not watering => go to sleep
    if (isDark && !isWatering) {
      delay(200);                                                // Allow MQTT to flush
      esp_sleep_enable_timer_wakeup(DARK_SEND_INTERVAL * 1000);  // ms to µs
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
  // If water level is below the threshold
  if (waterLevel <= WATER_LEVEL_THRESHOLD) {
    // Check if time has passed since the last notification
    if (!waterNotifSent || currentMillis - lastWaterNotificationTime >= WATER_NOTIFICATION_INTERVAL) {
      // Send notification in home assistant once
      sendNotification();
      lastWaterNotificationTime = currentMillis;
      waterNotifSent = true;
    }
  }

  // If moisture is higher than or equal to the threshold and watering isn't happening
  if (moisture >= MOISTURE_THRESHOLD && !isWatering) {
    waterPlant();
  }

  // Reset notification flag if interval has passed and water level is still low
  if (currentMillis - lastWaterNotificationTime >= WATER_NOTIFICATION_INTERVAL) {
    waterNotifSent = false;
  }
}

// Plant watering function
void waterPlant() {
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