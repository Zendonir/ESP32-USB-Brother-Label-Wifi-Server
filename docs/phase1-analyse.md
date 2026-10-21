# Phase 1 – Hardware, Protokolle, Ausgangslage

Stand: 20.10.2026. Dieses Dokument trennt strikt zwischen **verifiziert**, **aus Dokumentation
übernommen, aber nicht am Gerät geprüft** und **unbekannt – muss gemessen werden**.

## 1. Ergebnis in Kürze

| Thema | Status |
|---|---|
| ESP-IDF USB-Host-Library (ESP32-S3, Full Speed, Bulk/Control) | verifiziert (API gegen ESP-IDF v5.5.6 kompiliert) |
| Brother USB-Vendor-ID 0x04F9 | laut Brother Raster Command Reference (Anhang A) |
| Product-ID des TD-2020A | ✅ **am Gerät gemessen: 0x2055** – der TD-2020A meldet sich als „TD-2020“ (wie in der Raster-Referenz); alter Text: nicht dokumentiert (TD-2020 = 0x2055); wird von der Diagnose-Firmware ausgelesen |
| USB Printer Class, Bulk IN/OUT | laut Raster-Referenz für die TD-2000-Serie ja; für TD-2020A am Gerät zu bestätigen |
| Brother-Raster-Befehle | dokumentiert (Raster Command Reference 1.01, s. 4.); TD-2020A dort nicht namentlich genannt |
| P-touch-Template-Befehle | dokumentiert, **TD-2020A ausdrücklich genannt** (P-touch Template 2.0 Reference 3.08) |
| VBUS-Versorgung am Board | **unbekannt** – muss am Board gemessen werden (s. 3.) |
| Windows-RAW-Druck (Port 9100) | konzeptionell ohne Brother-Befehle möglich (Treiber erzeugt Daten) |

Konsequenz: Phase 2 liefert eine Diagnose-Firmware, die **nichts** über den Drucker voraussetzt
und alle benötigten Fakten am echten Gerät ermittelt.

## 2. ESP32-S3 und ESP-IDF

**Gewählte Version: ESP-IDF v5.5.x** (getestet: v5.5.6). Begründung:

- v5.5 ist der aktuelle stabile Zweig mit langer Wartung; die USB-Host-API (`usb/usb_host.h`)
  ist dort ausgereift.
- ESP-IDF v6.x ist ebenfalls verfügbar; die Kompatibilität wurde zusätzlich per Build geprüft
  (Ergebnis siehe README, Abschnitt „Build-Verifikation“).

Relevante Fakten der USB-Host-Library (geprüft in den Headern von v5.5.6):

- Ein OTG-Controller, **nur Low/Full Speed** (12 Mbit/s). Ein USB-2.0-Drucker muss sich also
  im Full-Speed-Modus melden – das ist für USB-2.0-Geräte vorgeschrieben.
- `usb_transfer_t.timeout_ms` ist laut Header *„currently not supported yet“*. Timeouts werden
  daher in der Firmware selbst umgesetzt (Semaphore mit Timeout, danach
  `usb_host_endpoint_halt/flush/clear` + `CLEAR_FEATURE(ENDPOINT_HALT)`).
- `usb_host_interface_claim()` sendet **kein** `SET_INTERFACE`; bei Alternate Setting ≠ 0
  erledigt das die Firmware.
- Standardgröße für Enumerations-Control-Transfers ist 256 Byte
  (`CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE`). Auf 1024 erhöht, damit lange
  Konfigurationsdeskriptoren die Enumeration nicht scheitern lassen.
- Der interne USB-PHY wird entweder vom USB-Serial-JTAG oder vom OTG-Controller genutzt.
  Die Konsole läuft deshalb **über UART0** (zweiter USB-C-Port mit USB-UART-Wandler).
- Externe Hubs werden von v5.5 nur experimentell unterstützt (`CONFIG_USB_HOST_HUBS_SUPPORTED`,
  standardmäßig aus). Der Drucker muss **direkt** angeschlossen werden.
