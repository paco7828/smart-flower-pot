#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi credentials
const char* WIFI_SSID = "SSID_HERE";       // change
const char* WIFI_PASSWORD = "PASSW_HERE";  // change

// MQTT broker info
const char* MQTT_SERVER_IP = "192.168.31.31";
const int MQTT_SERVER_PORT = 1883;
const char* MQTT_USERNAME = "okos-cserep";
const char* MQTT_PASSWORD = "okoscserep123";

// BME280 address
const byte BME_ADDR = 0x76;

// Sensor pins
const byte MOISTURE_PIN = 0;
const byte LDR_PIN = 1;
const byte WATER_LEVEL_PIN = 2;

// RGB LED pins
const byte RED_LED = 5;
const byte GREEN_LED = 6;
const byte BLUE_LED = 7;

// Water pump control pin
const byte WATER_PUMP_PIN = 3;

// Thresholds - change
const float TEMP_THRESHOLD = 35.00;
const int MOISTURE_THRESHOLD = 300;
const int SUNLIGHT_THRESHOLD = 200;
const int WATER_LEVEL_THRESHOLD = 200;

// Default sensor values
float temperature = 0.0;
float humidity = 0.0;
int ldrValue = 0;
int moisture = 0;
int waterLevel = 0;

// Notification timing
unsigned long lastWaterNotificationTime = 0;
const unsigned long WATER_NOTIFICATION_INTERVAL = 5000;  // change
bool waterNotifSent = false;
bool firstWaterNotificationSent = false;

// Watering control
bool isWatering = false;
unsigned long wateringStartTime = 0;
const unsigned long WATERING_DURATION = 3000;  // change
unsigned long lastWateringEndTime = 0;
const unsigned long WATERING_COOLDOWN = 5000;  // change
bool hasWateredOnce = false;

// Instances
Adafruit_BME280 bme;
WiFiClient espClient;
PubSubClient client(espClient);

// Function predefinitions
void turnLedOn(char color = 'a');
void setupWifi();
void reconnect();
void handleHighTemp();
void handleDarkness();
void handleOK();
void handleLowWaterLevel();
void waterPlant();

void setup() {
  // Begin serial communication
  Serial.begin(115200);
  Serial.println("Smart pot started!");

  // Connect to WiFi
  setupWifi();

  // Connect to MQTT server
  client.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);

  // BME280 error handling
  if (!bme.begin(BME_ADDR)) {
    Serial.println("Couldn't find BME280!");
    while (1)
      ;
  }

  // Input pins
  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(WATER_LEVEL_PIN, INPUT);

  // Output pins
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(WATER_PUMP_PIN, OUTPUT);

  // Default pin writings
  digitalWrite(WATER_PUMP_PIN, LOW);
  turnLedOn();  // All LEDs off
}

