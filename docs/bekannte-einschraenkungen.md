# Bekannte Einschränkungen (Stand Phase 2)

## Nicht mit Hardware getestet

**Diese Firmware wurde bisher nur kompiliert und auf dem PC (Parser-Unit-Tests) getestet.**
Sie lief noch nicht auf einem ESP32-S3 und war noch nie mit einem Brother TD-2020A verbunden.
Alle Hardware-Tests im [Testplan](testplan.md) sind offen.

## Funktionsumfang dieser Phase

- **Kein Druck über Port 9100, keine REST-Druck-API, keine Warteschlange.** Folgt in Phase 4/5.
- Es werden **keine Brother-Befehle** gesendet (keine Statusabfrage `ESC i S`, keine
  Initialisierung, keine Templates), weil die Brother-Referenz für den TD-2020A nicht
  verifiziert werden konnte (siehe [phase1-analyse.md](phase1-analyse.md#4-drucker-brother-td-2020a)).
- `usb_printer_write()` (Bulk OUT mit Timeout/Recovery) ist implementiert, aber nur über die
  optionale Entwicklungsfunktion `/api/diag/raw` erreichbar (standardmäßig **aus**).
- Ein herstellerspezifisches Interface (Klasse 0xFF) wird nur angezeigt, nicht belegt,
  solange `CONFIG_USB_PRINTER_ALLOW_VENDOR_INTERFACE` aus ist.
- Nur ein USB-Gerät, direkt angeschlossen (keine Hubs).
- Nur die aktive USB-Konfiguration wird ausgelesen (Gerät mit `bNumConfigurations > 1`
  wird im Bericht markiert).
- Keine WLAN-Netzsuche, keine statische IP, kein Hostname-Editor im Web (Phase 6).
- Kein API-Key: Die Diagnose-API ist **lesend**; schreibende Endpunkte (WLAN, Neustart) sind
  nur über das Setup-WLAN erreichbar.
- OTA ist vorbereitet (zwei OTA-Partitionen, Rollback aktiv), aber noch kein Upload (Phase 7).

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
- WLAN-Passwort und Setup-AP-Passwort liegen unverschlüsselt im NVS (keine Flash-/NVS-
  Verschlüsselung in dieser Phase). Wer physischen Zugriff auf das Board hat, kann sie auslesen.
- Das Setup-AP-Passwort wird nur auf der seriellen Konsole angezeigt (Befehl `ap_info`).
  Ohne serielle Konsole ist die Ersteinrichtung in dieser Phase nicht möglich.
- Die BOOT-Taste (GPIO0) als WLAN-Reset ist für DevKitC-1-kompatible Boards angenommen;
  bei abweichender Belegung `CONFIG_APP_RESET_BUTTON_GPIO` anpassen oder `-1` setzen.

## Umgebung

- Die Espressif-Komponentenregistry war aus der Entwicklungsumgebung nicht erreichbar; der
  Build wurde mit `espressif/mdns` v1.14.0 aus dem Git-Repository `esp-protocols` geprüft.
  Bei Ihnen lädt `idf.py build` die Komponente automatisch aus der Registry.
