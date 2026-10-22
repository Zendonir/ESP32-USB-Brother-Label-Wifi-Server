# ESP32-S3 WLAN-Printserver für Brother TD-2020A

[![Firmware](https://github.com/Zendonir/ESP32-USB-Brother-Label-Wifi-Server/actions/workflows/firmware.yml/badge.svg)](https://github.com/Zendonir/ESP32-USB-Brother-Label-Wifi-Server/actions/workflows/firmware.yml)

**➜ Firmware direkt im Browser flashen: <https://zendonir.github.io/ESP32-USB-Brother-Label-Wifi-Server/>**
(Chrome oder Edge am Desktop, Board über den UART/COM-Port anschließen)

Open-Source-Firmware (ESP-IDF, C), die einen Brother TD-2020A Etikettendrucker per USB an einen
ESP32-S3 anschließt und ihn im lokalen WLAN bereitstellt – ohne Cloud.

> **Aktueller Stand: Phase 2 – USB-Diagnose-Firmware (v0.2.2-diag).**
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
| 2 | USB-Diagnose-Firmware | ✅ **am echten TD-2020A getestet** (21.10.2026): VID/PID 0x04F9/0x2055, Printer Class, Bulk IN 0x81 / OUT 0x02 |
| 3 | Brother-Statusabfrage, Testdruck mit Treiberdatei, sichere Übertragung | ✅ **erster Druck über den ESP32** (21.10.2026); Fehlerfälle noch offen |
| 4 | WLAN + RAW-TCP 9100 für Windows, Druckwarteschlange | 🚧 implementiert (0.4.0-dev), ⏳ Test mit Windows |
| 5 | REST-API + zentrale Warteschlange | 🔜 |
| 6 | Vollständiges Webinterface | 🔜 (Diagnose-UI vorhanden) |
| 7 | OTA, API-Key, Recovery | 🟡 Firmware-Update im Web (ab 0.5.0), Rollback aktiv; API-Key offen |
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
- WLAN: **offener** Setup-Access-Point mit Captive Portal (Einrichtungsseite öffnet sich von selbst), Station-Modus mit
  Auto-Reconnect, Fallback-AP nach 120 s ohne Verbindung, mDNS `brother-td2020.local`
- Diagnose-Webinterface (Deutsch/Englisch), JSON-API, Log-Ansicht
- Serielle Konsole mit Diagnosebefehlen
- Watchdog, Coredump-Partition, zwei OTA-Partitionen

## Hardware und Verkabelung

- ESP32-S3-DevKitC-1-Nachbau mit Modul ESP32-S3-WROOM-1-N16R8 (16 MB Flash, 8 MB Octal-PSRAM), getestet wird mit einem Board von Binghe
- **Druckerkabel direkt angelötet:** VBUS → 5V, GND → GND, D− → GPIO19, D+ → GPIO20
  (USB-A-auf-Mini-B-Kabel, A-Stecker abgeschnitten). Alternativ OTG-Adapter an der „USB“-Buchse
  mit geschlossener Lötbrücke „USB-OTG“.
- Stromversorgung und Konsole über den **UART/COM**-USB-C-Port

**Bitte vor dem ersten Anschließen [docs/usb-verkabelung.md](docs/usb-verkabelung.md) lesen.**
Der Drucker wird nur erkannt, wenn an seinem USB-Anschluss 5 V (VBUS) anliegen. Ob das Board das ohne
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

### Im Browser (ohne Installation)

<https://zendonir.github.io/ESP32-USB-Brother-Label-Wifi-Server/> öffnen (Chrome/Edge), Board am
**UART/COM-Port** anschließen, „Firmware installieren“. Bei der Erstinstallation „Gerät löschen“
bestätigen, bei Updates nicht – dann bleiben die WLAN-Daten erhalten. Über „Logs & Konsole“ im
Dialog sehen Sie die serielle Ausgabe (Setup-Passwort, USB-Bericht) direkt im Browser.

### Fertige Dateien

GitHub Actions baut die Firmware bei jedem Push (ESP-IDF 5.5.6 und zur Kontrolle 6.1). Die Dateien
liegen als Artefakt am jeweiligen Lauf unter *Actions*. Fertige Versionen stehen unter
[**Releases**](https://github.com/Zendonir/ESP32-USB-Brother-Label-Wifi-Server/releases) (alle Images,
`manifest.json`, `SHA256SUMS`).

Neues Release erstellen: *Actions → Firmware → Run workflow*, bei `release_tag` z. B. `v0.3.0`
eintragen. Alternativ erzeugt auch ein gepushtes Git-Tag `v*` ein Release. Versionen mit
Bindestrich (z. B. `v0.2.0-diag`) werden als Vorabversion markiert. Der Browser-Flasher wird **nur** von einem
solchen Release-Lauf (*Run workflow* mit `release_tag`) aktualisiert und zeigt damit immer die
zuletzt veröffentlichte Version; normale Pushes bauen nur.

### Mit ESP-IDF

Board über den **UART/COM-Port** mit dem PC verbinden:

```bash
idf.py -p /dev/ttyUSB0 flash monitor     # Windows: -p COM5
```

Klappt der automatische Download-Modus nicht: **BOOT** gedrückt halten, **RESET** kurz drücken,
BOOT loslassen, dann flashen. Der native USB-Port ist nach dem Flashen USB-Host und steht nicht
mehr zum Flashen zur Verfügung.

Mit esptool und den Release-Dateien (WLAN-Daten bleiben erhalten):

```bash
python -m esptool --chip esp32s3 -p COM5 -b 460800 write_flash \
  0x0 bootloader.bin 0x8000 partition-table.bin 0x19000 ota_data_initial.bin 0x20000 brother_printserver.bin
```

Das `*-merged.bin` aus dem Release wird an Offset 0x0 geschrieben, überschreibt dabei aber auch
den NVS-Bereich (WLAN-Daten).

## Ersteinrichtung (WLAN)

1. Nach dem ersten Start (und immer, wenn das Gerät 120 s lang kein WLAN erreicht) öffnet es das
   **offene** WLAN `Brother-PrintServer-Setup-XXXX` – ohne Passwort.
2. Handy oder Laptop damit verbinden. Die Einrichtungsseite **öffnet sich von selbst**
   (Captive Portal: jeder Name wird auf `192.168.4.1` aufgelöst, die Verbindungsprüfung des
   Systems landet auf der Seite; DHCP meldet die Portal-Adresse zusätzlich nach RFC 8910).
   Falls nicht: `http://192.168.4.1/` öffnen. Dort **Netzwerke suchen**, das eigene WLAN
   antippen, Passwort eingeben, **Speichern & verbinden**.
   Alternativ auf der Konsole: `wifi_set "Mein WLAN" "geheim123"`; `ap_info` zeigt SSID und Adresse.
3. Die erhaltene IP-Adresse steht in der Konsole (`connected, IP …`). Der Setup-AP schaltet
   sich 60 s nach erfolgreicher Verbindung ab. Danach: `http://brother-td2020.local/`.
4. WLAN-Daten zurücksetzen: **BOOT-Taste im Betrieb 10 s halten** (oder Konsole `wifi_clear`).
   Danach startet das Gerät mit dem offenen Setup-WLAN, alles lässt sich neu eingeben.

**BOOT-Taste im Betrieb** (nicht beim Einschalten – dann startet der Download-Modus):

| Drücken | Wirkung |
|---|---|
| kurz (unter 2 s) | Neustart – USB-Verbindung zum Drucker und Warteschlange beginnen neu, z. B. wenn etwas hängt |
| 10 s halten | WLAN-Daten löschen, Neustart ins Setup-WLAN |
| 2–10 s, dann loslassen | nichts |

Der Neustart setzt den Printserver zurück, nicht den Drucker selbst; hängt der TD-2020A, ihn
aus- und einschalten. Die Warteschlange des Printservers liegt im RAM: Aufträge, die dort noch
warteten, schickt der Lebensmittel-Server neu; einer, der gerade druckte, steht dort danach als
„Ergebnis unbekannt“ (nachdrucken von Hand).

Aus Sicherheitsgründen lassen sich WLAN-Daten und Neustart über das Web **nur** aus dem
Setup-WLAN heraus ändern, nicht aus dem Heimnetz. Umgekehrt nimmt das Gerät ein
**Firmware-Update nur aus dem Heimnetz** an, nie über das offene Setup-WLAN.

Das Setup-WLAN ist bewusst offen: Es läuft nur, solange das Gerät kein WLAN hat (erste
Einrichtung, oder 120 s ohne Verbindung), und schaltet sich 60 s nach erfolgreicher Verbindung
ab. In dieser Zeit kann jeder in Funkreichweite die WLAN-Daten setzen.

## Firmware-Update (OTA)

Reiter **System → Firmware-Update**: `brother_printserver.bin` aus dem
[neuesten Release](https://github.com/Zendonir/ESP32-USB-Brother-Label-Wifi-Server/releases/latest)
wählen (nicht die `-merged.bin`), **Installieren**. Die Datei wird in die zweite OTA-Partition
geschrieben, geprüft (Projektname, Format, Prüfsumme) und erst dann aktiviert; danach startet das
Gerät neu. Startet die neue Firmware nicht, kehrt der Bootloader zur alten zurück (Rollback).
WLAN-Daten bleiben erhalten. Per Kommandozeile:
`curl --data-binary @brother_printserver.bin -H 'Content-Type: application/octet-stream' http://brother-td2020.local/api/system/ota`

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

## Phase 4: Drucken aus Windows (Port 9100)

Anleitung: **[docs/windows-einrichtung.md](docs/windows-einrichtung.md)** – Standard-TCP/IP-Port,
Protokoll **Raw**, Port **9100**, SNMP aus, originaler Brother-Treiber.

- Jeder Auftrag wird vollständig empfangen und erst dann gedruckt; bricht die Übertragung ab,
  wird nichts gedruckt (`unvollständig`).
- Eine zentrale Warteschlange (FIFO) für Port 9100 und HTTP; nie zwei Aufträge gleichzeitig.
- Vor dem Druck wird der Brother-Status geprüft: Bei offenem Deckel/fehlendem Papier oder ohne
  USB-Verbindung **wartet** der Auftrag (bis 10 min) und druckt danach automatisch.
- Ergebnis laut Drucker: `gedruckt`, `fehlgeschlagen` (Drucker verwirft den Auftrag – kein
  automatischer Nachdruck, „Wiederholen“ im Webinterface), `unbestätigt`.
- Webinterface → **Aufträge**; Konsole `jobs`, `jobs retry <id>`, `jobs cancel <id>`;
  `raw_allow 192.168.3.0/24` begrenzt Port 9100 auf ein Netz.

## Zusammenspiel mit dem Lebensmittel-Server

Der [Lebensmittel-Scanner](https://github.com/Zendonir/ESP32_Lebensmittel_BT_Scanner_Docker)
druckt seine Etiketten über diesen Printserver. Die Aufgaben sind klar getrennt:

| Lebensmittel-Server (Docker) | Printserver (dieser ESP32) |
|---|---|
| zeichnet das Etikett (Name, MHD, QR-Code) | nimmt fertige Druckdaten an, spoolt sie vollständig |
| setzt es in Brother-Rasterbefehle um – Vorlage ist eine Druckdatei des Originaltreibers | prüft vor dem Druck den Brother-Status, wartet bei offenem Deckel/ohne Papier |
| fragt den Auftrag ab und zeigt Ergebnis/Fehler im Browser und am Terminal | meldet, was der Drucker zurückgibt (`gedruckt`, `fehlgeschlagen`, `unbestätigt`) |

Die Schnittstelle ist `POST /api/print/raw` mit zwei Kopfzeilen (ab 0.4.1):

- `X-Job-Name` – erscheint in der Auftragsliste (z. B. „Etikett LEB000123“)
- `X-Job-Key` – Idempotenzschlüssel. Geht die Antwort im WLAN verloren, schickt der
  Server denselben Auftrag mit demselben Schlüssel noch einmal; der Printserver erkennt ihn,
  antwortet `200` mit dem vorhandenen Auftrag (`"duplicate": true`) und druckt **nicht** doppelt.

Etiketten werden über den **Lückensensor** positioniert: der Server kündigt in jedem Auftrag
gestanzte Etiketten mit Breite und Länge an (`ESC i z`), der Drucker setzt dann jedes Etikett
selbst an der Lücke an. Vorher vergleicht der Server die Maße mit dem, was der Drucker über
`/api/status` bzw. `/api/printer/status` als eingelegt meldet, und hält Aufträge bei falscher
Rolle zurück.

Das Ergebnis holt der Server über `GET /api/jobs/{id}`; der Schlüssel steht im Auftrag. Nach
einem Neustart des Printservers fängt die Nummerierung wieder bei 1 an – passt der Schlüssel
nicht, gilt der Auftrag als „Ergebnis unbekannt“ statt eines fremden Ergebnisses.
`/api/status` meldet unter `print_api` (`job_key: true`), dass die Firmware das kann.

Einrichtung im Lebensmittel-Server: *Etiketten → Brother-Drucker (WLAN)* – die **IP-Adresse**
des Printservers eintragen (`.local` löst ein Docker-Container meist nicht auf; am besten im
Router eine feste Adresse vergeben), die Treiberdatei des ersten Testdrucks (`test.prn`)
einlesen, Testetikett drucken.

## Druckereinstellungen (ab 0.4.2)

Startseite → **Druckereinstellungen** (und im Lebensmittel-Server unter *Etiketten → Brother-Drucker*):
Einstellungen, die der TD-2020A in seinem Speicher behält. Gesetzt werden sie mit den
dokumentierten statischen Befehlen der P-touch Template Command Reference (`ESC i X <x> 2 01h 00h n`,
gelesen mit `ESC i X <x> 1 00h 00h` → `01h 00h n`):

| Einstellung | Befehl | Empfehlung |
|---|---|---|
| Befehlsmodus (ESC/P, Raster, P-touch Template) | `ESC i X i` | **Raster** – Server und Windows-Treiber schicken Rasterdaten |
| Druckpriorität (Geschwindigkeit/Qualität) | `ESC i X q` | Geschwindigkeit; Qualität, wenn kleine QR-Codes schlecht lesbar sind |
| Druck um 180° drehen | `ESC i X h` | aus (gedreht wird im Lebensmittel-Server) |
| Stoppposition (Abreißkante/Druckkopf) | `ESC i X ^` | Abreißkante |
| Nach Fehler erneut drucken | `ESC i X d` | **aus** – sonst druckt der Drucker nach einem Fehler selbst nach |

Stromsparen, Druckdichte, „Vorderkante ausrichten“, Sensorempfindlichkeit und das **Papierformat**
(Etiketten oder Endlosband, Größe) haben keinen dokumentierten Befehl. Sie bleiben im Brother-Tool
„Geräteeinstellungen“ bzw. im Treiber unter *Papierformat einrichten*. Eine damit gedruckte
`.prn` enthält die Medieninformation (`ESC i U`); im Lebensmittel-Server eingelesen, stellt sie
den Drucker bei jedem Auftrag auf das richtige Medium.

## Phase 3: Status und Testdruck

**Status:** Konsole `brother_status` oder im Webinterface „Status abfragen“. Gezeigt werden Medium
(Endlos/Etiketten), Breite × Länge, Phase und Fehler (kein Medium, Deckel offen, …).

**Testdruck mit Daten des Originaltreibers:**
1. Unter Windows den Brother-Treiber für den TD-2020 installieren (Drucker dafür kurz per USB am
   PC oder direkt Anschluss `FILE:` wählen).
2. Ein kleines Etikett drucken mit „Ausgabe in Datei umleiten“ bzw. Anschluss `FILE:` → `test.prn`.
3. Webinterface → **System** → „Testdruck mit Treiberdatei“ → Datei wählen → „An Drucker senden“.
   Alternativ: `curl --data-binary @test.prn -H "Content-Type: application/octet-stream" http://<ip>/api/diag/raw`
4. Die Antwort zeigt `result`: `printed` (Drucker meldet „Druck abgeschlossen“), `printer_error`,
   `sent_no_confirmation` oder `transfer_failed`, dazu alle Meldungen des Druckers.

## Fehlerbehebung

### Neustart-Schleife mit `E BOD: Brownout detector was triggered`

Die Versorgungsspannung bricht ein, sobald das WLAN startet (Funkkalibrierung, Stromspitzen von
mehreren hundert mA). Ursache ist praktisch immer die Stromversorgung:

- anderes, **kurzes USB-Kabel** verwenden (dünne/lange Kabel haben zu viel Widerstand)
- anderen USB-Port am PC (Rückseite) oder einen **aktiven USB-Hub** bzw. ein 5-V-Netzteil (≥ 1 A)
- Drucker **abziehen**, solange das Problem besteht
- falls eine Lötbrücke „USB-OTG“ geschlossen ist: prüfen, ob am nativen Port etwas Strom zieht
- notfalls einen Elko (z. B. 470–1000 µF) zwischen 5 V und GND des Boards

Ab Firmware 0.2.1 startet das Board nach zwei Brownouts in Folge im **Sicherheitsmodus** ohne
WLAN. Die USB-Diagnose über die serielle Konsole (`usb`, `usb_refresh`, `usb_read`) funktioniert
dann trotzdem; `wifi_start` versucht das WLAN erneut.

### Drucker angesteckt, aber keinerlei USB-Meldung im Log

Ab 0.2.2 erscheint beim Anstecken mindestens `D (...) HUB: Root port reset` und danach die
Enumeration (`ENUM: ...`). Kommt **gar nichts**, sieht der ESP32 keinen Pull-up des Druckers:

1. Drucker eingeschaltet? Kabel ab- und wieder anstecken, Log beobachten.
2. Board **stromlos**: Durchgang Mini-B Pin 3 → **GPIO20**, Pin 2 → **GPIO19**, Pin 5 → **GND**,
   Pin 1 → **5V**. Kein Durchgang zwischen den Adern untereinander.
3. Board an, Drucker an und gesteckt: Spannung gegen GND messen an
   - **D+ (GPIO20)**: ca. 3,0–3,6 V → Full-Speed-Gerät meldet sich korrekt an
   - **D− (GPIO19)**: ca. 0 V
   - D− hoch und D+ ≈ 0 V → **D+ und D− vertauscht**
   - beide ≈ 0 V → Drucker sieht kein VBUS oder ist nicht im USB-Modus
4. Ohne Messgerät: die beiden Datenadern an **GPIO19/GPIO20 probeweise tauschen** – die Aderfarben
   sind nicht genormt, ein Vertauschen ist der häufigste Fehler (ohne Erkennung, ohne Fehlermeldung).

## Serielle Konsole

| Befehl | Funktion |
|---|---|
| `usb` | vollständigen USB-Bericht ausgeben |
| `usb_refresh` | IEEE-1284-ID und Port-Status neu abfragen, Bericht ausgeben |
| `usb_read` | einmal vom Bulk-IN-Endpoint lesen (1 s), Hexdump |
| `jobs [cancel <id>\|retry <id>]` | Druckwarteschlange anzeigen, Auftrag abbrechen/wiederholen |
| `raw_allow [any\|a.b.c.d/nn]` | Port 9100 auf ein Netz/eine Adresse beschränken |
| `brother_status` | Brother-Statusabfrage (`ESC i S`): Medium, Breite, Fehler, Phase |
| `raw_send [on\|off]` | Testdruck-Upload (`/api/diag/raw`) bis zum Neustart abschalten/einschalten (Standard: an) |
| `status` | Drucker, WLAN, Speicher |
| `wifi_set <ssid> [passwort]` | WLAN speichern und verbinden |
| `wifi_start` | WLAN und Webserver starten (nach Sicherheitsmodus) |
| `wifi_clear` | WLAN-Daten löschen, Neustart in den Setup-Modus |
| `ap_info` | Setup-WLAN (SSID, Adresse) anzeigen |
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
| POST | `/api/print/raw` | druckfertige Daten in die Warteschlange (202 + Job-ID); `X-Job-Name`, `X-Job-Key` (keine Doppeldrucke) |
| GET | `/api/jobs` | alle Aufträge + Statistik |
| GET/DELETE | `/api/jobs/{id}` | Auftrag abfragen / abbrechen (wartend) |
| POST | `/api/jobs/{id}/retry` | fehlgeschlagenen Auftrag erneut drucken |
| GET | `/api/printer/status` | Brother-Status live abfragen (`?cached=1`: letzter Stand ohne Druckerzugriff) |
| GET/POST | `/api/printer/settings` | im Drucker gespeicherte Einstellungen lesen / setzen (`command_mode=1&recovery=0`), ab 0.4.2 |
| POST | `/api/wifi` | WLAN setzen – **nur über Setup-WLAN** |
| GET | `/api/wifi/scan` | WLANs in Reichweite – **nur über Setup-WLAN** |
| POST | `/api/system/ota` | Firmware-Update (Rumpf: `brother_printserver.bin`) – **nur aus dem Heimnetz**, ab 0.5.0 |
| POST | `/api/system/restart` | Neustart – **nur über Setup-WLAN** |
| POST | `/api/diag/raw` | Treiberdatei unverändert an den Drucker (Testdruck) |

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
├── flasher/               Browser-Flasher-Seite (GitHub Pages, ESP Web Tools)
├── tools/                 package_firmware.py (Images, manifest.json, Prüfsummen)
├── .github/workflows/     CI: Build, Tests, Release, Pages
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