- Es gibt keine Hardware-Schaltung von VBUS durch den ESP32-S3; `root_port_unpowered` steuert
  nur den Port-Zustand im Controller, nicht die 5-V-Leitung.

## 3. Board: ESP32-S3-DevKitC-1-Nachbau N16R8 (Binghe)

Ohne Schaltplan der konkreten Platinenrevision werden **keine** Annahmen getroffen.
Bekannt bzw. plausibel (bitte am Board prüfen):

- N16R8 = 16 MB Quad-SPI-Flash, 8 MB **Octal**-PSRAM (Modul ESP32-S3-WROOM-1-N16R8).
  Octal-PSRAM belegt GPIO35–37; diese dürfen nicht anderweitig benutzt werden.
- Zwei USB-C-Buchsen. Eine ist an GPIO19/20 (nativer USB), die andere an einen
  USB-UART-Wandler (CH340/CH343/CP210x). Zur Identifikation: am PC zeigt der native Port
  mit Werks-Firmware typischerweise „USB JTAG/serial debug unit“ (VID 0x303A), der andere
  den Namen des UART-Wandlers.
- **VBUS am nativen Port**: Beim Originaldesign von Espressif speist VBUS des nativen Ports
  das Board (über eine Diode) – es liefert dann **keine** 5 V nach außen. Viele Nachbauten
  haben eine Lötbrücke (oft mit „USB-OTG“ beschriftet), die 5 V auf den VBUS des nativen
  Ports legt. **Beim verwendeten Binghe-Board gibt es diese Brücke („USB-OTG“), sie ist im Auslieferungszustand offen (s. usb-verkabelung.md).**

Messung, die vor dem Anschluss des Druckers nötig ist: siehe [usb-verkabelung.md](usb-verkabelung.md).

## 4. Drucker: Brother TD-2020A

### Quellen

