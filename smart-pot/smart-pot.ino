#include "config.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "wifi-handler.h"
#include <esp_sleep.h>

// DS18B20 Temperature sensor setup
OneWire oneWire(DS_TEMP_PIN);
DallasTemperature temperatureSensor(&oneWire);

// Instances
WifiHandler wifiHandler;

void setup() {
  // Initialize sensors and actuators
  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(WATER_PUMP_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(WATER_SENSOR_PIN, INPUT);

  digitalWrite(WATER_PUMP_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  // Initialize DS18B20 temperature sensor
  temperatureSensor.begin();

  // Initialize RTC data on first boot
  if (!rtcData.isInitialized) {
    rtcData.isInitialized = true;
    rtcData.bootCount = 0;
    rtcData.totalSleepTime = 0;
    rtcData.lastLowMoistureBeep = 0;
    rtcData.lastNoWaterBeep = 0;
  }
  rtcData.bootCount++;

  // Add sleep time to total (except for first boot)
  if (rtcData.bootCount > 1) {
    rtcData.totalSleepTime += (DARK_SEND_INTERVAL / 1000);  // Convert to milliseconds
  }

  // Record wake up time
  wakeupTime = millis();
  tasksCompleted = false;
  justWokeUp = true;

  // Load existing credentials and configuration
  bool hasCredentials = wifiHandler.loadWiFiCredentials();
  wifiHandler.loadMQTTConfig();

  if (!hasCredentials) {
    // No credentials saved - start in setup mode (stay awake)
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
      // Connection failed - start AP for reconfiguration (stay awake)
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
      // Initial setup mode with AP - stay awake until configured
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
      // Attempt to connect to WiFi
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

      // Handle sensor operations and check for sleep
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
      // If we can't connect and have been awake too long, go to sleep (only if dark)
      else if (currentMillis - wakeupTime >= AWAKE_TIME_MS && !wifiHandler.isApModeActive()) {
        // Read light sensor to determine if we should sleep
        ldrValue = analogRead(LDR_PIN);
        isDark = ldrValue <= SUNLIGHT_THRESHOLD;
        if (isDark) {
          goToDeepSleep();
        }
      }
      break;
  }

  // Handle buzzer alerts regardless of WiFi state
  handleBuzzerAlerts(currentMillis);

  // Check if we should go to deep sleep (only when dark, not in AP mode, and tasks completed)
  if (!wifiHandler.isApModeActive() && tasksCompleted && (currentMillis - wakeupTime >= AWAKE_TIME_MS)) {
    // Read light sensor to determine if we should sleep
    ldrValue = analogRead(LDR_PIN);
    isDark = ldrValue <= SUNLIGHT_THRESHOLD;

    if (isDark) {
      goToDeepSleep();
    } else {
      // It's sunny - reset task completion to continue operation
      tasksCompleted = false;
      justWokeUp = false;  // Reset to allow periodic data sending
    }
  }

  delay(100);
}

// Function to handle sensor operations (only when connected to WiFi)
void handleSensorOperations(unsigned long currentMillis) {
  checkWateringStatus();

  // Read light sensor to determine current light condition
  ldrValue = analogRead(LDR_PIN);
  isDark = ldrValue <= SUNLIGHT_THRESHOLD;

  // Send data when we wake up OR every minute when sunny OR every 10 minutes when dark
  bool shouldSendData = false;

  if (justWokeUp) {
    shouldSendData = true;
    justWokeUp = false;
  } else if (!isDark && (currentMillis - lastDataSendTime >= LIGHT_SEND_INTERVAL)) {
    shouldSendData = true;
  }

  if (shouldSendData) {
    lastDataSendTime = currentMillis;

    // Read DS18B20 temperature sensor
    temperatureSensor.requestTemperatures();
    temperature = temperatureSensor.getTempCByIndex(0);

    // Read other sensors
    moisture = analogRead(MOISTURE_PIN);
    hasWater = digitalRead(WATER_SENSOR_PIN);

    // Buffer for MQTT data
    char dataBuffer[10];

    // Temperature (only if valid reading)
    if (temperature != DEVICE_DISCONNECTED_C && temperature > -55 && temperature < 125) {
      dtostrf(temperature, 1, 2, dataBuffer);
      wifiHandler.sendTemperature(dataBuffer);
    }

    // Soil moisture
    sprintf(dataBuffer, "%d", moisture);
    wifiHandler.sendMoisture(dataBuffer);

    // Sunlight presence
    sprintf(dataBuffer, "%d", isDark ? 0 : 1);
    wifiHandler.sendSunlightPresence(dataBuffer);

    // Water level status
    sprintf(dataBuffer, "%d", hasWater ? 1 : 0);
    wifiHandler.sendWaterPresence(dataBuffer);

    // Allow some time for MQTT messages to be sent
    delay(1000);
  }

  // Handle automated tasks
  handleAutomation(currentMillis);

  // Mark tasks as completed only if it's dark (for sleep decision)
  if (isDark) {
    tasksCompleted = true;
  }
}

// Function to handle buzzer alerts
void handleBuzzerAlerts(unsigned long currentMillis) {
  // Read current sensor values
  moisture = analogRead(MOISTURE_PIN);
  hasWater = digitalRead(WATER_SENSOR_PIN);

  // Calculate time since last beeps (considering deep sleep cycles)
  unsigned long timeSinceLastLowMoistureBeep = rtcData.totalSleepTime + currentMillis - rtcData.lastLowMoistureBeep;
  unsigned long timeSinceLastNoWaterBeep = rtcData.totalSleepTime + currentMillis - rtcData.lastNoWaterBeep;

  // Low moisture beep every 5 minutes (300000 ms)
  if (moisture >= MOISTURE_THRESHOLD && timeSinceLastLowMoistureBeep >= LOW_MOISTURE_BEEP_INTERVAL) {
    singleBeep();
    rtcData.lastLowMoistureBeep = rtcData.totalSleepTime + currentMillis;
  }

  // No water beep every 10 minutes (600000 ms)
  if (!hasWater && timeSinceLastNoWaterBeep >= NO_WATER_BEEP_INTERVAL) {
    doubleBeep();
    rtcData.lastNoWaterBeep = rtcData.totalSleepTime + currentMillis;
  }
}

// Function to check if watering is happening
void checkWateringStatus() {
  if (isWatering && millis() - wateringStartTime >= WATERING_DURATION) {
    isWatering = false;
    digitalWrite(WATER_PUMP_PIN, LOW);
  }
}

// Automation function
void handleAutomation(unsigned long currentMillis) {
  // Read water sensor status
  hasWater = digitalRead(WATER_SENSOR_PIN);

  // Plant watering logic - moisture-based watering only if water is available
  if (moisture >= MOISTURE_THRESHOLD && !isWatering && hasWater) {
    waterPlant();
  }
}

// Plant watering function
void waterPlant() {
  // Start watering only if water is available
  if (hasWater) {
    isWatering = true;
    digitalWrite(WATER_PUMP_PIN, HIGH);
    wateringStartTime = millis();

    // Don't go to sleep while watering, regardless of light condition
    tasksCompleted = false;
  }
}

// Buzzer functions
void singleBeep() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);  // 200ms beep
  digitalWrite(BUZZER_PIN, LOW);
}

void doubleBeep() {
  // First beep
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);

  // Second beep
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
}

// Function to go to deep sleep
void goToDeepSleep() {
  // Configure wake up timer - now 10 minutes
  esp_sleep_enable_timer_wakeup(DARK_SEND_INTERVAL);

  // Disconnect WiFi to save power
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);

  // Go to deep sleep
  esp_deep_sleep_start();
}