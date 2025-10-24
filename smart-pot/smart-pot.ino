#include "config.h"
#include "wifi-handler.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <esp_sleep.h>

// Instances
OneWire oneWire(DS_TEMP_PIN);
DallasTemperature temperatureSensor(&oneWire);
WifiHandler wifiHandler;

// Function prototypes
void printSensorValues(unsigned long currentMillis);
void handleSensorOperations(unsigned long currentMillis);
void handleBuzzerAlerts(unsigned long currentMillis);
void checkWateringStatus();
void playStartupSound();
void handleAutomation(unsigned long currentMillis);
void lowMoistureBeep();
void goToDeepSleep();

void setup() {
  Serial.begin(115200);

  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Only play startup sound on cold boot, not deep sleep wakeup
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) {
    // This is a cold boot (power on), play startup sound
    playStartupSound();
  } else {
    // This is a deep sleep wakeup, just do a quick silent initialization
    Serial.println("Woke up from deep sleep - skipping startup sound");
  }

  temperatureSensor.begin();

  // Initialize RTC data on first boot
  if (!rtcData.isInitialized) {
    rtcData.isInitialized = true;
    rtcData.bootCount = 0;
    rtcData.totalSleepTime = 0;
    rtcData.lastLowMoistureBeep = 0;
    rtcData.lastWateringTime = 0;  // This causes the issue!
  } else {
    rtcData.bootCount++;
    if (rtcData.bootCount > 1) {
      rtcData.totalSleepTime += (DARK_SEND_INTERVAL / 1000);
    }
  }

  wakeupTime = millis();
  tasksCompleted = false;
  justWokeUp = true;

  bool hasCredentials = wifiHandler.loadWiFiCredentials();
  wifiHandler.loadMQTTConfig();

  // Check if we have credentials and skip AP mode if we do
  if (hasCredentials) {
    Serial.println("Credentials found, attempting direct WiFi connection...");
    WiFi.mode(WIFI_STA);
    currentWiFiState = WIFI_CONNECTING;
  } else {
    Serial.println("Starting Access Point for 1 minute...");
    wifiHandler.startAccessPoint();
    wifiHandler.setInitialSetup(false);
    currentWiFiState = WIFI_SETUP_MODE;
  }

  Serial.print("Boot count: ");
  Serial.println(rtcData.bootCount);
  Serial.print("Has credentials: ");
  Serial.println(hasCredentials ? "YES" : "NO");

  // Debug: Print wakeup reason
  printWakeupReason();
}

void loop() {
  unsigned long currentMillis = millis();
  printSensorValues(currentMillis);

  // Handle web server requests during AP mode
  if (wifiHandler.isApModeActive()) {
    wifiHandler.dnsServer.processNextRequest();
    wifiHandler.server.handleClient();

    if (currentMillis - wifiHandler.getApStartTime() >= AP_TIMEOUT) {
      wifiHandler.stopAccessPoint();
      Serial.println("AP timeout reached, stopping AP");

      bool hasCredentials = wifiHandler.loadWiFiCredentials();
      if (hasCredentials) {
        WiFi.mode(WIFI_STA);
        currentWiFiState = WIFI_CONNECTING;
        Serial.println("Credentials available, attempting WiFi connection...");
      } else {
        currentWiFiState = WIFI_FAILED;
        Serial.println("No credentials available, watering disabled until configured");
      }
    }

    if (wifiHandler.areCredentialsSaved()) {
      wifiHandler.setCredentialsSaved(false);
      Serial.println("New credentials saved, stopping AP and attempting connection...");
      wifiHandler.stopAccessPoint();
      delay(1500);

      WiFi.mode(WIFI_STA);
      currentWiFiState = WIFI_CONNECTING;
    }
  }

  // Check watering status
  checkWateringStatus();

  // Handle different WiFi states
  switch (currentWiFiState) {
    case WIFI_SETUP_MODE:
      // Waiting in AP mode
      break;

    case WIFI_CONNECTING:
      if (wifiHandler.attemptWiFiConnection()) {
        currentWiFiState = WIFI_CONNECTED;
        Serial.println("WiFi connected successfully!");
      } else {
        currentWiFiState = WIFI_FAILED;
        lastWiFiAttempt = currentMillis;
        Serial.println("WiFi connection failed");
      }
      break;

    case WIFI_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) {
        currentWiFiState = WIFI_FAILED;
        lastWiFiAttempt = currentMillis;
        Serial.println("WiFi connection lost");
        break;
      }

      // Ensure MQTT is connected with more aggressive retry
      if (!wifiHandler.client.connected()) {
        Serial.println("MQTT disconnected, attempting to reconnect...");
        wifiHandler.reconnect();

        // If we just woke up and MQTT isn't connected, delay a bit to allow connection
        if (justWokeUp) {
          delay(2000);
        }
      }

      if (wifiHandler.client.connected()) {
        wifiHandler.client.loop();
        handleSensorOperations(currentMillis);
      }
      break;

    case WIFI_FAILED:
      if (currentMillis - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
        currentWiFiState = WIFI_CONNECTING;
        Serial.println("Retrying WiFi connection...");
      }
      break;
  }

  // Handle buzzer alerts regardless of WiFi state
  handleBuzzerAlerts(currentMillis);

  // Handle automation (watering) - only when connected
  handleAutomation(currentMillis);

  // Check if we should go to deep sleep
  if (!wifiHandler.isApModeActive() && areAllTasksCompleted()) {
    Serial.println("All conditions met for deep sleep:");
    Serial.println("- Not in AP mode: YES");
    Serial.println("- Tasks completed: YES");
    Serial.println("- Is dark: " + String(isDark ? "YES" : "NO"));
    Serial.println("- Time awake: " + String(millis() - wakeupTime) + "ms");
    Serial.println("Going to deep sleep...");
    goToDeepSleep();
  }

  delay(100);
}

