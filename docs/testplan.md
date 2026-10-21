# Testplan

Legende Status: ✅ bestanden · ❌ fehlgeschlagen · ⏳ ausstehend (Hardware nötig) · 🔜 spätere Phase

## A. Automatisch (ohne Hardware)

| ID | Test | Befehl | Status |
|---|---|---|---|
| A1 | Firmware kompiliert mit ESP-IDF v5.5.x ohne Warnungen | `idf.py build` | ✅ (v5.5.6) |
| A2 | Deskriptor-Parser/Report: Unit-Tests + 20 000 Fuzz-Iterationen unter ASan/UBSan, JSON-Validierung | `tests/host/run_tests.sh` | ✅ |

Die Test-Deskriptoren in A2 sind **synthetisch** (nach USB-Spezifikation aufgebaut), nicht
die des TD-2020A.

## B. Phase 2 – USB-Diagnose am echten Gerät

Vorbereitung: Firmware geflasht, serielle Konsole offen (`idf.py -p PORT monitor`, 115200 Baud).
Bei jedem Test bitte Konsolenausgabe bzw. `GET /api/usb` sichern.

| ID | Test (entspricht Prompt-Punkt) | Durchführung | Erwartet | Status |
|---|---|---|---|---|
| B1 | Start ohne Drucker (1) | Board ohne USB-Gerät starten | Banner mit Setup-WLAN, `USB host started`, kein Absturz; `/api/status` → `printer.connected=false` | ✅ (21.10.2026, mit ausreichender Versorgung) |
| B1a | Brownout-Schutz | Board an schwacher Versorgung starten | nach 2 Brownouts `SAFE MODE`, Konsole/`usb` nutzbar, `wifi_start` startet WLAN | ⏳ |
| B2 | VBUS-Messung | siehe usb-verkabelung.md | 4,7–5,2 V am Adapter | ✅ 4,86 V (21.10.2026) |
| B3 | Drucker nachträglich anschließen (2) | Drucker an, Kabel stecken | `device attached …`, vollständiger Bericht auf der Konsole und unter „USB-Diagnose“ | ✅ (21.10.2026) |
| B4 | Deskriptoren vollständig | Bericht prüfen | Device-, Config-, Interface-, Endpoint-Deskriptoren + Rohdaten-Hexdump; keine `WARNING: descriptor chain` | ✅ keine Kettenfehler (21.10.2026) |
| B5 | Printer-Class-Analyse | Abschnitt ANALYSIS | entweder „USB Printer Class interface … claimed: yes“ oder klar „Vendor-specific CANDIDATE“/„no interface“ | ✅ Printer Class 07/01/02, claimed (21.10.2026) |
| B6 | IEEE-1284-ID | Abschnitt IEEE 1284 | ID-String mit MFG/MDL/CMD oder dokumentierte Fehlermeldung | ✅ `MFG:Brother;CMD:PT-CBP;MDL:TD-2020;…` (21.10.2026) |
| B7 | Port-Status mit Etiketten | `usb_refresh` | raw-Wert notieren | ✅ raw 0x18 (21.10.2026) |
| B8 | Port-Status ohne Etiketten / Deckel offen (9) | Rolle entnehmen, `usb_refresh` | raw-Wert notieren (zeigt, ob Papiermangel gemeldet wird) | ⏳ |
| B9 | Bulk-IN-Lesetest | `usb_read` | Daten oder `ESP_ERR_TIMEOUT` (beides gültig, notieren) | ⏳ |
| B10 | Trennen im Betrieb (3) | USB-Kabel ziehen | `printer disconnected`, Status `connected=false`, kein Absturz | ✅ (21.10.2026) |
| B11 | Wiederverbinden | Kabel erneut stecken | neuer Bericht, `connects since boot` +1 | ✅ connects 2 (21.10.2026) |
| B12 | Drucker ausschalten (8) | Netzschalter aus/an | wie B10/B11 | ⏳ |
| B13 | 20× Stecken/Ziehen | wiederholen | kein Absturz, `internal_free` in `/api/status` stabil (±2 KiB) | ⏳ |
| B14 | ESP32-Neustart mit angeschlossenem Drucker (11) | `restart` | Drucker wird nach Boot erkannt | ⏳ |

## C. Phase 2 – WLAN / Web

