# AksisBME – akkumulátoros BME280 és ESP32-C6

Seeed XIAO ESP32-C6 alapú, akkumulátoros érzékelő a fotós eszközök tárolódobozához. Akkufeszültséget, hőmérsékletet, relatív páratartalmat, helyi légnyomást és Wi-Fi diagnosztikát küld MQTT-n. A Node-RED InfluxDB-be menti az adatokat, a Grafana jeleníti meg őket.

`XIAO + BME280 → MQTT → Node-RED → InfluxDB → Grafana`

## Dokumentáció és fájlok

| Terület | Fájl / leírás |
| --- | --- |
| Bekötés, működés, fordítás és feltöltés | [firmware/README.md](firmware/README.md) |
| Firmware forrása | [battery_mqtt.ino](firmware/battery_mqtt/battery_mqtt.ino) |
| Konfigurációs minta | [.env.example](.env.example) |
| Node-RED import és InfluxDB mezők | [integrations/node-red/README.md](integrations/node-red/README.md) |
| Importálható Node-RED flow | [aksisbme-influx-flow.json](integrations/node-red/aksisbme-influx-flow.json) |
| Grafana használat és importálás | [integrations/grafana/README.md](integrations/grafana/README.md) |
| Teljes VBAT dashboard | [aksisbme-vbat-dashboard.json](integrations/grafana/aksisbme-vbat-dashboard.json) |
| Önálló BME280 szenzorpanel | [aksisbme-sensor-panel.json](integrations/grafana/aksisbme-sensor-panel.json) |

## Indulás

Python 3, Arduino CLI vagy Arduino IDE, az ESP32 boardcsomag és a firmware dokumentációjában felsorolt Arduino-könyvtárak szükségesek.

1. Új munkapéldányban másold a `.env.example` fájlt `.env` néven a projekt gyökerébe. A már kitöltött `.env` fájlt tartsd meg.
2. Töltsd ki a Wi-Fi adatokat, szükség esetén az MQTT-hitelesítést, és állítsd be a topicokat, valamint az alvási ütemezést.
3. Fordítsd le a firmware-t, majd az ellenőrzött ESP-portot megadva töltsd fel.

```powershell
python tools/firmware.py
python tools/firmware.py --upload --port COM7
```

A `COM7` ennek a gépnek a korábban ellenőrzött portja; másik gépen vagy újracsatlakoztatás után ellenőrizd újra. A `.env` változásai új fordítás és feltöltés után érvényesek.

## Alvás és adatküldés

A firmware mér, elküldi az adatokat, majd deep sleepbe lép. A mintakonfiguráció normál alvása 3600 másodperc; a beüzemelés során az eszközre **30 másodperces** alvás került. Az alvási időhöz az ébren töltött mérési és kapcsolódási idő hozzáadódik.

3,30 V-nál vagy alatta napi egyszeri ébredésre vált. A normál ütemre 3,50 V-tól tér vissza. Ezek és az ütemezés a `.env` fájlban állíthatók.

Az alapértelmezett topicok: `bme280esp32/battery/voltage`, `bme280esp32/battery/status`, `bme280esp32/sensor`, `bme280esp32/wifi`, `bme280esp32/sleep`.

Az InfluxDB bucket neve `AksisBME`, a measurement `aksis_bme`. Az önálló panel JSON-t panelként, a teljes dashboard JSON-t dashboardként kell használni. A Node-RED és a Grafana InfluxDB-jogosultságait a saját adatforrás-beállításaikban kell megadni.

## Helyi ellenőrzés

```powershell
python -m unittest discover -s tests -p "test_*.py"
node tests/nodered_flow_test.js
python tools/mqtt_watch.py --seconds 45
```

Az MQTT-figyelőnek el kell érnie a helyi brokert. A retained üzenet korábbi adat is lehet; a `live` üzenet jelzi az új küldést.

## Helyi Git-verziókövetés

A repó tartalmazza a forráskódot, a teszteket, a dokumentációt és az importálható integrációkat. A `.env`, a generált `secrets.h`, a szerkesztő `.history` mappája, a build és a flash-mentések kimaradnak. A helyi beállításokról szükség esetén külön mentést készíts.

```powershell
git status
git log --oneline
git add README.md
git commit -m "Update project documentation"
```

Ez helyi tároló; távoli Git-szolgáltatás nincs beállítva.