void printSensorValues(unsigned long currentMillis) {
  static unsigned long lastPrintTime = 0;

  if (currentMillis - lastPrintTime >= 5000) {
    lastPrintTime = currentMillis;

    int currentLdr = analogRead(LDR_PIN);

    temperatureSensor.requestTemperatures();
    float currentTemp = temperatureSensor.getTempCByIndex(0);

    Serial.println("=== Smart Pot Status ===");
    Serial.println("Sunlight: " + String(currentLdr) + (currentLdr > SUNLIGHT_THRESHOLD ? " BRIGHT" : " DARK"));
    Serial.println("Moisture: " + String(currentMoisture) + (currentMoisture < MOISTURE_THRESHOLD ? " DRY" : " WET"));
    Serial.println("Temperature: " + String(currentTemp, 2) + "°C");
    Serial.println("WiFi: " + String(currentWiFiState == WIFI_CONNECTED ? "CONNECTED" : currentWiFiState == WIFI_CONNECTING ? "CONNECTING"
                                                                                      : currentWiFiState == WIFI_FAILED     ? "FAILED"
                                                                                                                            : "SETUP MODE"));
    Serial.println("MQTT: " + String(wifiHandler.client.connected() ? "CONNECTED" : "DISCONNECTED"));
    Serial.println("Watering Status: " + String(isWatering ? "ACTIVE" : "IDLE"));

    if (currentWiFiState == WIFI_CONNECTED) {
      Serial.println("WiFi IP: " + WiFi.localIP().toString());
      Serial.println("WiFi RSSI: " + String(WiFi.RSSI()) + " dBm");
    }
    Serial.println();
  }
}

void handleSensorOperations(unsigned long currentMillis) {
  ldrValue = analogRead(LDR_PIN);
  isDark = ldrValue <= SUNLIGHT_THRESHOLD;

  bool shouldSendData = false;

  if (justWokeUp) {
    shouldSendData = true;
    justWokeUp = false;
  } else if (!isDark && (currentMillis - lastDataSendTime >= LIGHT_SEND_INTERVAL)) {
    shouldSendData = true;
  }

  if (shouldSendData) {
    lastDataSendTime = currentMillis;

    temperatureSensor.requestTemperatures();
    temperature = temperatureSensor.getTempCByIndex(0);
    moisture = analogRead(MOISTURE_PIN);

    char dataBuffer[10];

    if (temperature != DEVICE_DISCONNECTED_C && temperature > -55 && temperature < 125) {
      dtostrf(temperature, 1, 2, dataBuffer);
      wifiHandler.sendTemperature(dataBuffer);
    }

    sprintf(dataBuffer, "%d", moisture);
    wifiHandler.sendMoisture(dataBuffer);

    sprintf(dataBuffer, "%d", isDark ? 0 : 1);
    wifiHandler.sendSunlightPresence(dataBuffer);

    delay(1000);
  }
}

