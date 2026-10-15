const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const flow = JSON.parse(fs.readFileSync(path.join(__dirname, "../integrations/node-red/aksisbme-influx-flow.json"), "utf8"));
const parser = flow.find(n => n.type === "function");
const writer = flow.find(n => n.type === "influxdb out");
const inputs = flow.filter(n => n.type === "mqtt in");
const types = Object.fromEntries(inputs.map(n => [n.name.split(": ")[1], n.topic]));
const ids = new Set(flow.map(n => n.id));
assert.equal(ids.size, flow.length);
for (const n of flow) for (const group of n.wires || []) for (const id of group) assert(ids.has(id));
assert.equal(inputs.length, 5);
assert.equal(writer.bucket, "AksisBME");
assert.equal(writer.org, "WFS");
assert.equal(writer.precisionV18FluxV20, "ms");
assert.equal(writer.influxdb, "b0b0b0b0b0b0b0b0");
assert(inputs.every(n => n.broker === "b2b2b2b2b2b2b2b2"));
assert(!flow.some(n => n.type.includes("telegram") || n.credentials));

const execute = new Function("msg", "node", "context", "Buffer", "Date", parser.func);
const contextData = new Map();
const context = {get: key => contextData.get(key), set: (key, value) => contextData.set(key, value)};
const errors = [];
const node = {status() {}, error: (error, msg) => errors.push({error, msg})};
class FrozenDate extends Date { static now() { return 1760000000000; } }
function send(type, payload, extra = {}) {
    return execute({topic: types[type] || type, payload, ...extra}, node, context, Buffer, FrozenDate);
}
function fields(msg, type) {
    assert.equal(msg.measurement, "aksis_bme");
    assert.deepEqual(msg.payload[1], {device: "bme280esp32", type});
    assert.equal(typeof msg.payload[0].time, "number");
    assert.equal(typeof msg.payload[0].raw_payload, "string");
    return msg.payload[0];
}

assert.equal(send("battery", "4.018", {retain: true}), null);
assert.equal(send("unknown", "1"), null);
assert.equal(send("__proto__", "1"), null);
assert.equal(fields(send("battery", Buffer.from("4.018")), "battery").vbat, 4.018);
assert.equal(fields(send("battery", 4.018), "battery").vbat, 4.018);

const sensor = fields(send("sensor", '{"ok":true,"t":23.3,"h":44.91,"p":1015.67,"future":123}'), "sensor");
assert.equal(sensor.temperature_c, 23.3);
assert.equal(sensor.humidity_pct, 44.91);
assert.equal(sensor.pressure_hpa, 1015.67);
assert.equal(JSON.parse(sensor.raw_payload).future, 123);
const failedSensor = fields(send("sensor", {ok: false}), "sensor");
assert.equal(failedSensor.sensor_ok, false);
assert(!("temperature_c" in failedSensor));
assert(!("humidity_pct" in failedSensor));

const wifiInput = {rssi_dbm: -53, ssid: 'Lab "A"', bssid: "E4:FA:C4:82:CB:70", channel: 11,
    ip: "192.168.0.103", gateway: "192.168.0.1", mac: "98:A3:16:8E:C3:58", uptime_s: 7,
    wifi_connected_s: 0, wifi_reconnects: 0, mqtt_reconnects: 0, wake_count: 1};
const wifi = fields(send("wifi", JSON.stringify(wifiInput)), "wifi");
for (const [key, value] of Object.entries(wifiInput)) assert.equal(wifi[key], value);

const online = fields(send("status", "online"), "status");
const sleeping = fields(send("status", "sleeping"), "status");
assert.equal(online.state_code, 2);
assert.equal(sleeping.state_code, 1);
assert.equal(sleeping.time, online.time + 1, "Same-ms state changes must not overwrite each other");
assert.equal(fields(send("status", "offline"), "status").state_code, 0);

const sleepInput = {sleep_s: 86400, low_battery: true, battery_v: 3.29, wake_count: 22, wake_reason: "timer", cycle_id: "1234abcd"};
const sleep = fields(send("sleep", sleepInput), "sleep");
for (const [key, value] of Object.entries(sleepInput)) assert.equal(sleep[key], value);
assert(!Object.hasOwn(send("sleep", sleepInput).payload[1], "cycle_id"));

for (const value of [null, "", "NaN", "Infinity", false, {}, []]) {
    assert.equal(send("battery", value), null, "Malformed voltage must not turn into zero");
}
assert.equal(send("sensor", {ok: true, t: null, h: 40, p: 1010}), null);
assert.equal(send("sensor", "not JSON"), null);
assert.equal(send("sleep", {sleep_s: 300, low_battery: "false"}), null);
assert(errors.length >= 10);
console.log("PASS: import wiring, AksisBME bucket, all 5 telemetry types, raw values, retained filtering, invalid values, booleans and distinct status timestamps.");
