#include "config.h"
#include "wifi-handler.h"

// Instances
WifiHandler wifiHandler;

// Function prototypes
void mqttCallback(char* topic, uint8_t* payload, unsigned int length);
void activatePump();
void deactivatePump();
void checkAPStatus();

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PUMP_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);
  digitalWrite(PUMP_PIN, LOW);

  Serial.println("=== Water Station Starting ===");

  // Set MQTT callback
  wifiHandler.setMqttCallback(mqttCallback);

  Serial.println("Starting Access Point for 1 minute...");
  wifiHandler.startAccessPoint();

  Serial.print("Station MAC: ");
  Serial.println(WiFi.softAPmacAddress());

  bool hasCredentials = wifiHandler.loadWiFiCredentials();
  wifiHandler.loadMQTTConfig();

  Serial.print("Has credentials: ");
  Serial.println(hasCredentials ? "YES" : "NO");

  Serial.println("Setup complete - AP should be visible as 'Smart-flower-pot'");
  checkAPStatus();
}

void loop() {
  unsigned long currentMillis = millis();

  // Handle web server requests during AP mode
  if (wifiHandler.isApModeActive()) {
    wifiHandler.dnsServer.processNextRequest();
    wifiHandler.server.handleClient();

    // Check AP timeout (1 minute)
    if (currentMillis - wifiHandler.getApStartTime() >= AP_TIMEOUT) {
      bool hasCredentials = wifiHandler.loadWiFiCredentials();

      if (hasCredentials) {
        Serial.println("AP timeout - stopping AP and attempting WiFi connection...");
        wifiHandler.stopAccessPoint();
        delay(1000);
        WiFi.mode(WIFI_STA);
        currentWiFiState = WIFI_CONNECTING;
      } else {
        Serial.println("No credentials found - restarting AP...");
        wifiHandler.stopAccessPoint();
        delay(1000);
        wifiHandler.startAccessPoint();
      }
    }

    // Check if credentials were saved during AP mode
    if (wifiHandler.areCredentialsSaved()) {
      wifiHandler.setCredentialsSaved(false);
      Serial.println("New credentials saved - stopping AP and attempting connection...");
      wifiHandler.stopAccessPoint();
      delay(1500);

      WiFi.mode(WIFI_STA);
      currentWiFiState = WIFI_CONNECTING;
    }
  }

  // Handle different WiFi states
  switch (currentWiFiState) {
    case WIFI_SETUP_MODE:
      // Waiting for credentials or AP timeout
      break;

    case WIFI_CONNECTING:
      if (wifiHandler.attemptWiFiConnection()) {
        Serial.println("WiFi connected successfully!");
        currentWiFiState = WIFI_CONNECTED;
      } else {
        Serial.println("WiFi connection failed");
        currentWiFiState = WIFI_FAILED;
        lastWiFiAttempt = currentMillis;
      }
      break;

    case WIFI_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi connection lost");
        currentWiFiState = WIFI_FAILED;
        lastWiFiAttempt = currentMillis;
        break;
      }

      // Check if new credentials were saved
      if (wifiHandler.areCredentialsSaved()) {
        wifiHandler.setCredentialsSaved(false);
        Serial.println("New credentials detected - reconnecting...");
        WiFi.disconnect();
        delay(1000);
        currentWiFiState = WIFI_CONNECTING;
      }

      // Ensure MQTT is connected
      if (!wifiHandler.client.connected()) {
        wifiHandler.reconnect();
      }
      wifiHandler.client.loop();
      break;

    case WIFI_FAILED:
      // Retry WiFi connection every 30 seconds
      if (currentMillis - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
        // IMPORTANT: Fully disconnect and wait before retry
        Serial.println("Preparing to retry WiFi connection...");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(500);  // Wait for WiFi to fully stop

        WiFi.mode(WIFI_STA);
        delay(500);  // Wait for mode change

        Serial.println("Retrying WiFi connection...");
        currentWiFiState = WIFI_CONNECTING;
        lastWiFiAttempt = currentMillis;
      }
      break;
  }

  // Button manual override (active LOW)
  if (digitalRead(BTN_PIN) == LOW) {
    if (!pumpActive) {
      Serial.println("Manual pump activation via button");
      activatePump();
      pumpActive = true;
      pumpStartTime = currentMillis;
      wifiHandler.sendPumpStatus(true);
    }
  }

  // Check if pump should be deactivated
  if (pumpActive && (currentMillis - pumpStartTime >= PUMP_DURATION)) {
    deactivatePump();
    pumpActive = false;
    wifiHandler.sendPumpStatus(false);
    Serial.println("Pump deactivated after 5s");
  }

  // Status print every 10 seconds
  static unsigned long lastStatusPrint = 0;
  if (currentMillis - lastStatusPrint >= 10000) {
    lastStatusPrint = currentMillis;
    Serial.println("=== Water Station Status ===");
    checkAPStatus();
    Serial.println("WiFi: " + String(currentWiFiState == WIFI_CONNECTED ? "CONNECTED" : currentWiFiState == WIFI_CONNECTING ? "CONNECTING"
                                                                                      : currentWiFiState == WIFI_FAILED     ? "FAILED"
                                                                                                                            : "SETUP MODE"));
    Serial.println("MQTT: " + String(wifiHandler.client.connected() ? "CONNECTED" : "DISCONNECTED"));
    Serial.println("Pump: " + String(pumpActive ? "ACTIVE" : "IDLE"));

    if (currentWiFiState == WIFI_CONNECTED) {
      Serial.println("WiFi IP: " + WiFi.localIP().toString());
      Serial.println("WiFi RSSI: " + String(WiFi.RSSI()) + " dBm");
    }
    Serial.println();
  }

  delay(100);
}

void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
  Serial.print("MQTT message received on topic: ");
  Serial.println(topic);

  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message: ");
  Serial.println(message);

  if (String(topic) == MQTT_TOPIC_WATER_COMMAND) {
    if (message == "1" && !pumpActive) {
      Serial.println("✓ Watering command received - activating pump for 5s");

      // Directly control pump with delay for testing
      digitalWrite(PUMP_PIN, HIGH);
      Serial.println("Pump ACTIVATED");

      delay(5000);  // Block for 5 seconds

      digitalWrite(PUMP_PIN, LOW);
      Serial.println("Pump DEACTIVATED");

      wifiHandler.sendPumpStatus(false);
    }
  }
}

void activatePump() {
  digitalWrite(PUMP_PIN, HIGH);
  Serial.println("Pump ACTIVATED");
}

void deactivatePump() {
  digitalWrite(PUMP_PIN, LOW);
  Serial.println("Pump DEACTIVATED");
}

void checkAPStatus() {
  if (wifiHandler.isApModeActive()) {
    Serial.println("✓ AP is ACTIVE - SSID: Smart-flower-pot");
    Serial.print("✓ AP IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("✓ AP MAC: ");
    Serial.println(WiFi.softAPmacAddress());
    Serial.print("✓ Connected stations: ");
    Serial.println(WiFi.softAPgetStationNum());
  } else {
    Serial.println("✗ AP is INACTIVE");
  }
}