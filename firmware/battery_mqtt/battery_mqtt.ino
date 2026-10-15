#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <ArduinoJson.h>
#include <esp_timer.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_random.h>
#include <driver/gpio.h>
#include <math.h>
#include "secrets.h"
#include "config.h"
#include "power_policy.h"

#if !defined(ARDUINO_XIAO_ESP32C6)
#error Select the Seeed XIAO ESP32C6 board.
#endif

WiFiClient network;
PubSubClient mqtt(network);
Adafruit_BME280 bme;
bool bmeReady = false;
uint8_t bmeAddress = 0;
bool pendingBmePublish = false;
char bmePayload[128] = "{\"ok\":false}";
char clientId[40];
uint32_t lastWifiAttemptMs = 0;
uint32_t lastMqttAttemptMs = 0;
uint32_t lastPublishAttemptMs = 0;
uint32_t lastMarkerAttemptMs = 0;
bool haveSample = false;
bool pendingPublish = false;
bool wifiWasConnected = false;
bool pendingWifiPublish = false;
uint32_t wifiConnectionCount = 0;
uint32_t mqttConnectionCount = 0;
int64_t wifiConnectedAtUs = 0;
float batteryVoltage = 0.0f;
RTC_DATA_ATTR bool lowBatteryMode = false;
RTC_DATA_ATTR uint32_t wakeCount = 0;
SleepPlan sleepPlan = {false, NORMAL_SLEEP_SECONDS};
char sleepPayload[256];
bool sleepSubscribed = false;
bool sleepingStatusSent = false;
bool sleepConfirmed = false;

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  // A unique per-boot payload prevents a retained message from confirming
  // this cycle. The echo also proves the broker processed prior publishes.
  if (strcmp(topic, SLEEP_TOPIC) == 0 && length == strlen(sleepPayload)
      && memcmp(payload, sleepPayload, length) == 0) {
    sleepConfirmed = true;
  }
}

void prepareSleepPayload() {
  char cycleId[24];
  snprintf(cycleId, sizeof(cycleId), "%08lx%08lx",
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  JsonDocument doc;
  doc["sleep_s"] = sleepPlan.seconds;
  doc["low_battery"] = sleepPlan.lowBattery;
  doc["battery_v"] = serialized(String(batteryVoltage, 3));
  doc["wake_count"] = wakeCount;
  doc["wake_reason"] = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER ? "timer" : "reset";
  doc["cycle_id"] = cycleId;
  serializeJson(doc, sleepPayload, sizeof(sleepPayload));
}

void enterDeepSleep(bool delivered) {
  if (mqtt.connected()) {
    if (!sleepingStatusSent) {
      mqtt.publish(STATUS_TOPIC, "sleeping", true);
    }
    mqtt.disconnect();
  }
  network.stop();
  if (bmeReady) {
    bme.setSampling(Adafruit_BME280::MODE_SLEEP,
                    Adafruit_BME280::SAMPLING_NONE,
                    Adafruit_BME280::SAMPLING_NONE,
                    Adafruit_BME280::SAMPLING_NONE);
  }
  Wire.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  // Disable RF switch power, and retain that output level during deep sleep.
  digitalWrite(WIFI_ENABLE, HIGH);
  gpio_hold_en(static_cast<gpio_num_t>(WIFI_ENABLE));
  // ESP32-C6 retains each pin with gpio_hold_en; it has no global hold API.
  const uint64_t timerUs = sleepMicroseconds(sleepPlan.seconds);
  ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(timerUs));
  Serial.printf("Deep sleep: %lu s (%llu us), low_battery=%s, MQTT_confirmed=%s\n",
                static_cast<unsigned long>(sleepPlan.seconds),
                static_cast<unsigned long long>(timerUs),
                sleepPlan.lowBattery ? "true" : "false", delivered ? "true" : "false");
  if (Serial) {
    Serial.flush();
  }
  delay(20);
  esp_deep_sleep_start();
}

bool publishWifiTelemetry() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  const int64_t nowUs = esp_timer_get_time();
  JsonDocument doc;
  doc["rssi_dbm"] = WiFi.RSSI();
  doc["ssid"] = WiFi.SSID();
  doc["bssid"] = WiFi.BSSIDstr();
  doc["channel"] = WiFi.channel();
  doc["ip"] = WiFi.localIP().toString();
  doc["gateway"] = WiFi.gatewayIP().toString();
  doc["mac"] = WiFi.macAddress();
  doc["uptime_s"] = static_cast<uint64_t>(nowUs / 1000000);
  doc["wifi_connected_s"] = static_cast<uint64_t>((nowUs - wifiConnectedAtUs) / 1000000);
  doc["wifi_reconnects"] = wifiConnectionCount > 0 ? wifiConnectionCount - 1 : 0;
  doc["mqtt_reconnects"] = mqttConnectionCount > 0 ? mqttConnectionCount - 1 : 0;
  doc["wake_count"] = wakeCount;
  char payload[768];
  if (doc.overflowed() || measureJson(doc) >= sizeof(payload)) {
    Serial.println("Wi-Fi telemetry JSON could not fit in buffer");
    return false;
  }
  serializeJson(doc, payload, sizeof(payload));
  if (!mqtt.publish(WIFI_TOPIC, payload, true)) {
    return false;
  }
  Serial.printf("MQTT sent: %s = %s\n", WIFI_TOPIC, payload);
  return true;
}

