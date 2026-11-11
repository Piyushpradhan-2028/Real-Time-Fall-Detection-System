<<<<<<< HEAD
# Fall Detection System: Step-by-Step Guide

ESP32 wearable + GSM SMS alert + web dashboard
Gandhi Institute of Engineering and Technology University, Odisha, Dept. of CSE

---

## 1. What is in this project

```
fall-detection-system/
├── wokwi-simulation/          <- NO HARDWARE? run everything in the Wokwi simulator (see its README.md)
├── README.md                  <- this guide
├── .vscode/extensions.json    <- VS Code recommends PlatformIO
├── firmware/                  <- code that runs on the ESP32
│   ├── platformio.ini
│   ├── include/config.h       <- YOU EDIT THIS (Wi-Fi, server IP, phone number)
│   └── src/main.cpp
└── server/                    <- website + API that runs on your PC
    ├── server.js
    ├── simulate.js            <- fake device, for testing without hardware
    ├── package.json
    └── public/index.html      <- the dashboard
```

**How the pieces talk to each other**

```
 MPU6050 + GPS + button + buzzer + LED
              │
            ESP32 ──(Wi-Fi)──> Server on your PC ──> Dashboard in browser
              │
          SIM800L GSM ──(SMS)──> Emergency contact's phone
```

You can do **Part A (software only)** first, with no hardware, to see the dashboard working.
Then do **Part B (hardware)** when your components are connected.

---

## 2. Install the software (one time)

| Software | Download | Check it works |
|---|---|---|
| VS Code | https://code.visualstudio.com | open it |
| Node.js 18 or newer (LTS) | https://nodejs.org | open a terminal and run `node -v` |
| PlatformIO IDE extension | In VS Code: Extensions (Ctrl+Shift+X), search **PlatformIO IDE**, Install | a small ant-head icon appears on the left bar |

After installing PlatformIO, restart VS Code. The first start takes a few minutes because it sets itself up.
You may also need the USB driver for your ESP32 board (**CP210x** or **CH340**, depending on the board).

---

## 3. PART A: Run the dashboard (no hardware needed)

1. Unzip `fall-detection-system.zip`.
2. In VS Code: **File → Open Folder** → choose `fall-detection-system`.
3. Open a terminal: **Terminal → New Terminal**.
4. Install and start the server:

   ```bash
   cd server
   npm install
   npm start
   ```
   You should see: `Fall detection server running: http://localhost:3000`
5. Open **http://localhost:3000** in your browser. At first it shows **"Waiting for a device..."**. This is normal: nothing has sent data yet.

   **Quickest way to see it working:** click **Start demo device** at the bottom of the page (or start the server with `npm run demo`). Then use **Simulate fall** and **Simulate cancelled alarm**. No hardware and no extra terminals needed. The steps below do the same thing with the separate simulator.
6. Open a **second terminal** (click the `+` in the terminal panel) and run:

   ```bash
   cd server
   npm run simulate
   ```
   The dashboard now shows the device **Online** with a battery level. Leave this running.
7. Open a **third terminal** and trigger test events:

   ```bash
   cd server
   npm run simulate:fall      # red "FALL CONFIRMED" banner + a row in the table
   npm run simulate:cancel    # a "Cancelled" row
   ```
8. Click the **Administrator** tab to see device status, battery and the list of past events.
   The **Emergency contact** tab shows live status and the last location on a map.

Stop any running terminal with **Ctrl+C**.

---

## 4. PART B: Hardware setup

### 4.1 Parts list
- ESP32 DevKit
- MPU6050 accelerometer
- NEO-6M GPS module
- SIM800L GSM module + a 2G-enabled SIM card with SMS balance
- Active buzzer, LED, 220 Ω resistor, push button
- Li-ion battery (3.7 V), 2 × 100 kΩ resistors (battery divider)
- A separate power source for the SIM800L: **3.7–4.2 V, 2 A peak** (LiPo directly, or a buck converter) with a 1000 µF capacitor across its power pins

