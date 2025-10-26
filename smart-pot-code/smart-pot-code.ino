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
void handleSensorOperations(unsigned long currentMillis);
void handleBuzzerAlerts(unsigned long currentMillis);
void handleAutomation(unsigned long currentMillis);
void handleAPMode(unsigned long currentMillis);
void handleWiFiStateMachine(unsigned long currentMillis);
void goToDeepSleep();
bool areAllTasksCompleted();

void setup() {
  Serial.begin(115200);

  // Pin modes
  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Check if this is a cold boot
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  isColdBoot = (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED);

  // Play startup sound only on cold boot
  if (isColdBoot) {
    Serial.println("Cold boot detected");
    for (int i = 0; i < 3; i++) {
      tone(BUZZER_PIN, MELODY[i], DURATIONS[i]);
      delay(DELAYS[i]);
    }
  } else {
    Serial.println("Waking from deep sleep");
  }

  temperatureSensor.begin();

  // Initialize or update RTC data
  if (!rtcData.isInitialized) {
    rtcData = { true, 0, 0, 0, 0 };  // Aggregate initialization
  } else {
    rtcData.bootCount++;
    if (rtcData.bootCount > 1) {
      rtcData.totalSleepTime += (DARK_SEND_INTERVAL / 1000);
    }
  }

  // Initialize state variables
  wakeupTime = millis();
  tasksCompleted = false;
  justWokeUp = true;

  // Load configuration
  bool hasCredentials = wifiHandler.loadWiFiCredentials();
  wifiHandler.loadMQTTConfig();

  // Determine initial WiFi state based on boot type and credentials
  if (isColdBoot) {
    // Cold boot - start AP mode first
    Serial.println("Cold boot: Starting Access Point for configuration...");
    wifiHandler.startAccessPoint();
    currentWiFiState = WIFI_SETUP_MODE;
  } else if (hasCredentials) {
    // On wake from sleep with credentials, connect directly
    Serial.println("Wake from sleep: Attempting direct WiFi connection...");
    WiFi.mode(WIFI_STA);
    currentWiFiState = WIFI_CONNECTING;
  } else {
    // Wake from sleep without credentials - start AP
    Serial.println("Wake from sleep: No credentials, starting AP...");
    wifiHandler.startAccessPoint();
    currentWiFiState = WIFI_SETUP_MODE;
  }
}

void loop() {
  unsigned long currentMillis = millis();

  // Handle AP mode if active
  if (wifiHandler.isApModeActive()) {
    handleAPMode(currentMillis);
  }

  // State-independent operations
  isWatering = false;
  handleBuzzerAlerts(currentMillis);

  // WiFi state machine
  handleWiFiStateMachine(currentMillis);

  // Check for deep sleep conditions
  if (!wifiHandler.isApModeActive() && areAllTasksCompleted()) {
    goToDeepSleep();
  }

  delay(100);
}

void handleAPMode(unsigned long currentMillis) {
  wifiHandler.dnsServer.processNextRequest();
  wifiHandler.server.handleClient();

  // Check for AP timeout (only on cold boot)
  if (isColdBoot && (currentMillis - wifiHandler.getApStartTime() >= AP_TIMEOUT)) {
    wifiHandler.stopAccessPoint();
    Serial.println("AP timeout reached on cold boot, stopping AP");

    // Attempt WiFi connection if credentials exist
    if (wifiHandler.loadWiFiCredentials()) {
      WiFi.mode(WIFI_STA);
      currentWiFiState = WIFI_CONNECTING;
      Serial.println("Credentials available, attempting WiFi connection...");
    } else {
      currentWiFiState = WIFI_FAILED;
      Serial.println("No credentials available, operations limited until configured");
    }

    // Clear cold boot flag after first AP timeout
    isColdBoot = false;
  }

  // Handle new credentials saved during AP mode
  if (wifiHandler.areCredentialsSaved()) {
    wifiHandler.setCredentialsSaved(false);
    Serial.println("New credentials saved, stopping AP and attempting connection...");

    // Small delay to allow the HTTP response to be sent
    delay(100);

    wifiHandler.stopAccessPoint();
    delay(1000);
    WiFi.mode(WIFI_STA);
    currentWiFiState = WIFI_CONNECTING;
    isColdBoot = false;  // Clear cold boot flag after successful config
  }
}

void handleWiFiStateMachine(unsigned long currentMillis) {
  switch (currentWiFiState) {
    case WIFI_SETUP_MODE:
      // Waiting in AP mode - nothing to do
      break;

    case WIFI_CONNECTING:
      if (wifiHandler.connectWiFi()) {
        currentWiFiState = WIFI_CONNECTED;
        Serial.println("WiFi connected successfully!");
      } else {
        currentWiFiState = WIFI_FAILED;
        lastWiFiAttempt = currentMillis;
        Serial.println("WiFi connection failed");
      }
      break;

    case WIFI_CONNECTED:
      // Check if WiFi connection is still alive
      if (WiFi.status() != WL_CONNECTED) {
        currentWiFiState = WIFI_FAILED;
        lastWiFiAttempt = currentMillis;
        Serial.println("WiFi connection lost");
        break;
      }

      // Ensure MQTT connection
      if (!wifiHandler.client.connected()) {
        Serial.println("Connecting to MQTT...");
        wifiHandler.reconnectMQTT();
      }

      // Process MQTT and sensor operations if connected
      if (wifiHandler.client.connected()) {
        wifiHandler.client.loop();
        handleSensorOperations(currentMillis);
        handleAutomation(currentMillis);
      }
      break;

    case WIFI_FAILED:
      // Retry WiFi connection after interval
      if (currentMillis - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
        currentWiFiState = WIFI_CONNECTING;
        Serial.println("Retrying WiFi connection...");
      }
      break;
  }
}

