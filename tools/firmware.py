"""Build/upload the XIAO firmware using the project-root .env file."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
SKETCH = ROOT / "firmware" / "battery_mqtt"
FQBN = "esp32:esp32:XIAO_ESP32C6:CDCOnBoot=cdc"
KEYS = ("WIFI_SSID", "WIFI_PASSWORD", "MQTT_USERNAME", "MQTT_PASSWORD")
TOPIC_DEFAULTS = {
    "MQTT_TOPIC": "bme280esp32/battery/voltage",
    "MQTT_STATUS_TOPIC": "bme280esp32/battery/status",
    "MQTT_BME_TOPIC": "bme280esp32/sensor",
    "MQTT_WIFI_TOPIC": "bme280esp32/wifi",
    "MQTT_SLEEP_TOPIC": "bme280esp32/sleep",
}
POWER_DEFAULTS = {
    "NORMAL_SLEEP_SECONDS": "3600",
    "LOW_BATTERY_SLEEP_SECONDS": "86400",
    "LOW_BATTERY_THRESHOLD_V": "3.30",
    "LOW_BATTERY_RECOVERY_V": "3.50",
    "MAX_AWAKE_SECONDS": "45",
}


def validate_power_settings(values):
    for key, default in POWER_DEFAULTS.items():
        values.setdefault(key, default)
    for key in ("NORMAL_SLEEP_SECONDS", "LOW_BATTERY_SLEEP_SECONDS", "MAX_AWAKE_SECONDS"):
        if not re.fullmatch(r"[0-9]+", values[key]):
            raise ValueError(f"{key} must be an integer number of seconds")
        number = int(values[key])
        minimum, maximum = (10, 300) if key == "MAX_AWAKE_SECONDS" else (1, 604800)
        if not minimum <= number <= maximum:
            raise ValueError(f"{key} must be between {minimum} and {maximum} seconds")
    if int(values["LOW_BATTERY_SLEEP_SECONDS"]) < int(values["NORMAL_SLEEP_SECONDS"]):
        raise ValueError("LOW_BATTERY_SLEEP_SECONDS must be at least NORMAL_SLEEP_SECONDS")
    for key in ("LOW_BATTERY_THRESHOLD_V", "LOW_BATTERY_RECOVERY_V"):
        try:
            voltage = float(values[key])
        except ValueError:
            raise ValueError(f"{key} must be a voltage with a decimal point") from None
        if not math.isfinite(voltage) or not 3.0 <= voltage <= 4.2:
            raise ValueError(f"{key} must be between 3.0 and 4.2 volts")
    if float(values["LOW_BATTERY_THRESHOLD_V"]) >= float(values["LOW_BATTERY_RECOVERY_V"]):
        raise ValueError("LOW_BATTERY_RECOVERY_V must exceed LOW_BATTERY_THRESHOLD_V")


def read_env(path, require_wifi=True):
    values = {}
    for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        match = re.fullmatch(r"(?:export\s+)?([A-Za-z_][A-Za-z_0-9]*)\s*=\s*(.*)", line)
        if not match:
            raise ValueError(f"Invalid .env syntax on line {number}")
        key, value = match.groups()
        if value.startswith(('"', "'")):
            quote = value[0]
            end = value.find(quote, 1)
            if end < 0 or (value[end + 1:].strip() and not value[end + 1:].strip().startswith("#")):
                raise ValueError(f"Invalid quoted value on .env line {number}")
            value = value[1:end]
        else:
            value = re.split(r"\s+#", value, maxsplit=1)[0].rstrip()
        if key in values:
            raise ValueError(f"Duplicate .env key on line {number}")
        values[key] = value
    if require_wifi and not values.get("WIFI_SSID"):
        raise ValueError("Fill in WIFI_SSID in the project-root .env file first")
    validate_power_settings(values)
    for key, default in TOPIC_DEFAULTS.items():
        values.setdefault(key, default)
        topic = values[key]
        if not topic or "+" in topic or "#" in topic or len(topic.encode("utf-8")) > 180:
            raise ValueError(f"{key} must be a nonempty publish topic without wildcards, at most 180 UTF-8 bytes")
    if len({values[key] for key in TOPIC_DEFAULTS}) != len(TOPIC_DEFAULTS):
        raise ValueError("All MQTT publish topics must be different")
    for key in (*KEYS, *TOPIC_DEFAULTS):
        value = values.get(key, "")
        if any(ord(char) < 32 for char in value):
            raise ValueError(f"Control characters are not supported in {key}")
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--env-file", type=Path, default=ROOT / ".env")
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--prepare-only", action="store_true", help="Only generate secrets.h for Arduino IDE")
    action.add_argument("--upload", action="store_true", help="Build and upload; requires --port")
    parser.add_argument("--port", help="Verified XIAO ESP32C6 serial port, e.g. COM7")
    args = parser.parse_args()
    if args.upload and not args.port:
        parser.error("--upload requires --port (the verified XIAO ESP32C6 port)")
    values = read_env(args.env_file)
    if len(values["WIFI_SSID"].encode("utf-8")) > 32:
        raise ValueError("WIFI_SSID must be at most 32 UTF-8 bytes")

    cli = shutil.which("arduino-cli")
    bundled = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
    if not args.prepare_only and not cli:
        if not bundled.is_file():
            raise FileNotFoundError("arduino-cli was not found in PATH or the Arduino IDE installation")
        cli = str(bundled)

    header = "#pragma once\n// Generated by tools/firmware.py from .env; do not commit.\n"
    for key in (*KEYS, *TOPIC_DEFAULTS):
        header += f"#define {key} {json.dumps(values.get(key, ''), ensure_ascii=False)}\n"
    for key in POWER_DEFAULTS:
        literal = f"{float(values[key])}f" if key.endswith("_V") else f"{int(values[key])}U"
        header += f"#define {key} {literal}\n"
    (SKETCH / "secrets.h").write_text(header, encoding="utf-8")
    print("Generated secrets.h from .env (values hidden)", flush=True)
    if args.prepare_only:
        return

    tools_dir = ROOT / ".tools"
    tools_dir.mkdir(exist_ok=True)
    config = tools_dir / "arduino-cli.yaml"
    config.write_text(json.dumps({
        "directories": {
            "data": str(Path(os.environ.get("LOCALAPPDATA", str(Path.home() / "AppData/Local"))) / "Arduino15"),
            "downloads": str(tools_dir / "downloads"),
            "user": str(Path.home() / "Documents/Arduino"),
        },
        "build_cache": {"path": str(ROOT / ".build/cache")},
    }, indent=2), encoding="utf-8")
    build = ROOT / ".build/battery_mqtt"
    common = ["--config-file", str(config), "--fqbn", FQBN]
    subprocess.run([cli, "compile", *common, "--build-path", str(build), str(SKETCH)], check=True, cwd=ROOT)
    if args.upload:
        subprocess.run([cli, "upload", *common, "--port", args.port, "--input-dir", str(build), str(SKETCH)], check=True, cwd=ROOT)
        print("Upload complete. If you entered download mode with BOOT + RESET, press RESET alone to start the firmware.", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
