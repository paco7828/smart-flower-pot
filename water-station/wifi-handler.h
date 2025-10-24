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

  // Callback function pointer for MQTT messages
  std::function<void(char*, uint8_t*, unsigned int)> mqttCallback;

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

  // Set MQTT callback
  void setMqttCallback(std::function<void(char*, uint8_t*, unsigned int)> callback) {
    mqttCallback = callback;
    client.setCallback(callback);
  }

  // --------------------------------------------------------------------------
  // ------------------------- MQTT FUNCTIONS ---------------------------------
  // --------------------------------------------------------------------------

  // Function to connect to MQTT and subscribe to topics
  void reconnect() {
    int attempts = 0;
    while (!client.connected() && WiFi.status() == WL_CONNECTED && attempts < 5) {
      String clientId = "water_station_" + String(random(0xffff), HEX);
      if (client.connect(clientId.c_str(), MQTT_USERNAME.c_str(), MQTT_PASSWORD.c_str())) {
        Serial.println("MQTT connected");
        
        // Subscribe to water command topic
        if (client.subscribe(MQTT_TOPIC_WATER_COMMAND)) {
          Serial.println("✓ Subscribed to: " + String(MQTT_TOPIC_WATER_COMMAND));
        } else {
          Serial.println("✗ Failed to subscribe to: " + String(MQTT_TOPIC_WATER_COMMAND));
        }
        break;
      } else {
        Serial.print("MQTT connection failed, rc=");
        Serial.println(client.state());
        delay(1000);
        attempts++;
      }
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

  // Send pump status via MQTT
  void sendPumpStatus(bool active) {
    if (client.connected()) {
      client.publish(MQTT_TOPIC_WATER_STATUS, active ? "1" : "0", false);
      client.loop();
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

    Serial.println("Attempting WiFi connection to: " + savedSSID);
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

      if (initialSetup) {
        configTime(3600, 3600, NTP_SERVER_URL);
        getLocalTime(&localTime);
      }
      return true;
    }

    Serial.println("WiFi connection failed");
    return false;
  }
};