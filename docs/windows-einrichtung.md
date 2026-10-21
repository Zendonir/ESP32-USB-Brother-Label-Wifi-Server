# Windows 11: Drucker über den ESP32 einrichten (Port 9100)

Der ESP32 nimmt Druckaufträge wie ein Netzwerkdrucker („RAW“, Port 9100) an und gibt sie
unverändert per USB an den TD-2020A weiter. Die Druckdaten erzeugt der **originale
Brother-Treiber** auf dem PC.

## Voraussetzungen
- ESP32 im WLAN, IP-Adresse bekannt (Webinterface → Übersicht, z. B. `192.168.3.177`).
  Tipp: im Router eine **feste IP** (DHCP-Reservierung) für den ESP32 einstellen.
- Brother-Treiber für den **TD-2020** (der TD-2020A meldet sich per USB als TD-2020) von der
  Brother-Supportseite.

## 1. Treiber installieren
Den Brother-Treiber installieren. Fragt das Setup nach dem Anschluss, „später verbinden“ bzw.
eine beliebige Option wählen – der Anschluss wird im nächsten Schritt geändert. Ist der Drucker
schon (z. B. vom USB-Test) als „Brother TD-2020“ vorhanden, diesen verwenden.

## 2. Anschluss auf den ESP32 umstellen
1. *Einstellungen → Bluetooth & Geräte → Drucker & Scanner → Brother TD-2020 →
   Druckereigenschaften* → Reiter **Anschlüsse**.
2. **Hinzufügen… → Standard TCP/IP Port → Neuer Anschluss…**
3. **Druckername oder IP-Adresse:** die IP des ESP32 (oder `brother-td2020.local`, wenn mDNS im
   Netz funktioniert). Portname beliebig, z. B. `ESP32-TD2020`.
4. Windows sucht kurz und meldet „Zusätzliche Portinformationen erforderlich“ → **Benutzerdefiniert
   → Einstellungen…**:
   - Protokoll: **Raw**
   - Portnummer: **9100**
   - **„SNMP-Status aktiviert“ ausschalten** (der ESP32 beantwortet kein SNMP)
5. Fertig stellen, den neuen Port in der Liste anhaken → **Übernehmen**.
6. Im Reiter **Anschlüsse** die Option **„Bidirektionale Unterstützung aktivieren“ ausschalten**
   (über RAW gibt es keinen Rückkanal zum PC).

## 3. Testen
- **Testseite drucken** (Reiter *Allgemein*) oder ein Etikett aus P-touch Editor/einer
  beliebigen Anwendung drucken.
- Im Webinterface → **Aufträge** erscheint der Auftrag:
  - `gedruckt` – der Drucker hat „Druck abgeschlossen“ gemeldet
  - `wartet auf Drucker` – z. B. Deckel offen / kein Papier: nach Beheben druckt er automatisch
  - `fehlgeschlagen` – Fehler während des Drucks (der Drucker verwirft den Auftrag);
    mit **Wiederholen** erneut drucken
  - `unvollständig` – die Übertragung vom PC brach ab; es wurde nichts gedruckt

## Hinweise
- Windows zeigt einen Auftrag als „gedruckt“, sobald er an den ESP32 übertragen ist. Ob er
  wirklich gedruckt wurde, zeigt das Webinterface.
- Mehrere PCs können gleichzeitig drucken; die Aufträge werden nacheinander gedruckt, nie vermischt.
- Port 9100 hat (wie jeder Netzwerkdrucker) keine Anmeldung. Zugriff einschränken auf der
  seriellen Konsole, z. B. `raw_allow 192.168.3.0/24` (`raw_allow any` hebt es auf).
