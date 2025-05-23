#include <DHT22.h>
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi credentials
const char* WIFI_SSID = "SSID";
const char* WIFI_PASSWORD = "PASSW";

// MQTT broker info
const char* MQTT_SERVER_IP = "192.168.31.31";
const int MQTT_SERVER_PORT = 1883;
const char* MQTT_USERNAME = "okos-cserep";
const char* MQTT_PASSWORD = "okoscserep123";

// NTP server
const char* ntpServerURL = "pool.ntp.org";

// Pins
const byte MOISTURE_PIN = 0;
const byte LDR_PIN = 1;
const byte WATER_LEVEL_PIN = 2;
const byte WATER_PUMP_PIN = 3;
const byte DHT_PIN = 4;

// Thresholds
const int MOISTURE_THRESHOLD = 2000;
const int SUNLIGHT_THRESHOLD = 3000;
const int WATER_LEVEL_THRESHOLD = 1700;

// Timing
const unsigned long WATER_NOTIFICATION_INTERVAL = 86400000UL;  // 24 hours
const unsigned long WATERING_DURATION = 5000;                  // 5 seconds
const unsigned long LIGHT_SEND_INTERVAL = 60000;               // 1 minute
const unsigned long DARK_SEND_INTERVAL = 600000UL;             // 10 minutes

// Default values
float temperature = 0.0;
float humidity = 0.0;
int ldrValue = 0;
int moisture = 0;
int waterLevel = 0;

// Helper variables
unsigned long lastWaterNotificationTime = 0;
unsigned long wateringStartTime = 0;
unsigned long lastMQTTSendTime = -60000;
bool waterNotifSent = false;
bool isWatering = false;

// Instances
WiFiClient espClient;
PubSubClient client(espClient);
DHT22 dht(DHT_PIN);
struct tm localTime;

// Function to connect to WiFi
void setupWifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

// Function to connect to MQTT in home assistant
void reconnect() {
  while (!client.connected()) {
    client.connect("smart_flower_pot", MQTT_USERNAME, MQTT_PASSWORD);
    delay(5000);
  }
}

void setup() {
  // Connect to WiFi, MQTT
  setupWifi();
  client.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);

  // Get current time
  configTime(3600, 3600, ntpServerURL);
  getLocalTime(&localTime);

  // Set INPUT & OUTPUT pins
  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(WATER_LEVEL_PIN, INPUT);
  pinMode(WATER_PUMP_PIN, OUTPUT);

  // Turn off water pump by default
  digitalWrite(WATER_PUMP_PIN, LOW);
}

void loop() {
  // Reconnect if client has disconnected
  if (!client.connected()) reconnect();
  client.loop();

  unsigned long currentMillis = millis();
  checkWateringStatus();

  // Determine sending interval based on sunlight detection
  ldrValue = analogRead(LDR_PIN);
  bool isDark = ldrValue <= SUNLIGHT_THRESHOLD;
  unsigned long sendInterval = isDark ? DARK_SEND_INTERVAL : LIGHT_SEND_INTERVAL;

  if (currentMillis - lastMQTTSendTime >= sendInterval) {
    lastMQTTSendTime = currentMillis;

    // Read sensors
    temperature = dht.getTemperature();
    humidity = dht.getHumidity();
    moisture = analogRead(MOISTURE_PIN);
    waterLevel = analogRead(WATER_LEVEL_PIN);

    // Publish to MQTT
    char dataBuffer[10];
    dtostrf(temperature, 1, 2, dataBuffer);
    client.publish("okoscserep/temperature", dataBuffer);
    dtostrf(humidity, 1, 2, dataBuffer);
    client.publish("okoscserep/humidity", dataBuffer);
    sprintf(dataBuffer, "%d", waterLevel);
    client.publish("okoscserep/water_level", dataBuffer);
    sprintf(dataBuffer, "%d", moisture);
    client.publish("okoscserep/soil_moisture", dataBuffer);
    sprintf(dataBuffer, "%d", isDark);
    client.publish("okoscserep/sunlight", dataBuffer);

    // Handle automated tasks
    handleAutomation(currentMillis);

    // If dark and not watering => go to sleep
    if (isDark && !isWatering) {
      delay(200);                                                // Allow MQTT to flush
      esp_sleep_enable_timer_wakeup(DARK_SEND_INTERVAL * 1000);  // ms to µs
      esp_light_sleep_start();
    }
  }

  delay(100);
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
  // If water level is below the threshold
  if (waterLevel <= WATER_LEVEL_THRESHOLD) {
    // Check if time has passed since the last notification
    if (!waterNotifSent || currentMillis - lastWaterNotificationTime >= WATER_NOTIFICATION_INTERVAL) {
      // Send notification in home assistant once
      sendNotification();
      lastWaterNotificationTime = currentMillis;
      waterNotifSent = true;
    }
  }

  // If moisture is higher than or equal to the threshold and watering isn't happening
  if (moisture >= MOISTURE_THRESHOLD && !isWatering) {
    waterPlant();
  }

  // Reset notification flag if interval has passed and water level is still low
  if (currentMillis - lastWaterNotificationTime >= WATER_NOTIFICATION_INTERVAL) {
    waterNotifSent = false;
  }
}

// Plant watering function
void waterPlant() {
  // If time is obtained
  if (getLocalTime(&localTime)) {
    char timeBuffer[9];
    // Format time
    sprintf(timeBuffer, "%02d:%02d:%02d", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
    // Publish to MQTT
    client.publish("okoscserep/last_watering_time", timeBuffer);
  }

  // Actual watering
  isWatering = true;
  digitalWrite(WATER_PUMP_PIN, HIGH);
  wateringStartTime = millis();
}

// Notification sending
void sendNotification() {
  // Turn on for 1 second
  client.publish("smart_flower_pot/notify", "ON");
  delay(1000);
  client.publish("smart_flower_pot/notify", "OFF");
}