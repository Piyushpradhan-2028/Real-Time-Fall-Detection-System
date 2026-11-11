# Fall Detection System: Wokwi Simulation (no hardware)

The whole project runs inside the **Wokwi** simulator. Nothing needs to be bought or wired.

| Real project part | In the simulator |
|---|---|
| ESP32 | Wokwi ESP32 DevKit |
| MPU6050 | Wokwi MPU6050 chip (has sliders) **plus** scripted fall / sit buttons |
| Buzzer, LED, cancel button | Wokwi buzzer, LED, push button |
| NEO-6M GPS | simulated in code: "fix" appears 3 s after start (Gunupur coordinates) |
| SIM800L GSM + SMS | simulated: AT commands and the received SMS are printed in the Serial Monitor |
| Web dashboard | 128x64 **OLED** (status, battery, GPS, counters) + dashboard messages and event history in the Serial Monitor |
| Battery level | potentiometer (turn the knob = battery %) |

> Wokwi has no SIM800L part, and its free plan cannot reach a server on your PC, which is why the SMS and dashboard are shown on the Serial Monitor and OLED. The firmware prints the same JSON it would send to the real server (`POST /api/telemetry` and `POST /api/events`).

---

## Option 1: wokwi.com in the browser (easiest, 2 minutes)

1. Go to **https://wokwi.com** and sign in (free).
2. Click **New Project → ESP32** (the plain "ESP32" Arduino template).
3. You will see tabs: `sketch.ino`, `diagram.json`, `libraries.txt`.
4. For each tab, delete the existing text and paste the matching file from this folder:
   - `sketch.ino` → paste the contents of `sketch.ino`
   - `diagram.json` → paste the contents of `diagram.json`
   - `libraries.txt` → paste the contents of `libraries.txt` (if the tab is missing, click the small **▼** next to the tabs and choose *New file*, name it `libraries.txt`)
5. Click the green **▶ Start the simulation** button.

The circuit appears (ESP32, MPU6050, OLED, buzzer, LED, 3 buttons, potentiometer). The first run takes a few seconds while the libraries download.

## Option 2: VS Code (Wokwi extension)

1. In VS Code, install the **PlatformIO IDE** and **Wokwi Simulator** extensions.
2. Open **this `wokwi-simulation` folder** in VS Code (File → Open Folder).
3. Build: click the PlatformIO ✓ (**Build**) button in the bottom bar. Wait for `SUCCESS`.
4. Press **F1** → type **Wokwi: Request a new License** → follow the browser login (free).
5. Press **F1** → **Wokwi: Start Simulator**.

The Serial Monitor output appears in the simulator's terminal panel.

---

## How to run the demo

You will see `Monitoring started` in the Serial Monitor, one beep, and the OLED showing **MONITORING**.

### A) A fall that is NOT cancelled (alert is sent)
1. Click the **SIM FALL** button (red). The Serial Monitor shows `[SIM] Scripted FALL`.
2. After about 2.5 seconds: `[DETECT] ... tilt=87 deg` and **`Possible fall!`**. The **buzzer sounds, the LED blinks, and the OLED shows `CANCEL 10s`** counting down.
3. Do nothing. After 10 s: `FALL CONFIRMED`, the **simulated SMS** appears in the Serial Monitor with the Google Maps link, and the OLED shows **FALL SENT!**
4. After 30 s (or pressing CANCEL) the system re-arms.

### B) A fall that IS cancelled (false alarm stopped)
1. Click **SIM FALL**, wait for `CANCEL 10s` on the OLED.
2. Click the **CANCEL** button (green) within 10 s. Two short beeps, `Cancelled by wearer - no SMS sent`, and the cancel counter on the OLED goes up.

### C) Normal activity is ignored (double-check, US-02)
1. Click **SIM SIT** (blue): a hard sit-down with a big jolt.
2. The Serial Monitor shows `Impact 3.8 g -> checking posture...` and then `Normal activity (not lying down) -> ignored`. No alarm.

### D) Use the real MPU6050 sliders instead
Click the **MPU6050 chip** in the diagram to open its sliders. At rest it should read about 1 g on Z (the OLED line `Acc 1.00g`). If the OLED shows `Acc 0.00g`, drag the **Z acceleration slider up to 1 g**.
To make a manual fall within 1 second: set all axes to ~0 (free-fall), then set X to about 3 g (impact), then set X = 1 g and Z = 0 (lying on its side) and leave it still. The scripted buttons do exactly this for you.

### E) Battery and event history
- Turn the **potentiometer**: the OLED battery % changes (also visible in the 10-second `[DASH] POST /api/telemetry` lines).
- Click inside the Serial Monitor, type **`h`** and press Enter to print the **event history** (US-07: time, type, location, SMS status).
- Serial keys also work as buttons: **f** = fall, **s** = sit, **c** = cancel.

---

## Mapping to your user stories

| Story | Where you see it in the simulation |
|---|---|
| US-01 Continuous monitoring | OLED `Acc ..g Tilt ..` updates live (50 Hz sampling) |
| US-02 Double-check before deciding | `[DETECT]` lines: fall accepted only if lying + still; SIM SIT is ignored |
| US-03 10 s countdown + cancel | OLED `CANCEL 10s`, blinking LED, buzzer, CANCEL button |
| US-04 SMS with location | `SMS RECEIVED` box in the Serial Monitor with the Google Maps link |
| US-05 Status + location | OLED status line and GPS coordinates |
| US-06 Device online + battery | OLED `WiFi:ok  BAT:..%`, `[DASH] telemetry` lines |
| US-07 Past fall events | type `h` in the Serial Monitor |
| GSM works without Wi-Fi | SMS is produced by the GSM path only; Wi-Fi is not needed |

## Troubleshooting

| Problem | Fix |
|---|---|
| `MPU6050: not found` | Re-paste `diagram.json`; the SIM FALL / SIM SIT buttons still work without it |
| OLED blank | Make sure `libraries.txt` contains the Adafruit GFX and SSD1306 libraries |
| `Acc 0.00g` all the time | Set the MPU6050 Z slider to 1 g (see D) |
| No sound from the buzzer | Click anywhere in the Wokwi window once (browsers block sound until you interact) and check your volume |
| `WiFi:--` on the OLED | Normal for a few seconds at start; the sim joins the `Wokwi-GUEST` network |
| Compile error about a library | Check `libraries.txt` has all 5 lines |
