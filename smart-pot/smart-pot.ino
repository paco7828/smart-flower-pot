#include "config.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "wifi-handler.h"
#include <esp_sleep.h>
#include <esp_now.h>

// DS18B20 Temperature sensor setup
OneWire oneWire(DS_TEMP_PIN);
DallasTemperature temperatureSensor(&oneWire);

// Instances
WifiHandler wifiHandler;

// ESP-NOW peer info
esp_now_peer_info_t peerInfo;

// ESP-NOW send callback (updated signature for newer ESP32 core)
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.print("ESP-NOW Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

// Function to initialize ESP-NOW
bool initESPNow() {
  if (espNowInitialized) {
    return true;
  }

  // ESP-NOW works with WiFi already in STA mode
  // Don't change WiFi mode here if already connected
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
  }
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return false;
  }

  // Register send callback
  esp_now_register_send_cb(OnDataSent);

  // Register water station peer
  memcpy(peerInfo.peer_addr, waterStationMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer");
    return false;
  }

  espNowInitialized = true;
  Serial.println("ESP-NOW initialized successfully");
  return true;
}

// Function to send watering command via ESP-NOW
void sendWateringCommand() {
  if (!espNowInitialized) {
    if (!initESPNow()) {
      Serial.println("Cannot send watering command - ESP-NOW not initialized");
      return;
    }
  }

  Serial.println("Sending watering command to water station...");
  
  esp_err_t result = esp_now_send(waterStationMAC, (uint8_t *)SECRET_CODE, strlen(SECRET_CODE));
  
  if (result == ESP_OK) {
    Serial.println("Watering command sent successfully");
    isWatering = true;
    wateringStartTime = millis();
    
    // Record watering time for cooldown tracking
    rtcData.lastWateringTime = rtcData.totalSleepTime + millis();
  } else {
    Serial.println("Error sending watering command");
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize sensors and actuators
  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  playStartupSound();

  Serial.println("Smart Flower Pot - Remote Watering Edition");

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
      // Initialize ESP-NOW after WiFi is connected
      initESPNow();
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
    Serial.println("Moisture: " + String(currentMoisture) + " (Threshold: " + String(MOISTURE_THRESHOLD) + ") - " + (currentMoisture < MOISTURE_THRESHOLD ? "DRY" : "WET"));
    if (currentTemp != DEVICE_DISCONNECTED_C && currentTemp > -55 && currentTemp < 125) {
      Serial.println("Temperature: " + String(currentTemp, 2) + "°C");
    } else {
      Serial.println("Temperature: SENSOR ERROR");
    }
    Serial.println("WiFi State: " + getWiFiStateString());
    Serial.println("ESP-NOW: " + String(espNowInitialized ? "READY" : "NOT INITIALIZED"));
    Serial.println("Watering Status: " + String(isWatering ? "ACTIVE" : "IDLE"));
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

  // Check watering status (for tracking only, no physical pump control)
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
          initESPNow();  // Initialize ESP-NOW after WiFi connection
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
        initESPNow();  // Initialize ESP-NOW after WiFi connection
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

  // Handle automation regardless of WiFi state (requires ESP-NOW)
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
  if (moisture < MOISTURE_THRESHOLD && timeSinceLastLowMoistureBeep >= LOW_MOISTURE_BEEP_INTERVAL) {
    singleBeep();
    rtcData.lastLowMoistureBeep = rtcData.totalSleepTime + currentMillis;
    Serial.println("Low moisture beep! Moisture: " + String(moisture) + " (Threshold: " + String(MOISTURE_THRESHOLD) + ")");
  }
}

// Function to check if watering is happening (for tracking only)
void checkWateringStatus() {
  if (isWatering && millis() - wateringStartTime >= WATERING_DURATION) {
    isWatering = false;
    Serial.println("Watering cycle completed (tracking)");
  }
}

void playStartupSound() {
  Serial.println("Playing startup sound...");

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

    // If moisture is below threshold (dry soil), trigger watering via ESP-NOW
    if (moisture < MOISTURE_THRESHOLD && !isWatering) {
      // Check cooldown before attempting to water
      unsigned long timeSinceLastWatering = rtcData.totalSleepTime + millis() - rtcData.lastWateringTime;

      if (timeSinceLastWatering >= WATERING_COOLDOWN) {
        sendWateringCommand();
      } else {
        Serial.println("Soil is dry but watering is in cooldown. Next watering in: " + String((WATERING_COOLDOWN - timeSinceLastWatering) / 1000) + "s");
      }
    }
  }
}

// Buzzer functions
void singleBeep() {
  tone(BUZZER_PIN, 1000, 200);
}

// Function to go to deep sleep
void goToDeepSleep() {
  // Make sure watering flag is cleared
  if (isWatering) {
    isWatering = false;
  }

  // Configure wake up timer - now 10 minutes
  esp_sleep_enable_timer_wakeup(DARK_SEND_INTERVAL);

  // Deinitialize ESP-NOW before sleep
  if (espNowInitialized) {
    esp_now_deinit();
    espNowInitialized = false;
  }

  // Disconnect WiFi to save power
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);

  delay(100);

  Serial.println("Going to deep sleep...");

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