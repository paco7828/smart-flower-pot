#include "config.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "html.h"

// --------------------------------------------------------------------------
// ------------------------- GLOBAL INSTANCES -------------------------------
// --------------------------------------------------------------------------

WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences;
DNSServer dnsServer;
WebServer server(80);

// --------------------------------------------------------------------------
// ------------------------- GLOBAL VARIABLES -------------------------------
// --------------------------------------------------------------------------

unsigned long apStartTime = 0;
bool apModeActive = false;
bool credentialsSaved = false;
String savedSSID = "";
String savedPassword = "";

// --------------------------------------------------------------------------
// ------------------------- FUNCTION PROTOTYPES ----------------------------
// --------------------------------------------------------------------------

void mqttCallback(char* topic, uint8_t* payload, unsigned int length);
void reconnectMQTT();
bool loadMQTTConfig();
bool loadWiFiCredentials();
void saveConfiguration(String ssid, String wifiPass, String mqttServer, int mqttPort, String mqttUser, String mqttPass);
void startAccessPoint();
void stopAccessPoint();
void setupWebServer();
bool connectWiFi();

// --------------------------------------------------------------------------
// ------------------------- SETUP ------------------------------------------
// --------------------------------------------------------------------------

void setup() {
  // Start serial communication
  Serial.begin(115200);
  delay(1000);

  // Pin modes
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);

  // Default pin states
  digitalWrite(PUMP_PIN, LOW);

  // Add MQTT callback
  client.setCallback(mqttCallback);

  // Load MQTT config
  loadMQTTConfig();

  // Start AP
  startAccessPoint();
}

// --------------------------------------------------------------------------
// -------------------------------LOOP --------------------------------------
// --------------------------------------------------------------------------

void loop() {
  unsigned long currentMillis = millis();

  // Handle AP mode
  if (apModeActive) {
    dnsServer.processNextRequest();
    server.handleClient();

    // AP timeout
    if (currentMillis - apStartTime >= AP_TIMEOUT) {
      if (loadWiFiCredentials()) {
        // Stop AP & connect to WiFi
        Serial.println("AP timeout - switching to WiFi mode");
        stopAccessPoint();
        WiFi.mode(WIFI_STA);
        currentWiFiState = WIFI_CONNECTING;
      } else {
        // Restart AP
        Serial.println("AP timeout - no credentials, restarting AP");
        stopAccessPoint();
        startAccessPoint();
      }
    }

    // Save new credentials & connect to WiFi
    if (credentialsSaved) {
      credentialsSaved = false;
      Serial.println("New credentials - switching to WiFi mode");
      stopAccessPoint();
      WiFi.mode(WIFI_STA);
      currentWiFiState = WIFI_CONNECTING;
    }
  }

  // WiFi state machine
  switch (currentWiFiState) {
    case WIFI_CONNECTING:
      currentWiFiState = connectWiFi() ? WIFI_CONNECTED : WIFI_FAILED;
      if (currentWiFiState == WIFI_FAILED) lastWiFiAttempt = currentMillis;
      break;

    case WIFI_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi connection lost");
        currentWiFiState = WIFI_FAILED;
        lastWiFiAttempt = currentMillis;
        break;
      }

      // Check for new credentials
      if (credentialsSaved) {
        credentialsSaved = false;
        Serial.println("New credentials - reconnecting");
        WiFi.disconnect();
        delay(1000);
        currentWiFiState = WIFI_CONNECTING;
        break;
      }

      // Maintain MQTT connection
      if (!client.connected()) reconnectMQTT();
      client.loop();
      break;

    case WIFI_FAILED:
      // Retry connection
      if (currentMillis - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
        Serial.println("Retrying WiFi connection");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(500);
        WiFi.mode(WIFI_STA);
        delay(500);
        currentWiFiState = WIFI_CONNECTING;
        lastWiFiAttempt = currentMillis;
      }
      break;
  }

  // Manual pump button (active LOW)
  if (digitalRead(BTN_PIN) == LOW && !pumpActive) {
    Serial.println("Manual pump activation");
    digitalWrite(PUMP_PIN, HIGH);
    pumpActive = true;
    pumpStartTime = currentMillis;
  }

  // Auto-stop pump after duration
  if (pumpActive && (currentMillis - pumpStartTime >= WATERING_DURATION)) {
    digitalWrite(PUMP_PIN, LOW);
    pumpActive = false;
    Serial.println("Pump deactivated");
  }

  // Status logging
  static unsigned long lastStatusPrint = 0;
  if (currentMillis - lastStatusPrint >= STATUS_LOG_INTERVAL) {
    lastStatusPrint = currentMillis;

    const char* wifiStatus[] = { "SETUP", "CONNECTING", "CONNECTED", "FAILED" };
    Serial.println("WiFi: " + String(wifiStatus[currentWiFiState]));
    Serial.println("MQTT: " + String(client.connected() ? "CONNECTED" : "DISCONNECTED"));
  }

  delay(100);
}