void handleSensorOperations(unsigned long currentMillis) {
  ldrValue = analogRead(LDR_PIN);
  isDark = ldrValue <= SUNLIGHT_THRESHOLD;

  bool shouldSendData = justWokeUp || (!isDark && (currentMillis - lastDataSendTime >= LIGHT_SEND_INTERVAL));

  if (shouldSendData) {
    lastDataSendTime = currentMillis;
    justWokeUp = false;

    // Read sensors
    temperatureSensor.requestTemperatures();
    temperature = temperatureSensor.getTempCByIndex(0);
    moisture = analogRead(MOISTURE_PIN);

    char dataBuffer[10];

    // Send temperature if valid
    if (temperature != DEVICE_DISCONNECTED_C && temperature > -55 && temperature < 125) {
      dtostrf(temperature, 1, 2, dataBuffer);
      wifiHandler.sendTemperature(dataBuffer);
    }

    // Send moisture
    itoa(moisture, dataBuffer, 10);
    wifiHandler.sendMoisture(dataBuffer);

    // Send sunlight presence
    dataBuffer[0] = isDark ? '0' : '1';
    dataBuffer[1] = '\0';
    wifiHandler.sendSunlightPresence(dataBuffer);

    delay(1000);
  }
}

bool areAllTasksCompleted() {
  unsigned long currentMillis = millis();

  // Minimum awake time requirement
  if (currentMillis - wakeupTime < 15000) {
    return false;
  }

  // Must be dark, data sent after wakeup, and MQTT connected
  if (!isDark || lastDataSendTime <= wakeupTime || !wifiHandler.client.connected()) {
    return false;
  }

  // Track MQTT connection time for watering opportunity
  static unsigned long mqttConnectedTime = 0;

  if (mqttConnectedTime == 0) {
    mqttConnectedTime = currentMillis;
  }

  // Give at least 10 seconds after MQTT connection for watering decision
  if (currentMillis - mqttConnectedTime < 10000) {
    return false;
  }

  return true;
}

void handleBuzzerAlerts(unsigned long currentMillis) {
  // Periodic moisture reading
  if (currentMillis - lastMoistureReading >= 5000) {
    moisture = analogRead(MOISTURE_PIN);
    lastMoistureReading = currentMillis;
  }

  // Calculate time since last beep including sleep time
  unsigned long timeSinceLastBeep = rtcData.totalSleepTime + currentMillis - rtcData.lastLowMoistureBeep;

  // Trigger low moisture beep if needed
  if (moisture < MOISTURE_THRESHOLD && timeSinceLastBeep >= LOW_MOISTURE_BEEP_INTERVAL) {
    tone(BUZZER_PIN, LOW_MOISTURE_HZ, 200);
    rtcData.lastLowMoistureBeep = rtcData.totalSleepTime + currentMillis;
    Serial.println("Low moisture beep triggered");
  }
}

void handleAutomation(unsigned long currentMillis) {
  // Only operate when MQTT is connected
  if (!wifiHandler.client.connected()) {
    return;
  }

  // Periodic moisture check and watering decision
  static unsigned long lastMoistureCheck = 0;

  if (currentMillis - lastMoistureCheck >= 2000) {
    lastMoistureCheck = currentMillis;
    moisture = analogRead(MOISTURE_PIN);
    lastMoistureReading = currentMillis;

    Serial.print("Automation - Moisture: ");
    Serial.print(moisture);
    Serial.print(" | Threshold: ");
    Serial.print(MOISTURE_THRESHOLD);
    Serial.print(" | Dry: ");
    Serial.println(moisture < MOISTURE_THRESHOLD ? "YES" : "NO");

    // Check if watering is needed
    if (moisture < MOISTURE_THRESHOLD) {
      unsigned long timeSinceLastWatering = rtcData.totalSleepTime + millis() - rtcData.lastWateringTime;

      if (timeSinceLastWatering >= WATERING_COOLDOWN) {
        // Trigger watering sequence
        wifiHandler.sendWaterCommand();

        String timestamp = wifiHandler.getCurrentTimestamp();
        wifiHandler.sendLastWateringTime(timestamp.c_str());

        Serial.print("Watering triggered at: ");
        Serial.println(timestamp);

        wateringStartTime = millis();
        rtcData.lastWateringTime = rtcData.totalSleepTime + millis();

        // Reset the low moisture beep timer when watering occurs
        rtcData.lastLowMoistureBeep = rtcData.totalSleepTime + millis();
        Serial.println("Low moisture beep timer reset after watering");

        delay(500);  // Ensure MQTT messages are sent
      } else {
        unsigned long cooldownRemaining = (WATERING_COOLDOWN - timeSinceLastWatering) / 1000;
        Serial.print("Soil is dry but watering is in cooldown. Next watering in: ");
        Serial.print(cooldownRemaining);
        Serial.println("s");
      }
    }
  }
}

void goToDeepSleep() {
  Serial.println("Going to deep sleep...");

  isWatering = false;
  esp_sleep_enable_timer_wakeup(DARK_SEND_INTERVAL);

  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);

  delay(100);
  esp_deep_sleep_start();
}