/*
 * Fall Detection System - WOKWI SIMULATION VERSION
 * Gandhi Institute of Engineering and Technology University, Odisha
 *
 * Everything runs in the simulator, no real hardware needed:
 *   MPU6050      -> real Wokwi chip (use its sliders) OR scripted fall/sit via buttons
 *   GPS (NEO-6M) -> simulated: gets a "fix" 3 s after start (Gunupur coordinates)
 *   GSM (SIM800L)-> simulated: AT commands + SMS text printed on the Serial Monitor
 *   Dashboard    -> SSD1306 OLED (status, battery, GPS, counters) + Serial event history
 *   Battery      -> potentiometer on GPIO34 (0-100 %)
 *
 * Buttons:  CANCEL (GPIO27)  |  SIM FALL (GPIO13)  |  SIM SIT (GPIO14)
 * Serial Monitor keys: f = fall, s = sit fast, c = cancel, h = event history
 */
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ---------------- pins ----------------
const int PIN_SDA = 21, PIN_SCL = 22;
const int PIN_BUZZER = 25, PIN_LED = 26;
const int PIN_CANCEL = 27, PIN_SIM_FALL = 13, PIN_SIM_SIT = 14;
const int PIN_BATT = 34;

// ---------------- settings ----------------
const char*    WEARER_NAME   = "Piyush";
const char*    CONTACT_NO    = "+919876543210";
const double   LAT = 19.0800, LON = 83.8200;     // demo GPS position (Gunupur)
const uint32_t GPS_FIX_AFTER = 3000;

const uint32_t SAMPLE_MS       = 20;
const float    FREEFALL_G      = 0.5f;
const float    IMPACT_G        = 2.5f;
const float    HARD_IMPACT_G   = 3.5f;
const uint32_t FREEFALL_WINDOW = 1000;
const uint32_t SETTLE_MS       = 1500;
const uint32_t MEASURE_MS      = 1000;
const float    STILL_DEV_G     = 0.25f;
const float    TILT_DEG        = 55.0f;
const int      UPRIGHT_AXIS    = 2;              // Z is "up" when standing
const uint32_t COUNTDOWN_MS    = 10000;
const uint32_t ALERT_COOLDOWN  = 30000;

// ---------------- objects / state ----------------
enum State { MONITORING = 0, VERIFYING, COUNTDOWN, ALERT_SENT };
const char* STATE_NAMES[] = {"MONITORING", "VERIFYING", "COUNTDOWN", "ALERT_SENT"};
enum Sim { SIM_NONE = 0, SIM_FALL, SIM_SIT };

Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_MPU6050 mpu;

State state = MONITORING;
Sim   simMode = SIM_NONE;
bool  mpuOk = false, oledOk = false;

float ax = 0, ay = 0, az = 1, svm = 1, tilt = 0;
uint8_t  freefallCount = 0;
uint32_t lastFreefall = 0, verifyStart = 0, lastSample = 0, simStart = 0;
bool     freefallValid = false;
float    devSum = 0, tiltSum = 0;
uint16_t verifyN = 0;
uint32_t countdownStart = 0, lastBlink = 0, alertAt = 0, lastDraw = 0, lastTelemetry = 0;
bool     blinkOn = false;

struct Evt { uint32_t t; bool confirmed; bool sms; bool fix; };
Evt hist[10];
int histN = 0, nConfirmed = 0, nCancelled = 0;

struct Btn { int pin; bool last; uint32_t t; };
Btn bCancel = {PIN_CANCEL, HIGH, 0};
Btn bFall   = {PIN_SIM_FALL, HIGH, 0};
Btn bSit    = {PIN_SIM_SIT, HIGH, 0};

// ---------------- helpers ----------------
bool btnPressed(Btn &b) {
  bool cur = digitalRead(b.pin);
  if (cur != b.last && millis() - b.t > 30) {
    b.t = millis();
    b.last = cur;
    if (cur == LOW) return true;
  }
  return false;
}

void buzzerBegin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_BUZZER, 2000, 8);
#else
  ledcSetup(0, 2000, 8);
  ledcAttachPin(PIN_BUZZER, 0);
#endif
}

void buzzerTone(int freq) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(PIN_BUZZER, freq);
#else
  ledcWriteTone(0, freq);
#endif
}

