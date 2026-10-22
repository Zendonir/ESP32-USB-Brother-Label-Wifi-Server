# Bekannte Einschränkungen (Stand Phase 4)

## Stand der Hardware-Tests

Am 21.10.2026 auf einem ESP32-S3-DevKitC-1 (N16R8) mit einem Brother TD-2020A getestet:
Start, WLAN, Webinterface, USB-Erkennung, Deskriptorbericht, Printer-Class-Anfragen sowie
Trennen/Wiederverbinden funktionieren (Details im [Testplan](testplan.md)).
Ein Testdruck mit einer Datei des Originaltreibers wurde gedruckt; der Drucker hat über Bulk IN
„Druck läuft“, „Druck abgeschlossen“ und „empfangsbereit“ gemeldet.
**Noch nicht getestet:** Verhalten ohne Papier/mit offenem Deckel, große Aufträge, Abbruch, Dauerbetrieb.

## Funktionsumfang dieser Phase

- Port 9100 (RAW) und `POST /api/print/raw` mit gemeinsamer Warteschlange (ab 0.4.0); ab 0.4.1
  mit Auftragsname und Idempotenzschlüssel (`X-Job-Name`, `X-Job-Key`) für den
  Lebensmittel-Server. Der Schlüssel wird nur erkannt, solange der Auftrag noch in der Liste
  (24 Einträge, RAM) steht. Noch nicht:
  P-touch-Template-API, API-Key (Phase 5/7), IPP/AirPrint.
- Aufträge werden vollständig im PSRAM zwischengespeichert (max. 4 MiB je Auftrag, 6 MiB gesamt),
  bevor sie gedruckt werden – so erreicht eine abgebrochene Übertragung nie den Drucker.
- Port 9100 nimmt Verbindungen nacheinander an (weitere warten im Backlog); da nur empfangen und
  nicht gedruckt wird, ist das schnell. Kein SNMP, kein bidirektionaler Kanal zu Windows.
- Die Auftragsliste lebt im RAM (24 Einträge) und geht bei einem Neustart verloren; Zeiten sind
  Laufzeit seit Start (noch keine Uhrzeit/SNTP).
- Brother-Befehle (ab 0.3.0): nur die dokumentierte Statusabfrage (Invalidate, `ESC @`, `ESC i S`)
  und der Abbruch eines unvollständigen Auftrags (Invalidate + `ESC @`). Keine eigenen Raster- oder
  Template-Daten.
- Testdruck nur über `/api/diag/raw` mit einer Datei des Originaltreibers; ohne Authentifizierung
  (wie später Port 9100: Druckdaten werden aus dem lokalen Netz angenommen). Abschaltbar mit
  `raw_send off` auf der seriellen Konsole.
  Während des Testdrucks blockiert der Webserver bis zu 30 s (Warten auf die Druckermeldungen).
- Ein herstellerspezifisches Interface (Klasse 0xFF) wird nur angezeigt, nicht belegt,
  solange `CONFIG_USB_PRINTER_ALLOW_VENDOR_INTERFACE` aus ist.
- Nur ein USB-Gerät, direkt angeschlossen (keine Hubs).
- Nur die aktive USB-Konfiguration wird ausgelesen (Gerät mit `bNumConfigurations > 1`
  wird im Bericht markiert).
- Keine statische IP, kein Hostname-Editor im Web (Phase 6). Die WLAN-Suche gibt es nur im Setup-WLAN.
- Kein API-Key: WLAN-Daten und Neustart nur über das Setup-WLAN. Druckaufträge, Druckereinstellungen
  und das **Firmware-Update** nimmt das Gerät von jedem im Heimnetz an (nicht über das Setup-WLAN).
  Wer im Heimnetz ist, kann also eine andere Firmware aufspielen – sie muss nur den Projektnamen
  `brother_printserver` tragen. Kein Secure Boot, keine Signaturprüfung.
- Firmware-Update: laufende Druckaufträge (RAM) gehen beim Neustart verloren.

## Technische Grenzen

- ESP32-S3 USB-OTG: nur Full Speed (12 Mbit/s) – für Etikettendruck ausreichend.
- `usb_transfer_t.timeout_ms` wird von ESP-IDF nicht ausgewertet; Timeouts sind in der
  Firmware umgesetzt. Hängt ein **Control**-Transfer (EP0), kann er über die öffentliche API
  nicht abgebrochen werden; weitere Control-Anfragen werden dann bis zum Abstecken abgelehnt
  (Logmeldung `control request … timed out`).
- Eine erfolgreiche USB-Übertragung bestätigt **nicht**, dass ein Etikett gedruckt wurde.
- Im Mischbetrieb (Setup-AP + Verbindungsversuche zum Heim-WLAN) wechselt der AP kurz den
  Kanal; Clients am Setup-AP können dabei kurz getrennt werden. Die Firmware versucht dann nur
  alle 15 s neu zu verbinden.
- Das WLAN-Passwort liegt unverschlüsselt im NVS (keine Flash-/NVS-Verschlüsselung).
  Wer physischen Zugriff auf das Board hat, kann es auslesen.
- Das Setup-WLAN ist **offen** (ab 0.5.0). Solange es läuft (keine WLAN-Verbindung: erste
  Einrichtung oder 120 s ohne Verbindung), kann jeder in Reichweite die WLAN-Daten setzen und
  das Gerät damit in ein fremdes Netz schicken. Der Verkehr im Setup-WLAN ist unverschlüsselt –
  auch das eingegebene Heim-WLAN-Passwort.
- Captive Portal: Manche Systeme öffnen die Seite nicht selbst (z. B. mit fest eingestelltem
  privatem DNS oder VPN). Dann `http://192.168.4.1/` von Hand öffnen.
- Die BOOT-Taste (GPIO0) als WLAN-Reset ist für DevKitC-1-kompatible Boards angenommen;
  bei abweichender Belegung `CONFIG_APP_RESET_BUTTON_GPIO` anpassen oder `-1` setzen.

## Umgebung

- Die Espressif-Komponentenregistry war aus der Entwicklungsumgebung nicht erreichbar; der
  Build wurde mit `espressif/mdns` v1.14.0 aus dem Git-Repository `esp-protocols` geprüft.
  Bei Ihnen lädt `idf.py build` die Komponente automatisch aus der Registry.
