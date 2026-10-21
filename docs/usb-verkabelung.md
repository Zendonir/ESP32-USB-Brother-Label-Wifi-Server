# USB-Verkabelung und VBUS-Versorgung

## Standardaufbau: Druckerkabel direkt angelötet

```
 PC / Netzteil ──► [USB-C "COM"]  ESP32-S3-DevKitC-1 (N16R8)      [USB-C "USB"]  ← bleibt FREI
                   Strom, Flashen,
                   Konsole           5V ─────── rot   (VBUS) ─┐
                                     GND ────── schwarz (GND) ─┤ USB-Kabel ── Mini-B ──► Brother TD-2020A
                                     GPIO19 ─── weiß  (D−)   ─┤                          (eigenes Netzteil)
                                     GPIO20 ─── grün  (D+)   ─┘
```

Kein OTG-Adapter und keine Lötbrücke nötig: **„USB-OTG“ und „IN-OUT“ bleiben offen.**

### Material
- USB-A-auf-Mini-B-Kabel, möglichst kurz (der A-Stecker wird abgeschnitten)
- optional: selbstrückstellende Sicherung (PTC, ca. 500 mA) für die VBUS-Ader, Schrumpfschlauch

### Schritte
1. **Adern bestimmen:** A-Stecker abschneiden, ca. 3 cm abisolieren. Mit dem Durchgangsprüfer
   jede Ader gegen den Mini-B-Stecker prüfen – Farben sind nicht genormt.
   Mini-B-Pinbelegung (Stecker): 1 = VBUS, 2 = D−, 3 = D+, 4 = ID (frei), 5 = GND.
   Die Abschirmung (Geflecht/blanker Draht) kommt zusätzlich an GND.
2. **Anlöten** an die Stiftleiste (Beschriftung auf dem Board prüfen):
   VBUS → **5V**, GND → **G/GND**, D− → **19**, D+ → **20**.
   D− und D+ nicht vertauschen; die beiden Adern verdrillt und kurz lassen (< 20 cm).
   Widerstände sind nicht nötig (die Pull-downs für den Host-Betrieb sind im ESP32-S3).
3. **Zugentlastung:** Kabel mit Kabelbinder oder Heißkleber am Board fixieren, damit die Lötstellen
   nicht abreißen.
4. **Prüfen vor dem Einschalten:** kein Kurzschluss zwischen 5V und GND (Durchgangsprüfer).
5. **Messen:** Board über **COM** versorgen, Drucker noch **nicht** angesteckt. Am Mini-B-Stecker
   Pin 1 gegen Pin 5 messen: erwartet ca. 4,5–5,0 V (bei offener „IN-OUT“-Brücke wegen der Diode
   etwas unter 5 V).
6. Drucker einschalten, Mini-B einstecken → in der Konsole erscheint `device attached …`.

### Regeln für den Betrieb
- In die USB-C-Buchse **„USB“** kommt **nichts** – sie hängt an denselben Leitungen GPIO19/20.
- Strom wahlweise über **COM** (Entwicklung, mit Konsole) **oder** später ein 5-V-Netzteil am
  **5V**-Pin (Dauerbetrieb). Nicht beides gleichzeitig, solange nicht gemessen ist, ob das Board
  eine Rückstromdiode zwischen COM-Buchse und 5V-Pin hat.
- Der TD-2020A ist laut Brother self-powered; er nutzt VBUS vermutlich nur zur Erkennung. Der
  tatsächliche Wert steht nach der Enumeration im Bericht (`bMaxPower`).

---

Der folgende Teil beschreibt die frühere Variante mit **OTG-Adapter** an der „USB“-Buchse.

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

**Binghe ESP32-S3-DevKitC-1 N16R8:** Die Buchsen sind mit **„COM“** und **„USB“** beschriftet (vom Nutzer
bestätigt). „COM“ = USB-UART-Wandler (Strom, Flashen, Konsole), „USB“ = nativer USB-Port
(GPIO19/20) für OTG-Adapter und Drucker. Das Board hat eine Lötbrücke **„USB-OTG“**
(vom Nutzer bestätigt), die 5 V auf VBUS der „USB“-Buchse legt – siehe „Ohne VBUS – Optionen“, Punkt 1.
Außerdem gibt es eine Brücke **„IN-OUT“**. Bei vergleichbaren Boards überbrückt sie die Diode
zwischen USB-5-V und dem 5-V-Pin (Pin dann auch als Ausgang nutzbar, aber ohne Rückstromschutz).
Für diesen Aufbau wird sie **nicht** benötigt; die genaue Beschaltung ist für diese Revision nicht
belegt.

Im Auslieferungszustand sind **beide Brücken offen** (vom Nutzer bestätigt). Die „USB“-Buchse liefert
dann kein VBUS; für den Drucker muss die Brücke **„USB-OTG“** geschlossen werden, „IN-OUT“ bleibt offen.

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

## Alternative: Kabel direkt an die Stiftleiste löten

Statt USB-C-Buchse, Lötbrücke und OTG-Adapter kann ein USB-Kabel (z. B. ein aufgeschnittenes
USB-A-auf-Mini-B-Kabel, A-Seite abgeschnitten) direkt angelötet werden. Elektrisch ist das dieselbe
Verbindung wie die „USB“-Buchse:

| Kabelader (übliche Farbe) | Board-Pin |
|---|---|
| VBUS (rot) | **5V** |
| GND (schwarz) | **GND** |
| D− (weiß) | **GPIO19** |
| D+ (grün) | **GPIO20** |

- Aderfarben sind nicht garantiert – vorher mit dem Durchgangsprüfer gegen den Mini-B-Stecker prüfen.
- D+/D− kurz halten (möglichst < 20 cm) und miteinander verdrillt führen.
- GPIO19/20 sind dieselben Leitungen wie die „USB“-Buchse: dort dann **nichts** zusätzlich einstecken.
- Die Spannung am 5V-Pin vorher messen (bei offener „IN-OUT“-Brücke liegt sie wegen der Diode etwas
  unter 5 V); optional eine selbstrückstellende Sicherung (PTC, ca. 500 mA) in die VBUS-Ader.

## Schritt 3 – Drucker anschließen

1. Drucker mit eigenem Netzteil einschalten.
2. Kabel USB-A (Adapter) → Mini-B (Drucker) stecken.
3. In der seriellen Konsole erscheint `device attached: ... VID=0x.... PID=0x....` und
   danach der vollständige Deskriptorbericht.