> **Important:** do **not** power the SIM800L from the ESP32's 3.3 V pin. It will keep resetting and SMS will fail.

### 4.2 Wiring

| Module pin | Connect to ESP32 |
|---|---|
| MPU6050 VCC | 3.3 V |
| MPU6050 GND | GND |
| MPU6050 SDA | GPIO 21 |
| MPU6050 SCL | GPIO 22 |
| Buzzer (+) | GPIO 25 (other leg to GND) |
| LED (+) through 220 Ω | GPIO 26 (LED − to GND) |
| Cancel button | one leg GPIO 27, other leg GND |
| SIM800L TXD | GPIO 16 |
| SIM800L RXD | GPIO 17 (use a 1 kΩ + 2 kΩ divider if your module is not 3.3 V tolerant) |
| NEO-6M TXD | GPIO 32 |
| NEO-6M RXD | GPIO 33 |
| NEO-6M VCC / GND | 3.3 V (or 5 V, depending on your module) / GND |
| Battery + → 100 kΩ → GPIO 34 → 100 kΩ → GND | battery level reading |

**All grounds must be connected together**, including the SIM800L's separate power supply.

### 4.3 Find your PC's IP address
The ESP32 needs your PC's address on the Wi-Fi network (not `localhost`).

- **Windows:** open Command Prompt, run `ipconfig`, and look for **IPv4 Address** (like `192.168.1.50`)
- **Mac / Linux:** run `ifconfig` or `ip a`

Your PC and the ESP32 must be on the **same Wi-Fi network**.
If Windows shows a firewall popup when the server starts, click **Allow access**.

### 4.4 Edit the settings
Open `firmware/include/config.h` and change:

```cpp
#define WIFI_SSID    "YOUR_WIFI_NAME"
#define WIFI_PASS    "YOUR_WIFI_PASSWORD"
#define SERVER_URL   "http://192.168.1.50:3000"   // your PC's IP from step 4.3
#define API_KEY      "change-me"                  // must match the server
#define WEARER_NAME  "Piyush"
static const char* EMERGENCY_NUMBERS[] = { "+919876543210" };  // with country code
```
Use a Wi-Fi network at 2.4 GHz (the ESP32 does not support 5 GHz).

To use a custom API key, start the server like this (and put the same key in `config.h`):
- Windows PowerShell: `$env:API_KEY="mysecret"; npm start`
- Mac / Linux: `API_KEY=mysecret npm start`

### 4.5 Upload the firmware
1. In VS Code, **File → Open Folder** and open the **`firmware`** folder itself (PlatformIO needs `platformio.ini` at the top level).
2. Wait for PlatformIO to finish loading (bottom status bar).
3. Connect the ESP32 with a USB data cable.
4. Click the **→ (Upload)** button in the bottom blue bar.
   If upload hangs on "Connecting....", hold the **BOOT** button on the ESP32 until it starts.
5. Click the **plug icon (Serial Monitor)**, baud 115200.

You should see:
```
=== Fall Detection System ===
MPU6050: OK
GSM:     OK
Monitoring started
```
and hear **one beep**. **Five beeps** means the MPU6050 was not found (check SDA/SCL wiring).

Make sure the server (`npm start`) is running. Within about 10 seconds the dashboard shows the device **Online**.

---

## 5. Test a fall

Wear or hold the device so that the **Z axis of the MPU6050 points up** when "standing" (a board lying flat counts as standing).

**Desk demo:**
1. Hold the board flat.
2. Lift it a little and drop it onto its side (free-fall, then impact, then lying still).
3. Keep it still on its side. After about 2.5 seconds, the **buzzer and LED start blinking**. This is the 10-second countdown.

Then try both outcomes:

- **Cancel:** press the button within 10 seconds. You hear two short beeps. The dashboard logs a **Cancelled** event.
- **Real alert:** do nothing for 10 seconds. The ESP32 sends an SMS like:
  `FALL ALERT: Piyush may have fallen and needs help. Location: https://maps.google.com/?q=19.080000,83.820000`
  The dashboard shows a red banner and a **Fall confirmed** row. The device re-arms after 30 seconds, or when you press the button.

