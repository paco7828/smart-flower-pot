#pragma once
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include "html.h"

class WifiHandler
{

private:
    unsigned long apStartTime = 0;
    bool apModeActive = false;
    bool credentialsSaved = false;
    bool initialSetup = true;
    unsigned long lastWaterNotificationTime = 0;
    String savedSSID = "";
    String savedPassword = "";
    unsigned long lastWateringTime = 0;

public:
    // Instances
    WiFiClient espClient;
    PubSubClient client;
    Preferences preferences;
    DNSServer dnsServer;
    AsyncWebServer server;
    struct tm localTime;

    // Constructor
    WifiHandler() : client(espClient), server(80) {}

    // --------------------------------------------------------------------------
    // ------------------------- GETTER FUNCTIONS -------------------------------
    // --------------------------------------------------------------------------
    unsigned long getApStartTime()
    {
        return this->apStartTime;
    }

    bool isApModeActive()
    {
        return this->apModeActive;
    }

    bool areCredentialsSaved()
    {
        return this->credentialsSaved;
    }

    bool isInitialSetup()
    {
        return this->initialSetup;
    }

    unsigned long getLastWaterNotificationTime()
    {
        return this->lastWaterNotificationTime;
    }

    unsigned long getLastWateringTime()
    {
        return this->lastWateringTime;
    }

    // --------------------------------------------------------------------------
    // ------------------------- SETTER FUNCTIONS -------------------------------
    // --------------------------------------------------------------------------

    void setCredentialsSaved(bool newCredsSaved)
    {
        this->credentialsSaved = newCredsSaved;
    }

    void setInitialSetup(bool newInitialSetup)
    {
        this->initialSetup = newInitialSetup;
    }

    void setLastWaterNotificationTime(unsigned long newTime)
    {
        this->lastWaterNotificationTime = newTime;
    }

    void setLastWateringTime(unsigned long newTime)
    {
        this->lastWateringTime = newTime;
    }

    // --------------------------------------------------------------------------
    // ------------------------- MQTT FUNCTIONS ---------------------------------
    // --------------------------------------------------------------------------

    // Function to connect to MQTT
    void reconnect()
    {
        int attempts = 0;
        while (!client.connected() && WiFi.status() == WL_CONNECTED && attempts < 3)
        {
            if (client.connect("smart_flower_pot", MQTT_USERNAME.c_str(), MQTT_PASSWORD.c_str()))
            {
                break;
            }
            else
            {
                delay(2000);
                attempts++;
            }
        }
    }

    // Function to load MQTT configuration from flash
    bool loadMQTTConfig()
    {
        preferences.begin("mqtt", true);
        MQTT_SERVER_IP = preferences.getString("server", "192.168.31.31");
        MQTT_SERVER_PORT = preferences.getInt("port", 1883);
        MQTT_USERNAME = preferences.getString("user", "okos-cserep");
        MQTT_PASSWORD = preferences.getString("pass", "okoscserep123");
        preferences.end();

        return (MQTT_SERVER_IP != "");
    }

    // Notification sending
    void sendNotification()
    {
        client.publish("smart_flower_pot/notify", "ON");
        delay(1000);
        client.publish("smart_flower_pot/notify", "OFF");
    }

    void sendTemperature(char buffer[10])
    {
        client.publish("okoscserep/temperature", buffer);
    }

    void sendHumidity(char buffer[10])
    {
        client.publish("okoscserep/humidity", buffer);
    }

    void sendWaterPresence(char buffer[10])
    {
        client.publish("okoscserep/water_presence", buffer);
    }

    void sendMoisture(char buffer[10])
    {
        client.publish("okoscserep/soil_moisture", buffer);
    }

    void sendSunlightPresence(char buffer[10])
    {
        client.publish("okoscserep/sunlight_presence", buffer);
    }

    void sendLastWateringTime(char buffer[9])
    {
        client.publish("okoscserep/last_watering_time", buffer);
    }

