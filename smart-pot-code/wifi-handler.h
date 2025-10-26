#pragma once
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "html.h"

class WifiHandler {
private:
  unsigned long apStartTime;
  bool apModeActive;
  bool credentialsSaved;
  bool initialSetup;
  String savedSSID;
  String savedPassword;

  // Helper function for MQTT publishing
  inline bool publishMQTT(const char* topic, const char* payload, bool retain = false) {
    if (client.connected()) {
      bool result = client.publish(topic, payload, retain);
      client.loop();
      return result;
    }
    return false;
  }

public:
  // Instances
  WiFiClient espClient;
  PubSubClient client;
  Preferences preferences;
  DNSServer dnsServer;
  WebServer server;
  struct tm localTime;

  // Constructor with member initializer list
  WifiHandler()
    : client(espClient),
      server(80),
      apStartTime(0),
      apModeActive(false),
      credentialsSaved(false),
      initialSetup(true) {}

  // --------------------------------------------------------------------------
  // ------------------------- GETTER FUNCTIONS -------------------------------
  // --------------------------------------------------------------------------
  inline unsigned long getApStartTime() const {
    return apStartTime;
  }
  inline bool isApModeActive() const {
    return apModeActive;
  }
  inline bool areCredentialsSaved() const {
    return credentialsSaved;
  }

  // --------------------------------------------------------------------------
  // ------------------------- SETTER FUNCTIONS -------------------------------
  // --------------------------------------------------------------------------
  inline void setCredentialsSaved(bool newCredsSaved) {
    credentialsSaved = newCredsSaved;
  }
  inline void setInitialSetup(bool newInitialSetup) {
    initialSetup = newInitialSetup;
  }

  // --------------------------------------------------------------------------
  // ------------------------- MQTT FUNCTIONS ---------------------------------
  // --------------------------------------------------------------------------

  void reconnectMQTT() {
    if (WiFi.status() != WL_CONNECTED) return;

    // Attempt MQTT connection with retry logic
    for (int attempts = 0; attempts < MQTT_RECONNECT_ATTEMPTS && !client.connected(); attempts++) {
      String clientId = "water_station_" + String(random(0xffff), HEX);

      if (client.connect(clientId.c_str(), MQTT_USERNAME.c_str(), MQTT_PASSWORD.c_str())) {
        if (client.subscribe(MQTT_TOPIC_WATER_COMMAND)) {
          Serial.println("MQTT subscribed to: " + String(MQTT_TOPIC_WATER_COMMAND));
        }
        return;
      }

      Serial.print("MQTT failed, rc=");
      Serial.print(client.state());
      Serial.print(" attempt ");
      Serial.println(attempts + 1);
      delay(1000);
    }
  }

  bool loadMQTTConfig() {
    preferences.begin("mqtt", true);
    MQTT_SERVER_IP = preferences.getString("server", MQTT_SERVER_IP);
    MQTT_SERVER_PORT = preferences.getInt("port", MQTT_SERVER_PORT);
    MQTT_USERNAME = preferences.getString("user", MQTT_USERNAME);
    MQTT_PASSWORD = preferences.getString("pass", MQTT_PASSWORD);
    preferences.end();

    Serial.print("MQTT: ");
    Serial.print(MQTT_SERVER_IP);
    Serial.print(":");
    Serial.println(MQTT_SERVER_PORT);
    return !MQTT_SERVER_IP.isEmpty();
  }

  // Simplified sensor data publishing methods
  inline void sendTemperature(const char* buffer) {
    publishMQTT(MQTT_TOPIC_TEMPERATURE, buffer);
  }

  inline void sendMoisture(const char* buffer) {
    publishMQTT(MQTT_TOPIC_SOIL_MOISTURE, buffer);
  }

  inline void sendSunlightPresence(const char* buffer) {
    publishMQTT(MQTT_TOPIC_SUNLIGHT_PRESENCE, buffer);
  }

  void sendWaterCommand() {
    if (publishMQTT(MQTT_TOPIC_WATER_COMMAND, WATERING_CODE)) {
      Serial.println("MQTT: Watering command sent");
    } else {
      Serial.println("MQTT: Not connected, cannot send watering command");
    }
  }

  void sendLastWateringTime(const char* timestamp) {
    if (publishMQTT(MQTT_TOPIC_LAST_WATERING_TIME, timestamp, true)) {
      Serial.print("MQTT: Last watering time sent: ");
      Serial.println(timestamp);
    } else {
      Serial.println("MQTT: Not connected, cannot send watering time");
    }
  }

