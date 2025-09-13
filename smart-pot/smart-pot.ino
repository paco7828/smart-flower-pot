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
  Serial.begin(115200);

  // Initialize sensors and actuators
  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(WATER_PUMP_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(WATER_PUMP_PIN, LOW);

  playStartupSound();

  // Initialize DS18B20 temperature sensor
  temperatureSensor.begin();

  // Initialize RTC data on first boot
  if (!rtcData.isInitialized) {
    rtcData.isInitialized = true;
    rtcData.bootCount = 0;
    rtcData.totalSleepTime = 0;
    rtcData.lastLowMoistureBeep = 0;
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

void printSensorValues(unsigned long currentMillis) {
  static unsigned long lastPrintTime = 0;

  // Print sensor values every 5 seconds
  if (currentMillis - lastPrintTime >= 5000) {
    lastPrintTime = currentMillis;

    // Read current sensor values
    int currentLdr = analogRead(LDR_PIN);
    int currentMoisture = analogRead(MOISTURE_PIN);

    // Read temperature
    temperatureSensor.requestTemperatures();
    float currentTemp = temperatureSensor.getTempCByIndex(0);

    // Print formatted output
    Serial.println("=== SENSOR VALUES ===");
    Serial.println("Sunlight: " + String(currentLdr) + " (Threshold: " + String(SUNLIGHT_THRESHOLD) + ") - " + (currentLdr > SUNLIGHT_THRESHOLD ? "BRIGHT" : "DARK"));
    // FIXED: Moisture logic - lower values = drier soil
    Serial.println("Moisture: " + String(currentMoisture) + " (Threshold: " + String(MOISTURE_THRESHOLD) + ") - " + (currentMoisture < MOISTURE_THRESHOLD ? "DRY" : "WET"));
    if (currentTemp != DEVICE_DISCONNECTED_C && currentTemp > -55 && currentTemp < 125) {
      Serial.println("Temperature: " + String(currentTemp, 2) + "°C");
    } else {
      Serial.println("Temperature: SENSOR ERROR");
    }
    Serial.println("WiFi State: " + getWiFiStateString());
    Serial.println("Watering: " + String(isWatering ? "ACTIVE" : "IDLE"));
    Serial.println("====================");
  }
}

void loop() {
  unsigned long currentMillis = millis();
  printSensorValues(currentMillis);

  // Handle web server requests
  if (wifiHandler.isApModeActive()) {
    wifiHandler.dnsServer.processNextRequest();
  }

  // Always check watering status first (critical safety)
  checkWateringStatus();

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

  // Handle automation regardless of WiFi state
  handleAutomation(currentMillis);

  // Check if we should go to deep sleep (only when dark, not in AP mode, and tasks completed)
  if (!wifiHandler.isApModeActive() && tasksCompleted && (currentMillis - wakeupTime >= AWAKE_TIME_MS) && !isWatering) {
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

    // Read soil moisture
    moisture = analogRead(MOISTURE_PIN);

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

    // Allow some time for MQTT messages to be sent
    delay(1000);
  }

  // Mark tasks as completed only if it's dark (for sleep decision)
  if (isDark) {
    tasksCompleted = true;
  }
}

// Function to handle buzzer alerts
void handleBuzzerAlerts(unsigned long currentMillis) {
  // Read moisture every 5 seconds to ensure we have current reading
  if (currentMillis - lastMoistureReading >= 5000) {
    moisture = analogRead(MOISTURE_PIN);
    lastMoistureReading = currentMillis;
  }

  // Calculate time since last beep (considering deep sleep cycles)
  unsigned long timeSinceLastLowMoistureBeep = rtcData.totalSleepTime + currentMillis - rtcData.lastLowMoistureBeep;

  // Low moisture beep every 5 minutes (300000 ms)
  // FIXED: If moisture is BELOW threshold (dry), beep
  if (moisture < MOISTURE_THRESHOLD && timeSinceLastLowMoistureBeep >= LOW_MOISTURE_BEEP_INTERVAL) {
    singleBeep();
    rtcData.lastLowMoistureBeep = rtcData.totalSleepTime + currentMillis;
    Serial.println("Low moisture beep! Moisture: " + String(moisture) + " (Threshold: " + String(MOISTURE_THRESHOLD) + ")");
  }
}

// Function to check if watering is happening
void checkWateringStatus() {
  if (isWatering && millis() - wateringStartTime >= WATERING_DURATION) {
    isWatering = false;
    digitalWrite(WATER_PUMP_PIN, LOW);
    Serial.println("Watering stopped automatically");
  }
}

void playStartupSound() {
  Serial.println("Playing startup sound...");

  // Method 1: Try tone() function (for passive buzzers)
  tone(BUZZER_PIN, 1000, 200);  // 1000Hz for 200ms
  delay(250);
  tone(BUZZER_PIN, 1500, 200);  // 1500Hz for 200ms
  delay(250);
  tone(BUZZER_PIN, 2000, 300);  // 2000Hz for 300ms
  delay(350);

  Serial.println("Startup sound completed");
}

// Automation function
void handleAutomation(unsigned long currentMillis) {
  // Read moisture every 2 seconds when not watering
  if (!isWatering && (currentMillis - lastMoistureReading >= 2000)) {
    moisture = analogRead(MOISTURE_PIN);
    lastMoistureReading = currentMillis;

    // FIXED: Plant watering logic - if moisture is below threshold (dry soil), start watering
    if (moisture < MOISTURE_THRESHOLD && !isWatering) {
      // Check cooldown before attempting to water
      unsigned long timeSinceLastWatering = rtcData.totalSleepTime + millis() - rtcData.lastWateringTime;

      if (timeSinceLastWatering >= WATERING_COOLDOWN) {
        waterPlant();
      } else {
        Serial.println("Soil is dry but watering is in cooldown. Next watering in: " + String((WATERING_COOLDOWN - timeSinceLastWatering) / 1000) + "s");
      }
    }
  }
}

// Plant watering function
void waterPlant() {
  // Check if enough time has passed since last watering
  unsigned long timeSinceLastWatering = rtcData.totalSleepTime + millis() - rtcData.lastWateringTime;

  if (timeSinceLastWatering < WATERING_COOLDOWN) {
    Serial.println("Watering cooldown active. Time since last: " + String(timeSinceLastWatering / 1000) + "s");
    return;  // Don't water if still in cooldown period
  }

  isWatering = true;
  digitalWrite(WATER_PUMP_PIN, HIGH);
  wateringStartTime = millis();

  // Record watering time for cooldown tracking in RTC memory
  rtcData.lastWateringTime = rtcData.totalSleepTime + millis();

  Serial.println("Starting watering - Moisture: " + String(moisture) + " (Threshold: " + String(MOISTURE_THRESHOLD) + ")");

  // Don't go to sleep while watering, regardless of light condition
  tasksCompleted = false;
}

// Buzzer functions
void singleBeep() {
  tone(BUZZER_PIN, 1000, 200);
}

// Function to go to deep sleep
void goToDeepSleep() {
  // Make sure water pump is off before sleeping
  if (isWatering) {
    digitalWrite(WATER_PUMP_PIN, LOW);
    isWatering = false;
  }

  // Configure wake up timer - now 10 minutes
  esp_sleep_enable_timer_wakeup(DARK_SEND_INTERVAL);

  // Disconnect WiFi to save power
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);

  delay(100);

  // Go to deep sleep
  esp_deep_sleep_start();
}

// Helper function to get WiFi state as string
String getWiFiStateString() {
  switch (currentWiFiState) {
    case WIFI_SETUP_MODE: return "SETUP_MODE";
    case WIFI_CONNECTING: return "CONNECTING";
    case WIFI_CONNECTED: return "CONNECTED";
    case WIFI_FAILED: return "FAILED";
    default: return "UNKNOWN";
  }
}