#include "config.h"
#include <DHT.h>
#include "wifi-handler.h"
#include <esp_sleep.h>

// Instances
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);
WifiHandler wifiHandler;

void setup()
{
  // Initialize sensors
  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(WATER_PUMP_PIN, OUTPUT);
  digitalWrite(WATER_PUMP_PIN, LOW);
  dht.begin();

  // Initialize RTC data on first boot
  if (!rtcData.isInitialized)
  {
    rtcData.isInitialized = true;
    rtcData.bootCount = 0;
    rtcData.totalSleepTime = 0;
  }
  rtcData.bootCount++;

  // Add sleep time to total (except for first boot)
  if (rtcData.bootCount > 1)
  {
    rtcData.totalSleepTime += (DARK_SEND_INTERVAL / 1000); // Convert to milliseconds
  }

  // Record wake up time
  wakeupTime = millis();
  tasksCompleted = false;
  justWokeUp = true;

  // Load existing credentials and configuration
  bool hasCredentials = wifiHandler.loadWiFiCredentials();
  wifiHandler.loadMQTTConfig();

  if (!hasCredentials)
  {
    // No credentials saved - start in setup mode (stay awake)
    wifiHandler.startAccessPoint();
    wifiHandler.setInitialSetup(true);
    currentWiFiState = WIFI_SETUP_MODE;
  }
  else
  {
    // Credentials exist - try to connect directly
    wifiHandler.setInitialSetup(false);
    currentWiFiState = WIFI_CONNECTING;

    if (wifiHandler.attemptWiFiConnection())
    {
      currentWiFiState = WIFI_CONNECTED;
    }
    else
    {
      // Connection failed - start AP for reconfiguration (stay awake)
      currentWiFiState = WIFI_FAILED;
      wifiHandler.startAccessPoint();
    }
  }
}

void loop()
{
  unsigned long currentMillis = millis();

  // Handle web server requests
  if (wifiHandler.isApModeActive())
  {
    wifiHandler.dnsServer.processNextRequest();
  }

  // Handle different WiFi states
  switch (currentWiFiState)
  {
  case WIFI_SETUP_MODE:
    // Initial setup mode with AP - stay awake until configured
    if (wifiHandler.areCredentialsSaved())
    {
      wifiHandler.setCredentialsSaved(false);
      delay(1500); // Give time for success page to be served

      if (wifiHandler.attemptWiFiConnection())
      {
        currentWiFiState = WIFI_CONNECTED;
        wifiHandler.setInitialSetup(false);
        wifiHandler.stopAccessPoint();
      }
      else
      {
        currentWiFiState = WIFI_FAILED;
      }
    }

    // Check AP timeout during initial setup
    if (currentMillis - wifiHandler.getApStartTime() >= AP_TIMEOUT)
    {
      // If we have saved credentials but couldn't connect, try without AP
      if (wifiHandler.loadWiFiCredentials())
      {
        wifiHandler.stopAccessPoint();
        wifiHandler.setInitialSetup(false);
        currentWiFiState = WIFI_CONNECTING;
      }
    }
    break;

  case WIFI_CONNECTING:
    // Attempt to connect to WiFi
    if (wifiHandler.attemptWiFiConnection())
    {
      currentWiFiState = WIFI_CONNECTED;
    }
    else
    {
      currentWiFiState = WIFI_FAILED;
      lastWiFiAttempt = currentMillis;
    }
    break;

  case WIFI_CONNECTED:
    // Check if WiFi connection is still alive
    if (WiFi.status() != WL_CONNECTED)
    {
      currentWiFiState = WIFI_FAILED;
      lastWiFiAttempt = currentMillis;
      break;
    }

    // Handle credential updates
    if (wifiHandler.areCredentialsSaved())
    {
      wifiHandler.setCredentialsSaved(false);
      delay(1500);
      WiFi.disconnect();
      delay(1000);
      currentWiFiState = WIFI_CONNECTING;
      break;
    }

    // Reconnect MQTT if needed
    if (!wifiHandler.client.connected())
    {
      wifiHandler.reconnect();
    }
    wifiHandler.client.loop();

    // Handle sensor operations and check for sleep
    handleSensorOperations(currentMillis);
    break;

  case WIFI_FAILED:
    // Handle credential updates
    if (wifiHandler.areCredentialsSaved())
    {
      wifiHandler.setCredentialsSaved(false);
      delay(1500);
      currentWiFiState = WIFI_CONNECTING;
    }
    // Retry WiFi connection periodically
    else if (currentMillis - lastWiFiAttempt >= WIFI_RETRY_INTERVAL)
    {
      currentWiFiState = WIFI_CONNECTING;
    }
    // If we can't connect and have been awake too long, go to sleep (only if dark)
    else if (currentMillis - wakeupTime >= AWAKE_TIME_MS && !wifiHandler.isApModeActive())
    {
      // Read light sensor to determine if we should sleep
      ldrValue = analogRead(LDR_PIN);
      isDark = ldrValue <= SUNLIGHT_THRESHOLD;
      if (isDark)
      {
        goToDeepSleep();
      }
    }
    break;
  }

  // Check if we should go to deep sleep (only when dark, not in AP mode, and tasks completed)
  if (!wifiHandler.isApModeActive() && tasksCompleted && (currentMillis - wakeupTime >= AWAKE_TIME_MS))
  {
    // Read light sensor to determine if we should sleep
    ldrValue = analogRead(LDR_PIN);
    isDark = ldrValue <= SUNLIGHT_THRESHOLD;

    if (isDark)
    {
      goToDeepSleep();
    }
    else
    {
      // It's sunny - reset task completion to continue operation
      tasksCompleted = false;
      justWokeUp = false; // Reset to allow periodic data sending
    }
  }

  delay(100);
}