void alarmOutputs(bool on) {
  digitalWrite(PIN_LED, on);
  buzzerTone(on ? 2500 : 0);
}

void beep(int times, int ms) {
  for (int i = 0; i < times; i++) {
    alarmOutputs(true);  delay(ms);
    alarmOutputs(false); delay(ms);
  }
}

bool gpsFix() { return millis() >= GPS_FIX_AFTER; }
bool wifiOk() { return WiFi.status() == WL_CONNECTED; }

int batteryPct() {
  int p = map(analogRead(PIN_BATT), 0, 4095, 0, 100);
  return constrain(p, 0, 100);
}

float tiltDegrees() {
  float up = (UPRIGHT_AXIS == 0) ? ax : (UPRIGHT_AXIS == 1) ? ay : az;
  if (svm < 0.1f) return 0;
  float c = fabsf(up) / svm;
  if (c > 1) c = 1;
  return acosf(c) * 57.2958f;
}

// ---------------- scripted motion (buttons SIM FALL / SIM SIT) ----------------
void startSim(Sim m) {
  simMode = m;
  simStart = millis();
  Serial.println(m == SIM_FALL ? "\n[SIM] Scripted FALL: free-fall -> impact -> lying still"
                               : "\n[SIM] Scripted SIT-DOWN-FAST: hard sit, then upright and still");
}

bool simSample(uint32_t now) {
  if (simMode == SIM_NONE) return false;
  uint32_t t = now - simStart;
  float n = ((int)random(-30, 31)) / 1000.0f;
  if (simMode == SIM_FALL) {
    if (t < 400)      { ax = 0.05f + n; ay = 0.05f; az = 0.10f; }   // free-fall
    else if (t < 500) { ax = 3.2f; ay = 0.4f; az = 0.4f; }          // impact (~3.3 g)
    else              { ax = 1.0f + n; ay = n; az = 0.05f; }        // lying on its side
    if (t > 800 && state == MONITORING) simMode = SIM_NONE;        // done / cancelled / re-armed
  } else {
    if (t < 300)      { ax = 0.1f; ay = 0.1f; az = 1.0f + n; }
    else if (t < 400) { ax = 0.3f; ay = 0.3f; az = 3.8f; }          // hard sit
    else              { ax = n; ay = n; az = 1.0f + n; }            // upright, still
    if (t > 4500) simMode = SIM_NONE;
  }
  svm = sqrtf(ax * ax + ay * ay + az * az);
  tilt = tiltDegrees();
  return true;
}

bool readMotion(uint32_t now) {
  if (simSample(now)) return true;
  if (!mpuOk) return false;
  sensors_event_t a, g, tmp;
  mpu.getEvent(&a, &g, &tmp);
  ax = a.acceleration.x / 9.80665f;
  ay = a.acceleration.y / 9.80665f;
  az = a.acceleration.z / 9.80665f;
  svm = sqrtf(ax * ax + ay * ay + az * az);
  tilt = tiltDegrees();
  return true;
}

// ---------------- dashboard (OLED) ----------------
void drawDisplay(uint32_t now, const char *override = nullptr) {
  if (!oledOk) return;
  char l[28], buf[16];
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  snprintf(l, sizeof(l), "WiFi:%-4s   BAT:%3d%%", wifiOk() ? "ok" : "--", batteryPct());
  display.print(l);

  const char *s = "";
  if (override) s = override;
  else if (state == MONITORING) s = "MONITORING";
  else if (state == VERIFYING)  s = "CHECKING";
  else if (state == ALERT_SENT) s = "FALL SENT!";
  else {
    unsigned long rem = (COUNTDOWN_MS - (now - countdownStart) + 999) / 1000;
    snprintf(buf, sizeof(buf), "CANCEL %2lus", rem);
    s = buf;
  }
  display.setTextSize(2);
  display.setCursor(0, 12);
  display.print(s);

  display.setTextSize(1);
  display.setCursor(0, 33);
  snprintf(l, sizeof(l), "Acc %.2fg Tilt %3.0f", svm, tilt);
  display.print(l);
  display.setCursor(0, 43);
  if (gpsFix()) snprintf(l, sizeof(l), "GPS %.4f,%.4f", LAT, LON);
  else          snprintf(l, sizeof(l), "GPS searching...");
  display.print(l);
  display.setCursor(0, 54);
  snprintf(l, sizeof(l), "Fall:%d Canc:%d SMS:%s", nConfirmed, nCancelled, nConfirmed ? "ok" : "-");
  display.print(l);
  display.display();
}