// --------------------------------------------------------------------------
// ------------------------- MQTT -------------------------------------------
// --------------------------------------------------------------------------

void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
  // Copy payload to message
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  // Output message
  Serial.println("MQTT: " + String(topic) + " = " + message);

  // If watering code received => turn pump on
  if (String(topic) == MQTT_TOPIC_WATER_COMMAND && message == "1" && !pumpActive) {
    Serial.println("MQTT watering command received");
    digitalWrite(PUMP_PIN, HIGH);
    pumpActive = true;
    pumpStartTime = millis();
  }
}

void reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;

  // Attempt MQTT connection
  for (int attempts = 0; attempts < MQTT_RECONNECT_ATTEMPTS && !client.connected(); attempts++) {
    String clientId = "water_station_" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), MQTT_USERNAME.c_str(), MQTT_PASSWORD.c_str())) {
      if (client.subscribe(MQTT_TOPIC_WATER_COMMAND)) {
        Serial.println("MQTT subscribed to: " + String(MQTT_TOPIC_WATER_COMMAND));
      }
      return;
    }

    // Failed
    Serial.println("MQTT failed, rc=" + String(client.state()));
    delay(1000);
  }
}

// Load MQTT config from flash
bool loadMQTTConfig() {
  preferences.begin("mqtt", true);
  MQTT_SERVER_IP = preferences.getString("server", MQTT_SERVER_IP);
  MQTT_SERVER_PORT = preferences.getInt("port", MQTT_SERVER_PORT);
  MQTT_USERNAME = preferences.getString("user", MQTT_USERNAME);
  MQTT_PASSWORD = preferences.getString("pass", MQTT_PASSWORD);
  preferences.end();

  Serial.println("MQTT: " + MQTT_SERVER_IP + ":" + String(MQTT_SERVER_PORT));
  return !MQTT_SERVER_IP.isEmpty();
}

// --------------------------------------------------------------------------
// ------------------------- PREFERENCES ------------------------------------
// --------------------------------------------------------------------------

bool loadWiFiCredentials() {
  preferences.begin("wifi", true);
  savedSSID = preferences.getString("ssid", "");
  savedPassword = preferences.getString("pass", "");
  preferences.end();

  if (!savedSSID.isEmpty() && !savedPassword.isEmpty()) {
    Serial.println("Loaded credentials: " + savedSSID);
    return true;
  }
  Serial.println("No saved credentials");
  return false;
}

void saveConfiguration(String ssid, String wifiPass, String mqttServer, int mqttPort, String mqttUser, String mqttPass) {
  // WiFi
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", wifiPass);
  preferences.end();

  // MQTT
  preferences.begin("mqtt", false);
  preferences.putString("server", mqttServer);
  preferences.putInt("port", mqttPort);
  preferences.putString("user", mqttUser);
  preferences.putString("pass", mqttPass);
  preferences.end();

  savedSSID = ssid;
  savedPassword = wifiPass;
  MQTT_SERVER_IP = mqttServer;
  MQTT_SERVER_PORT = mqttPort;
  MQTT_USERNAME = mqttUser;
  MQTT_PASSWORD = mqttPass;

  Serial.println("Configuration saved");
}