bool isBme280(uint8_t address) {
  Wire.beginTransmission(address);
  Wire.write(0xD0); // Chip identification register
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(address, uint8_t(1)) != 1) {
    return false;
  }
  const uint8_t chipId = Wire.read();
  if (chipId != 0x60) {
    Serial.printf("I2C 0x%02X: chip ID 0x%02X; expected BME280 ID 0x60\n", address, chipId);
    return false;
  }
  return true;
}

bool initBme() {
  for (uint8_t address : {uint8_t(0x76), uint8_t(0x77)}) {
    if (isBme280(address) && bme.begin(address, &Wire)) {
      bmeAddress = address;
      // One conversion per reporting interval; the sensor sleeps in between.
      bme.setSampling(Adafruit_BME280::MODE_FORCED,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::FILTER_OFF);
      Serial.printf("BME280 found at 0x%02X; SDA=GPIO22 SCL=GPIO23\n", address);
      return true;
    }
  }
  Serial.println("BME280 not found at 0x76 or 0x77; retrying next wake");
  return false;
}

void sampleBme() {
  pendingBmePublish = true;
  snprintf(bmePayload, sizeof(bmePayload), "{\"ok\":false}");
  if (!bmeReady) {
    bmeReady = initBme();
  }
  if (!bmeReady) {
    return;
  }
  if (!isBme280(bmeAddress) || !bme.takeForcedMeasurement()) {
    bmeReady = false;
    Serial.println("BME280 measurement failed; retrying next wake");
    return;
  }
  const float temperature = bme.readTemperature();
  const float humidity = bme.readHumidity();
  const float pressureHpa = bme.readPressure() / 100.0f;
  if (!isfinite(temperature) || !isfinite(humidity) || !isfinite(pressureHpa)
      || humidity < 0.0f || humidity > 100.0f || pressureHpa <= 0.0f) {
    bmeReady = false;
    Serial.println("Invalid BME280 reading; retrying next wake");
    return;
  }
  snprintf(bmePayload, sizeof(bmePayload),
           "{\"ok\":true,\"t\":%.2f,\"h\":%.2f,\"p\":%.2f}",
           temperature, humidity, pressureHpa);
  Serial.printf("BME280: %.2f C | %.2f %%RH | %.2f hPa\n", temperature, humidity, pressureHpa);
}

void sampleBattery() {
  constexpr unsigned samples = 64;
  uint32_t totalMv = 0;
  // Discard initial conversions, then average calibrated ADC readings.
  for (unsigned i = 0; i < 8; ++i) {
    analogReadMilliVolts(BATTERY_ADC_PIN);
    delay(2);
  }
  for (unsigned i = 0; i < samples; ++i) {
    totalMv += analogReadMilliVolts(BATTERY_ADC_PIN);
    delay(2);
  }
  const float adcMv = totalMv / static_cast<float>(samples);
  const float dividerRatio = (DIVIDER_TOP_OHMS + DIVIDER_BOTTOM_OHMS) / DIVIDER_BOTTOM_OHMS;
  batteryVoltage = adcMv * dividerRatio * BATTERY_CALIBRATION / 1000.0f;
  haveSample = true;
  pendingPublish = true;
  Serial.printf("ADC GPIO6: %.1f mV | Battery: %.3f V\n", adcMv, batteryVoltage);
}

