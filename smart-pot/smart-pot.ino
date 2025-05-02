#include <Adafruit_BME280.h>

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
const unsigned long WATER_NOTIFICATION_INTERVAL = 5000; // change
bool waterNotifSent = false;
bool firstWaterNotificationSent = false;

// Watering control
bool isWatering = false;
unsigned long wateringStartTime = 0;
const unsigned long WATERING_DURATION = 3000; // change
unsigned long lastWateringEndTime = 0;
const unsigned long WATERING_COOLDOWN = 5000; // change
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
  pinMode(WATER_PUMP_PIN, OUTPUT);

  digitalWrite(WATER_PUMP_PIN, LOW);
  turnLedOn();  // All LEDs off
}

void loop() {
  unsigned long currentMillis = millis();
  char activeAlert = 'n';  // none

  // Read sensor values
  temperature = bme.readTemperature();
  humidity = bme.readHumidity();
  ldrValue = analogRead(LDR_PIN);
  moisture = analogRead(MOISTURE_PIN);
  waterLevel = analogRead(WATER_LEVEL_PIN);

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

// Utility Functions
void turnLedOn(char color = 'a') {
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