# XIAO ESP32C6 akkufeszültség, BME280 és Wi-Fi → MQTT

A program ébredésenként egyszer mér és retained MQTT-üzenetben küldi az
akkufeszültséget, a BME280 adatait és a Wi-Fi diagnosztikát a
`192.168.0.107:1883` brokerre, majd deep sleepbe lép.
Normál esetben 3600 másodpercet (1 órát), alacsony akkufeszültségnél
86400 másodpercet (1 napot) alszik. A mérés és a küldés néhány másodperce
hozzáadódik ehhez az időközhöz.

A Node-RED / InfluxDB / Grafana beüzemeléséhez a gyökérkönyvtári `.env`
aktuális `NORMAL_SLEEP_SECONDS` értéke ideiglenesen **30** (30 másodperc).
Visszaállításhoz írd át 3600-ra, majd fordítsd újra és töltsd fel.
Az alacsony akkufeszültségnél alkalmazott napi ütemezés változatlan.
Az akkufeszültség mérése BME280 nélkül is működik.

| Topic | Tartalom |
| --- | --- |
| `bme280esp32/battery/voltage` | Feszültség voltban, például `4.087` |
| `bme280esp32/battery/status` | `online`, normál elalváskor `sleeping`, kapcsolatvesztéskor LWT: `offline` |
| `bme280esp32/sensor` | JSON: `{"ok":true,"t":22.35,"h":45.12,"p":1013.25}` |
| `bme280esp32/wifi` | JSON: jelerősség, hálózati címek, csatorna, üzemidő és újracsatlakozások |
| `bme280esp32/sleep` | JSON: következő alvás időtartama, alacsony akku jelzése, ébredésszámláló |

A BME280 JSON mezői: `t` = °C, `h` = relatív páratartalom %, `p` = helyi
légnyomás hPa-ban (nincs tengerszintre átszámítva). Az `ok` jelzi a mérés
érvényességét. Hiányzó/hibás szenzornál `{"ok":false}` érkezik, a program
a következő ébredéskor újrapróbálja az érzékelő felismerését.

A Wi-Fi JSON minden ébredéskor, MQTT-csatlakozás után frissül. Mezői:

| Mező | Jelentés |
| --- | --- |
| `rssi_dbm` | Vételi jelerősség dBm-ben; a kevésbé negatív érték erősebb jel |
| `ssid` | A csatlakoztatott Wi-Fi hálózat neve |
| `bssid` | Az aktuális hozzáférési pont MAC-címe; több AP esetén megmutatja, melyikhez csatlakozik |
| `channel` | Wi-Fi csatorna |
| `ip`, `gateway`, `mac` | ESP IP-címe, átjárója és Wi-Fi MAC-címe |
| `uptime_s` | ESP üzemideje másodpercben, indulás óta |
| `wifi_connected_s` | Az aktuális, észlelt Wi-Fi-kapcsolat időtartama másodpercben |
| `wifi_reconnects` | A főciklus által észlelt sikeres Wi-Fi-visszacsatlakozások száma indulás óta |
| `mqtt_reconnects` | Sikeres MQTT-visszacsatlakozások száma indulás óta |
| `wake_count` | Ébredések száma, deep sleepen át megőrizve |

Az első csatlakozás nem számít újracsatlakozásnak, a számlálók újraindításkor
nullázódnak (deep sleep utáni ébredéskor is). A `wake_count` RTC memóriában
megmarad, teljes resetnél/tápvesztésnél újra 1-ről indul.
A főciklus két ellenőrzése között lezajló rövid Wi-Fi-szakadást
a Wi-Fi számláló nem feltétlenül észleli. A retained Wi-Fi-adat az utolsó
ismert állapot; az elérhetőséget a status topic mutatja.

## Alvás és akkukímélő üzem

A program még a Wi-Fi elindítása előtt méri az akkufeszültséget.
**3,30 V vagy alatta** napi ébredésre vált; a normál óránkénti üzemhez
**legalább 3,50 V** szükséges. A köztes tartományban megtartja az előző
üzemmódot. Ez szoftveres takarékossági küszöb egycellás Li-ion akkumulátorhoz;
a cella hardveres védelmét továbbra is a BMS végzi.

