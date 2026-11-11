/*
 * Fall Detection System - ESP32 firmware
 * Gandhi Institute of Engineering and Technology University, Odisha
 *
 * Sensors / modules: MPU6050 (I2C), NEO-6M GPS, SIM800L GSM, buzzer, LED, cancel button
 *
 * Flow:  MONITORING -> VERIFYING -> COUNTDOWN (10 s, cancel button) -> ALERT_SENT -> MONITORING
 *   US-01  continuous monitoring (50 Hz accelerometer)
 *   US-02  double-check: impact + (free-fall) then lying posture + stillness
 *   US-03  10 s buzzer/LED countdown with cancel button
 *   US-04  SMS with Google Maps link over GSM (works without Wi-Fi)
 *   US-05/06/07  telemetry + events sent to the web dashboard over Wi-Fi (separate task,
 *                never blocks fall detection)
 */
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <TinyGPSPlus.h>
#include <math.h>
#include "config.h"

enum State { MONITORING = 0, VERIFYING, COUNTDOWN, ALERT_SENT };
static const char* STATE_NAMES[] = {"MONITORING", "VERIFYING", "COUNTDOWN", "ALERT_SENT"};

constexpr uint8_t MPU_ADDR = 0x68;
constexpr float   ACC_LSB  = 4096.0f;  // +-8g range

HardwareSerial &gsm = Serial2;
HardwareSerial &gpsSerial = Serial1;
TinyGPSPlus gps;

volatile State state = MONITORING;
bool mpuOk = false;
volatile bool gsmOk = false;

// ---- motion ----
float ax, ay, az, svm;
uint8_t  freefallCount = 0;
uint32_t lastFreefall = 0;
bool     freefallValid = false;
uint32_t verifyStart = 0, lastSample = 0;
float    devSum = 0, tiltSum = 0;
uint16_t verifyN = 0;

// ---- countdown / alert ----
uint32_t countdownStart = 0, lastBlink = 0, alertAt = 0;
bool blinkOn = false;

// ---- location ----
volatile bool everFix = false;
volatile double lastLat = 0, lastLon = 0;

// ---- event queue (shared with network task) ----
struct PendingEvent { char type[12]; double lat, lon; bool fix, sms; uint32_t at; };
constexpr int QMAX = 8;
PendingEvent evq[QMAX];
int qn = 0;
SemaphoreHandle_t qMux;

// =====================================================================
// MPU6050 (raw I2C, no library needed)
// =====================================================================
void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool mpuInit() {
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);  // PWR_MGMT_1: wake up
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  writeReg(0x1C, 0x10);  // accel +-8g
  writeReg(0x1A, 0x03);  // low-pass filter ~44 Hz
  return true;
}

bool readMotion() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)MPU_ADDR, 6) != 6) return false;
  int16_t raw[3];
  for (int i = 0; i < 3; i++) {
    uint8_t h = Wire.read();
    uint8_t l = Wire.read();
    raw[i] = (int16_t)((h << 8) | l);
  }
  ax = raw[0] / ACC_LSB;
  ay = raw[1] / ACC_LSB;
  az = raw[2] / ACC_LSB;
  svm = sqrtf(ax * ax + ay * ay + az * az);
  return true;
}

// angle (degrees) between body "up" axis and gravity: 0 = upright, 90 = lying
float tiltDegrees() {
  float up = (UPRIGHT_AXIS == 0) ? ax : (UPRIGHT_AXIS == 1) ? ay : az;
  if (svm < 0.1f) return 0;
  float c = fabsf(up) / svm;
  if (c > 1) c = 1;
  return acosf(c) * 57.2958f;
}

// =====================================================================
// GPS
// =====================================================================
void feedGps() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
  if (gps.location.isValid() && gps.location.isUpdated()) {
    lastLat = gps.location.lat();
    lastLon = gps.location.lng();
    everFix = true;
  }
}

bool getLocation(double &lat, double &lon) {
  if (everFix) { lat = lastLat; lon = lastLon; return true; }
#if USE_DEMO_LOCATION
  lat = DEMO_LAT; lon = DEMO_LON; return true;
#else
  return false;
#endif
}

// =====================================================================
// GSM (SIM800L, AT commands)
// =====================================================================
bool gsmWait(const char *expect, uint32_t timeoutMs) {
  String buf;
  uint32_t t = millis();
  while (millis() - t < timeoutMs) {
    while (gsm.available()) {
      buf += (char)gsm.read();
      if (buf.indexOf(expect) >= 0) return true;
      if (buf.indexOf("ERROR") >= 0) return false;
    }
    feedGps();
    delay(5);
  }
  return false;
}

bool gsmCmd(const char *cmd, const char *expect = "OK", uint32_t timeoutMs = 2000) {
  while (gsm.available()) gsm.read();
  gsm.print(cmd);
  gsm.print("\r");
  return gsmWait(expect, timeoutMs);
}