// --------------------------------------------------------------------------
// ------------------------- ACCESS POINT -----------------------------------
// --------------------------------------------------------------------------

void startAccessPoint() {
  // Disconnect from WiFi and set AP mode
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  delay(100);

  // Invalid AP config
  if (!WiFi.softAPConfig(localIP, gatewayIP, subnet)) {
    Serial.println("AP config failed!");
    return;
  }

  // Invalid SSID
  if (!WiFi.softAP(AP_SSID)) {
    Serial.println("AP start failed!");
    return;
  }

  Serial.println("AP started: " + String(AP_SSID) + " @ " + WiFi.softAPIP().toString());

  // Start web server
  dnsServer.start(53, "*", localIP);
  setupWebServer();

  apStartTime = millis();
  apModeActive = true;
}

void stopAccessPoint() {
  // Exit if AP isn't active
  if (!apModeActive) return;

  // Stop AP
  Serial.println("Stopping AP");
  dnsServer.stop();
  server.stop();
  WiFi.softAPdisconnect(true);
  apModeActive = false;
  delay(100);
}

void setupWebServer() {
  // Serve main configuration with placeholders (flash values)
  server.on("/", HTTP_GET, []() {
    String html = String(index_html);
    html.replace("%MQTT_SERVER%", MQTT_SERVER_IP);
    html.replace("%MQTT_PORT%", String(MQTT_SERVER_PORT));
    html.replace("%MQTT_USER%", MQTT_USERNAME);
    html.replace("%MQTT_PASS%", MQTT_PASSWORD);
    server.send(200, "text/html", html);
  });

  // Handle configuration form submission
  server.on("/config", HTTP_POST, []() {
    // Extract form parameters
    String wifiSSID = server.arg("wifi_ssid");
    String wifiPassword = server.arg("wifi_password");
    String mqttServer = server.arg("mqtt_server");
    String mqttPort = server.arg("mqtt_port");
    String mqttUser = server.arg("mqtt_username");
    String mqttPass = server.arg("mqtt_password");
    int port = mqttPort.toInt();

    // Validate all required fields
    if (wifiSSID.isEmpty() || wifiPassword.isEmpty() || mqttServer.isEmpty() || mqttUser.isEmpty() || mqttPass.isEmpty() || port < 1 || port > 65535) {
      server.send(400, "text/plain", "Invalid parameters");
      return;
    }

    // Save configuration and mark credentials as updated
    saveConfiguration(wifiSSID, wifiPassword, mqttServer, port, mqttUser, mqttPass);
    credentialsSaved = true;
    server.send(200, "text/html", success_html);
  });

  // Handle CORS preflight requests
  server.on("/config", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(200);
  });

  // Redirect all unknown requests to main page (captive portal behavior)
  server.onNotFound([]() {
    server.sendHeader("Location", "http://4.3.2.1/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  Serial.println("Web server started");
}

// --------------------------------------------------------------------------
// ------------------------- WIFI -------------------------------------------
// --------------------------------------------------------------------------

bool connectWiFi() {
  // No credentials
  if (savedSSID.isEmpty() || savedPassword.isEmpty()) {
    Serial.println("No credentials available");
    return false;
  }

  // Stop AP if running
  if (apModeActive) stopAccessPoint();

  // Connect to WiFi
  Serial.println("Connecting to: " + savedSSID);
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

  // Wait for WiFi connection
  for (int attempts = 0; attempts < 30 && WiFi.status() != WL_CONNECTED; attempts++) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
    client.setServer(MQTT_SERVER_IP.c_str(), MQTT_SERVER_PORT);

    // Get NTP time
    configTime(3600, 3600, NTP_SERVER_URL);
    return true;
  }

  Serial.println("WiFi connection failed");
  return false;
}