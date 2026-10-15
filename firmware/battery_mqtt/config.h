#pragma once

// Rear A6 / MTCK pad on the XIAO ESP32C6. D6 is a different pin (GPIO16).
constexpr uint8_t BATTERY_ADC_PIN = 6;
constexpr float DIVIDER_TOP_OHMS = 220000.0f;     // R2: BAT+ to A6
constexpr float DIVIDER_BOTTOM_OHMS = 220000.0f;  // R1: A6 to GND
// After a multimeter comparison: measured battery voltage / reported voltage.
constexpr float BATTERY_CALIBRATION = 1.0f;
constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;
constexpr uint32_t STARTUP_GRACE_MS = 5000; // Time to attach USB/reflash after reset.

constexpr char MQTT_HOST[] = "192.168.0.107";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char VOLTAGE_TOPIC[] = MQTT_TOPIC;
constexpr char STATUS_TOPIC[] = MQTT_STATUS_TOPIC;
constexpr char BME_TOPIC[] = MQTT_BME_TOPIC;
constexpr char WIFI_TOPIC[] = MQTT_WIFI_TOPIC;
constexpr char SLEEP_TOPIC[] = MQTT_SLEEP_TOPIC;
constexpr uint8_t BME_SDA_PIN = D4; // GPIO22
constexpr uint8_t BME_SCL_PIN = D5; // GPIO23

