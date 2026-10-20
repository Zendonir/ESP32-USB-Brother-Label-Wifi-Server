# ESP32-S3 WLAN-Printserver für Brother TD-2020A

Open-Source-Firmware (ESP-IDF, C), die einen Brother TD-2020A Etikettendrucker per USB an einen
ESP32-S3 anschließt und ihn im lokalen WLAN bereitstellt – ohne Cloud.

> **Aktueller Stand: Phase 2 – USB-Diagnose-Firmware (v0.2.0-diag).**
> Diese Version **druckt noch nicht**. Sie erkennt den Drucker, liest alle USB-Deskriptoren
> aus und zeigt sie auf der seriellen Konsole und im Webinterface an. Sie ist bisher **nur
> kompiliert und auf dem PC getestet, nicht auf echter Hardware.** Mit Ihren Messwerten wird
> darauf Phase 3 (Druck) aufgebaut.

## Inhalt

- [Roadmap und Status](#roadmap-und-status)
- [Was Phase 2 kann](#was-phase-2-kann)
- [Hardware und Verkabelung](#hardware-und-verkabelung)
- [Kompilieren](#kompilieren)
- [Flashen](#flashen)
- [Ersteinrichtung (WLAN)](#ersteinrichtung-wlan)
- [Diagnose durchführen – was ich von Ihnen brauche](#diagnose-durchführen--was-ich-von-ihnen-brauche)
- [Serielle Konsole](#serielle-konsole)
- [Web und API](#web-und-api)
- [Projektstruktur](#projektstruktur)
- Weitere Dokumente: [Phase-1-Analyse](docs/phase1-analyse.md) ·
  [USB-Verkabelung](docs/usb-verkabelung.md) · [Testplan](docs/testplan.md) ·
  [Bekannte Einschränkungen](docs/bekannte-einschraenkungen.md) · [OpenAPI](docs/openapi.yaml)

## Roadmap und Status

| Phase | Inhalt | Status |
|---|---|---|
| 1 | Hardware- und Protokollanalyse | ✅ [Bericht](docs/phase1-analyse.md) – offene Punkte müssen am Gerät gemessen werden |
| 2 | USB-Diagnose-Firmware | ✅ implementiert, kompiliert (ESP-IDF 5.5.6 und 6.1), ⏳ Hardwaretest ausstehend |
| 3 | Zuverlässige USB-Übertragung eines Brother-Druckauftrags | 🔜 wartet auf Diagnoseergebnis |
| 4 | WLAN + RAW-TCP 9100 für Windows | 🔜 |
| 5 | REST-API + zentrale Warteschlange | 🔜 |
| 6 | Vollständiges Webinterface | 🔜 (Diagnose-UI vorhanden) |
| 7 | OTA, API-Key, Recovery | 🔜 (Partitionen und Rollback vorbereitet) |
| 8 | Integrations- und Dauertests | 🔜 |

## Was Phase 2 kann

- **USB-Host** über den nativen USB-Port (GPIO19/20) mit der ESP-IDF USB Host Library
- Bei jedem Anstecken: vollständiger Bericht mit Device-/Configuration-/Interface-/Endpoint-
  Deskriptoren, allen String-Deskriptoren, unbekannten Deskriptoren als Hex, Rohdaten-Hexdump
- Automatische Analyse: USB Printer Class (0x07/0x01, Protokoll uni/bi/1284.4), Bulk-OUT/IN,
  Alternate Settings, herstellerspezifische Kandidaten, Massenspeicher-Hinweis
- Standard-Anfragen der USB Printer Class: **GET_DEVICE_ID** (IEEE-1284-ID) und
  **GET_PORT_STATUS** – **keine** Brother-spezifischen Befehle
- Erkennen von Trennen/Wiederverbinden, automatische Neuinitialisierung
- Bulk-OUT/IN-Transport mit eigener Timeout-Behandlung und Endpoint-Recovery (für Phase 3)
- WLAN: Setup-Access-Point mit **individuellem Zufallspasswort**, Station-Modus mit
  Auto-Reconnect, Fallback-AP nach 120 s ohne Verbindung, mDNS `brother-td2020.local`
- Diagnose-Webinterface (Deutsch/Englisch), JSON-API, Log-Ansicht
- Serielle Konsole mit Diagnosebefehlen
- Watchdog, Coredump-Partition, zwei OTA-Partitionen

## Hardware und Verkabelung

- Heemol ESP32-S3 N16R8 DevKitC-1 (16 MB Flash, 8 MB Octal-PSRAM)
- USB-C-OTG-Adapter am **nativen** USB-Port, USB-A-auf-Mini-B-Kabel zum Drucker
- Stromversorgung und Konsole über den **UART/COM**-USB-C-Port

**Bitte vor dem ersten Anschließen [docs/usb-verkabelung.md](docs/usb-verkabelung.md) lesen.**
Der Drucker wird nur erkannt, wenn am OTG-Adapter 5 V (VBUS) anliegen. Ob das Board das ohne
Änderung liefert, hängt von der Platinenrevision ab und muss gemessen werden.

## Kompilieren

Voraussetzung: [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
**v5.5.x** (empfohlen, getestet mit v5.5.6) oder **v6.x** (Build mit v6.1 geprüft).

```bash
git clone <dieses-repo> brother-printserver
cd brother-printserver
. $HOME/esp/esp-idf/export.sh      # Windows: ESP-IDF-Eingabeaufforderung verwenden
idf.py set-target esp32s3          # nur beim ersten Mal
idf.py build
```

Beim ersten Build lädt der Komponentenmanager automatisch `espressif/mdns` (und unter
ESP-IDF ≥ 6.0 zusätzlich `espressif/usb`) aus der Espressif Component Registry.

Optionen (`idf.py menuconfig`):

| Menü | Option | Standard |
|---|---|---|
| Brother print server | Hostname, Reset-Taste (GPIO), Logpuffer | `brother-td2020`, GPIO0, 32 KiB |
| USB printer transport | Bericht beim Anstecken, Vendor-Interface belegen, Chunkgröße | an, aus, 16 KiB |
| Wi-Fi manager | SSID-Präfix des Setup-AP, Fallback-Zeit | `Brother-PrintServer-Setup`, 120 s |
| Diagnostic web interface | `/api/diag/raw` (Rohdaten an Drucker) | **aus** |

### Build-Verifikation (durchgeführt)

| Prüfung | Ergebnis |
|---|---|
| `idf.py build` mit ESP-IDF v5.5.6 | ✅ 0 Fehler, 0 Warnungen, Image ≈ 1,0 MB (Partition 4 MB) |
| `idf.py build` mit ESP-IDF v6.1 | ✅ 0 Fehler, 0 Warnungen |
| `tests/host/run_tests.sh` (Parser-Unit-Tests, Fuzzing, ASan/UBSan) | ✅ |
| Lauf auf ESP32-S3 / mit TD-2020A | ⏳ **nicht getestet** |

## Flashen

Board über den **UART/COM-Port** mit dem PC verbinden:

```bash
idf.py -p /dev/ttyUSB0 flash monitor     # Windows: -p COM5
```

Klappt der automatische Download-Modus nicht: **BOOT** gedrückt halten, **RESET** kurz drücken,
BOOT loslassen, dann flashen. Der native USB-Port ist nach dem Flashen USB-Host und steht nicht
mehr zum Flashen zur Verfügung.

Ohne ESP-IDF (fertiges Image, Offset 0x0):

```bash
python -m esptool --chip esp32s3 -p COM5 -b 460800 write_flash 0x0 brother_printserver-merged.bin
```

Ein Merged-Image erzeugen Sie selbst mit `idf.py merge-bin` (liegt dann unter
`build/merged-binary.bin`).

## Ersteinrichtung (WLAN)

1. Nach dem ersten Start erscheint auf der seriellen Konsole (115200 Baud):
   ```
   *****************************************************
     Setup Wi-Fi   SSID    : Brother-PrintServer-Setup-A1B2
                   Password: kH7mP2xQr9Tz
                   URL     : http://192.168.4.1/
   *****************************************************
   ```
   Das Passwort wird beim ersten Start zufällig erzeugt und bleibt im NVS gespeichert.
   Jederzeit erneut anzeigen mit dem Konsolenbefehl `ap_info`.
2. Mit diesem WLAN verbinden, `http://192.168.4.1/` öffnen, Reiter **WLAN**, SSID und Passwort
   eingeben. Alternativ auf der Konsole: `wifi_set "Mein WLAN" "geheim123"`.
3. Die erhaltene IP-Adresse steht in der Konsole (`connected, IP …`). Der Setup-AP schaltet
   sich 60 s nach erfolgreicher Verbindung ab. Danach: `http://brother-td2020.local/`.
4. WLAN-Daten zurücksetzen: Konsole `wifi_clear` oder **BOOT-Taste im Betrieb 5 s halten**.

Aus Sicherheitsgründen lassen sich WLAN-Daten und Neustart über das Web **nur** aus dem
Setup-WLAN heraus ändern, nicht aus dem Heimnetz.

## Diagnose durchführen – was ich von Ihnen brauche

1. VBUS messen wie in [docs/usb-verkabelung.md](docs/usb-verkabelung.md) beschrieben.
2. Firmware flashen, `idf.py -p PORT monitor` offen lassen.
3. Drucker einschalten (Etikettenrolle eingelegt) und anstecken.
4. Folgendes zurückschicken:
   - den kompletten Bericht zwischen `==== USB DEVICE REPORT ====` und dem Abschlussstrich
     (oder `http://<ip>/api/usb` als JSON bzw. „Text herunterladen“ im Webinterface)
   - die Logzeilen rund ums Anstecken (insbesondere alles mit `ENUM`, `usb_printer`, `HUB`)
   - Ausgabe von `usb_refresh` **mit** und **ohne** Etikettenrolle / mit offenem Deckel
   - Ausgabe von `usb_read`
   - VBUS-Messwert, Fotos der Board-Ober- und Unterseite
5. Optional für Phase 3: eine mit dem **originalen Windows-Treiber** erzeugte Druckdatei
   (`.prn`, siehe [Testplan](docs/testplan.md#optionaler-vorab-test-für-phase-3-nur-mit-config_diag_enable_raw_sendy)).

Falls gar nichts erkannt wird: Ohne VBUS gibt es keine einzige USB-Logzeile. Mit VBUS, aber
fehlerhafter Enumeration, erscheinen `ENUM`-Fehlermeldungen der ESP-IDF – bitte mitschicken.

## Serielle Konsole

| Befehl | Funktion |
|---|---|
| `usb` | vollständigen USB-Bericht ausgeben |
| `usb_refresh` | IEEE-1284-ID und Port-Status neu abfragen, Bericht ausgeben |
| `usb_read` | einmal vom Bulk-IN-Endpoint lesen (1 s), Hexdump |
| `status` | Drucker, WLAN, Speicher |
| `wifi_set <ssid> [passwort]` | WLAN speichern und verbinden |
| `wifi_clear` | WLAN-Daten löschen, Neustart in den Setup-Modus |
| `ap_info` | Setup-WLAN und Passwort anzeigen |
| `log` | gepufferten Log ausgeben |
| `log_level <tag\|*> <none\|error\|warn\|info\|debug\|verbose>` | Log-Level ändern |
| `restart` | Neustart |
| `help` | Hilfe |

## Web und API

Webinterface: `http://brother-td2020.local/` bzw. `http://192.168.4.1/` (Setup-WLAN).

| Methode | Pfad | Beschreibung |
|---|---|---|
| GET | `/api/health` | Healthcheck |
| GET | `/api/status` | Firmware, WLAN, Speicher, Drucker |
| GET | `/api/usb` | USB-Bericht (JSON) |
| GET | `/api/usb/text` | USB-Bericht (Text, wie Konsole) |
| POST | `/api/usb/refresh` | IEEE-1284-ID/Port-Status neu abfragen |
| GET | `/api/log` | Log |
| POST | `/api/wifi` | WLAN setzen – **nur über Setup-WLAN** |
| POST | `/api/system/restart` | Neustart – **nur über Setup-WLAN** |
| POST | `/api/diag/raw` | nur wenn in menuconfig aktiviert: Rohdaten unverändert an den Drucker |

Beispiele:

```bash
curl http://brother-td2020.local/api/status
curl -o usb-report.json http://brother-td2020.local/api/usb
curl -X POST http://brother-td2020.local/api/usb/refresh
```

Vollständige Spezifikation: [docs/openapi.yaml](docs/openapi.yaml). Die Druck-Endpunkte
(`/api/print/raw`, `/api/print/template`, `/api/jobs`) und der API-Key folgen in Phase 5/7.

## Projektstruktur

```
├── CMakeLists.txt, sdkconfig.defaults, partitions.csv
├── main/                  app_main, serielle Konsole, Kconfig
├── components/
│   ├── usb_printer/       USB-Host, Enumeration, Deskriptor-Parser/-Bericht, Bulk-Transport
│   ├── wifi_manager/      Station + Setup-AP, Reconnect, Fallback
│   ├── configuration/     NVS-Zugriff
│   ├── diagnostics/       Log-Ringpuffer
│   ├── web_interface/     HTTP-Server und Diagnose-API
│   └── common_util/       String-/JSON-Builder
├── web/                   index.html, style.css, app.js (in die Firmware eingebettet)
├── tests/host/            Unit-Tests für den Deskriptor-Parser (läuft auf dem PC)
└── docs/                  Analyse, Verkabelung, Testplan, Einschränkungen, OpenAPI
```

Die Komponenten `print_queue`, `raw_tcp_server`, `http_api` (Druck), `brother_protocol` und
`ota_manager` entstehen in den Phasen 3–7 auf Basis der Diagnoseergebnisse.

### Partitionstabelle (16 MB)

| Name | Typ | Offset | Größe |
|---|---|---|---|
| nvs | data/nvs | 0x9000 | 64 KiB |
| otadata | data/ota | 0x19000 | 8 KiB |
| phy_init | data/phy | 0x1B000 | 4 KiB |
| ota_0 | app | 0x20000 | 4 MiB |
| ota_1 | app | 0x420000 | 4 MiB |
| storage | data/spiffs | 0x820000 | 7,8 MiB (Templates/Backups, später) |
| coredump | data/coredump | 0xFF0000 | 64 KiB |

## Lizenz

MIT – siehe [LICENSE](LICENSE).
