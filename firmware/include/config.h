#pragma once
// ============ EDIT THESE BEFORE UPLOADING ============
#define WIFI_SSID    "YOUR_WIFI_NAME"
#define WIFI_PASS    "YOUR_WIFI_PASSWORD"
// IP of the PC running the server (run `ipconfig` / `ifconfig`), NOT localhost
#define SERVER_URL   "http://192.168.1.50:3000"
#define API_KEY      "change-me"            // must match server API_KEY
#define DEVICE_ID    "fall-01"
#define WEARER_NAME  "Piyush"

// Emergency contacts (international format). Add more if you like.
static const char* EMERGENCY_NUMBERS[] = { "+910000000000" };
static const int   EMERGENCY_COUNT     = sizeof(EMERGENCY_NUMBERS) / sizeof(EMERGENCY_NUMBERS[0]);

// If 1 and the GPS has never had a fix, use DEMO_LAT/LON (handy indoors for demos)
#define USE_DEMO_LOCATION 1
#define DEMO_LAT 19.0800
#define DEMO_LON 83.8200

// ============ PINS ============
constexpr int PIN_SDA     = 21;   // MPU6050 SDA
constexpr int PIN_SCL     = 22;   // MPU6050 SCL
constexpr int PIN_BUZZER  = 25;   // active buzzer (+)
constexpr int PIN_LED     = 26;   // LED (+) via 220R
constexpr int PIN_BTN     = 27;   // cancel button to GND (internal pull-up)
constexpr int PIN_GSM_RX  = 16;   // ESP32 RX2  <- SIM800L TXD
constexpr int PIN_GSM_TX  = 17;   // ESP32 TX2  -> SIM800L RXD
constexpr int PIN_GPS_RX  = 32;   // ESP32 RX1  <- NEO-6M TXD
constexpr int PIN_GPS_TX  = 33;   // ESP32 TX1  -> NEO-6M RXD
constexpr int PIN_BATT    = 34;   // battery voltage via 100k/100k divider

constexpr uint32_t GSM_BAUD = 9600;
constexpr uint32_t GPS_BAUD = 9600;
constexpr float BATT_DIVIDER = 2.0f;

// ============ FALL DETECTION TUNING ============
constexpr uint32_t SAMPLE_MS        = 20;     // 50 Hz
constexpr float    FREEFALL_G       = 0.5f;   // below this = falling
constexpr float    IMPACT_G         = 2.5f;   // above this = impact
constexpr float    HARD_IMPACT_G    = 3.5f;   // impact strong enough without free-fall
constexpr uint32_t FREEFALL_WINDOW  = 1000;   // impact must follow free-fall within this
constexpr uint32_t SETTLE_MS        = 1500;   // wait after impact
constexpr uint32_t MEASURE_MS       = 1000;   // then measure posture/stillness
constexpr float    STILL_DEV_G      = 0.25f;  // avg |g-1| below this = lying still
constexpr float    TILT_DEG         = 55.0f;  // body angle from upright = lying down
constexpr int      UPRIGHT_AXIS     = 2;      // 0=X 1=Y 2=Z: sensor axis that points up when standing
constexpr uint32_t COUNTDOWN_MS     = 10000;  // cancel window (US-03)
constexpr uint32_t ALERT_COOLDOWN   = 30000;  // ms before re-arming after alert
constexpr uint32_t TELEMETRY_MS     = 10000;
