"""Generate the importable flow using only topic names from .env, never credentials."""
import json
from pathlib import Path
from firmware import ROOT, TOPIC_DEFAULTS, read_env

target = ROOT / "integrations/node-red"
values = read_env(ROOT / ".env") if (ROOT / ".env").is_file() else TOPIC_DEFAULTS
topics = {
    values["MQTT_TOPIC"]: "battery",
    values["MQTT_BME_TOPIC"]: "sensor",
    values["MQTT_WIFI_TOPIC"]: "wifi",
    values["MQTT_STATUS_TOPIC"]: "status",
    values["MQTT_SLEEP_TOPIC"]: "sleep",
}
tab_id = "a6b1280e10000001"
function_id = "a6b1280e10000002"
writer_id = "a6b1280e10000003"
debug_id = "a6b1280e10000004"
error_id = "a6b1280e10000005"
error_debug_id = "a6b1280e10000006"
info = "AksisBME bucket, WFS org. Uses existing Mosquitto and Influx v2 config nodes from the supplied Matrix flow. Retained snapshots are skipped; wait for the next 5-minute wake. No Telegram nodes."
nodes = [{"id": tab_id, "type": "tab", "label": "AksisBME → InfluxDB", "disabled": False, "info": info, "env": []}]
for index, (topic, kind) in enumerate(topics.items()):
    nodes.append({
        "id": f"a6b1280e100001{index:02x}", "type": "mqtt in", "z": tab_id,
        "name": "XIAO: " + kind, "topic": topic, "qos": "0", "datatype": "auto-detect",
        "broker": "b2b2b2b2b2b2b2b2", "nl": False, "rap": True, "rh": 0,
        "inputs": 0, "x": 180, "y": 140 + index * 65, "wires": [[function_id]],
    })
body = "const TOPICS = " + json.dumps(topics, ensure_ascii=False, indent=4) + ";\n\n"
body += (target / "aksisbme-parser.js").read_text(encoding="utf-8")
nodes.extend([
    {"id": function_id, "type": "function", "z": tab_id, "name": "Akku + BME + Wi-Fi + sleep → Influx", "func": body,
     "outputs": 1, "timeout": 0, "noerr": 0, "initialize": "", "finalize": "", "libs": [],
     "x": 540, "y": 270, "wires": [[writer_id, debug_id]]},
    {"id": writer_id, "type": "influxdb out", "z": tab_id, "influxdb": "b0b0b0b0b0b0b0b0",
     "name": "InfluxDB: AksisBME", "measurement": "", "precision": "", "retentionPolicy": "", "database": "",
     "precisionV18FluxV20": "ms", "retentionPolicyV18Flux": "", "org": "WFS", "bucket": "AksisBME",
     "x": 920, "y": 240, "wires": []},
    {"id": debug_id, "type": "debug", "z": tab_id, "name": "AksisBME: előkészített Influx pont (nem írásigazolás)",
     "active": False, "tosidebar": True, "console": False, "tostatus": False, "complete": "true", "targetType": "full",
     "x": 990, "y": 320, "wires": []},
    {"id": error_id, "type": "catch", "z": tab_id, "name": "AksisBME parser / Influx hibák",
     "scope": [function_id, writer_id], "uncaught": False, "x": 250, "y": 530, "wires": [[error_debug_id]]},
    {"id": error_debug_id, "type": "debug", "z": tab_id, "name": "AksisBME HIBA", "active": True,
     "tosidebar": True, "console": False, "tostatus": False, "complete": "true", "targetType": "full",
     "x": 620, "y": 530, "wires": []},
    {"id": "a6b1280e10000007", "type": "comment", "z": tab_id, "name": "Meglévő Mosquitto + Influx v2 kapcsolat; bucket: AksisBME, org: WFS",
     "info": "Config references intentionally reuse the existing supplied Matrix flow. Select your existing config nodes if a connection is missing after import. The Influx token needs write access to AksisBME. No token is embedded here.", "x": 420, "y": 65, "wires": []},
    {"id": "a6b1280e10000008", "type": "comment", "z": tab_id, "name": "Retained kihagyva → az első új adat a következő ébredéskor érkezik",
     "info": "Timestamp is the Node-RED receive time, not the original retained message publish time. Retained snapshots are skipped to prevent old values appearing as fresh measurements on reconnect or deploy.", "x": 490, "y": 450, "wires": []},
])
output = target / "aksisbme-influx-flow.json"
output.write_text(json.dumps(nodes, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(f"Generated {output.relative_to(ROOT)} ({len(nodes)} nodes); existing credentials referenced, not exported.")
