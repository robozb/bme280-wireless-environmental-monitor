#pragma once
// Copy to secrets.h; keep that file private.
#define WIFI_SSID "YOUR_2_4_GHZ_WIFI"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define MQTT_TOPIC "bme280esp32/battery/voltage"
#define MQTT_STATUS_TOPIC "bme280esp32/battery/status"
#define MQTT_BME_TOPIC "bme280esp32/sensor"
#define MQTT_WIFI_TOPIC "bme280esp32/wifi"
#define MQTT_SLEEP_TOPIC "bme280esp32/sleep"
#define NORMAL_SLEEP_SECONDS 3600U
#define LOW_BATTERY_SLEEP_SECONDS 86400U
#define LOW_BATTERY_THRESHOLD_V 3.30f
#define LOW_BATTERY_RECOVERY_V 3.50f
#define MAX_AWAKE_SECONDS 45U