bool gsmInit() {
  gsm.begin(GSM_BAUD, SERIAL_8N1, PIN_GSM_RX, PIN_GSM_TX);
  bool ok = false;
  for (int i = 0; i < 5 && !ok; i++) {
    ok = gsmCmd("AT");
    if (!ok) delay(500);
  }
  if (!ok) return false;
  gsmCmd("ATE0");
  gsmCmd("AT+CMGF=1");          // text mode
  gsmCmd("AT+CSCS=\"GSM\"");
  return true;
}

bool sendSms(const char *number, const char *text) {
  if (!gsmCmd("AT+CMGF=1")) return false;
  while (gsm.available()) gsm.read();
  gsm.print("AT+CMGS=\"");
  gsm.print(number);
  gsm.print("\"\r");
  if (!gsmWait(">", 5000)) return false;
  gsm.print(text);
  gsm.write(26);  // Ctrl+Z
  return gsmWait("+CMGS:", 20000);
}

bool sendAlertSms(const char *text) {
  bool any = false;
  for (int i = 0; i < EMERGENCY_COUNT; i++) {
    bool ok = sendSms(EMERGENCY_NUMBERS[i], text);
    Serial.printf("[GSM] SMS to %s: %s\n", EMERGENCY_NUMBERS[i], ok ? "sent" : "FAILED");
    any = any || ok;
    delay(1000);
  }
  return any;
}

// =====================================================================
// Events + battery
// =====================================================================
void queueEvent(const char *type, bool fix, double lat, double lon, bool sms) {
  xSemaphoreTake(qMux, portMAX_DELAY);
  if (qn == QMAX) {
    memmove(&evq[0], &evq[1], sizeof(PendingEvent) * (QMAX - 1));
    qn--;
  }
  PendingEvent &e = evq[qn++];
  strncpy(e.type, type, sizeof(e.type) - 1);
  e.type[sizeof(e.type) - 1] = 0;
  e.fix = fix; e.lat = lat; e.lon = lon; e.sms = sms; e.at = millis();
  xSemaphoreGive(qMux);
}

float readBatteryV() {
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BATT);
  return (mv / 8.0f) * BATT_DIVIDER / 1000.0f;
}

int batteryPct(float v) {
  int p = (int)((v - 3.3f) / (4.2f - 3.3f) * 100.0f);
  return constrain(p, 0, 100);
}

// =====================================================================
// Outputs
// =====================================================================
void alarmOutputs(bool on) {
  digitalWrite(PIN_LED, on);
  digitalWrite(PIN_BUZZER, on);
}

void beep(int times, int ms) {
  for (int i = 0; i < times; i++) {
    alarmOutputs(true); delay(ms);
    alarmOutputs(false); delay(ms);
  }
}

bool buttonPressed() {
  static bool last = HIGH;
  static uint32_t t = 0;
  bool cur = digitalRead(PIN_BTN);
  if (cur != last && millis() - t > 30) {
    t = millis();
    last = cur;
    if (cur == LOW) return true;
  }
  return false;
}

// =====================================================================
// Fall logic
// =====================================================================
void startCountdown(uint32_t now) {
  state = COUNTDOWN;
  countdownStart = now;
  lastBlink = now;
  blinkOn = true;
  alarmOutputs(true);  // buzzer + LED immediately (acceptance: within a few seconds)
  Serial.println("[FALL] Possible fall! Press button within 10 s to cancel");
}

void confirmFall() {
  Serial.println("[FALL] Confirmed - sending alert");
  alarmOutputs(true);
  double lat = 0, lon = 0;
  bool hasLoc = getLocation(lat, lon);

  char sms[220];
  if (hasLoc)
    snprintf(sms, sizeof(sms),
             "FALL ALERT: %s may have fallen and needs help. Location: https://maps.google.com/?q=%.6f,%.6f",
             WEARER_NAME, lat, lon);
  else
    snprintf(sms, sizeof(sms),
             "FALL ALERT: %s may have fallen and needs help. Location unavailable (no GPS fix). Please call now.",
             WEARER_NAME);

  if (!gsmOk) gsmOk = gsmInit();
  bool sent = gsmOk && sendAlertSms(sms);

  digitalWrite(PIN_BUZZER, LOW);  // LED stays on until re-armed
  queueEvent("confirmed", hasLoc, lat, lon, sent);
  state = ALERT_SENT;
  alertAt = millis();
}

void cancelAlert() {
  Serial.println("[FALL] Cancelled by wearer");
  alarmOutputs(false);
  double lat = 0, lon = 0;
  bool hasLoc = getLocation(lat, lon);
  queueEvent("cancelled", hasLoc, lat, lon, false);
  beep(2, 80);
  state = MONITORING;
  freefallValid = false;
}