Az alvás-topic például:

```json
{"sleep_s":3600,"low_battery":false,"battery_v":3.925,"wake_count":1,"wake_reason":"reset","cycle_id":"0123456789abcdef"}
```

`wake_reason`: `timer` időzített ébredésnél, `reset` egyéb indulásnál.
Az alacsony akkufeszültséghez tartozó üzemmód RTC memóriában megmarad
deep sleep alatt. A `sleep_s` a következő alvás hossza másodpercben.

Az eszköz minden adatsor után `sleeping` állapotot küld, majd egy egyedi
`cycle_id`-t tartalmazó alvási tervet. Erre a topicra maga is feliratkozik:
a saját üzenet brokeroldali visszaérkezése igazolja, hogy a broker az előtte
küldött adatokat is feldolgozta. Ezután szabályosan bontja az MQTT-kapcsolatot,
leállítja a Wi-Fi-t, a BME280-at sleep módba teszi, és beállítja az RTC időzítőt.
Az MQTT LWT ezért normál alváskor nem írja felül a `sleeping` állapotot.

Hálózati/brokerhiba vagy elmaradt visszaigazolás esetén az eszköz a
`MAX_AWAKE_SECONDS` időkeret lejártakor is elalszik. Egy már futó hálózati
hívás időkorlátja miatt ezt néhány másodperccel túllépheti. A sikertelen
küldést a következő normál/napi ébredéskor új méréssel próbálja újra.
Bekapcsoláskor/reset után 5 másodperces USB-karbantartási ablakot hagy.
Alvás közben az USB-s soros port eltűnhet; feltöltéshez szükség esetén a
BOOT nyomva tartása mellett nyomd meg a RESET-et, majd engedd el a BOOT-ot.
Ha így, kézzel léptél programozási módba, sikeres feltöltés után nyomd meg
egyszer **csak a RESET-et** a firmware elindításához. Az ESP32-C6 natív
USB-s szoftveres resetje nem mindig tud kilépni a kézzel indított
programozási módból. Bizonytalan USB-kapcsolatnál használj másik adatkábelt,
és a feltöltés alatt ne mozgasd a kábelt, illetve ne nyomj gombot.

## Bekötés

BME280: VCC → 3V3, GND → GND, SDA → D4/GPIO22, SCL → D5/GPIO23,
CSB → 3V3, SDO → GND. A program a 0x76 és 0x77 I²C-címen keresi a valódi
BME280-at (chip ID 0x60). A szenzor két mérés között forced módban alszik.

`BAT+ → R2 (220 kΩ) → A6/GPIO6 → R1 (220 kΩ) → GND`

A6 a hátoldali MTCK/GPIO6 pad. **D6 a szélső tüskesoron GPIO16**, ezért
nem helyettesíti A6-ot. A két azonos ellenállás osztásaránya 2, tehát
4,2 V akkufeszültségnél az ADC bemenetén 2,1 V várható.
A BMS P− és az ESP GND közös pont.

A nagy ellenállású osztó mérését az A6 és GND közé tett 100 nF kerámia
kondenzátor segítheti. A szoftver 64 kalibrált millivoltos ADC-mérést átlagol.
A tényleges pontosságot multiméteres összehasonlítással kell ellenőrizni;
a `BATTERY_CALIBRATION` érték erre szolgál a `battery_mqtt/config.h` fájlban.
A kiírt három tizedesjegy nem jelent millivoltos pontosságot.

## Fordítás és feltöltés

Arduino ESP32 core: 3.3.5; board: `esp32:esp32:XIAO_ESP32C6`, USB CDC enabled.
Könyvtárak: PubSubClient 2.8, ArduinoJson 7.4.2, Adafruit BME280 Library 2.3.0 és függőségei
(Adafruit Unified Sensor, Adafruit BusIO). Az azonosított eszköz COM7,
MAC-címe `98:A3:16:8E:C3:58`; újracsatlakoztatáskor a port változhat.