    // --------------------------------------------------------------------------
    // --------------------- FLASH MEMORY FUNCTIONS -----------------------------
    // --------------------------------------------------------------------------

    // Function to load last watering time from flash
    void loadLastWateringTime()
    {
        preferences.begin("watering", true);
        lastWateringTime = preferences.getULong("lastTime", 0);
        preferences.end();
    }

    // Function to load last water notification time
    void loadLastWaterNotificationTime()
    {
        preferences.begin("notification", true);
        lastWaterNotificationTime = preferences.getULong("lastNotif", 0);
        preferences.end();
    }

    // Function to save last watering time into flash
    void saveLastWateringTime()
    {
        preferences.begin("watering", false);
        preferences.putULong("lastTime", lastWateringTime);
        preferences.end();
    }

    // Function to save last water notif time into flash
    void saveLastWaterNotificationTime()
    {
        preferences.begin("notification", false);
        preferences.putULong("lastNotif", lastWaterNotificationTime);
        preferences.end();
    }

    // Function to load wifi credentials from flash
    bool loadWiFiCredentials()
    {
        preferences.begin("wifi", true);
        savedSSID = preferences.getString("ssid", "");
        savedPassword = preferences.getString("pass", "");
        preferences.end();
        return (savedSSID != "" && savedPassword != "");
    }

    // Function to save configuration to flash
    void saveConfiguration(String ssid, String wifiPass, String mqttServer, int mqttPort, String mqttUser, String mqttPass)
    {
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
    }

    // --------------------------------------------------------------------------
    // --------------------------- AP FUNCTIONS ---------------------------------
    // --------------------------------------------------------------------------

    // Function to start Access Point - ONLY during initial setup or when no WiFi credentials
    void startAccessPoint()
    {
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(localIP, gatewayIP, subnet);
        WiFi.softAP(AP_SSID);

        dnsServer.start(53, "*", localIP);
        setupWebServer();

        apStartTime = millis();
        apModeActive = true;
    }

    // Function to stop Access Point
    void stopAccessPoint()
    {
        if (apModeActive)
        {
            dnsServer.stop();
            server.end();
            WiFi.softAPdisconnect(true);
            apModeActive = false;
        }
    }

    // Function to setup web server for captive portal
    void setupWebServer()
    {
        server.on("/", HTTP_ANY, [](AsyncWebServerRequest *request)
                  {
    AsyncWebServerResponse* response = request->beginResponse(200, "text/html", index_html);
    response->addHeader("Cache-Control", "no-cache");
    request->send(response); });

        server.on("/config", HTTP_POST, [this](AsyncWebServerRequest *request)
                  {
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
    request->send(response); });

        // Handle CORS for AJAX requests
        server.on("/config", HTTP_OPTIONS, [](AsyncWebServerRequest *request)
                  {
    AsyncWebServerResponse* response = request->beginResponse(200);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request->send(response); });

        server.onNotFound([](AsyncWebServerRequest *request)
                          { request->redirect("http://4.3.2.1"); });

        server.begin();
    }

    // --------------------------------------------------------------------------
    // ------------------------- WIFI FUNCTIONS ---------------------------------
    // --------------------------------------------------------------------------

    // Function to attempt WiFi connection
    bool attemptWiFiConnection()
    {
        if (savedSSID == "" || savedPassword == "")
        {
            return false;
        }

        // Stop AP during connection attempt to save power
        if (apModeActive && !initialSetup)
        {
            stopAccessPoint();
        }

        WiFi.mode(WIFI_STA);
        WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

        // Wait up to 20 seconds for connection
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 40)
        {
            delay(500);
            attempts++;

            // Continue processing AP requests during initial setup only
            if (apModeActive && initialSetup)
            {
                dnsServer.processNextRequest();
            }
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            // Initialize MQTT with saved configuration
            client.setServer(MQTT_SERVER_IP.c_str(), MQTT_SERVER_PORT);
            configTime(3600, 3600, NTP_SERVER_URL);
            getLocalTime(&localTime);
            return true;
        }

        return false;
    }
};