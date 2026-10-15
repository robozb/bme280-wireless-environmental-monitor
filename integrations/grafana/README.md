# AksisBME VBAT – Grafana

## BME280 szenzorpanel a meglévő dashboardhoz

Az `aksisbme-sensor-panel.json` **egyetlen panel**, nem teljes dashboard. Egy grafikonon mutatja a relatív páratartalmat (%), a hőmérsékletet (°C) és a helyi légnyomást (hPa). Mindhárom görbéhez külön skála tartozik; a jelmagyarázatban a kiválasztott időtartomány utolsó, minimum és maximum értéke is látható. A [Grafana mezőfelülírásai](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/time-series/#multiple-y-axes) biztosítják az eltérő mértékegységű tengelyeket.

1. A meglévő AksisBME dashboardon adj hozzá egy új panelt: Add → Visualization, majd válaszd ki a már működő InfluxDB adatforrást.
2. Az új panel menüjében nyisd meg az Inspect → JSON nézetet, és válaszd a Panel JSON lehetőséget.
3. Jegyezd meg az új panel `id` értékét. Illeszd be az `aksisbme-sensor-panel.json` tartalmát; ha az eredeti `id` nem 3 volt, a beillesztett JSON-ban állítsd vissza arra. A VBAT panel JSON-ját ne cseréld le.
4. Apply, majd Save dashboard.

Adatforrás: `cezmp4srm3bpcd`; bucket: `AksisBME`; measurement: `aksis_bme`; tagek: `type=sensor`, `device=bme280esp32`. A mezők a Node-RED parserével egyeznek: `humidity_pct`, `temperature_c`, `pressure_hpa`. A panel a dashboard időtartományát és frissítési ütemét követi. A sikertelen BME-mérésekhez nem készít nullaértékű helyettesítést.

A JSON szerkezetét, a három lekérdezés mezőneveit és a mértékegységeket ellenőriztük; élő Grafana-renderelés nem történt.

## VBAT dashboard

Az `aksisbme-vbat-dashboard.json` a megadott Grafana 12.2-es minta alapján készült, importálható dashboard. Balra a kiválasztott időtartomány legutóbbi VBAT értéke, jobbra az akkufeszültség időbeli alakulása jelenik meg. Mindkettő voltban, három tizedessel mutatja az adatokat.

## Importálás

1. Grafana → Dashboards → New → Import.
2. Töltsd fel az `aksisbme-vbat-dashboard.json` fájlt, majd Import.
3. A dashboard a mintában használt InfluxDB adatforrásra hivatkozik: `cezmp4srm3bpcd`. Ha az adatforrás más, mindkét panel szerkesztőjében válaszd ki a megfelelő InfluxDB/Flux adatforrást.
4. A Grafana adatforrás tokenjének olvasnia kell az `AksisBME` bucketet. A Node-RED-ben megadott token és a Grafana adatforrás hitelesítése külön beállítás.

A dashboard új azonosítót használ (`aksisbme-vbat`). Az időtartomány alapból az utolsó 7 nap, az automatikus frissítés 30 másodperc. A frissítés csak lekérdezi az eltárolt adatokat; az ESP ébredési ütemét nem változtatja meg.

## Adatok

- Bucket: `AksisBME`
- Measurement: `aksis_bme`
- Field: `vbat`
- Tagek: `type=battery`, `device=bme280esp32`

Ezek az `integrations/node-red/aksisbme-parser.js` által előállított nevekkel egyeznek. A grafikon nyers mérési pontokat használ, a külön `vbat.flux` fájl ugyanazt a lekérdezést tartalmazza meglévő panelbe másoláshoz.

A számkijelző piros 3,30 V alatt, narancs 3,30 V-tól 3,50 V alatt, zöld 3,50 V-tól. Ez feszültségjelzés, nem töltöttségi százalék. A firmware alacsony feszültségű módjának hiszterézisét nem állapotjelzőként jeleníti meg.

Ha nincs adat: előbb ellenőrizd, hogy a Node-RED valóban sikeresen ír-e az InfluxDB-be. Az előkészített pontot mutató Debug üzenet önmagában nem írásigazolás. A kijelző az időtartomány legutolsó mért értékét mutatja; deep sleep alatt az érték nem frissül.

A JSON szerkezetét és a lekérdezések illeszkedését ellenőriztük; élő Grafana-renderelés és Influx-lekérdezés nem történt.

Formátum: [Grafana dashboard JSON modell](https://grafana.com/docs/grafana/latest/visualizations/dashboards/build-dashboards/view-dashboard-json-model/).
