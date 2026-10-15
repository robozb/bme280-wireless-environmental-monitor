// Function-node body. TOPICS is prepended by tools/generate_nodered_flow.py.
// Do not give retained snapshots a new measurement timestamp.
if (msg.retain === true) {
    node.status({fill: "grey", shape: "ring", text: "Retained kihagyva; új mérésre vár"});
    return null;
}
if (!Object.prototype.hasOwnProperty.call(TOPICS, msg.topic)) return null;
const type = TOPICS[msg.topic];

try {
    const original = Buffer.isBuffer(msg.payload) ? msg.payload.toString("utf8") : msg.payload;
    const raw = typeof original === "string" ? original : JSON.stringify(original);
    if (typeof raw !== "string" || !raw.length) throw new Error("Üres payload");
    const fields = {mqtt_topic: String(msg.topic), raw_payload: raw};

    function number(value, name) {
        if (typeof value === "string" && value.trim() !== "") value = Number(value);
        if (typeof value !== "number" || !Number.isFinite(value)) {
            throw new Error("Nem véges szám: " + name);
        }
        return value;
    }
    function object() {
        const value = typeof original === "string" ? JSON.parse(original) : original;
        if (!value || typeof value !== "object" || Array.isArray(value)) {
            throw new Error("JSON objektum szükséges");
        }
        return value;
    }
    function copyNumber(data, key, destination = key, required = false) {
        if (Object.prototype.hasOwnProperty.call(data, key)) {
            fields[destination] = number(data[key], key);
        } else if (required) {
            throw new Error("Hiányzó mező: " + key);
        }
    }
    function copyString(data, key) {
        if (Object.prototype.hasOwnProperty.call(data, key)) {
            if (typeof data[key] !== "string") throw new Error("Nem szöveg: " + key);
            fields[key] = data[key];
        }
    }
    function copyBoolean(data, key, destination) {
        if (typeof data[key] !== "boolean") throw new Error("Nem logikai érték: " + key);
        fields[destination] = data[key];
    }

    if (type === "battery") {
        fields.vbat = number(original, "vbat");
    } else if (type === "sensor") {
        const data = object();
        copyBoolean(data, "ok", "sensor_ok");
        // A failed reading must never be converted into zero temperature/humidity.
        if (data.ok) {
            copyNumber(data, "t", "temperature_c", true);
            copyNumber(data, "h", "humidity_pct", true);
            copyNumber(data, "p", "pressure_hpa", true);
        }
    } else if (type === "wifi") {
        const data = object();
        copyNumber(data, "rssi_dbm", "rssi_dbm", true);
        for (const key of ["channel", "uptime_s", "wifi_connected_s", "wifi_reconnects", "mqtt_reconnects", "wake_count"]) {
            copyNumber(data, key);
        }
        for (const key of ["ssid", "bssid", "ip", "gateway", "mac"]) copyString(data, key);
    } else if (type === "status") {
        const state = typeof original === "string" ? original.trim() : "";
        const codes = {offline: 0, sleeping: 1, online: 2};
        if (!Object.prototype.hasOwnProperty.call(codes, state)) throw new Error("Ismeretlen állapot");
        fields.state = state;
        fields.state_code = codes[state];
    } else if (type === "sleep") {
        const data = object();
        copyNumber(data, "sleep_s", "sleep_s", true);
        copyBoolean(data, "low_battery", "low_battery");
        copyNumber(data, "battery_v");
        copyNumber(data, "wake_count");
        copyString(data, "wake_reason");
        copyString(data, "cycle_id");
    }

    // The device has no UTC measurement timestamp. Archive server receive time.
    // Keep online -> sleeping events distinct even if both arrive in one ms.
    const clocks = context.get("aksisbmeLastTimes") || {};
    const timestamp = Math.max(Date.now(), (clocks[type] || 0) + 1);
    clocks[type] = timestamp;
    context.set("aksisbmeLastTimes", clocks);
    fields.time = timestamp; // Special timestamp field in node-red-contrib-influxdb 0.7.0.
    fields.received_at = new Date(timestamp).toISOString();

    msg.measurement = "aksis_bme";
    msg.payload = [fields, {device: "bme280esp32", type: type}];
    node.status({fill: "green", shape: "dot", text: type + " → AksisBME"});
    return msg;
} catch (error) {
    node.status({fill: "red", shape: "ring", text: "Hibás " + type + " adat"});
    node.error("AksisBME: " + error.message, msg);
    return null;
}