void runDetector(uint32_t now) {
  if (state == MONITORING) {
    // free-fall detection (needs 2 consecutive low samples)
    if (svm < FREEFALL_G) {
      if (++freefallCount >= 2) { lastFreefall = now; freefallValid = true; }
    } else {
      freefallCount = 0;
    }
    if (svm > IMPACT_G) {
      bool afterFall = freefallValid && (now - lastFreefall < FREEFALL_WINDOW);
      if (afterFall || svm > HARD_IMPACT_G) {
        Serial.printf("[DETECT] Impact %.2f g - verifying posture\n", svm);
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
      tiltSum += tiltDegrees();
      verifyN++;
    } else if (el >= SETTLE_MS + MEASURE_MS) {
      float dev = verifyN ? devSum / verifyN : 9;
      float tilt = verifyN ? tiltSum / verifyN : 0;
      Serial.printf("[DETECT] still=%.2f g  tilt=%.0f deg\n", dev, tilt);
      if (dev < STILL_DEV_G && tilt > TILT_DEG) startCountdown(now);
      else { Serial.println("[DETECT] Normal activity - ignored"); state = MONITORING; }
    }
  }
}

// =====================================================================
// Network task (core 0) - Wi-Fi + HTTP never blocks fall detection
// =====================================================================
bool httpPost(const char *path, const char *body) {
  HTTPClient http;
  http.begin(String(SERVER_URL) + path);
  http.setConnectTimeout(2000);
  http.setTimeout(3000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", API_KEY);
  int code = http.POST(String(body));
  http.end();
  return code >= 200 && code < 300;
}

void networkTask(void *) {
  uint32_t lastTelemetry = 0, lastWifiTry = 0, lastFlushTry = 0;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  char body[320];

  for (;;) {
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (now - lastWifiTry > 15000) {
        lastWifiTry = now;
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASS);
      }
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    // 1) flush queued events
    if (qn > 0 && now - lastFlushTry > 2000) {
      lastFlushTry = now;
      PendingEvent e;
      xSemaphoreTake(qMux, portMAX_DELAY);
      e = evq[0];
      xSemaphoreGive(qMux);
      snprintf(body, sizeof(body),
               "{\"deviceId\":\"%s\",\"type\":\"%s\",\"fix\":%s,\"lat\":%.6f,\"lon\":%.6f,\"smsSent\":%s,\"ageMs\":%lu}",
               DEVICE_ID, e.type, e.fix ? "true" : "false", e.lat, e.lon, e.sms ? "true" : "false",
               (unsigned long)(millis() - e.at));
      if (httpPost("/api/events", body)) {
        xSemaphoreTake(qMux, portMAX_DELAY);
        if (qn > 0) { memmove(&evq[0], &evq[1], sizeof(PendingEvent) * (QMAX - 1)); qn--; }
        xSemaphoreGive(qMux);
        Serial.println("[NET] Event delivered");
      }
    }

    // 2) periodic telemetry
    if (now - lastTelemetry >= TELEMETRY_MS) {
      lastTelemetry = now;
      float v = readBatteryV();
      double lat = 0, lon = 0;
      bool fix = everFix;
      if (fix) { lat = lastLat; lon = lastLon; }
      snprintf(body, sizeof(body),
               "{\"deviceId\":\"%s\",\"battery\":%.2f,\"batteryPct\":%d,\"fix\":%s,\"lat\":%.6f,\"lon\":%.6f,"
               "\"state\":\"%s\",\"rssi\":%d,\"gsmOk\":%s}",
               DEVICE_ID, v, batteryPct(v), fix ? "true" : "false", lat, lon,
               STATE_NAMES[state], WiFi.RSSI(), gsmOk ? "true" : "false");
      httpPost("/api/telemetry", body);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// =====================================================================
// Arduino entry points
// =====================================================================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  alarmOutputs(false);
  qMux = xSemaphoreCreateMutex();

  Serial.println("\n=== Fall Detection System ===");
  mpuOk = mpuInit();
  Serial.printf("MPU6050: %s\n", mpuOk ? "OK" : "NOT FOUND (check wiring)");
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  gsmOk = gsmInit();
  Serial.printf("GSM:     %s\n", gsmOk ? "OK" : "NOT RESPONDING (SMS will retry on alert)");

  xTaskCreatePinnedToCore(networkTask, "net", 8192, nullptr, 1, nullptr, 0);

  beep(mpuOk ? 1 : 5, 150);  // 1 beep = ready, 5 beeps = sensor error
  Serial.println("Monitoring started");
}

void loop() {
  uint32_t now = millis();
  feedGps();
  bool pressed = buttonPressed();

  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;
    if (mpuOk && readMotion()) runDetector(now);
  }

  if (state == COUNTDOWN) {
    if (pressed) {
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
    if (pressed || millis() - alertAt >= ALERT_COOLDOWN) {
      digitalWrite(PIN_LED, LOW);
      state = MONITORING;
      Serial.println("[SYS] Re-armed");
    }
  }
}
