#include <Adafruit_BME280.h>

// BME280 address
const byte BME_ADDR = 0x76;

// Sensor pins
const byte LDR_PIN = A0;
const byte MOISTURE_PIN = A1;
const byte WATER_LEVEL_PIN = A2;

// RGB LED pins
const byte RED_LED = 2;
const byte GREEN_LED = 3;
const byte BLUE_LED = 4;

// Screen backlight pin
const byte BL_PIN = 5;

// Thresholds
const float TEMP_THRESHOLD = 35.00;
const int MOISTURE_THRESHOLD = 300;
const int SUNLIGHT_THRESHOLD = 200;
const int WATER_LEVEL_THRESHOLD = 200;

// Sensor values
float temperature = 0.0;
float humidity = 0.0;
int ldrValue = 0;
int moisture = 0;
int waterLevel = 0;

// Notification timing
unsigned long lastWaterNotificationTime = 0;
const unsigned long WATER_NOTIFICATION_INTERVAL = 5000;
bool waterNotifSent = false;
bool firstWaterNotificationSent = false;

// Watering control
bool isWatering = false;
unsigned long wateringStartTime = 0;
const unsigned long WATERING_DURATION = 3000;
unsigned long lastWateringEndTime = 0;
const unsigned long WATERING_COOLDOWN = 5000;
bool hasWateredOnce = false;

// BME280 instance
Adafruit_BME280 bme;

void turnLedOn(char color = 'a');

void setup() {
  Serial.begin(9600);
  Serial.println("Smart pot started!");

  if (!bme.begin(BME_ADDR)) {
    Serial.println("Couldn't find BME280!");
    while (1)
      ;
  }

  pinMode(LDR_PIN, INPUT);
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(WATER_LEVEL_PIN, INPUT);

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(BL_PIN, OUTPUT);

  turnLedOn();  // All LEDs off
}

void loop() {
  unsigned long currentMillis = millis();
  bool alertActive = false;

  // Read sensor values
  temperature = bme.readTemperature();
  humidity = bme.readHumidity();
  ldrValue = analogRead(LDR_PIN);
  moisture = 150;
  waterLevel = 500;

  Serial.println(temperature);
  Serial.println(humidity);
  Serial.println(ldrValue);
  Serial.println(waterLevel);

  // Stop watering after duration
  if (isWatering) {
    if (currentMillis - wateringStartTime >= WATERING_DURATION) {
      isWatering = false;
      lastWateringEndTime = currentMillis;
      Serial.println("Stopping watering...");
    } else {
      return;
    }
  }

  // Darkness check
  if (ldrValue < SUNLIGHT_THRESHOLD) {
    handleDarkness();
    alertActive = true;
  } else {
    digitalWrite(BL_PIN, HIGH);  // Backlight on
  }

  // Soil moisture check with cooldown
  if (!isWatering && (!hasWateredOnce || currentMillis - lastWateringEndTime >= WATERING_COOLDOWN)) {
    if (moisture <= MOISTURE_THRESHOLD) {
      waterPlant();
      alertActive = true;
      hasWateredOnce = true;
    }
  }

  // High temperature check
  if (temperature >= TEMP_THRESHOLD) {
    handleHighTemp();
    alertActive = true;
  }

  // Water level check
  if (waterLevel <= WATER_LEVEL_THRESHOLD) {
    if (!firstWaterNotificationSent) {
      handleLowWaterLevel();
      lastWaterNotificationTime = currentMillis;
      waterNotifSent = true;
      firstWaterNotificationSent = true;
    } else if (!waterNotifSent && currentMillis - lastWaterNotificationTime >= WATER_NOTIFICATION_INTERVAL) {
      handleLowWaterLevel();
      lastWaterNotificationTime = currentMillis;
      waterNotifSent = true;
    }
  }

  // Reset notification flag after interval
  if (currentMillis - lastWaterNotificationTime >= WATER_NOTIFICATION_INTERVAL) {
    waterNotifSent = false;
  }

  // If no alerts were triggered
  if (!alertActive) {
    handleOK();
  }
}

// Utility Functions
void turnLedOn(char color = 'a') {
  digitalWrite(RED_LED, HIGH);
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(BLUE_LED, HIGH);
  switch (color) {
    case 'r': digitalWrite(RED_LED, LOW); break;
    case 'g': digitalWrite(GREEN_LED, LOW); break;
    case 'b': digitalWrite(BLUE_LED, LOW); break;
    default: break;
  }
}

void handleHighTemp() {
  Serial.println("High temperature!");
  turnLedOn('r');
}

void handleDarkness() {
  Serial.println("Darkness detected!");
  digitalWrite(BL_PIN, LOW);  // Turn off screen
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
  // Activate pump here
  isWatering = true;
  wateringStartTime = millis();
}
