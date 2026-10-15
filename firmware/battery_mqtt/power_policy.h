#pragma once
#include <stdint.h>

struct SleepPlan {
  bool lowBattery;
  uint32_t seconds;
};

constexpr SleepPlan chooseSleepPlan(float voltage, bool wasLow,
                                    float lowThreshold, float recoveryThreshold,
                                    uint32_t normalSeconds, uint32_t lowSeconds) {
  // Invalid/zero ADC input also selects the more conservative daily mode.
  const bool low = !(voltage > 0.0f)
      || (wasLow ? voltage < recoveryThreshold : voltage <= lowThreshold);
  return {low, low ? lowSeconds : normalSeconds};
}

constexpr uint64_t sleepMicroseconds(uint32_t seconds) {
  return static_cast<uint64_t>(seconds) * 1000000ULL;
}