bool areAllTasksCompleted() {
  unsigned long currentMillis = millis();

  // Don't even consider sleep until we've been awake for at least 15 seconds
  if (currentMillis - wakeupTime < 15000) {
    return false;
  }

  // Only consider tasks completed if:
  // 1. We're in dark conditions AND
  // 2. We've sent at least one set of data after wakeup AND
  // 3. MQTT is connected AND
  // 4. We've had enough time to check for watering after MQTT connection

  if (isDark && lastDataSendTime > wakeupTime && wifiHandler.client.connected()) {

    // Additional safety: if soil was dry, make sure we waited long enough
    // for the automation to potentially trigger watering
    static bool wateringOpportunityGiven = false;
    static unsigned long mqttConnectedTime = 0;

    // Track when MQTT first connected
    if (wifiHandler.client.connected() && mqttConnectedTime == 0) {
      mqttConnectedTime = currentMillis;
    }

    // Give at least 10 seconds after MQTT connection for watering decision
    if (mqttConnectedTime > 0 && (currentMillis - mqttConnectedTime < 10000)) {
      return false;
    }

    // If we get here, we've given enough time for watering to trigger
    wateringOpportunityGiven = true;
    return true;
  }

  return false;
}

void handleBuzzerAlerts(unsigned long currentMillis) {
  // Read moisture every 5 seconds
  if (currentMillis - lastMoistureReading >= 5000) {
    moisture = analogRead(MOISTURE_PIN);
    lastMoistureReading = currentMillis;
  }

  unsigned long timeSinceLastLowMoistureBeep = rtcData.totalSleepTime + currentMillis - rtcData.lastLowMoistureBeep;

  // Low moisture beep should work regardless of sleep state
  if (moisture < MOISTURE_THRESHOLD && timeSinceLastLowMoistureBeep >= LOW_MOISTURE_BEEP_INTERVAL) {
    lowMoistureBeep();
    rtcData.lastLowMoistureBeep = rtcData.totalSleepTime + currentMillis;
    Serial.println("Low moisture beep triggered");
  }
}

void checkWateringStatus() {
  if (isWatering) {
    isWatering = false;
  }
}

void playStartupSound() {
  tone(BUZZER_PIN, 1000, 200);
  delay(250);
  tone(BUZZER_PIN, 1500, 200);
  delay(250);
  tone(BUZZER_PIN, 2000, 300);
  delay(350);
}

void handleAutomation(unsigned long currentMillis) {
  // Only trigger watering when connected to WiFi and MQTT
  if (currentWiFiState != WIFI_CONNECTED || !wifiHandler.client.connected()) {
    Serial.println("Automation: Waiting for MQTT connection...");
    return;
  }

  // Debug: Print MQTT status
  static unsigned long lastMqttDebug = 0;
  if (currentMillis - lastMqttDebug >= 2000) {
    lastMqttDebug = currentMillis;
    Serial.println("Automation: MQTT connected, checking moisture...");
  }

  // Read moisture every 2 seconds
  if (currentMillis - lastMoistureReading >= 2000) {
    moisture = analogRead(MOISTURE_PIN);
    lastMoistureReading = currentMillis;

    Serial.println("Automation - Moisture: " + String(moisture) + " | Threshold: " + String(MOISTURE_THRESHOLD) + " | Dry: " + String(moisture < MOISTURE_THRESHOLD ? "YES" : "NO"));

    // If moisture is below threshold (dry soil), trigger watering via MQTT
    if (moisture < MOISTURE_THRESHOLD) {
      unsigned long timeSinceLastWatering = rtcData.totalSleepTime + millis() - rtcData.lastWateringTime;

      // Special case for first boot: if lastWateringTime is 0, allow immediate watering
      bool allowWatering = (rtcData.lastWateringTime == 0) || (timeSinceLastWatering >= WATERING_COOLDOWN);

      if (allowWatering) {
        Serial.println("!!! SOIL IS DRY - SENDING WATERING COMMAND VIA MQTT !!!");
        wifiHandler.sendWaterCommand();

        wateringStartTime = millis();
        rtcData.lastWateringTime = rtcData.totalSleepTime + millis();

        // Add small delay to ensure MQTT message is sent
        delay(500);
      } else {
        Serial.println("Soil is dry but watering is in cooldown. Next watering in: " + String((WATERING_COOLDOWN - timeSinceLastWatering) / 1000) + "s");
      }
    }
  }
}

void lowMoistureBeep() {
  tone(BUZZER_PIN, LOW_MOISTURE_HZ, 200);
}

void goToDeepSleep() {
  Serial.println("Going to deep sleep for 30 minutes...");

  if (isWatering) {
    isWatering = false;
  }

  esp_sleep_enable_timer_wakeup(DARK_SEND_INTERVAL);

  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);

  delay(100);
  esp_deep_sleep_start();
}

void printWakeupReason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  Serial.print("Wakeup reason: ");
  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("external signal using RTC_IO");
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("external signal using RTC_CNTL");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("timer");
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      Serial.println("touchpad");
      break;
    case ESP_SLEEP_WAKEUP_ULP:
      Serial.println("ULP program");
      break;
    default:
      Serial.println("NOT from deep sleep (cold boot)");
      break;
  }
}