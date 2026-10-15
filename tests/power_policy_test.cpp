#include "../firmware/battery_mqtt/power_policy.h"

constexpr SleepPlan plan(float voltage, bool wasLow = false) {
  return chooseSleepPlan(voltage, wasLow, 3.3f, 3.5f, 3600, 86400);
}
static_assert(!plan(3.9f).lowBattery && plan(3.9f).seconds == 3600);
static_assert(plan(3.3f).lowBattery && plan(3.3f).seconds == 86400);
static_assert(plan(3.0f).seconds == 86400);
static_assert(plan(0.0f).seconds == 86400);
static_assert(plan(3.4f, true).seconds == 86400); // Voltage rebound must not exit low mode.
static_assert(plan(3.4f, false).seconds == 3600);
static_assert(!plan(3.5f, true).lowBattery && plan(3.5f, true).seconds == 3600);
static_assert(sleepMicroseconds(86400) == 86400000000ULL); // Must not overflow 32 bits.
static_assert(sleepMicroseconds(3600) == 3600000000ULL);