Töltsd ki a projekt gyökerében lévő `.env` fájlt (`.env.example` a minta):

```dotenv
WIFI_SSID="A_WIFI_NEVE"
WIFI_PASSWORD="A_WIFI_JELSZAVA"
MQTT_USERNAME=""
MQTT_PASSWORD=""
MQTT_TOPIC="bme280esp32/battery/voltage"
MQTT_STATUS_TOPIC="bme280esp32/battery/status"
MQTT_BME_TOPIC="bme280esp32/sensor"
MQTT_WIFI_TOPIC="bme280esp32/wifi"
MQTT_SLEEP_TOPIC="bme280esp32/sleep"
NORMAL_SLEEP_SECONDS=3600
LOW_BATTERY_SLEEP_SECONDS=86400
LOW_BATTERY_THRESHOLD_V=3.30
LOW_BATTERY_RECOVERY_V=3.50
MAX_AWAKE_SECONDS=45
```

Az idézőjelek közötti karakterek szó szerint értendők, így a `#`, `$` és
backslash is megmarad. Ha az értékben dupla idézőjel van, használj kívül
egyszeres idézőjelet. A `.env`, a generált `secrets.h`, a build és a
flash-mentés gitignore alatt van. A program a fordításkor építi be az adatokat;
a `.env` módosítása után új fordítás és feltöltés szükséges.
Az `MQTT_TOPIC` a feszültség, az `MQTT_STATUS_TOPIC` az online/offline állapot
topicja, az `MQTT_BME_TOPIC` a BME280 JSON adataié,
az `MQTT_WIFI_TOPIC` a Wi-Fi diagnosztikáé, az `MQTT_SLEEP_TOPIC` az alvási tervé;
a fenti táblázat az alapértelmezéseket mutatja. A topicok legyenek
eltérőek, nem üresek, legfeljebb 180 UTF-8 bájtosak, `+` és `#` nélkül.

A projekt gyökerében, PowerShellből:

```powershell
python tools/firmware.py
python tools/firmware.py --upload --port COM7
```

Az első parancs csak fordít, a második fordít és feltölt. A script a meglévő
Arduino IDE fordítókörnyezetét használja. A portot feltöltés előtt ellenőrizd.

Arduino IDE használata előtt a `.env` adataiból generáld a fejlécet:

```powershell
python tools/firmware.py --prepare-only
```

Arduino IDE-ben nyisd meg a `battery_mqtt/battery_mqtt.ino` fájlt,
válaszd a XIAO_ESP32C6 boardot és az ellenőrzött portot, majd töltsd fel.
A soros napló sebessége 115200 baud. A program USB nélkül is elindul,
Wi-Fi- és MQTT-hiba után az ébrenléti időkereten belül újracsatlakozik.

A brokeroldali ellenőrzéshez a projekt gyökerében:

```powershell
python tools/mqtt_watch.py --seconds 35
```

Az ellenőrző script a `.env` topicjait és MQTT-belépését is beolvassa.
Hitelesítéshez a `MQTT_USERNAME` és `MQTT_PASSWORD` környezeti változók is használhatók.
A `retained` üzenet korábbról tárolt érték lehet; a `live` jelzi az új küldést.

A retained feszültség az utolsó ismert érték, nem önmagában az elérhetőség jele;
ehhez a status topicot kell figyelni.

## Források

- [Seeed lábkiosztás és akkumulátoros működés](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)
- [Espressif Arduino ADC API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/adc.html)
- [Adafruit BME280 könyvtár](https://github.com/adafruit/Adafruit_BME280_Library)
- [Espressif ESP32-C6 deep sleep és időzített ébredés](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32c6/api-reference/system/sleep_modes.html)
- [Kilépés kézzel indított USB-programozási módból](https://docs.espressif.com/projects/esptool/en/latest/esp32c6/troubleshooting.html#leaving-download-mode-in-usb-serial-jtag-mode)

