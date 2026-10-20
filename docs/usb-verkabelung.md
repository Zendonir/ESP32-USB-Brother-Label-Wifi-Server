# USB-Verkabelung und VBUS-Versorgung

```
 Netzteil 5 V ≥ 1 A ──► [USB-C "COM/UART"]  ESP32-S3 DevKitC-1  [USB-C "USB/OTG"] ──► USB-C-OTG-Adapter
                                    (Konsole, Flashen)              (GPIO19 D−, GPIO20 D+)      │
                                                                                                ▼
                                                              USB-A ── Kabel ── Mini-B  Brother TD-2020A
                                                                                         (eigenes Netzteil)
```

## Warum VBUS wichtig ist

Ein USB-Gerät meldet sich erst an, wenn es an seinem Anschluss 5 V (VBUS) sieht. Der ESP32-S3
selbst schaltet kein VBUS. **Ohne 5 V am OTG-Adapter erscheint der Drucker nie** – die
Firmware zeigt dann „Seit dem Start wurde kein USB-Gerät erkannt“, es gibt keinerlei
`ENUM`-Logmeldungen.

Der TD-2020A hat ein eigenes Netzteil. Wie viel Strom er über VBUS zieht, steht nach der
Enumeration im Bericht (`bmAttributes` self-/bus-powered, `bMaxPower`). Bis dahin ist der
Wert unbekannt.

## Schritt 1 – Ports identifizieren

**Heemol ESP32-S3 N16R8:** Die Buchsen sind mit **„COM“** und **„USB“** beschriftet (vom Nutzer
bestätigt). „COM“ = USB-UART-Wandler (Strom, Flashen, Konsole), „USB“ = nativer USB-Port
(GPIO19/20) für OTG-Adapter und Drucker. Das Board hat eine Lötbrücke **„USB-OTG“**
(vom Nutzer bestätigt), die 5 V auf VBUS der „USB“-Buchse legt – siehe „Ohne VBUS – Optionen“, Punkt 1.

Bei anderen Boards:

1. Board ohne Drucker per USB an den PC.
2. Port, an dem ein Wandler (CH340/CH343/CP210x) erscheint = **UART/COM** → Stromversorgung,
   Flashen, Konsole.
3. Port, an dem „USB JTAG/serial debug unit“ (VID 0x303A) erscheint = **nativer USB/OTG**
   (GPIO19/20) → hier kommt der OTG-Adapter hin.

Hinweis: Nach dem Flashen dieser Firmware ist der native Port USB-Host; dort erscheint am PC
nichts mehr. Flashen daher immer über den UART-Port.

## Schritt 2 – VBUS messen (ohne Drucker!)

1. Board nur über den **UART-Port** versorgen.
2. OTG-Adapter in den nativen Port stecken, **kein** Kabel zum Drucker.
3. Multimeter zwischen Pin 1 (VBUS) und Pin 4 (GND) der USB-A-Buchse des Adapters.

| Ergebnis | Bedeutung | Vorgehen |
|---|---|---|
| ca. 4,7–5,2 V | VBUS wird ausgegeben | weiter mit Schritt 3 |
| 0 V | Board gibt kein VBUS aus | Abschnitt „Ohne VBUS“ |
| deutlich < 4,5 V | Diodenabfall oder schwaches Netzteil | Netzteil/Kabel prüfen; Diode-Spannungsabfall ist meist unkritisch, Messwert notieren |

## Ohne VBUS – Optionen

Bitte **nicht** einfach 5 V an beliebige Pins löten. Die Optionen in absteigender Präferenz:

1. **Lötbrücke auf dem Board** (häufig „USB-OTG“ beschriftet, bei Nachbauten verbreitet):
   Verbindet die 5-V-Schiene mit VBUS des nativen Ports. Nur verwenden, wenn die Beschriftung
   oder der Schaltplan der Revision das eindeutig belegt. Danach erneut messen.
   Achtung: Bei geschlossener Brücke **nie** gleichzeitig ein PC-Kabel am nativen Port stecken.
2. **Separat versorgtes VBUS** über eine Schutzschaltung: 5 V aus derselben Quelle wie das
   Board über eine selbstrückstellende Sicherung (PTC, z. B. 500 mA) und eine Diode bzw.
   einen Load-Switch mit Rückstromschutz auf VBUS des Adapters. Die Masse ist gemeinsam,
   D+/D− bleiben unverändert.
3. Ein aktiver USB-Hub ist mit ESP-IDF v5.5 nur mit experimenteller Hub-Unterstützung
   möglich und wird in dieser Phase **nicht** empfohlen.

Melden Sie bitte das Messergebnis und die Board-Beschriftungen (Foto beider Seiten), dann
kann die passende Variante konkret festgelegt werden.

## Schritt 3 – Drucker anschließen

1. Drucker mit eigenem Netzteil einschalten.
2. Kabel USB-A (Adapter) → Mini-B (Drucker) stecken.
3. In der seriellen Konsole erscheint `device attached: ... VID=0x.... PID=0x....` und
   danach der vollständige Deskriptorbericht.