Beide Dokumente gibt es im **Brother Developer Center**
(<https://support.brother.com/g/s/es/dev/en/index.html?navi=offall>) bzw. auf der Brother-Support-
Seite des Modells unter *Handbücher → Software Developer's Manual*:

| Dokument | Datei | Abgedeckte Modelle |
|---|---|---|
| Raster Command Reference, Version 1.01 (2013) | `cv_td2000_eng_raster_101.pdf` | TD-2020/2120N/2130N, TD-2030A/2125N/2125NWB/2135N/2135NWB – **TD-2020A nicht genannt** |
| P-touch Template 2.0 Command Reference, Version 3.08 (2025) | `cv_rj_td_eng_ptemp_308.pdf` | u. a. TD-2020/2120N/2130N und **TD-2020A**/2030A/2125N/… |

Laut Nutzungsbedingungen dürfen die PDFs **nicht weitergegeben** werden. Sie gehören daher nicht
ins Repository; lokal können sie unter `docs/vendor/` liegen (per `.gitignore` ausgeschlossen).
Im Folgenden werden nur Fakten genannt, die für die Implementierung nötig sind.

### USB laut Raster-Referenz, Anhang A (TD-2000-Serie)

| Merkmal | Wert |
|---|---|
| USB | 1.1, Full Speed |
| Vendor-ID | 0x04F9 |
| Product-ID | TD-2020 0x2055, TD-2120N 0x2057, TD-2130N 0x2058, TD-2030A 0x20F6, TD-2125N 0x20F7, TD-2125NWB 0x20F8, TD-2135N 0x20F9, TD-2135NWB 0x20FA – **TD-2020A fehlt** |
| Klasse | Printer **oder** Mass Storage (Speichermodus) |
| Interfaces | 1, keine Alternate Settings |
| Versorgung | self-powered |
| Endpoint 1 | Bulk IN, 64 Byte (Status vom Drucker) |
| Endpoint 2 | Bulk OUT, 64 Byte (Befehle und Druckdaten) |
| Strings | Hersteller „Brother“ (Index 1), Seriennummer (Index 3) |

Die Diagnose-Firmware passt dazu ohne Änderung: Sie wählt die Printer-Class-Schnittstelle mit
Bulk OUT, nutzt Bulk IN für Status und markiert einen Massenspeicher-Modus.

**TD-2020A:** In der Template-Referenz hat er denselben Modellcode wie der TD-2020 („3“, 33h)
und steht in derselben Gerätegruppe. Daher ist zu **vermuten**, aber nicht belegt, dass er sich
per USB wie der TD-2020 verhält (eventuell auch PID 0x2055). Das klärt der erste
Diagnosebericht.

Unterstützte Druckersprachen des TD-2020A laut Template-Referenz (Anhang B): Raster, ESC/P,
P-touch Template, ZPL-II-Emulation, CPCL-Emulation; 203 dpi; USB und RS-232C.

### Für die nächsten Phasen relevante Befehle (Raster-Referenz, Kap. 3–5)

| Befehl | Bytes | Verwendung |
|---|---|---|
| Invalidate | `00` (200×) | bricht einen unvollständigen Auftrag ab, gefolgt von Initialize |
| Initialize | `1B 40` | Moduseinstellungen zurücksetzen, Druck abbrechen |
| Status information request | `1B 69 53` | Antwort **immer 32 Byte** über Bulk IN |

Statusantwort (32 Byte): Byte 0 = 80h, Byte 1 = 20h (Größe), Byte 2 = „B“, Byte 3 = Serien-
code „5“, Byte 4 = Modellcode (TD-2020: „3“), Byte 8 = Fehlerinfo 1 (Bit 0 kein Medium, Bit 1
Medienende, Bit 4 Drucker belegt), Byte 9 = Fehlerinfo 2 (Bit 0 falsches Medium, Bit 2
Kommunikationsfehler, Bit 4 Deckel offen, Bit 6 Medium lässt sich nicht transportieren, Bit 7
Systemfehler), Byte 10/17 = Medienbreite/-länge in mm, Byte 11 = Medientyp (00h keins,
4Ah Endlos, 4Bh Etiketten), Byte 18 = Statustyp, Byte 19–21 = Phase, Byte 22 = Meldung.

Wichtige Regeln aus der Referenz:

- Statusabfrage **einmal vor** dem Senden von Druckdaten; **nicht während** des Drucks – der
  Drucker meldet Fehler und Druckende dann **selbst** über Bulk IN.
- Nach Druckdaten darf bis zur Druckende-Meldung kein Befehl gesendet werden.
- Über USB beginnt der Drucker bei unkomprimierten Rasterdaten schon während des Empfangs
  (*concurrent printing*); Fehlerabläufe beschreiben die Flussdiagramme in Kap. 5.

Folgen für die Firmware: Das Ende eines Druckauftrags lässt sich über die Statusmeldungen auf
Bulk IN feststellen. Damit kann eine Warteschlange zwischen „übertragen“ und „vom Drucker als
gedruckt gemeldet“ unterscheiden. Das gilt erst, wenn es am Gerät bestätigt ist.

## 4a. Ergebnis der USB-Diagnose am echten Gerät (21.10.2026)

| Merkmal | gemessen |
|---|---|
| Vendor-/Product-ID | 0x04F9 / **0x2055** (identisch mit TD-2020) |
| Strings | Hersteller „Brother“, Produkt „TD-2020“, Seriennummer vorhanden |
| USB / Speed | bcdUSB 2.00, Full Speed, bMaxPacketSize0 64 |
| Konfiguration | 1 Konfiguration, 1 Interface, self-powered, bMaxPower 2 mA |
| Interface 0 | Printer Class 07/01/**02 (bidirektional)**, keine Alternate Settings |
| Endpoints | **0x81 Bulk IN** (64 Byte), **0x02 Bulk OUT** (64 Byte) |
| IEEE 1284 Device ID | `MFG:Brother;CMD:PT-CBP;MDL:TD-2020;CLS:PRINTER;CID:Brother TD Type3;` |
| GET_PORT_STATUS | 0x18 (selected, kein Fehler, Papier vorhanden) |

Das entspricht exakt Anhang A der Raster Command Reference für den TD-2020. Damit gilt diese
Referenz (inkl. Statusabfrage `ESC i S`, Modellcode „3“) für das vorhandene Gerät. Interface-Wahl,
Claim, Printer-Class-Anfragen sowie Trennen/Wiederverbinden funktionieren mit der Firmware.

## 5. Bewertung von MAXXTANG/ESP32S3_PrintServer

Geprüft wurde der Stand vom 21.05.2026 (2 Commits).

| Kriterium | Befund |
|---|---|
| Lizenz | MIT – Wiederverwendung wäre möglich |
| Framework | Arduino (ESP32-Arduino-Core), kein reines ESP-IDF-Projekt |
| USB-Host | nutzt intern die ESP-IDF-USB-Host-Library; Polling aus `loop()` |
| Interface-Wahl | nur Printer Class 0x07/0x01, Alternate Setting fest 0, Protokoll ignoriert |
| Bulk OUT | pro **MPS-Paket (64 Byte)** ein `transfer_alloc` + Submit + Free → sehr langsam |
| Timeout | nach 5 s wird der Transfer freigegeben, **obwohl er noch läuft** (Use-after-free-Risiko) |
| Netzwerk | Port 9100, eine Verbindung, kein Job-Konzept, keine Warteschlange |
| Erkennung | VID/PID-Anzeige, keine String-/Device-ID-Abfrage, keine vollständige Deskriptor-Ausgabe |
| Zielgerät | Brother HL-1110 (Laser), nicht TD-2020A |

**Entscheidung:** nicht als Grundlage übernommen. Es wurde kein Code kopiert. Die
Architektur (USB-Bulk-OUT-Weiterleitung von Port 9100) ist dieselbe Grundidee, wird aber
hier als ESP-IDF-Projekt mit Tasks, Timeouts, Recovery und Diagnose neu umgesetzt.

## 6. AirPrint / IPP – Vorab-Einschätzung

AirPrint verlangt IPP 2.0 inklusive Attribut-Verarbeitung, mDNS-Dienst `_ipp._tcp` mit
`_universal`-Subtyp und die Annahme von PWG-Raster/URF (und meist PDF). Der ESP32-S3 müsste
diese Rasterdaten in die Brother-Druckersprache umsetzen. Das setzt die verifizierte
Raster-Befehlsreferenz voraus (s. 4.); PDF-Rendering ist auf dem ESP32-S3 nicht sinnvoll.
**Empfehlung:** erst nach stabilem RAW-Druck als optionales Modul neu bewerten.

## 7. Am echten Gerät zu ermitteln (Phase 2 liefert das automatisch)

1. Spannung an VBUS des OTG-Adapters (Messung, s. Verkabelung)
   – Erwartung laut Referenz bei 3.–8.: PID ähnlich TD-2020 (0x2055), 1 Printer-Interface,
   Bulk IN 0x81 / Bulk OUT 0x02 (je 64 Byte), self-powered
2. Wird der Drucker überhaupt enumeriert? (Log-Zeile `device attached`)
3. VID/PID, bcdDevice, Hersteller-/Produkt-String
4. Interfaces: Klasse, Protokoll, Alternate Settings, Endpoints (Bulk OUT/IN, MPS)
5. Self-/Bus-powered, bMaxPower
6. IEEE-1284-Device-ID (insbesondere `CMD:`-Feld)
7. Port-Status mit/ohne eingelegte Etiketten, Deckel offen/zu
8. Ob ein Massenspeicher-Interface vorhanden ist (Editor-/Speichermodus)