  String getCurrentTimestamp() {
    struct tm timeinfo;

    // Fast timeout for non-blocking time check
    if (!getLocalTime(&timeinfo, 100)) {
      Serial.println("Time not synced yet, using uptime");
      // Return empty timestamp on failure (fallback handled in main code)
      return "0000-00-00 00:00:00";
    }

    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timestamp);
  }

  // --------------------------------------------------------------------------
  // --------------------- FLASH MEMORY FUNCTIONS -----------------------------
  // --------------------------------------------------------------------------

  bool loadWiFiCredentials() {
    preferences.begin("wifi", true);
    savedSSID = preferences.getString("ssid", "");
    savedPassword = preferences.getString("pass", "");
    preferences.end();

    if (!savedSSID.isEmpty() && !savedPassword.isEmpty()) {
      Serial.print("Loaded credentials: ");
      Serial.println(savedSSID);
      return true;
    }

    Serial.println("No saved credentials");
    return false;
  }

  void saveConfiguration(const String& ssid, const String& wifiPass,
                         const String& mqttServer, int mqttPort,
                         const String& mqttUser, const String& mqttPass) {
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

    // Update cached values
    savedSSID = ssid;
    savedPassword = wifiPass;
    MQTT_SERVER_IP = mqttServer;
    MQTT_SERVER_PORT = mqttPort;
    MQTT_USERNAME = mqttUser;
    MQTT_PASSWORD = mqttPass;

    Serial.println("Configuration saved");
  }

  // --------------------------------------------------------------------------
  // --------------------------- AP FUNCTIONS ---------------------------------
  // --------------------------------------------------------------------------

  void startAccessPoint() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    delay(100);

    if (!WiFi.softAPConfig(localIP, gatewayIP, subnet)) {
      Serial.println("AP config failed!");
      return;
    }

    if (!WiFi.softAP(AP_SSID)) {
      Serial.println("AP start failed!");
      return;
    }

    Serial.print("AP started: ");
    Serial.print(AP_SSID);
    Serial.print(" @ ");
    Serial.println(WiFi.softAPIP());

    dnsServer.start(53, "*", localIP);
    setupWebServer();

    apStartTime = millis();
    apModeActive = true;
  }

  void stopAccessPoint() {
    if (!apModeActive) return;

    Serial.println("Stopping AP");
    dnsServer.stop();
    server.stop();
    WiFi.softAPdisconnect(true);
    apModeActive = false;
    delay(100);
  }

  void setupWebServer() {
    // Main configuration page with placeholder replacement
    server.on("/", HTTP_GET, [this]() {
      String html = String(index_html);
      html.replace("%MQTT_SERVER%", MQTT_SERVER_IP);
      html.replace("%MQTT_PORT%", String(MQTT_SERVER_PORT));
      html.replace("%MQTT_USER%", MQTT_USERNAME);
      html.replace("%MQTT_PASS%", MQTT_PASSWORD);
      server.send(200, "text/html", html);
    });

    // Configuration form submission handler
    server.on("/config", HTTP_POST, [this]() {
      String wifiSSID = server.arg("wifi_ssid");
      String wifiPassword = server.arg("wifi_password");
      String mqttServer = server.arg("mqtt_server");
      String mqttPortStr = server.arg("mqtt_port");
      String mqttUser = server.arg("mqtt_username");
      String mqttPass = server.arg("mqtt_password");

      int port = mqttPortStr.toInt();

      // Validate inputs
      if (wifiSSID.isEmpty() || wifiPassword.isEmpty() || mqttServer.isEmpty() || mqttUser.isEmpty() || mqttPass.isEmpty() || port < 1 || port > 65535) {
        server.send(400, "text/plain", "Invalid parameters");
        return;
      }

      // Save configuration and send success response
      saveConfiguration(wifiSSID, wifiPassword, mqttServer, port, mqttUser, mqttPass);

      // Send a simple success response
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", "OK");

      // Mark credentials as saved to trigger reconnection
      credentialsSaved = true;
    });

    // CORS preflight
    server.on("/config", HTTP_OPTIONS, [this]() {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
      server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
      server.send(200);
    });

    // Captive portal redirect
    server.onNotFound([this]() {
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302, "text/plain", "");
    });

    server.begin();
    Serial.println("Web server started");
  }

  // --------------------------------------------------------------------------
  // ------------------------- WIFI FUNCTIONS ---------------------------------
  // --------------------------------------------------------------------------

  bool connectWiFi() {
    if (savedSSID.isEmpty() || savedPassword.isEmpty()) {
      Serial.println("No credentials available");
      return false;
    }

    if (apModeActive) stopAccessPoint();

    Serial.print("Connecting to: ");
    Serial.println(savedSSID);

    // Properly clean up any existing connection attempt
    WiFi.disconnect(true);
    delay(100);

    WiFi.mode(WIFI_STA);
    delay(100);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

    // Wait up to 15 seconds for connection
    for (int attempts = 0; attempts < 30 && WiFi.status() != WL_CONNECTED; attempts++) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("WiFi connected: ");
      Serial.println(WiFi.localIP());

      client.setServer(MQTT_SERVER_IP.c_str(), MQTT_SERVER_PORT);
      configTime(3600, 3600, NTP_SERVER_URL);
      return true;
    }

    Serial.println("WiFi connection failed");
    return false;
  }
};