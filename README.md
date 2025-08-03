# Smart Flower Pot

An ESP32-based smart plant monitoring and watering system with Home Assistant integration via MQTT.

## Features

- **Automatic plant watering** based on soil moisture levels
- **Environmental monitoring** (temperature, humidity, light, water level)
- **Home Assistant integration** with real-time sensor data
- **Captive portal setup** for easy WiFi and MQTT configuration
- **Low water level alerts** and watering notifications
- **Energy efficient** with light-based sleep modes

## Hardware Requirements

- ESP32 development board
- DHT22 temperature/humidity sensor
- Soil moisture sensor
- LDR (Light Dependent Resistor)
- Water level sensor
- Water pump (5V)
- Connecting wires and breadboard/PCB

## Pin Configuration

| Component | ESP32 Pin |
|-----------|-----------|
| Soil Moisture Sensor | GPIO 0 |
| LDR (Light Sensor) | GPIO 1 |
| Water Level Sensor | GPIO 2 |
| Water Pump | GPIO 3 |
| DHT22 Sensor | GPIO 4 |

## Installation

### 1. Flash the ESP32

1. Install the Arduino IDE with ESP32 board support
2. Install required libraries:
   - `WiFi` (built-in)
   - `PubSubClient`
   - `DHT sensor library`
   - `Preferences` (built-in)
   - `ESPAsyncWebServer`
   - `AsyncTCP`
3. Upload the `smart-pot.ino` sketch to your ESP32

### 2. Initial Setup

1. Power on the ESP32
2. Connect to the WiFi hotspot named **"Smart-flower-pot"**
3. A captive portal will open automatically (or navigate to `http://4.3.2.1`)
4. Enter your WiFi credentials and MQTT broker settings:
   - **WiFi SSID**: Your home WiFi network name
   - **WiFi Password**: Your home WiFi password
   - **MQTT Server**: IP address of your MQTT broker
   - **MQTT Port**: Usually `1883`
   - **MQTT Username**: Your MQTT broker username
   - **MQTT Password**: Your MQTT broker password
5. Click "Save Configuration"
6. The device will restart and connect to your network

## Home Assistant Integration

### Step 1: Set up MQTT Broker

**Option A: Mosquitto Add-on (Recommended)**
1. Go to **Settings** → **Add-ons** → **Add-on Store**
2. Install **"Mosquitto broker"**
3. Start the add-on and enable **"Start on boot"**
4. Create MQTT user credentials in the add-on configuration

**Option B: External MQTT Broker**
- Ensure your existing MQTT broker is accessible from Home Assistant
- Note the IP address, port, username, and password

### Step 2: Configure MQTT Integration

1. Go to **Settings** → **Devices & Services** → **Add Integration**
2. Search for **"MQTT"** and add it
3. Configure with your broker details:
   - **Broker**: Your MQTT broker IP (or `localhost` for Mosquitto add-on)
   - **Port**: `1883`
   - **Username**: Your MQTT username
   - **Password**: Your MQTT password

### Step 3: Add MQTT Entities

Add the following to your `configuration.yaml` file:

```yaml
mqtt:
  sensor:
    # Temperature sensor
    - name: "Smart Pot Temperature"
      state_topic: "okoscserep/temperature"
      unit_of_measurement: "°C"
      device_class: temperature
      icon: mdi:thermometer
    
    # Humidity sensor
    - name: "Smart Pot Humidity"
      state_topic: "okoscserep/humidity"
      unit_of_measurement: "%"
      device_class: humidity
      icon: mdi:water-percent
    
    # Soil moisture sensor
    - name: "Smart Pot Soil Moisture"
      state_topic: "okoscserep/soil_moisture"
      unit_of_measurement: ""
      icon: mdi:water-outline
    
    # Water level sensor
    - name: "Smart Pot Water Level"
      state_topic: "okoscserep/water_level"
      unit_of_measurement: ""
      icon: mdi:cup-water
    
    # Last watering time
    - name: "Smart Pot Last Watering"
      state_topic: "okoscserep/last_watering_time"
      icon: mdi:watering-can
    
  binary_sensor:
    # Sunlight detection
    - name: "Smart Pot Sunlight"
      state_topic: "okoscserep/sunlight"
      payload_on: "0"  # 0 means it's dark (no sunlight)
      payload_off: "1" # 1 means there's sunlight
      device_class: light
      icon: mdi:weather-sunny
  
  switch:
    # Water refill notification trigger
    - name: "Smart Pot Water Alert"
      state_topic: "smart_flower_pot/notify"
      command_topic: "smart_flower_pot/notify"
      payload_on: "ON"
      payload_off: "OFF"
      icon: mdi:bell-alert
```

### Step 4: Restart Home Assistant

Restart Home Assistant to load the new MQTT entities.

### Step 5: Create Dashboard Card

Add this card to your Lovelace dashboard:

```yaml
type: vertical-stack
cards:
  - type: custom:mushroom-title-card
    title: Smart Flower Pot
    subtitle: Monitoring & Care System
  
  - type: horizontal-stack
    cards:
      - type: custom:mushroom-entity-card
        entity: sensor.smart_pot_temperature
        name: Temperature
        icon: mdi:thermometer
      - type: custom:mushroom-entity-card
        entity: sensor.smart_pot_humidity
        name: Humidity
        icon: mdi:water-percent
  
  - type: horizontal-stack
    cards:
      - type: custom:mushroom-entity-card
        entity: sensor.smart_pot_soil_moisture
        name: Soil Moisture
        icon: mdi:water-outline
      - type: custom:mushroom-entity-card
        entity: sensor.smart_pot_water_level
        name: Water Level
        icon: mdi:cup-water
  
  - type: horizontal-stack
    cards:
      - type: custom:mushroom-entity-card
        entity: binary_sensor.smart_pot_sunlight
        name: Sunlight
        icon: mdi:weather-sunny
      - type: custom:mushroom-entity-card
        entity: sensor.smart_pot_last_watering
        name: Last Watered
        icon: mdi:watering-can
  
  - type: custom:mushroom-entity-card
    entity: switch.smart_pot_water_alert
    name: Water Refill Alert
    icon: mdi:bell-alert
```

> **Note**: The above card uses Mushroom cards. Install them via HACS or use a simple `entities` card instead.

### Step 6: Set up Automations (Optional)

Create automations for notifications and alerts:

```yaml
# Low water level notification
- id: smart_pot_low_water_alert
  alias: "Smart Pot - Low Water Alert"
  trigger:
    - platform: state
      entity_id: switch.smart_pot_water_alert
      to: "on"
  action:
    - service: notify.mobile_app_your_phone  # Replace with your device
      data:
        title: "🌱 Smart Flower Pot"
        message: "Water level is low! Time to refill the reservoir."

# Plant watered notification
- id: smart_pot_watered_notification
  alias: "Smart Pot - Plant Watered"
  trigger:
    - platform: state
      entity_id: sensor.smart_pot_last_watering
  condition:
    - condition: template
      value_template: "{{ trigger.to_state.state != 'unknown' }}"
  action:
    - service: notify.mobile_app_your_phone  # Replace with your device
      data:
        title: "🌱 Plant Watered"
        message: "Your plant was automatically watered at {{ states('sensor.smart_pot_last_watering') }}"
```

## MQTT Topics

The device publishes to the following MQTT topics:

| Topic | Description | Unit |
|-------|-------------|------|
| `okoscserep/temperature` | Temperature reading | °C |
| `okoscserep/humidity` | Humidity percentage | % |
| `okoscserep/soil_moisture` | Soil moisture level | Raw ADC value |
| `okoscserep/water_level` | Water reservoir level | Raw ADC value |
| `okoscserep/sunlight` | Light detection | 0 (dark) / 1 (light) |
| `okoscserep/last_watering_time` | Last watering timestamp | HH:MM:SS |
| `smart_flower_pot/notify` | Water refill alert | ON/OFF |

## Troubleshooting

### Device Not Connecting to WiFi
1. Ensure WiFi credentials are correct
2. Check if your router supports 2.4GHz (ESP32 doesn't support 5GHz)
3. Verify the device is within WiFi range
4. Reset the device and reconfigure through the captive portal

### MQTT Connection Issues
1. Verify MQTT broker is running and accessible
2. Check firewall settings on the MQTT broker
3. Ensure MQTT credentials are correct
4. Test MQTT connection with tools like MQTT Explorer

### Sensors Not Appearing in Home Assistant
1. Check if MQTT integration is properly configured
2. Verify the MQTT configuration in `configuration.yaml`
3. Restart Home Assistant after adding MQTT entities
4. Check Home Assistant logs for MQTT-related errors

### Captive Portal Not Opening
1. Make sure you're connected to the "Smart-flower-pot" WiFi network
2. Try navigating manually to `http://4.3.2.1`
3. Clear your browser cache and try again
4. Try a different device or browser

## Configuration

### Sensor Thresholds
You can modify these values in the Arduino code:

```cpp
const int MOISTURE_THRESHOLD = 2000;        // Soil moisture threshold for watering
const int SUNLIGHT_THRESHOLD = 3000;       // Light threshold for day/night detection
const int WATER_LEVEL_THRESHOLD = 1700;    // Low water level threshold
```

### Timing Settings
```cpp
const unsigned long WATERING_DURATION = 5000;           // Watering duration (5 seconds)
const unsigned long LIGHT_SEND_INTERVAL = 60000;        // Data send interval during day (1 minute)
const unsigned long DARK_SEND_INTERVAL = 600000UL;      // Data send interval during night (10 minutes)
```