void setup() {
  Serial.begin(115200);
  delay(STARTUP_GRACE_MS); // Brief USB maintenance window, also when USB is absent.
  Serial.println("\nXIAO ESP32C6 battery + BME280 MQTT, deep sleep mode");
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
    lowBatteryMode = false;
    wakeCount = 0;
  }
  ++wakeCount;
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  pinMode(BATTERY_ADC_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  Wire.begin(BME_SDA_PIN, BME_SCL_PIN, 100000);
  Wire.setTimeOut(50);

  // Measure with Wi-Fi still off, before its current draw affects the cell.
  sampleBattery();
  sleepPlan = chooseSleepPlan(batteryVoltage, lowBatteryMode,
                              LOW_BATTERY_THRESHOLD_V, LOW_BATTERY_RECOVERY_V,
                              NORMAL_SLEEP_SECONDS, LOW_BATTERY_SLEEP_SECONDS);
  lowBatteryMode = sleepPlan.lowBattery;
  prepareSleepPayload();
  sampleBme();

  // Release the held RF control output, then enable the on-board antenna.
  gpio_hold_dis(static_cast<gpio_num_t>(WIFI_ENABLE));
  pinMode(WIFI_ENABLE, OUTPUT);
  digitalWrite(WIFI_ENABLE, LOW);
  pinMode(WIFI_ANT_CONFIG, OUTPUT);
  digitalWrite(WIFI_ANT_CONFIG, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("xiao-battery");
  WiFi.setAutoReconnect(true);
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(clientId, sizeof(clientId), "xiao-battery-%04x%08lx",
           static_cast<unsigned>((mac >> 32) & 0xffff),
           static_cast<unsigned long>(mac & 0xffffffff));
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(2);
  network.setConnectionTimeout(2000);

  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttemptMs = millis();
  lastMqttAttemptMs = millis() - RECONNECT_INTERVAL_MS;
}

void loop() {
  const uint32_t now = millis();
  if (sleepConfirmed) {
    enterDeepSleep(true);
  }
  if (esp_timer_get_time() >= static_cast<int64_t>(MAX_AWAKE_SECONDS) * 1000000LL) {
    Serial.println("Awake deadline reached; sleeping even without confirmed MQTT delivery");
    enterDeepSleep(false);
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasConnected) {
      Serial.println("Wi-Fi disconnected");
      wifiWasConnected = false;
      network.stop();
    }
    if (now - lastWifiAttemptMs >= RECONNECT_INTERVAL_MS) {
      lastWifiAttemptMs = now;
      Serial.printf("Retrying Wi-Fi (status %d)...\n", WiFi.status());
      WiFi.reconnect();
    }
    delay(10);
    return;
  }

  if (!wifiWasConnected) {
    wifiWasConnected = true;
    ++wifiConnectionCount;
    wifiConnectedAtUs = esp_timer_get_time();
    pendingWifiPublish = true;
    Serial.printf("Wi-Fi connected; IP: %s\n", WiFi.localIP().toString().c_str());
    lastMqttAttemptMs = now - RECONNECT_INTERVAL_MS;
  }

  if (!mqtt.connected() && now - lastMqttAttemptMs >= RECONNECT_INTERVAL_MS) {
    lastMqttAttemptMs = now;
    const bool connected = MQTT_USERNAME[0]
        ? mqtt.connect(clientId, MQTT_USERNAME, MQTT_PASSWORD, STATUS_TOPIC, 1, true, "offline")
        : mqtt.connect(clientId, STATUS_TOPIC, 1, true, "offline");
    if (connected) {
      ++mqttConnectionCount;
      pendingWifiPublish = true;
      Serial.println("MQTT connected");
      mqtt.publish(STATUS_TOPIC, "online", true);
      // Resend the same measurement batch if the connection drops mid-cycle.
      pendingPublish = true;
      pendingBmePublish = true;
      sleepingStatusSent = false;
      sleepSubscribed = mqtt.subscribe(SLEEP_TOPIC);
      lastPublishAttemptMs = now - 1000;
      lastMarkerAttemptMs = now - 1000;
      if (!sleepSubscribed) {
        mqtt.disconnect();
      }
    } else {
      Serial.printf("MQTT connection failed (state %d)\n", mqtt.state());
    }
  }

  if (mqtt.connected()) {
    mqtt.loop();
    if (sleepConfirmed) {
      enterDeepSleep(true);
    }
    if (!mqtt.connected() || !sleepSubscribed || now - lastPublishAttemptMs < 1000) {
      delay(10);
      return;
    }
    lastPublishAttemptMs = now;
    if (haveSample && pendingPublish) {
      char payload[24];
      snprintf(payload, sizeof(payload), "%.3f", batteryVoltage);
      if (mqtt.publish(VOLTAGE_TOPIC, payload, true)) {
        Serial.printf("MQTT sent: %s = %s V\n", VOLTAGE_TOPIC, payload);
        pendingPublish = false;
      }
    }
    if (pendingBmePublish && mqtt.publish(BME_TOPIC, bmePayload, true)) {
      Serial.printf("MQTT sent: %s = %s\n", BME_TOPIC, bmePayload);
      pendingBmePublish = false;
    }
    if (pendingWifiPublish && publishWifiTelemetry()) {
      pendingWifiPublish = false;
    }
    if (!pendingPublish && !pendingBmePublish && !pendingWifiPublish) {
      if (!sleepingStatusSent) {
        sleepingStatusSent = mqtt.publish(STATUS_TOPIC, "sleeping", true);
      }
      if (sleepingStatusSent && now - lastMarkerAttemptMs >= 1000) {
        lastMarkerAttemptMs = now;
        if (mqtt.publish(SLEEP_TOPIC, sleepPayload, true)) {
          Serial.printf("MQTT sleep plan sent; waiting for broker echo: %s\n", sleepPayload);
        }
      }
    }
  }
  delay(10);
}
