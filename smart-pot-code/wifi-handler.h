#pragma once
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "html.h"

class WifiHandler {

private:
  unsigned long apStartTime = 0;
  bool apModeActive = false;
  bool credentialsSaved = false;
  bool initialSetup = true;
  String savedSSID = "";
  String savedPassword = "";

public:
  // Instances
  WiFiClient espClient;
  PubSubClient client;
  Preferences preferences;
  DNSServer dnsServer;
  WebServer server;
  struct tm localTime;

  // Constructor
  WifiHandler()
    : client(espClient), server(80) {}

  // --------------------------------------------------------------------------
  // ------------------------- GETTER FUNCTIONS -------------------------------
  // --------------------------------------------------------------------------
  unsigned long getApStartTime() {
    return this->apStartTime;
  }

  bool isApModeActive() {
    return this->apModeActive;
  }

  bool areCredentialsSaved() {
    return this->credentialsSaved;
  }

  // --------------------------------------------------------------------------
  // ------------------------- SETTER FUNCTIONS -------------------------------
  // --------------------------------------------------------------------------

  void setCredentialsSaved(bool newCredsSaved) {
    this->credentialsSaved = newCredsSaved;
  }

  void setInitialSetup(bool newInitialSetup) {
    this->initialSetup = newInitialSetup;
  }

  // --------------------------------------------------------------------------
  // ------------------------- MQTT FUNCTIONS ---------------------------------
  // --------------------------------------------------------------------------

  // Function to connect to MQTT with faster connection for deep sleep cycles
  void reconnect() {
    int attempts = 0;
    while (!client.connected() && WiFi.status() == WL_CONNECTED && attempts < 5) {
      Serial.print("Attempting MQTT connection... ");
      String clientId = "smart_flower_pot_" + String(random(0xffff), HEX);
      if (client.connect(clientId.c_str(), MQTT_USERNAME.c_str(), MQTT_PASSWORD.c_str())) {
        Serial.println("✓ MQTT connected");
        break;
      } else {
        Serial.print("✗ Failed, rc=");
        Serial.print(client.state());
        Serial.println(" - Retrying in 1s");
        delay(1000);
        attempts++;
      }
    }

    if (!client.connected()) {
      Serial.println("✗ MQTT connection failed after 5 attempts");
    }
  }

  // Function to load MQTT configuration from flash
  bool loadMQTTConfig() {
    preferences.begin("mqtt", true);
    MQTT_SERVER_IP = preferences.getString("server", "192.168.31.31");
    MQTT_SERVER_PORT = preferences.getInt("port", 1883);
    MQTT_USERNAME = preferences.getString("user", "okos-cserep");
    MQTT_PASSWORD = preferences.getString("pass", "okoscserep123");
    preferences.end();

    Serial.println("MQTT Config: " + MQTT_SERVER_IP + ":" + String(MQTT_SERVER_PORT));
    return (MQTT_SERVER_IP != "");
  }

  void sendTemperature(char buffer[10]) {
    client.publish("okoscserep/temperature", buffer, false);
    client.loop();
  }

  void sendMoisture(char buffer[10]) {
    client.publish("okoscserep/soil_moisture", buffer, false);
    client.loop();
  }

  void sendSunlightPresence(char buffer[10]) {
    client.publish("okoscserep/sunlight_presence", buffer, false);
    client.loop();
  }

  // NEW: Send watering command via MQTT
  void sendWaterCommand() {
    if (client.connected()) {
      client.publish(MQTT_TOPIC_WATER_COMMAND, "1", false);
      client.loop();
      Serial.println("✓ MQTT: Watering command sent");
    } else {
      Serial.println("✗ MQTT: Not connected, cannot send watering command");
    }
  }

  // --------------------------------------------------------------------------
  // --------------------- FLASH MEMORY FUNCTIONS -----------------------------
  // --------------------------------------------------------------------------

  // Function to load wifi credentials from flash
  bool loadWiFiCredentials() {
    preferences.begin("wifi", true);
    savedSSID = preferences.getString("ssid", "");
    savedPassword = preferences.getString("pass", "");
    preferences.end();

    if (savedSSID != "" && savedPassword != "") {
      Serial.println("Credentials loaded: " + savedSSID);
      return true;
    }
    Serial.println("No credentials found - starting AP mode");
    return false;
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
    MQTT_SERVER_IP = mqttServer;
    MQTT_SERVER_PORT = mqttPort;
    MQTT_USERNAME = mqttUser;
    MQTT_PASSWORD = mqttPass;

    Serial.println("Configuration saved successfully");
  }

  // --------------------------------------------------------------------------
  // --------------------------- AP FUNCTIONS ---------------------------------
  // --------------------------------------------------------------------------

