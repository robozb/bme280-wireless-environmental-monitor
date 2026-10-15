import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from firmware import read_env


class PowerConfigTests(unittest.TestCase):
    def parse(self, extra=""):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / ".env"
            path.write_text('WIFI_SSID="test"\n' + extra, encoding="utf-8")
            return read_env(path)

    def test_default_schedule(self):
        values = self.parse()
        self.assertEqual(int(values["NORMAL_SLEEP_SECONDS"]), 3600)
        self.assertEqual(int(values["LOW_BATTERY_SLEEP_SECONDS"]), 86400)
        self.assertLess(float(values["LOW_BATTERY_THRESHOLD_V"]), float(values["LOW_BATTERY_RECOVERY_V"]))

    def test_invalid_power_values(self):
        for setting in ("NORMAL_SLEEP_SECONDS=0", "NORMAL_SLEEP_SECONDS=-1",
                        "LOW_BATTERY_SLEEP_SECONDS=1", "NORMAL_SLEEP_SECONDS=3.5",
                        "LOW_BATTERY_THRESHOLD_V=nan", "LOW_BATTERY_THRESHOLD_V=2.5",
                        "LOW_BATTERY_RECOVERY_V=3.2", "MAX_AWAKE_SECONDS=0"):
            with self.subTest(setting=setting), self.assertRaises(ValueError):
                self.parse(setting + "\n")

    def test_sleep_topic_must_be_unique(self):
        with self.assertRaises(ValueError):
            self.parse('MQTT_SLEEP_TOPIC="bme280esp32/wifi"\n')


if __name__ == "__main__":
    unittest.main()
