# AksisBME: MQTT → Node-RED → InfluxDB v2

Az importálandó fájl: **aksisbme-influx-flow.json**. Új flow tabot hoz létre;
a Matrix billentyűzet meglévő flow-ját nem módosítja.

A kapott minta a `matrix_billentyuzet/#` topicokat dolgozza fel,
és `voltage`, `millivolts`, `percent`, valamint állapotmezőket ír a
`MatrixBillentyuzet` bucketbe. A jelen projekt ehhez külön flow-t kapott:

- Org: **WFS**, a mintaflow szerint.
- Bucket: **AksisBME**, a képernyőkép szerinti névvel (ID: `158aea3b24cf4432`).
- Measurement: **aksis_bme**.
- Tag: `device=bme280esp32`, `type=battery|sensor|wifi|status|sleep`.

## Importálás

1. Node-RED menü → **Import** → válaszd az `aksisbme-influx-flow.json` fájlt.
2. A flow a kapott mintában használt meglévő config node-okra hivatkozik:
   MQTT `b2b2b2b2b2b2b2b2` (Mosquitto), Influx `b0b0b0b0b0b0b0b0`
   (Influx v2). Ha valamelyik hiányzik, az adott node-ban válaszd ki a már
   működő kapcsolatot. Konténeres környezetben a mintában lévő `mosquitto`
   és `http://influxdb:8086` elnevezés maradhat.
3. Az Influx node-ban ellenőrizd az orgot (**WFS**) és a bucketet (**AksisBME**).
   A meglévő Influx-tokennek írási jog kell ehhez a buckethez is. A token
   nem része az exportnak; ne másold bele a flow JSON-ba.
4. **Deploy**, majd várd meg a következő ébredést (jelenleg kb. 30 másodperc,
   plusz a mérés és a csatlakozás ideje).
5. Influx Data Explorerben futtasd a `check-data.flux` lekérdezést. A tényleges
   tárolást ez igazolja; az előkészítő function zöld jelzése és debug ága
   csak a payload összeállítását jelzi. Az írási/parser hibák az aktív
   **AksisBME HIBA** debug node-ra kerülnek.

Telegram node nincs az új flow-ban. Tesztüzenet vagy riasztás nem kerül kiküldésre.

## Tárolt mezők

| MQTT topic | `type` | Mezők |
| --- | --- | --- |
| `bme280esp32/battery/voltage` | `battery` | `vbat` (V) |
| `bme280esp32/sensor` | `sensor` | `sensor_ok` (boolean), siker esetén `temperature_c`, `humidity_pct`, `pressure_hpa` |
| `bme280esp32/wifi` | `wifi` | `rssi_dbm`, `channel`, `ssid`, `bssid`, `ip`, `gateway`, `mac`, `uptime_s`, `wifi_connected_s`, `wifi_reconnects`, `mqtt_reconnects`, `wake_count` |
| `bme280esp32/battery/status` | `status` | `state`, `state_code` (`offline=0`, `sleeping=1`, `online=2`) |
| `bme280esp32/sleep` | `sleep` | `sleep_s`, `low_battery` (boolean), `battery_v`, `wake_count`, `wake_reason`, `cycle_id` |

Minden pontban megmarad az eredeti `raw_payload`, az `mqtt_topic`, illetve
a `received_at` ISO időpont. Így a később hozzáadott, még külön mezőre nem
fordított JSON-adatok is visszakereshetők. A változó `cycle_id`, IP és SSID
mezőként tárolódik, nem új Influx-tagként.

A hibás, üres, `null`, NaN és végtelen számok nem íródnak be. `ok=false`
szenzorüzenetnél a hibajelző megmarad, de a hőmérséklet/páratartalom/légnyomás
nem kap hamis nulla értéket.

## Időpontok és retained üzenetek

Az ESP jelenleg nem küld UTC mérési időpontot. Az `_time` ezért a Node-RED
fogadási ideje, millisecond precisionnel; a Node-RED gép órája legyen pontos.
Az ugyanabban a milliszekundumban érkező azonos típusú események külön,
1 ms-mal növelt időpontot kapnak, hogy az `online` és `sleeping` ne írja
felül egymást.

Az MQTT feliratkozáskor visszajátszott, `msg.retain=true` pillanatképeket a
flow kihagyja: nincs bennük megbízható eredeti időpont, így az újraindításkor
nem jelennek meg hamisan friss mérésként. Az élő, retained tárolásra küldött
MQTT 3.1.1 üzenetek a normál feliratkozónál `retain=false` jelzővel érkeznek,
ezért a rendszeres mérés mentése működik. A mintában használt MQTT broker
config protokollja `4` (MQTT 3.1.1); ezt a flow megtartja.

Az első mentéshez így meg kell várni a következő ébredést. Az órás/napi
deep sleephez később sem kell módosítani a flow-t: csak ritkábban érkezik adat.

## Grafana: első panel

Flux, InfluxDB v2 adatforrással, feszültség grafikonhoz:

```flux
from(bucket: "AksisBME")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "aksis_bme")
  |> filter(fn: (r) => r.device == "bme280esp32" and r.type == "battery")
  |> filter(fn: (r) => r._field == "vbat")
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
```

További panelek: `type=sensor` + `temperature_c` (°C), `humidity_pct` (%)
vagy `pressure_hpa` (hPa); `type=wifi` + `rssi_dbm` (dBm).
Állapotpanelhez `type=status` + `state` + `last()` használható.
Az összes mezőt tartalmazó lekérdezés vegyes típusai miatt a `mean()`
aggregációt mindig a kívánt numerikus mezőre szűrés után alkalmazd.

## Újragenerálás és ellenőrzés

Ha a `.env` topicnevei változnak, a projekt gyökerében:

```powershell
python tools/generate_nodered_flow.py
node tests/nodered_flow_test.js
```

A generator csak a topicneveket olvassa be a `.env`-ből; a Wi-Fi/MQTT
jelszavakat nem exportálja. A parser forrása `aksisbme-parser.js`.

Forrás: [node-red-contrib-influxdb 0.7.0 implementáció](https://github.com/mblackstock/node-red-contrib-influxdb/blob/0.7.0/influxdb.js).
A kész flow helyi transzformációs tesztje ellenőrizhető; az éles Node-RED
telepítés és az InfluxDB-írás ellenőrzése az import + Deploy után végezhető el.