// ---------------- events / SMS / dashboard payload ----------------
void logEvent(bool confirmed, bool sms, bool fix) {
  if (histN == 10) { memmove(&hist[0], &hist[1], sizeof(Evt) * 9); histN--; }
  Evt &e = hist[histN++];
  e.t = millis() / 1000; e.confirmed = confirmed; e.sms = sms; e.fix = fix;
  if (confirmed) nConfirmed++; else nCancelled++;
  // same JSON the real device POSTs to /api/events
  Serial.printf("[DASH] POST /api/events {\"deviceId\":\"fall-sim\",\"type\":\"%s\",\"fix\":%s,\"lat\":%.6f,\"lon\":%.6f,\"smsSent\":%s}\n",
                confirmed ? "confirmed" : "cancelled", fix ? "true" : "false",
                fix ? LAT : 0.0, fix ? LON : 0.0, sms ? "true" : "false");
}

void printHistory() {
  Serial.println("\n------------- EVENT HISTORY (US-07) -------------");
  if (histN == 0) Serial.println("(no events yet)");
  for (int i = 0; i < histN; i++) {
    Serial.printf("#%d  t=%lus  %-15s  %s  SMS:%s\n", i + 1, (unsigned long)hist[i].t,
                  hist[i].confirmed ? "FALL CONFIRMED" : "CANCELLED",
                  hist[i].fix ? "19.0800,83.8200" : "location n/a   ",
                  hist[i].confirmed ? (hist[i].sms ? "sent" : "failed") : "-");
  }
  Serial.println("--------------------------------------------------\n");
}

void simulatedSms(const char *number, const char *text) {
  Serial.println("[GSM] AT                 -> OK");                     delay(200);
  Serial.println("[GSM] AT+CMGF=1          -> OK");                     delay(200);
  Serial.printf ("[GSM] AT+CMGS=\"%s\" -> >\n", number);                delay(300);
  Serial.printf ("[GSM] %s <Ctrl+Z>\n", text);                          delay(600);
  Serial.println("[GSM] +CMGS: 1 -> OK  (sent over GSM, no Wi-Fi needed)");
  Serial.println("\n  +--------------- SMS RECEIVED ---------------+");
  Serial.printf ("  | To: %s\n", number);
  Serial.printf ("  | %s\n", text);
  Serial.println("  +--------------------------------------------+\n");
}

// ---------------- fall logic ----------------
void startCountdown(uint32_t now) {
  state = COUNTDOWN;
  countdownStart = now;
  lastBlink = now;
  blinkOn = true;
  alarmOutputs(true);
  Serial.println("[FALL] Possible fall! Buzzer + LED ON. Press CANCEL within 10 s");
}

void confirmFall() {
  Serial.println("[FALL] Not cancelled -> FALL CONFIRMED, sending SMS");
  alarmOutputs(true);
  drawDisplay(millis(), "SENDING SMS");
  bool fix = gpsFix();
  char sms[160];
  if (fix) snprintf(sms, sizeof(sms), "FALL ALERT: %s may have fallen and needs help. Location: https://maps.google.com/?q=%.6f,%.6f", WEARER_NAME, LAT, LON);
  else     snprintf(sms, sizeof(sms), "FALL ALERT: %s may have fallen. Location unavailable (no GPS fix). Please call now.", WEARER_NAME);
  simulatedSms(CONTACT_NO, sms);
  alarmOutputs(false);
  digitalWrite(PIN_LED, HIGH);
  logEvent(true, true, fix);
  state = ALERT_SENT;
  alertAt = millis();
}

void cancelAlert() {
  Serial.println("[FALL] Cancelled by wearer - no SMS sent");
  alarmOutputs(false);
  logEvent(false, false, gpsFix());
  beep(2, 80);
  state = MONITORING;
  freefallValid = false;
}