// Function to handle sensor operations (only when connected to WiFi)
void handleSensorOperations(unsigned long currentMillis)
{
  checkWateringStatus();

  // Read light sensor to determine current light condition
  ldrValue = analogRead(LDR_PIN);
  isDark = ldrValue <= SUNLIGHT_THRESHOLD;

  // Send data when we wake up OR every minute when sunny OR every 10 minutes when dark
  bool shouldSendData = false;

  if (justWokeUp)
  {
    shouldSendData = true;
    justWokeUp = false;
  }
  else if (!isDark && (currentMillis - lastDataSendTime >= LIGHT_SEND_INTERVAL))
  {
    shouldSendData = true;
  }

  if (shouldSendData)
  {
    lastDataSendTime = currentMillis;

    // Read all sensors
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    moisture = analogRead(MOISTURE_PIN);

    // Buffer for MQTT data
    char dataBuffer[10];

    // Temperature
    if (!isnan(temperature))
    {
      dtostrf(temperature, 1, 2, dataBuffer);
      wifiHandler.sendTemperature(dataBuffer);
    }

    // Humidity
    if (!isnan(humidity))
    {
      dtostrf(humidity, 1, 2, dataBuffer);
      wifiHandler.sendHumidity(dataBuffer);
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

  // Handle automated tasks
  handleAutomation(currentMillis);

  // Mark tasks as completed only if it's dark (for sleep decision)
  if (isDark)
  {
    tasksCompleted = true;
  }
}

// Function to check if watering is happening
void checkWateringStatus()
{
  if (isWatering && millis() - wateringStartTime >= WATERING_DURATION)
  {
    isWatering = false;
    digitalWrite(WATER_PUMP_PIN, LOW);
  }
}

// Automation function
void handleAutomation(unsigned long currentMillis)
{
  // Plant watering logic - simple moisture-based watering
  if (moisture >= MOISTURE_THRESHOLD && !isWatering)
  {
    waterPlant();
  }
}

// Plant watering function
void waterPlant()
{
  // Start watering
  isWatering = true;
  digitalWrite(WATER_PUMP_PIN, HIGH);
  wateringStartTime = millis();

  // Don't go to sleep while watering, regardless of light condition
  tasksCompleted = false;
}

// Function to go to deep sleep
void goToDeepSleep()
{
  // Configure wake up timer - now 10 minutes
  esp_sleep_enable_timer_wakeup(DARK_SEND_INTERVAL);

  // Disconnect WiFi to save power
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);

  // Go to deep sleep
  esp_deep_sleep_start();
}