**Normal movements** (walking, sitting down, shaking the board while keeping it upright) should **not** start the countdown, because the posture/stillness check fails.

**GSM works without Wi-Fi:** turn off your router (or the PC's server) and repeat the test. The SMS still goes out. The dashboard event arrives later, once Wi-Fi is back.

The Serial Monitor shows what the detector decides, for example:
```
[DETECT] Impact 3.10 g - verifying posture
[DETECT] still=0.08 g  tilt=82 deg
[FALL] Possible fall! Press button within 10 s to cancel
```

---

## 6. How the detection works (for your viva)

| Step | What happens |
|---|---|
| 1. Monitoring | MPU6050 is read 50 times per second (US-01) |
| 2. Impact check | Free-fall (< 0.5 g) then impact (> 2.5 g), or a very hard impact (> 3.5 g) |
| 3. Double-check (US-02) | Wait 1.5 s, then for 1 s check: body is **lying** (tilt > 55°) and **still** |
| 4. Countdown (US-03) | Buzzer + LED blink for 10 s; button cancels |
| 5. Alert (US-04) | SMS with Google Maps link through the GSM module |
| 6. Dashboard (US-05/06/07) | Status, battery, online state and event history over Wi-Fi |

Networking runs in a separate task on the second ESP32 core, so a slow Wi-Fi never delays fall detection.

---

## 7. Adjusting sensitivity

All settings are at the bottom of `firmware/include/config.h`.

| Problem | Change |
|---|---|
| Too many false alarms | raise `IMPACT_G` (e.g. 3.0) or `TILT_DEG` (e.g. 65) |
| Real falls are missed | lower `IMPACT_G` (e.g. 2.0), raise `STILL_DEV_G` (e.g. 0.4) |
| Lying position not recognised | change `UPRIGHT_AXIS` (0 = X, 1 = Y, 2 = Z) to match how the board is mounted |
| GPS has no fix indoors | keep `USE_DEMO_LOCATION 1` for demos; set to `0` for real use |

After changing anything, upload again.

---

## 8. Troubleshooting

| Problem | What to try |
|---|---|
| `npm` or `node` not recognised | Reinstall Node.js, then restart VS Code |
| `npm install` fails | Check internet; delete `node_modules` and run again |
| Dashboard says "Waiting for a device" / "No device has reported yet" | No device has sent data yet. Click **Start demo device** on the page, or run `npm run demo` / `npm run simulate`. The Wokwi simulation cannot send to this page. |
| Demo buttons are missing | Demo buttons only show when you open the page on the same PC as the server (`http://localhost:3000`). To allow other devices, start with `DEMO=1 npm start`. |
| Dashboard says "Device offline" | Wrong `SERVER_URL` or IP; firewall blocking port 3000; PC and ESP32 on different networks; API key mismatch |
| Serial Monitor shows nothing | Wrong COM port or cable is charge-only; press the EN (reset) button |
| `MPU6050: NOT FOUND` | Check SDA = 21, SCL = 22, 3.3 V and GND |
| `GSM: NOT RESPONDING` | SIM800L power (needs 2 A), common ground, TX/RX not swapped |
| SMS `FAILED` | SIM has balance and 2G signal; number includes country code (+91…); network LED blinks about every 3 s when registered |
| Map not showing | The map needs internet access in your browser |
| Upload fails | Hold BOOT while uploading; try another USB cable/port; install CP210x/CH340 driver |

---

## 9. Notes

- This is a student prototype, **not** a certified medical alert device.
- The dashboard has no login. Use it on a local network only, and change `API_KEY` from the default.
- Data (events and device status) is stored in `server/data/db.json`. Delete that file to reset history.
=======
# Real-Time-Fall-Detection-System
>>>>>>> 986d7a53909d4b325325cc17518b17ff267091dc
