# Phase 1 – Hardware, Protokolle, Ausgangslage

Stand: 20.10.2026. Dieses Dokument trennt strikt zwischen **verifiziert**, **aus Dokumentation
übernommen, aber nicht am Gerät geprüft** und **unbekannt – muss gemessen werden**.

## 1. Ergebnis in Kürze

| Thema | Status |
|---|---|
| ESP-IDF USB-Host-Library (ESP32-S3, Full Speed, Bulk/Control) | verifiziert (API gegen ESP-IDF v5.5.6 kompiliert) |
| Brother USB-Vendor-ID 0x04F9 | aus öffentlicher `usb.ids`-Datenbank; TD-2020A dort **nicht** gelistet |
| Product-ID des TD-2020A | **unbekannt** – wird von der Diagnose-Firmware ausgelesen |
| USB Printer Class (0x07) am TD-2020A | **unbekannt** – wird von der Diagnose-Firmware ausgewertet |
| Bulk-IN / Statuskanal | **unbekannt** |
| Brother-Raster-/Template-Befehle für TD-2020A | **nicht verifiziert** (Dokumente aus dieser Umgebung nicht abrufbar, s. 4.) |
| VBUS-Versorgung am Heemol-Board | **unbekannt** – muss am Board gemessen werden (s. 3.) |
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

## 3. Board: Heemol ESP32-S3 N16R8 DevKitC-1

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
  Ports legt. **Ob das bei Ihrer Heemol-Revision so ist, ist unbekannt.**

Messung, die vor dem Anschluss des Druckers nötig ist: siehe [usb-verkabelung.md](usb-verkabelung.md).

## 4. Drucker: Brother TD-2020A

### Was öffentlich bekannt ist

- Brother bietet für die TD-2000-Serie eine *Raster Command Reference*
  (`cv_td2000_eng_raster_101.pdf`, Modelle TD-2020/2120N/2130N und TD-2030A/2125N/…)
  mit einem Anhang „USB Specifications“ sowie eine P-touch-Template-Referenz an.
- Ein Händlereintrag nennt für den TD-2020A zusätzlich eine ZPL-II-Emulation.
  Ob das auf Ihre Variante zutrifft und wie umgeschaltet wird, ist unklar.

### Was hier **nicht** verifiziert werden konnte

Die Brother-Server (`download.brother.com`) und Archivkopien waren aus der Build-Umgebung
gesperrt (HTTP 403). Deshalb wurden **keine** Brother-Befehle implementiert – weder
Statusabfrage noch Initialisierung, Raster- oder Template-Befehle. Für Phase 3 (Testetikett)
und Phase 5 (Template-API) wird eines davon benötigt:

1. das PDF der Raster Command Reference bzw. P-touch-Template-Referenz für den TD-2020A
   (bitte ins Repository unter `docs/vendor/` legen oder den Inhalt bereitstellen), **oder**
2. eine mit dem originalen Windows-Treiber erzeugte Druckdatei („Ausgabe in Datei“), die
   unverändert gesendet wird.

Für den Windows-Weg (RAW, Port 9100) sind **keine** Brother-Befehle in der Firmware nötig:
Der Treiber erzeugt die Daten, der ESP32 reicht sie durch.

### USB-Printer-Class-Standardanfragen (keine Brother-Befehle)

Die Diagnose nutzt nur Anfragen aus der USB Printer Class Spezifikation 1.1:

| Anfrage | bmRequestType | bRequest | wValue | wIndex | Zweck |
|---|---|---|---|---|---|
| GET_DEVICE_ID | 0xA1 | 0x00 | Konfig-Index (0) | Interface << 8 \| AltSetting | IEEE-1284-ID (MFG/MDL/CMD …) |
| GET_PORT_STATUS | 0xA1 | 0x01 | 0 | Interface | Paper Empty / Select / Not Error |

Die IEEE-1284-ID (Feld `CMD:`) zeigt, welche Befehlssprachen der Drucker selbst meldet –
genau die Information, die für Phase 3 fehlt.

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
2. Wird der Drucker überhaupt enumeriert? (Log-Zeile `device attached`)
3. VID/PID, bcdDevice, Hersteller-/Produkt-String
4. Interfaces: Klasse, Protokoll, Alternate Settings, Endpoints (Bulk OUT/IN, MPS)
5. Self-/Bus-powered, bMaxPower
6. IEEE-1284-Device-ID (insbesondere `CMD:`-Feld)
7. Port-Status mit/ohne eingelegte Etiketten, Deckel offen/zu
8. Ob ein Massenspeicher-Interface vorhanden ist (Editor-/Speichermodus)