  void startAccessPoint() {
    Serial.println("Starting Access Point...");
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    delay(100);

    if (!WiFi.softAPConfig(localIP, gatewayIP, subnet)) {
      Serial.println("AP Config Failed!");
      return;
    }

    bool apStarted = WiFi.softAP(AP_SSID);
    if (!apStarted) {
      Serial.println("Failed to start AP!");
      return;
    }

    Serial.println("AP started successfully");
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    delay(1000);

    dnsServer.start(53, "*", localIP);
    setupWebServer();

    apStartTime = millis();
    apModeActive = true;
  }

  void stopAccessPoint() {
    if (apModeActive) {
      Serial.println("Stopping Access Point...");
      dnsServer.stop();
      server.stop();
      WiFi.softAPdisconnect(true);
      apModeActive = false;
      delay(100);
    }
  }

  void setupWebServer() {
    server.on("/", HTTP_GET, [this]() {
      server.send(200, "text/html", index_html);
    });

    server.on("/config", HTTP_POST, [this]() {
      String wifiSSID = server.arg("wifi_ssid");
      String wifiPassword = server.arg("wifi_password");
      String mqttServer = server.arg("mqtt_server");
      String mqttPort = server.arg("mqtt_port");
      String mqttUser = server.arg("mqtt_username");
      String mqttPass = server.arg("mqtt_password");

      if (wifiSSID.length() == 0 || wifiPassword.length() == 0 || mqttServer.length() == 0 || mqttUser.length() == 0 || mqttPass.length() == 0 || mqttPort.toInt() < 1 || mqttPort.toInt() > 65535) {
        server.send(400, "text/plain", "Invalid configuration parameters");
        return;
      }

      saveConfiguration(wifiSSID, wifiPassword, mqttServer, mqttPort.toInt(), mqttUser, mqttPass);
      credentialsSaved = true;
      server.send(200, "text/html", success_html);
    });

    server.on("/config", HTTP_OPTIONS, [this]() {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
      server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
      server.send(200);
    });

    server.onNotFound([this]() {
      server.sendHeader("Location", "http://4.3.2.1/", true);
      server.send(302, "text/plain", "");
    });

    server.begin();
    Serial.println("Web server started");
  }

  // --------------------------------------------------------------------------
  // ------------------------- WIFI FUNCTIONS ---------------------------------
  // --------------------------------------------------------------------------

  bool attemptWiFiConnection() {
    if (savedSSID == "" || savedPassword == "") {
      Serial.println("No credentials to connect with");
      return false;
    }

    if (apModeActive && !initialSetup) {
      stopAccessPoint();
    }

    // FIX: Properly disconnect any existing connection attempt
    Serial.println("Attempting WiFi connection to: " + savedSSID);
    WiFi.disconnect(true);  // Disconnect and clear credentials
    delay(500);             // Give it time to disconnect
    WiFi.mode(WIFI_STA);
    delay(100);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
      delay(500);
      Serial.print(".");
      attempts++;

      if (apModeActive && initialSetup) {
        dnsServer.processNextRequest();
        server.handleClient();
      }
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());

      client.setServer(MQTT_SERVER_IP.c_str(), MQTT_SERVER_PORT);

      // Always sync time when WiFi connects
      configTime(3600, 3600, NTP_SERVER_URL);

      // Quick non-blocking check if time is synced
      Serial.println("Initiating time sync with NTP server...");
      delay(100);  // Brief delay to allow NTP request to start

      if (getLocalTime(&localTime, 1000)) {  // 1 second timeout
        Serial.println("Time synced successfully");
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime);
        Serial.println("Current time: " + String(timeStr));
      } else {
        Serial.println("Time sync in progress (will complete in background)");
      }

      return true;
    }

    Serial.println("WiFi connection failed");
    // FIX: Clean up after failed connection
    WiFi.disconnect(true);
    return false;
  }

  void sendLastWateringTime(const char* timestamp) {
    if (client.connected()) {
      client.publish(MQTT_TOPIC_LAST_WATERING_TIME, timestamp, true);  // retained message
      client.loop();
      Serial.println("MQTT: Last watering time sent: " + String(timestamp));
    } else {
      Serial.println("MQTT: Not connected, cannot send watering time");
    }
  }

  String getCurrentTimestamp() {
    struct tm timeinfo;
    // Use non-blocking time check with timeout
    if (!getLocalTime(&timeinfo, 100)) {  // 100ms timeout instead of default 5000ms
      Serial.println("Time not synced yet, using uptime");
      // Fallback: use uptime in seconds since boot
      unsigned long uptimeSeconds = millis() / 1000;
      unsigned long hours = uptimeSeconds / 3600;
      unsigned long minutes = (uptimeSeconds % 3600) / 60;
      unsigned long seconds = uptimeSeconds % 60;
      char timestamp[64] = "0000-00-00 00:00:00";
      return String(timestamp);
    }

    char timestamp[64];
    // Format: YYYY-MM-DD HH:MM:SS
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timestamp);
  }
};