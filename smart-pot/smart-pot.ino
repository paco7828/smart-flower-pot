#include "config.h"
#include <DHT.h>
#include "wifi-handler.h"
#include <esp_sleep.h>

// Instances
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);
WifiHandler wifiHandler;

void setup() {
  // Initialize sensors
  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(WATER_LEVEL_PIN, INPUT);
  pinMode(WATER_PUMP_PIN, OUTPUT);
  digitalWrite(WATER_PUMP_PIN, LOW);
  dht.begin();

  // Load existing credentials and configuration
  bool hasCredentials = wifiHandler.loadWiFiCredentials();
  wifiHandler.loadMQTTConfig();
  wifiHandler.loadLastWateringTime();
  wifiHandler.loadLastWaterNotificationTime();

  if (!hasCredentials) {
    // No credentials saved - start in setup mode
    wifiHandler.startAccessPoint();
    wifiHandler.setInitialSetup(true);
    currentWiFiState = WIFI_SETUP_MODE;
  } else {
    // Credentials exist - try to connect directly
    wifiHandler.setInitialSetup(false);
    currentWiFiState = WIFI_CONNECTING;

    if (wifiHandler.attemptWiFiConnection()) {
      currentWiFiState = WIFI_CONNECTED;
    } else {
      // Connection failed - start AP for reconfiguration
      currentWiFiState = WIFI_FAILED;
      wifiHandler.startAccessPoint();
    }
  }
}

void loop() {
  unsigned long currentMillis = millis();

  // Handle web server requests
  if (wifiHandler.isApModeActive()) {
    wifiHandler.dnsServer.processNextRequest();
  }

  // Handle different WiFi states
  switch (currentWiFiState) {
    case WIFI_SETUP_MODE:
      // Initial setup mode with AP
      if (wifiHandler.areCredentialsSaved()) {
        wifiHandler.setCredentialsSaved(false);
        delay(1500);  // Give time for success page to be served

        if (wifiHandler.attemptWiFiConnection()) {
          currentWiFiState = WIFI_CONNECTED;
          wifiHandler.setInitialSetup(false);
          wifiHandler.stopAccessPoint();
        } else {
          currentWiFiState = WIFI_FAILED;
        }
      }

      // Check AP timeout during initial setup
      if (currentMillis - wifiHandler.getApStartTime() >= AP_TIMEOUT) {
        // If we have saved credentials but couldn't connect, try without AP
        if (wifiHandler.loadWiFiCredentials()) {
          wifiHandler.stopAccessPoint();
          wifiHandler.setInitialSetup(false);
          currentWiFiState = WIFI_CONNECTING;
        }
      }
      break;

    case WIFI_CONNECTING:
      if (wifiHandler.attemptWiFiConnection()) {
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
      if (wifiHandler.areCredentialsSaved()) {
        wifiHandler.setCredentialsSaved(false);
        delay(1500);
        WiFi.disconnect();
        delay(1000);
        currentWiFiState = WIFI_CONNECTING;
        break;
      }

      // Reconnect MQTT if needed
      if (!wifiHandler.client.connected()) {
        wifiHandler.reconnect();
      }
      wifiHandler.client.loop();

      // Handle sensor operations
      handleSensorOperations(currentMillis);
      break;

    case WIFI_FAILED:
      // Handle credential updates
      if (wifiHandler.areCredentialsSaved()) {
        wifiHandler.setCredentialsSaved(false);
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
    wifiHandler.sendTemperature(dataBuffer);

    // Humidity
    dtostrf(humidity, 1, 2, dataBuffer);
    wifiHandler.sendHumidity(dataBuffer);

    // Water presence
    sprintf(dataBuffer, "%d", waterPresent ? 1 : 0);
    wifiHandler.sendWaterPresence(dataBuffer);

    // Soil moisture
    sprintf(dataBuffer, "%d", moisture);
    wifiHandler.sendMoisture(dataBuffer);

    // Sunlight presence
    sprintf(dataBuffer, "%d", isDark ? 0 : 1);
    wifiHandler.sendSunlightPresence(dataBuffer);

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
    if (!waterNotifSent || currentMillis - wifiHandler.getLastWaterNotificationTime() >= WATER_NOTIFICATION_INTERVAL) {
      wifiHandler.sendNotification();
      wifiHandler.setLastWaterNotificationTime(currentMillis);
      wifiHandler.saveLastWaterNotificationTime();
      waterNotifSent = true;
    }
  } else {
    // Reset notification flag when water is present
    waterNotifSent = false;
  }

  // Plant watering logic
  if (moisture >= MOISTURE_THRESHOLD && waterPresent && !isWatering && (currentMillis - wifiHandler.getLastWateringTime() >= WATERING_INTERVAL)) {
    waterPlant();
  }
}

// Plant watering function
void waterPlant() {
  // Update last watering time
  wifiHandler.setLastWateringTime(millis());
  wifiHandler.saveLastWateringTime();

  // Publish watering time to MQTT
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeBuffer[9];
    sprintf(timeBuffer, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    wifiHandler.sendLastWateringTime(timeBuffer);
  }

  // Start watering
  isWatering = true;
  digitalWrite(WATER_PUMP_PIN, HIGH);
  wateringStartTime = millis();
}