| ID | Test | Erwartet | Status |
|---|---|---|---|
| C1 | Erster Start | Setup-WLAN `Brother-PrintServer-Setup-XXXX` mit 12-stelligem Passwort (nur Konsole) | ✅ (21.10.2026, mit ausreichender Versorgung) |
| C2 | Webinterface über Setup-WLAN | `http://192.168.4.1/` lädt, DE/EN umschaltbar | ⏳ |
| C3 | WLAN-Daten setzen (Web) | Verbindung, IP in Konsole; Setup-AP schaltet nach 60 s ab | ⏳ |
| C4 | WLAN-Daten ändern aus dem Heimnetz | `403` (nur über Setup-AP erlaubt) | ⏳ |
| C5 | mDNS | `http://brother-td2020.local/` erreichbar | ⏳ |
| C6 | Router aus/an | automatische Wiederverbindung, nach 120 s ohne Verbindung Setup-AP aktiv | ⏳ |
| C7 | BOOT-Taste 5 s halten | WLAN-Daten gelöscht, Neustart im Setup-Modus | ⏳ |
| C8 | Log im Web | `/api/log` zeigt Logzeilen, **kein** Passwort | ⏳ |

## P3. Phase 3 – Brother-Status und Testdruck (Firmware ≥ 0.3.0)

| ID | Test | Durchführung | Erwartet | Status |
|---|---|---|---|---|
| P3-1 | Statusabfrage | Konsole `brother_status` bzw. Dashboard „Status abfragen“ | Modell TD-2020, Medientyp/-breite passend zur Rolle, Fehler „none“ | ⏳ |
| P3-2 | Deckel offen | Deckel öffnen, `brother_status` | Fehler „Cover open“ | ⏳ |
| P3-3 | Ohne Rolle | Rolle entnehmen, `brother_status` | Fehler „No media“, Medium „none“ | ⏳ |
| P3-4 | Testdruck | `.prn` des Originaltreibers erzeugen, Datei im Reiter System senden | Etikett wird gedruckt; Antwort `result: printed` | ✅ 4454 Byte (Treiberdatei), Drucker meldet Druck → abgeschlossen → empfangsbereit (21.10.2026) |
| P3-5 | Testdruck mit offenem Deckel | wie P3-4 bei offenem Deckel | `result: printer_error`, kein Hängen; danach druckt P3-4 wieder | ⏳ |
| P3-6 | Abbruch | während des Uploads Browser/WLAN trennen | Log `raw send INCOMPLETE … aborting job`, Drucker druckt nichts Halbes bzw. bricht ab | ⏳ |
| P3-7 | Abschalten | `raw_send off`, Upload versuchen | `403`; nach Neustart wieder möglich | ⏳ |

## D. Spätere Phasen (Gesamtplan)

| Prompt-Punkt | Test | Phase |
|---|---|---|
| 4 | Windows sendet Druckauftrag über Port 9100 mit Originaltreiber | 4 🔜 |
| 5 | HTTP-API sendet Druckauftrag | 5 🔜 |
| 6 | Windows und HTTP gleichzeitig → FIFO, keine Vermischung | 5 🔜 |
| 7 | WLAN-Ausfall während Auftrag → Job „failed/unknown“, nie „success“ | 4/5 🔜 |
| 10 | Große Aufträge (> 1 MB) gestreamt, RAM stabil | 4 🔜 |
| 12 | Firmware-Update per Web, Rollback bei defektem Image | 7 🔜 |
| 13 | Dauertest: 1 Auftrag/min über 8 h | 8 🔜 |
| 14 | Speicherverbrauch über Dauertest (`internal_min_free`) | 8 🔜 |
| 15 | Verschiedene Etikettenformate | 3/8 🔜 |

### Testdruck mit Treiberdatei (siehe P3-4)

1. Unter Windows den Original-Brother-Treiber installieren, Drucker temporär per USB am PC.
2. Etikett drucken mit „Ausgabe in Datei umleiten“ (oder in den Druckereigenschaften den Anschluss `FILE:` wählen) → `test.prn`.
3. `curl --data-binary @test.prn -H "Content-Type: application/octet-stream" http://<ip>/api/diag/raw`
4. Erwartet: JSON mit `bytes_acknowledged_by_usb` = Dateigröße. Ob ein Etikett gedruckt wurde,
   **muss visuell geprüft werden** – die USB-Bestätigung ist kein Druckbeleg.
