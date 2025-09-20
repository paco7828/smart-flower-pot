#include <esp_now.h>
#include <WiFi.h>

// Water pump and button
constexpr byte PUMP_PIN = 0;
constexpr byte BTN_PIN = 1;

// Secret remote code
constexpr char secretCode[] = "WaterOn123";

// Pump control flags
bool pumpActive = false;
unsigned long pumpStartTime = 0;

// Callback: triggered when data is received
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  // Convert data into string
  String received;
  for (int i = 0; i < len; i++) {
    received += (char)incomingData[i];
  }

  Serial.print("Received: ");
  Serial.println(received);

  // Check code
  if (received == secretCode) {
    Serial.println("Remote code OK → activating pump for 5s");
    pumpActive = true;
    pumpStartTime = millis();
    digitalWrite(PUMP_PIN, HIGH);
  } else {
    Serial.println("Incorrect code");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PUMP_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);
  digitalWrite(PUMP_PIN, LOW);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("Water station ready.");
}

void loop() {
  // Button manual override (active LOW)
  if (digitalRead(BTN_PIN) == LOW) {
    digitalWrite(PUMP_PIN, HIGH);
    pumpActive = false;  // cancel any timer
  } else {
    // Check if pump should still run from remote command
    if (pumpActive && millis() - pumpStartTime >= 5000) {
      digitalWrite(PUMP_PIN, LOW);
      pumpActive = false;
      Serial.println("Pump deactivated after 5s");
    } else if (!pumpActive) {
      digitalWrite(PUMP_PIN, LOW);
    }
  }
}