void runDetector(uint32_t now) {
  if (state == MONITORING) {
    if (svm < FREEFALL_G) {
      if (++freefallCount >= 2) { lastFreefall = now; freefallValid = true; }
    } else {
      freefallCount = 0;
    }
    if (svm > IMPACT_G) {
      bool afterFall = freefallValid && (now - lastFreefall < FREEFALL_WINDOW);
      if (afterFall || svm > HARD_IMPACT_G) {
        Serial.printf("[DETECT] Impact %.2f g -> checking posture...\n", svm);
        state = VERIFYING;
        verifyStart = now;
        devSum = 0; tiltSum = 0; verifyN = 0;
        freefallValid = false;
      }
    }
  } else if (state == VERIFYING) {
    uint32_t el = now - verifyStart;
    if (el >= SETTLE_MS && el < SETTLE_MS + MEASURE_MS) {
      devSum += fabsf(svm - 1.0f);
      tiltSum += tilt;
      verifyN++;
    } else if (el >= SETTLE_MS + MEASURE_MS) {
      float dev = verifyN ? devSum / verifyN : 9;
      float tl  = verifyN ? tiltSum / verifyN : 0;
      Serial.printf("[DETECT] movement=%.2f g, tilt=%.0f deg (need <%.2f and >%.0f)\n", dev, tl, STILL_DEV_G, TILT_DEG);
      if (dev < STILL_DEV_G && tl > TILT_DEG) startCountdown(now);
      else { Serial.println("[DETECT] Normal activity (not lying down) -> ignored"); state = MONITORING; }
    }
  }
}

void handleSerial(bool &pFall, bool &pSit, bool &pCancel) {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'f') pFall = true;
    else if (c == 's') pSit = true;
    else if (c == 'c') pCancel = true;
    else if (c == 'h') printHistory();
  }
}

// ---------------- Arduino entry points ----------------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_CANCEL, INPUT_PULLUP);
  pinMode(PIN_SIM_FALL, INPUT_PULLUP);
  pinMode(PIN_SIM_SIT, INPUT_PULLUP);
  buzzerBegin();
  alarmOutputs(false);

  Serial.println("\n=== Fall Detection System (Wokwi simulation) ===");
  Wire.begin(PIN_SDA, PIN_SCL);
  mpuOk = mpu.begin(0x68, &Wire);
  if (mpuOk) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);
  }
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  Serial.printf("MPU6050: %s | OLED: %s | GSM: simulated | GPS: simulated\n",
                mpuOk ? "OK" : "not found", oledOk ? "OK" : "not found");

  WiFi.mode(WIFI_STA);
  WiFi.begin("Wokwi-GUEST", "", 6);

  Serial.println("Press SIM FALL (or type f) to simulate a fall, SIM SIT (s) for a fast sit-down,");
  Serial.println("CANCEL (c) to cancel the alarm, h = event history.\n");
  beep(1, 150);
  Serial.println("Monitoring started");
}

void loop() {
  uint32_t now = millis();
  bool pCancel = btnPressed(bCancel);
  bool pFall = btnPressed(bFall);
  bool pSit = btnPressed(bSit);
  handleSerial(pFall, pSit, pCancel);

  if (state == MONITORING && simMode == SIM_NONE) {
    if (pFall) startSim(SIM_FALL);
    else if (pSit) startSim(SIM_SIT);
  }

  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;
    if (readMotion(now)) runDetector(now);
  }

  if (state == COUNTDOWN) {
    if (pCancel) {
      cancelAlert();
    } else {
      if (now - lastBlink >= 250) {
        lastBlink = now;
        blinkOn = !blinkOn;
        alarmOutputs(blinkOn);
      }
      if (now - countdownStart >= COUNTDOWN_MS) confirmFall();
    }
  } else if (state == ALERT_SENT) {
    if (pCancel || millis() - alertAt >= ALERT_COOLDOWN) {
      digitalWrite(PIN_LED, LOW);
      state = MONITORING;
      Serial.println("[SYS] Re-armed, monitoring again");
    }
  }

  if (now - lastDraw >= 250) { lastDraw = now; drawDisplay(now); }

  if (now - lastTelemetry >= 10000) {   // what the real device POSTs to /api/telemetry
    lastTelemetry = now;
    int p = batteryPct();
    Serial.printf("[DASH] POST /api/telemetry {\"deviceId\":\"fall-sim\",\"batteryPct\":%d,\"battery\":%.2f,\"state\":\"%s\",\"wifi\":%s}\n",
                  p, 3.3f + p * 0.009f, STATE_NAMES[state], wifiOk() ? "true" : "false");
  }
}
