This is a custom PCB-based smart plant watering system using an onboard ESP32-C3 Supermini. It includes JST connectors for the following:

 - 5V solar panel: charges a 3.7V LiPo battery during the day

 - Soil moisture sensor: measures soil moisture and waters the plant if it is too dry

 - Water pump: pumps water from a container to the plant

 - Water level sensor: monitors the water container level

 - 3.7V LiPo battery: powers the system

The PCB includes:

 - Step-up converter (3.7V → 5V for ESP and sensors)

 - Battery charger circuit (via solar)

 - DHT22 sensor (temperature & humidity)

 - LDR (to detect sunlight and control sleep mode)

What it does:

 - Sends sensor data to Home Assistant via MQTT every 1 min (light) or 10 min (dark)

 - Waters the plant for 5 seconds when the soil is dry

 - Sends a notification if the water level is low (max once per 24h)

 - Goes into light sleep when it’s dark to save power