void loop() {
  // Check if client is connected
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Default values
  unsigned long currentMillis = millis();
  char activeAlert = 'n';  // none

  // Read sensor values
  temperature = bme.readTemperature();
  humidity = bme.readHumidity();
  ldrValue = analogRead(LDR_PIN);
  moisture = analogRead(MOISTURE_PIN);
  waterLevel = analogRead(WATER_LEVEL_PIN);

  // Buffer to store published data
  char dataBuffer[10];

  // Show real-time values in serial
  Serial.println("------------------------------");
  Serial.print("Temperature: ");
  Serial.println(temperature);
  Serial.print("Humidity: ");
  Serial.println(humidity);
  Serial.print("LDR: ");
  Serial.println(ldrValue);
  Serial.print("Moisture: ");
  Serial.println(moisture);
  Serial.print("Water Level: ");
  Serial.println(waterLevel);
  Serial.println("------------------------------");

  // Send real-time values to home assistant
  // Temperature
  dtostrf(temperature, 1, 2, dataBuffer);
  client.publish("okoscserep/temperature", dataBuffer);

  // Humidity
  dtostrf(humidity, 1, 2, dataBuffer);
  client.publish("okoscserep/humidity", dataBuffer);

  // Water level
  sprintf(dataBuffer, "%d", waterLevel);
  client.publish("okoscserep/water_level", dataBuffer);

  // Soil moisture
  sprintf(dataBuffer, "%d", moisture);
  client.publish("okoscserep/soil_moisture", dataBuffer);

  Serial.println("MQTT data sent!");

  // Stop watering after duration
  if (isWatering) {
    if (currentMillis - wateringStartTime >= WATERING_DURATION) {
      isWatering = false;
      digitalWrite(WATER_PUMP_PIN, LOW);
      lastWateringEndTime = currentMillis;
      Serial.println("Stopping watering...");
    } else {
      return;
    }
  }

  // Priority 1: Water level check
  if (waterLevel <= WATER_LEVEL_THRESHOLD) {
    if (!firstWaterNotificationSent) {
      handleLowWaterLevel();
      lastWaterNotificationTime = currentMillis;
      waterNotifSent = true;
      firstWaterNotificationSent = true;
      activeAlert = 'w';
    } else if (!waterNotifSent && currentMillis - lastWaterNotificationTime >= WATER_NOTIFICATION_INTERVAL) {
      handleLowWaterLevel();
      lastWaterNotificationTime = currentMillis;
      waterNotifSent = true;
      activeAlert = 'w';
    }
  }

  // Reset water level notification flag
  if (currentMillis - lastWaterNotificationTime >= WATER_NOTIFICATION_INTERVAL) {
    waterNotifSent = false;
  }

  // Priority 2: High temperature
  if (activeAlert == 'n' && temperature >= TEMP_THRESHOLD) {
    handleHighTemp();
    activeAlert = 'h';
  }

  // Priority 3: Moisture-based watering (only if enough water)
  if (activeAlert == 'n' && !isWatering && (!hasWateredOnce || currentMillis - lastWateringEndTime >= WATERING_COOLDOWN)) {
    if (moisture <= MOISTURE_THRESHOLD && waterLevel > WATER_LEVEL_THRESHOLD) {
      waterPlant();
      activeAlert = 'm';
      hasWateredOnce = true;
    }
  }

  // Priority 4: Darkness check
  if (activeAlert == 'n' && ldrValue < SUNLIGHT_THRESHOLD) {
    handleDarkness();
    activeAlert = 'd';
  }

  // If nothing else triggered
  if (activeAlert == 'n') {
    handleOK();
  }
}

// Home assistant functions
void setupWifi() {
  Serial.println("Connecting to WiFi - ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connection established!");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT server...");
    if (client.connect("ESP8266Client", MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.println("MQTT server connection established!");
    } else {
      Serial.print("Error occured: ");
      Serial.print(client.state());
      Serial.println("Retrying in 5 seconds");
      delay(5000);
    }
  }
}

// Utility Functions
void turnLedOn(char color) {
  digitalWrite(RED_LED, HIGH);
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(BLUE_LED, HIGH);
  switch (color) {
    case 'r':
      digitalWrite(RED_LED, LOW);
      break;
    case 'g':
      digitalWrite(GREEN_LED, LOW);
      break;
    case 'b':
      digitalWrite(BLUE_LED, LOW);
      break;
    default:
      break;
  }
}

void handleHighTemp() {
  Serial.println("High temperature!");
  turnLedOn('r');
}

void handleDarkness() {
  Serial.println("Darkness detected!");
  turnLedOn('b');
}

void handleOK() {
  Serial.println("Everything OK!");
  turnLedOn('g');
}

void handleLowWaterLevel() {
  Serial.println("Water level low!");
  Serial.println("Sending notification!");
}

void waterPlant() {
  Serial.println("Watering plant...");
  isWatering = true;
  digitalWrite(WATER_PUMP_PIN, HIGH);  // Activate pump
  wateringStartTime = millis();
}