#include <WiFi.h>
#include <PubSubClient.h>

// WiFi credentials
const char* ssid = "SSID_HERE"; // change
const char* password = "PASSW_HERE"; // change

// MQTT broker info
const char* mqtt_server = "192.168.31.31";
const char* mqtt_user = "okos-cserep";
const char* mqtt_password = "okoscserep123";

float temperature = 0;

WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {
  delay(10);
  Serial.print("Connecting to WiFi...");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("Connected successfully to WiFi!");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT server...");
    if (client.connect("ESP8266Client", mqtt_user, mqtt_password)) {
      Serial.println("Successfully connected to MQTT server!");
    } else {
      Serial.print("Error occured: ");
      Serial.print(client.state());
      Serial.println("Retrying in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  temperature++;
  float humidity = 45.2;
  int water_level = 1200;
  int soil_moisture = 320;

  char buffer[10];

  dtostrf(temperature, 1, 2, buffer);
  client.publish("okoscserep/temperature", buffer);

  dtostrf(humidity, 1, 2, buffer);
  client.publish("okoscserep/humidity", buffer);

  sprintf(buffer, "%d", water_level);
  client.publish("okoscserep/water_level", buffer);

  sprintf(buffer, "%d", soil_moisture);
  client.publish("okoscserep/soil_moisture", buffer);

  Serial.println("MQTT data sent!");

  delay(5000);
}