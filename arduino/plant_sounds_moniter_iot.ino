/*
   ================================================================
   PLANT SOUND MONITORING SYSTEM v13
   Base: v7 (deviceCheckSequence fix + APLL warmup)

   การทำงานคร่าวๆ:
   - เริ่มต้นด้วยการตรวจอุปกรณ์ เช่น DHT22, SD Card, Mic(INMP 441), และ WiFi
   - เมื่อกดปุ่มจะเข้าสู่ countdown แล้วเริ่มบันทึกเสียงเป็นไฟล์ WAV
   - เสียงจะถูกบันทึกเป็นไฟล์ราย session / วัน เพื่อเก็บข้อมูลสภาพแปลง
   - ขณะบันทึกข้อมูลจะส่งสภาพอากาศและสถิติเสียงไปยัง Google Apps Script
   - ระบบสามารถรับคำสั่งจาก server เช่น เริ่ม/หยุด/รีเซ็ต ผ่าน polling
   - การทำงานหลักแบ่งเป็น: setup(), loop(), ledTask(), wifiTask()

   ================================================================
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <SD.h>
#include "driver/i2s.h"
#include "DHT.h"
#include <math.h>
#include <time.h>
#include <ArduinoJson.h>

// ================================================================
//  CONFIGURATION
//  ค่าตั้งต้นหลักสำหรับการทำงานของ ESP32
//  - ssid / password: ข้อมูล WiFi ที่ใช้เชื่อมต่ออินเทอร์เน็ต
//  - APPS_SCRIPT_URL: URL ของ Google Apps Script ที่รับข้อมูลจากบอร์ด
//  - NTP_SERVER1/2 และ TZ_STRING: ใช้ sync เวลาให้ตรงกับ timezone ไทย
//  - SESSION_MS: ระยะเวลา session อัตโนมัติ หากระบบถูกสั่งให้ทำงานโดย server
//  - POLL_INTERVAL_MS: ช่วงเวลาที่ ESP32 จะเช็กคำสั่งจาก server
//  - Pins: กำหนดขาสำหรับ DHT, SD card, I2S mic, LED และปุ่มกด
//  - Audio: กำหนด sample rate และเวลาการบันทึกเสียงต่อไฟล์
// ================================================================
const char* ssid     = "";
const char* password = "";
const char* APPS_SCRIPT_URL = "";

#define NTP_SERVER1       "pool.ntp.org"
#define NTP_SERVER2       "time.google.com"
const char* TZ_STRING   = "ICT-7";

#define SESSION_HOURS     2UL
#define SESSION_MS        (SESSION_HOURS * 3600UL * 1000UL)
#define POLL_INTERVAL_MS  10000UL

// ─── Pins ────────────────────────────────────────────────────────
#define DHTPIN       4
#define DHTTYPE      DHT22
#define SD_CS        10
#define SD_MOSI      11
#define SD_MISO      13
#define SD_SCK       12
#define I2S_SCK      16
#define I2S_WS       15
#define I2S_SD_PIN   17
#define LED_GREEN    38
#define LED_RED      39
#define BTN_PIN      40

// ─── Audio ───────────────────────────────────────────────────────
#define SAMPLE_RATE   32000
#define BUFFER_SIZE   1024
#define RECORD_SEC    10
#define INTERVAL_MS   15000UL   // ช่วงห่างระหว่างไฟล์ (ms)

// ================================================================
//  FILE MANAGEMENT
//  ส่วนนี้จัดการเรื่องการจัดเก็บไฟล์ WAV ตามวันและ session
//  - สร้างโฟลเดอร์ /DD-MM-YYYY
//  - ภายในแต่ละวันจะมี session_xxx
//  - ไฟล์เสียงแต่ละชิ้นจะมีชื่อแบบ DDMM_sX_001.wav
//  - ใช้เมื่อ ESP ถูก reboot หรือ resume การทำงานต่อจาก session ที่ค้าง
// ================================================================
static char  currentDayFolder[20]  = "";
static char  currentSessFolder[40] = "";
static char  currentDayStr[12]     = "";
static int   sessionNum            = 0;
static int   fileCountInSession    = 0;
static bool  sessionFolderReady    = false;

void initSessionFolder(struct tm &ti) {
  if (sessionFolderReady && strlen(currentSessFolder) > 0) {
    Serial.printf("[FS]   Skip re-init: %s\n", currentSessFolder);
    return;
  }
  char dayStr[12];
  sprintf(dayStr, "%02d-%02d-%04d", ti.tm_mday, ti.tm_mon+1, ti.tm_year+1900);
  strncpy(currentDayStr, dayStr, 11);
  char dayPath[20];
  sprintf(dayPath, "/%s", dayStr);
  if (!SD.exists(dayPath)) {
    SD.mkdir(dayPath);
    sessionNum = 1;
  } else {
    sessionNum = 1;
    char testPath[60];
    while (true) {
      sprintf(testPath, "%s/session_%d", dayPath, sessionNum);
      if (!SD.exists(testPath)) break;
      sessionNum++;
    }
  }
  char sessPath[60];
  sprintf(sessPath, "%s/session_%d", dayPath, sessionNum);
  SD.mkdir(sessPath);
  strncpy(currentDayFolder,  dayPath,  19);
  strncpy(currentSessFolder, sessPath, 39);
  fileCountInSession = 0;
  sessionFolderReady = true;
  Serial.printf("[FS]   Session folder: %s\n", sessPath);
}

bool restoreSessionFolder(const char* cday, int csessNum) {
  if (!cday || strlen(cday) == 0) return false;
  char dayPath[24], sessPath[48];
  sprintf(dayPath,  "/%s", cday);
  sprintf(sessPath, "%s/session_%d", dayPath, csessNum);
  if (!SD.exists(sessPath)) {
    Serial.printf("[FS]   Resume target missing: %s\n", sessPath);
    return false;
  }
  strncpy(currentDayFolder,  dayPath,  19);
  strncpy(currentSessFolder, sessPath, 39);
  sprintf(currentDayStr, "%s", cday);
  sessionNum = csessNum;
  fileCountInSession = 0;
  sessionFolderReady = true;
  Serial.printf("[FS]   Resumed folder: %s\n", sessPath);
  return true;
}

void buildFilename(char *out, int maxLen) {
  fileCountInSession++;
  char ddmm[6];
  strncpy(ddmm, currentDayStr, 5); ddmm[5] = '\0';
  snprintf(out, maxLen, "%s/%s_s%d_%03d.wav",
           currentSessFolder, ddmm, sessionNum, fileCountInSession);
}

// ================================================================
//  STATES
// ================================================================
enum SystemState {
  ST_BOOT, ST_CHECKING, ST_DEV_PASS, ST_DEV_FAIL,
  ST_READY, ST_COUNTDOWN, ST_RECORDING,
  ST_WAITING, ST_STOP, ST_IDLE
};
volatile SystemState sysState  = ST_BOOT;
volatile SystemState prevState = ST_BOOT;

// ================================================================
//  GLOBALS
// ================================================================
static uint32_t sessionStart   = 0;
static bool     sessionDone    = false;
// ── manualSession: กดปุ่มเอง ไม่มี timeout ────────────────────
// true = อัดต่อเนื่องไม่มีกำหนด จนกว่าจะกดหยุด
static bool     manualSession  = false;

DHT dht(DHTPIN, DHTTYPE);
SemaphoreHandle_t sdMutex = NULL;
SemaphoreHandle_t dataSem = NULL;
TaskHandle_t wifiTaskHandle = NULL;
TaskHandle_t ledTaskHandle  = NULL;

struct SensorData {
  float   temp, hum, rms, db;
  int32_t amplitude;
  char    filename[80], sdStatus[8], recStatus[12];
  char    timestamp[30], dhtStatus[8], micStatus[8], sheet[40];
};
static SensorData   sharedData;
static portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;

volatile bool triggerRecord  = false;
volatile bool triggerReset   = false;
volatile bool triggerStop    = false;
volatile bool autoMode       = false;
volatile char pollSheet[40]  = "";

volatile uint32_t serverSessionEndMs = 0;
volatile bool isRecordingAudio = false;

volatile bool pendingResume        = false;
volatile char pendingResumeDay[12] = "";
volatile int  pendingResumeSessNum = 1;
volatile bool pendingResumeActive  = false;

int32_t buf32[BUFFER_SIZE];
int16_t buf16[BUFFER_SIZE];

// ================================================================
//  FORWARD DECLARATIONS
// ================================================================
void setupI2S();
bool testI2SMicrophone();
bool recordAudio_unsafe(const char*, uint32_t, int32_t&, double&, long&);
bool recordAudioNoSD(uint32_t, int32_t&, double&, long&);
void writeWAVHeader(File&);
void updateWAVHeader(File&);
void connectWiFi();
void sendData(float,float,const char*,const char*,int32_t,float,float,const char*,const char*,const char*);
bool pollAppsScript();
void ledTask(void*);
void wifiTask(void*);

// ================================================================
//  LED
// ================================================================
uint32_t ledTimer   = 0;
uint8_t  ledPhase   = 0;
uint8_t  blinkCount = 0;

void setLED(bool g, bool r) {
  digitalWrite(LED_GREEN, g ? HIGH : LOW);
  digitalWrite(LED_RED,   r ? HIGH : LOW);
}

void updateLED() {
  uint32_t now = millis();
  switch (sysState) {
    case ST_CHECKING:
      if (prevState != ST_CHECKING) {
        ledTimer = now; ledPhase = 0; prevState = ST_CHECKING;
        setLED(false, false);
      }
      if (now - ledTimer >= 500) {
        ledTimer = now; ledPhase = !ledPhase;
        setLED(false, ledPhase == 1);
      }
      break;
    case ST_DEV_PASS:
      setLED(true, false);
      break;
    case ST_DEV_FAIL:
      setLED(false, true);
      break;
    case ST_READY:
    case ST_IDLE:
      setLED(true, false);
      break;
    case ST_COUNTDOWN:
      if (prevState != ST_COUNTDOWN) {
        ledTimer = now; ledPhase = 0; prevState = ST_COUNTDOWN;
        setLED(true, false);
      }
      if (now - ledTimer >= 500) {
        ledTimer = now; ledPhase = !ledPhase;
        setLED(ledPhase == 0, ledPhase == 1);
      }
      break;
    case ST_RECORDING:
      setLED(false, false);
      break;
    case ST_STOP:
      if (prevState != ST_STOP) {
        ledTimer = now; ledPhase = 0; blinkCount = 0; prevState = ST_STOP;
        setLED(true, false);
      }
      if (blinkCount < 5) {
        if (now - ledTimer >= 500) {
          ledTimer = now; ledPhase = !ledPhase;
          setLED(ledPhase == 0, ledPhase == 1);
          if (ledPhase == 0) blinkCount++;
        }
      } else {
        setLED(false, false);
        if (now - ledTimer >= 1000) {
          sysState = ST_IDLE; prevState = ST_BOOT;
        }
      }
      break;
    default:
      setLED(false, false);
      break;
  }
}

// ================================================================
//  SWITCH
// ================================================================
#define BTN_DEBOUNCE_MS   50
#define BTN_LONGPRESS_MS  5000
#define COUNTDOWN_MS      (2UL * 60UL * 1000UL)

uint32_t btnPressTime    = 0;
bool     btnLastRaw      = HIGH;
bool     btnDebounced    = HIGH;
uint32_t btnDebTimer     = 0;
bool     longPressFired  = false;
uint32_t countdownStart  = 0;
bool     countdownActive = false;

void stopSession() {
  countdownActive    = false;
  autoMode           = false;
  manualSession      = false;   // ← reset manual flag
  sessionDone        = true;
  serverSessionEndMs = 0;
  triggerRecord      = false;
  sysState  = ST_STOP; prevState = ST_BOOT;
}

void handleSwitch() {
  bool raw = digitalRead(BTN_PIN);
  uint32_t now = millis();

  if (raw != btnLastRaw) { btnDebTimer = now; btnLastRaw = raw; }
  if (now - btnDebTimer < BTN_DEBOUNCE_MS) return;
  if (raw == btnDebounced) {
    // long press check
    if (btnDebounced == LOW && !longPressFired &&
        (now - btnPressTime) >= BTN_LONGPRESS_MS) {
      longPressFired = true;
      stopSession();
      Serial.println("[BTN] LONG PRESS → STOP");
    }
    return;
  }

  btnDebounced = raw;

  if (btnDebounced == LOW) {
    btnPressTime = now; longPressFired = false;
  } else {
    if (longPressFired) return;
    if (now - btnPressTime >= BTN_LONGPRESS_MS) return;

    if (sysState == ST_READY || sysState == ST_IDLE) {
      // ── เริ่ม session แบบ manual (ไม่มี timeout) ──────────────
      countdownStart = now; countdownActive = true;
      manualSession  = true;   // ← บอกว่าเป็น manual ไม่มีกำหนดหยุด
      sysState = ST_COUNTDOWN; prevState = ST_BOOT;
      Serial.println("[BTN] 1x press → COUNTDOWN 2 min (manual session)");

    } else if (sysState == ST_COUNTDOWN || sysState == ST_RECORDING) {
      stopSession();
      Serial.println("[BTN] 1x press → STOP");
    }
  }
}

// ================================================================
//  COUNTDOWN
// ================================================================
void handleCountdown() {
  if (!countdownActive || sysState != ST_COUNTDOWN) {
    countdownActive = false; return;
  }
  if (millis() - countdownStart >= COUNTDOWN_MS) {
    countdownActive = false;
    // เริ่ม session
    sessionStart  = millis();
    sessionDone   = false;
    autoMode      = true;
    triggerRecord = true;
    struct tm ti;
    if (getLocalTime(&ti)) {
      char s[40];
      sprintf(s, "RAW_%02d-%02d-%04d",
              ti.tm_mday, ti.tm_mon+1, ti.tm_year+1900+543);
      strncpy((char*)pollSheet, s, 39);
    }
    Serial.println("[CDN] Countdown done → start continuous recording");
  }
}

// ================================================================
//  DEVICE CHECK
// ================================================================
bool deviceCheckSequence(bool &dhtOk, bool &sdOk, bool &micOk) {
  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  struct { const char* name; bool ok; } checks[4] = {
    {"DHT22", dhtOk}, {"SD", sdOk}, {"MIC", micOk}, {"WiFi", wifiOk}
  };
  bool allOk = true;

  for (int i = 0; i < 4; i++) {
    setLED(false, false);
    sysState = ST_BOOT; prevState = ST_BOOT;
    delay(300);

    sysState = ST_CHECKING; prevState = ST_CHECKING;
    {
      uint32_t st2 = millis(); bool ph2 = false; setLED(false, false);
      while (millis() - st2 < 1200) {
        bool newPh = ((millis() - st2) / 500) % 2;
        if (newPh != ph2) { ph2 = newPh; setLED(false, ph2); }
        delay(10);
      }
    }

    Serial.printf("[Check] %-6s : %s\n", checks[i].name, checks[i].ok ? "OK" : "FAIL");

    if (checks[i].ok) {
      setLED(false, false);
      sysState = ST_BOOT; prevState = ST_BOOT;
    } else {
      allOk = false;
      sysState = ST_DEV_FAIL; prevState = ST_DEV_FAIL;
      setLED(false, true);
      delay(800);
    }
  }

  if (allOk) {
    sysState = ST_DEV_PASS; prevState = ST_DEV_PASS;
    setLED(true, false);
  }
  return allOk;
}

// ================================================================
//  SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  uint32_t serialWait = millis();
  while (!Serial && millis() - serialWait < 2000) delay(10);
  delay(500);
  Serial.println("\n================================================");
  Serial.println("       PLANT SOUND MONITORING SYSTEM v13");
  Serial.println("================================================");

  pinMode(LED_GREEN, OUTPUT); pinMode(LED_RED, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);
  setLED(false, false);

  sdMutex = xSemaphoreCreateMutex();
  dataSem = xSemaphoreCreateBinary();

  dht.begin(); delay(2000);
  float testTemp = dht.readTemperature();
  bool dhtOk = !isnan(testTemp) && testTemp != 0;
  Serial.printf("[BOOT] DHT22   : %s (%.1f°C)\n", dhtOk ? "READY" : "ERROR", testTemp);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  bool sdOk = false;
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
    sdOk = SD.begin(SD_CS);
    xSemaphoreGive(sdMutex);
  }
  Serial.printf("[BOOT] SD Card : %s\n", sdOk ? "READY" : "ERROR");

  setupI2S();
  {
    Serial.println("[BOOT] I2S APLL warm-up 5s...");
    size_t dummy = 0;
    uint32_t wt = millis();
    while (millis() - wt < 5000) {
      i2s_read(I2S_NUM_0, buf32, sizeof(buf32), &dummy, pdMS_TO_TICKS(100));
    }
    i2s_zero_dma_buffer(I2S_NUM_0);
    Serial.println("[BOOT] I2S APLL ready");
  }

  bool micOk = testI2SMicrophone();
  Serial.printf("[BOOT] I2S Mic : %s\n", micOk ? "READY" : "ERROR");

  connectWiFi();
  configTime(0, 0, NTP_SERVER1, NTP_SERVER2);
  setenv("TZ", TZ_STRING, 1); tzset();
  struct tm ti;
  uint32_t tw = millis();
  while (!getLocalTime(&ti) && millis() - tw < 15000) delay(500);
  if (getLocalTime(&ti)) {
    char tbuf[30]; strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &ti);
    Serial.printf("[BOOT] NTP     : %s (TH)\n", tbuf);
  }

  bool allOk = deviceCheckSequence(dhtOk, sdOk, micOk);
  Serial.printf("[BOOT] Devices : %s\n", allOk ? "ALL PASS" : "SOME FAIL");

  portENTER_CRITICAL(&dataMux);
  strncpy(sharedData.dhtStatus, dhtOk ? "READY" : "ERROR", 7);
  strncpy(sharedData.micStatus, micOk ? "READY" : "ERROR", 7);
  portEXIT_CRITICAL(&dataMux);

  if (!allOk) {
    sysState = ST_DEV_FAIL; prevState = ST_DEV_FAIL;
    setLED(false, true);
    Serial.println("[BOOT] !! DEVICE FAIL");
    Serial.println("[BOOT]    กด 1ครั้ง = reboot | กดค้าง 5s = force continue");

    xTaskCreatePinnedToCore(ledTask, "LEDTask", 2048, NULL, 2, &ledTaskHandle, 0);

    uint32_t pressStart = 0; bool lastBtn = HIGH; bool forceContinue = false;
    while (!forceContinue) {
      bool btn = digitalRead(BTN_PIN);
      if (btn == LOW && lastBtn == HIGH) pressStart = millis();
      if (btn == HIGH && lastBtn == LOW) {
        if (millis() - pressStart < (uint32_t)BTN_LONGPRESS_MS) {
          Serial.println("[BOOT] Reboot...");
          delay(300); ESP.restart();
        }
      }
      if (btn == LOW && millis() - pressStart >= (uint32_t)BTN_LONGPRESS_MS) {
        forceContinue = true;
        Serial.println("[BOOT] Force continue");
      }
      lastBtn = btn; delay(20);
    }
    setLED(false, false); delay(300);
    sysState = ST_READY; prevState = ST_BOOT;
    Serial.println("[BOOT] → ST_READY (forced)");
    xTaskCreatePinnedToCore(wifiTask, "WiFiTask", 8192, NULL, 1, &wifiTaskHandle, 0);
    Serial.println("[BOOT] Tasks ready\n================================================\n");
    return;
  }

  sysState = ST_READY; prevState = ST_BOOT;
  Serial.println("[BOOT] → ST_READY");
  xTaskCreatePinnedToCore(ledTask,  "LEDTask",  2048, NULL, 2, &ledTaskHandle,  0);
  xTaskCreatePinnedToCore(wifiTask, "WiFiTask", 8192, NULL, 1, &wifiTaskHandle, 0);
  Serial.println("[BOOT] Tasks ready\n================================================\n");
}

// ================================================================
//  MAIN LOOP (Core 1)
//  ทำหน้าที่เป็น state machine หลักของระบบ
//  - ตรวจสถานะปุ่มกด / countdown / session timeout
//  - เรียก trigger ให้เริ่มบันทึกเสียงเมื่อถึงเวลา
//  - สร้าง folder สำหรับ session ใหม่ หรือ resume จาก server
//  - อ่านค่า DHT22 และเริ่ม recordAudio_unsafe()
//  - สรุปข้อมูลแล้วส่งไปยัง sharedData เพื่อให้ wifiTask POST
// ================================================================
void loop() {
  static uint32_t lastRecordTime = 0;
  static uint32_t lastPrint      = 0;

  handleSwitch();
  handleCountdown();

  // triggerStop จาก server หรือปุ่ม
  if (triggerStop) {
    triggerStop = false;
    stopSession();
    Serial.println("[POLL] Stop from server → ST_STOP");
    return;
  }

  // ── session timeout: เฉพาะ poll-triggered session (ไม่ใช่ manual) ──
  // manualSession = true → ไม่มี timeout รอกดปุ่มหยุดเอง
  if (autoMode && !sessionDone && !manualSession) {
    bool expired = false;
    if (serverSessionEndMs > 0) {
      struct tm ti2;
      if (getLocalTime(&ti2)) {
        time_t nowEp = mktime(&ti2);
        if ((uint32_t)(nowEp * 1000UL) >= serverSessionEndMs) expired = true;
      }
    } else {
      if (millis() - sessionStart >= SESSION_MS) expired = true;
    }
    if (expired) {
      sessionDone = true; autoMode = false;
      serverSessionEndMs = 0;
      sysState = ST_IDLE; prevState = ST_BOOT;
      Serial.println("[SES]  *** SESSION COMPLETE ***");
    }
  }

  if (triggerReset) {
    triggerReset = false;
    Serial.println("[POLL] Reset → ESP.restart()");
    delay(500); ESP.restart();
  }

  if (sessionDone && !triggerRecord) { delay(100); return; }

  if (sessionDone && triggerRecord) {
    sessionDone          = false;
    sessionFolderReady   = false;
    currentSessFolder[0] = '\0';
    Serial.println("[SES]  New session after previous complete");
  }

  // ── auto trigger ไฟล์ถัดไป (ทั้ง manual และ poll session) ──────
  if (!triggerRecord && autoMode && !sessionDone &&
      (sysState == ST_IDLE || sysState == ST_RECORDING)) {
    uint32_t el = millis() - lastRecordTime;
    if (el >= INTERVAL_MS) {
      triggerRecord = true;
      struct tm ti;
      if (getLocalTime(&ti)) {
        char s[40];
        sprintf(s, "RAW_%02d-%02d-%04d", ti.tm_mday, ti.tm_mon+1, ti.tm_year+1900+543);
        strncpy((char*)pollSheet, s, 39);
      }
      Serial.printf("[AUTO] trigger file #%d\n", fileCountInSession + 1);
    } else if (millis() - lastPrint >= 5000) {
      lastPrint = millis();
      uint32_t rem = INTERVAL_MS - el;
      Serial.printf("[AUTO] next record in %lus\n", rem / 1000);
    }
  }

  if (!triggerRecord) { delay(10); return; }
  triggerRecord = false;

  struct tm ti;
  if (!getLocalTime(&ti)) { delay(1000); return; }

  // init session folder ครั้งแรกของ session
  if (!sessionFolderReady || strlen(currentSessFolder) == 0) {
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
      if (SD.cardType() != CARD_NONE) {
        bool resumed = false;
        if (pendingResume) {
          resumed = restoreSessionFolder((const char*)pendingResumeDay, pendingResumeSessNum);
          pendingResume = false;
        }
        if (!resumed) initSessionFolder(ti);
        Serial.printf("[SES]  Using folder: %s\n", currentSessFolder);
      }
      xSemaphoreGive(sdMutex);
    }
  }

  lastRecordTime = millis();

  char sheetName[40] = "";
  if (strlen((char*)pollSheet) > 0) strncpy(sheetName, (char*)pollSheet, 39);
  else sprintf(sheetName, "RAW_%02d-%02d-%04d", ti.tm_mday, ti.tm_mon+1, ti.tm_year+1900+543);

  sysState = ST_RECORDING; prevState = ST_BOOT;

  char tsNow[30] = "0000-00-00 00:00:00";
  strftime(tsNow, sizeof(tsNow), "%Y-%m-%d %H:%M:%S", &ti);
  Serial.println("\n------------------------------------------------");
  Serial.printf("[REC]  %s | %s | file=%d\n", tsNow, sheetName, fileCountInSession + 1);

  bool sdReady = false;
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
    sdReady = (SD.cardType() != CARD_NONE);
    xSemaphoreGive(sdMutex);
  }

  float temp = dht.readTemperature(), hum = dht.readHumidity();
  Serial.printf("[REC]  DHT: %.1f°C  %.1f%%RH\n", temp, hum);

  char filename[80] = "no_sd";
  if (sdReady && sessionFolderReady && strlen(currentSessFolder) > 0) {
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      buildFilename(filename, sizeof(filename));
      xSemaphoreGive(sdMutex);
    }
    Serial.printf("[REC]  File: %s\n", filename);
  }

  int32_t peakAmp = 0; double sumSq = 0; long smpCt = 0; bool recOk = false;
  Serial.printf("[REC]  Recording %d sec...\n", RECORD_SEC);

  isRecordingAudio = true;
  delay(50);

  if (sdReady && strcmp(filename, "no_sd") != 0) {
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(RECORD_SEC*1000+4000)) == pdTRUE) {
      recOk = recordAudio_unsafe(filename, RECORD_SEC, peakAmp, sumSq, smpCt);
      xSemaphoreGive(sdMutex);
    }
  } else {
    recOk = recordAudioNoSD(RECORD_SEC, peakAmp, sumSq, smpCt);
  }

  isRecordingAudio = false;

  float rms = 0, db = 0;
  if (smpCt > 0) {
    rms = sqrtf((float)(sumSq / smpCt));
    if (rms > 0) { db = 20.0f * log10f(rms / 32768.0f) + 94.0f; if (db < 0) db = 0; }
  }
  Serial.printf("[REC]  %s  Peak=%d  RMS=%.1f  dB=%.1f\n",
                recOk ? "COMPLETE" : "FAIL", peakAmp, rms, db);

  portENTER_CRITICAL(&dataMux);
  sharedData.temp = temp; sharedData.hum = hum;
  sharedData.amplitude = peakAmp; sharedData.rms = rms; sharedData.db = db;
  strncpy(sharedData.filename,  filename,               79);
  strncpy(sharedData.sdStatus,  sdReady?"READY":"ERROR", 7);
  strncpy(sharedData.recStatus, recOk?"COMPLETE":"FAIL", 11);
  strncpy(sharedData.timestamp, tsNow,                   29);
  strncpy(sharedData.sheet,     sheetName,               39);
  portEXIT_CRITICAL(&dataMux);

  xSemaphoreGive(dataSem);
  lastRecordTime = millis();

  // ── LED: ค้าง ST_RECORDING ตลอด session (ดับ) ────────────────
  // กลับ ST_IDLE (GREEN) เมื่อ session จบจริงๆ เท่านั้น
  if (sysState != ST_STOP) {
    if (autoMode && !sessionDone) {
      sysState = ST_RECORDING;  // LED ดับต่อเนื่อง
    } else {
      sysState = ST_IDLE; prevState = ST_BOOT;  // GREEN ค้าง
    }
  }
  Serial.printf("[SES]  File %d done | manual=%d\n",
                fileCountInSession, manualSession ? 1 : 0);
  Serial.println("------------------------------------------------");
}

// ================================================================
//  LED TASK (Core 0)
// ================================================================
void ledTask(void *param) {
  for (;;) { updateLED(); vTaskDelay(pdMS_TO_TICKS(10)); }
}

// ================================================================
//  WIFI TASK (Core 0)
//  ทำงานแยกจาก loop เพื่อไม่ให้การเชื่อมต่อ WiFi / POST / polling
//  รบกวนระหว่างการบันทึกเสียง
//  - ถ้า WiFi หลุด จะ reconnect อัตโนมัติ
//  - ส่ง SensorData ไปยัง Google Apps Script
//  - polling ข้อมูลจาก server เช่น record, reset, session_active
//  - หากมีคำสั่งหยุดหรือเริ่ม session ใหม่ จะตั้ง flag ให้ loop ประมวลผลต่อ
// ================================================================
void wifiTask(void *param) {
  static uint32_t lastPollTime = 0;
  for (;;) {
    if (isRecordingAudio) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Reconnecting...");
      WiFi.begin(ssid, password);
      uint32_t t = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t < 12000) {
        if (isRecordingAudio) break;
        vTaskDelay(pdMS_TO_TICKS(300));
      }
      if (WiFi.status() == WL_CONNECTED)
        Serial.printf("[WiFi] Connected  IP=%s\n", WiFi.localIP().toString().c_str());
    }

    if (xSemaphoreTake(dataSem, pdMS_TO_TICKS(10)) == pdTRUE) {
      SensorData snap;
      portENTER_CRITICAL(&dataMux); snap = sharedData; portEXIT_CRITICAL(&dataMux);
      if (WiFi.status() == WL_CONNECTED && !isRecordingAudio) {
        Serial.printf("[WiFi] POST → %s\n", snap.timestamp);
        sendData(snap.temp, snap.hum, snap.sdStatus, snap.recStatus,
                 snap.amplitude, snap.rms, snap.db,
                 snap.filename, snap.timestamp, snap.sheet);
      } else { Serial.println("[WiFi] Skip POST"); }
    }

    if (WiFi.status() == WL_CONNECTED && !isRecordingAudio &&
        millis() - lastPollTime >= POLL_INTERVAL_MS) {
      lastPollTime = millis();
      pollAppsScript();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ================================================================
//  POLL APPS SCRIPT
//  ฟังก์ชันนี้เป็นแนวคิดหลักสำหรับการควบคุมระยะสั้นจาก server
//  - action=poll
//  - server ส่ง JSON ว่าให้ record หรือ reset หรือ session_active ไหม
//  - ถ้ามี record flag จะตั้ง triggerRecord เพื่อให้ loop เริ่มอัดไฟล์ทันที
//  - ถ้ามี session_end จะคำนวณเวลาหยุด session เพื่อให้ autoMode ถูกยกเลิก
// ================================================================
bool pollAppsScript() {
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  String url = String(APPS_SCRIPT_URL) + "?action=poll";
  http.begin(client, url);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  int code = http.GET();
  String body = http.getString();
  http.end();

  Serial.printf("[POLL] HTTP %d  len=%d\n", code, body.length());
  int jsonStart = body.indexOf('{');
  if (jsonStart < 0) { Serial.println("[POLL] No JSON"); return false; }

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, body.substring(jsonStart))) {
    Serial.println("[POLL] JSON error"); return false;
  }

  bool recFlag    = doc["record"]               | false;
  bool rstFlag    = doc["reset"]                | false;
  bool sessActive = doc["session_active"]       | false;
  const char* sh  = doc["sheet"]               | "";
  const char* end = doc["session_end"]         | "";
  const char* cday= doc["current_day"]         | "";
  int  csessNum   = doc["current_session_num"] | 1;

  Serial.printf("[POLL] record=%d reset=%d sess=%d sheet=%s\n",
                recFlag, rstFlag, sessActive, sh);

  if (sessActive && end && strlen(end) > 0 && autoMode && !manualSession) {
    struct tm t = {}; int yr,mo,dy,hh,mm,ss;
    if (sscanf(end, "%d-%d-%dT%d:%d:%d", &yr,&mo,&dy,&hh,&mm,&ss) == 6) {
      t.tm_year=yr-1900; t.tm_mon=mo-1; t.tm_mday=dy;
      t.tm_hour=hh; t.tm_min=mm; t.tm_sec=ss;
      time_t ep = mktime(&t);
      struct tm now_ti; if (getLocalTime(&now_ti)) {
        time_t nowEp = mktime(&now_ti);
        if (ep > nowEp) serverSessionEndMs = (uint32_t)(ep * 1000UL);
      }
    }
  }

  // server สั่งหยุด — หยุดเฉพาะ poll session ไม่ใช่ manual
  if (!sessActive && autoMode && !manualSession) {
    Serial.println("[POLL] Server: session stopped → triggerStop");
    triggerStop = true;
  }

  if (recFlag && !triggerRecord && !manualSession) {
    if (sh && strlen(sh) > 0) strncpy((char*)pollSheet, sh, 39);
    bool wasIdle = !autoMode;
    if (wasIdle) {
      sessionDone        = false;
      sessionStart       = millis();
      serverSessionEndMs = 0;
      autoMode           = true;
      manualSession      = false;
      if (cday && strlen(cday) > 0 && sessActive) {
        strncpy((char*)pendingResumeDay, cday, 11);
        pendingResumeSessNum = csessNum;
        pendingResumeActive  = true;
      } else {
        pendingResumeDay[0] = '\0';
        pendingResumeActive = false;
      }
      sessionFolderReady   = false;
      currentSessFolder[0] = '\0';
      pendingResume        = true;
      Serial.println("[POLL] Resume requested — will init in loop()");
    }
    triggerRecord = true;
  }

  if (rstFlag) triggerReset = true;
  return true;
}

// ================================================================
//  HELPERS
// ================================================================
void connectWiFi() {
  Serial.printf("[WiFi] SSID: %s\n", ssid);
  WiFi.begin(ssid, password);
  uint32_t st = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - st < 15000) delay(300);
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("[WiFi] CONNECTED  IP=%s\n", WiFi.localIP().toString().c_str());
  else Serial.println("[WiFi] FAILED");
}

void sendData(float t, float h, const char* sd, const char* rec,
              int32_t amp, float rms, float db,
              const char* fn, const char* ts, const char* sheet) {
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.begin(client, APPS_SCRIPT_URL);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("Content-Type", "application/json");
  String json = "{\"sheet\":\"" + String(sheet) + "\","
                "\"timestamp\":\"" + String(ts) + "\","
                "\"temp\":"  + String(t,1) + ","
                "\"hum\":"   + String(h,1) + ","
                "\"sd\":\""  + String(sd)  + "\","
                "\"record\":\"" + String(rec) + "\","
                "\"amplitude\":" + String(amp) + ","
                "\"rms\":"   + String(rms,1) + ","
                "\"db\":"    + String(db,1)  + ","
                "\"filename\":\"" + String(fn) + "\"}";
  int code = http.POST(json);
  Serial.printf("[HTTP] %d → %s\n", code, http.getString().c_str());
  http.end();
}

// ================================================================
//  I2S / AUDIO CAPTURE
//  ส่วนนี้ทำหน้าที่อ่านสัญญาณจากไมโครโฟนผ่าน I2S
//  - setupI2S(): ตั้งค่า I2S, sample rate, DMA buffer, pin mapping
//  - testI2SMicrophone(): ตรวจว่ามีสัญญาณจากไมโครโฟนจริง
//  - recordAudio_unsafe(): บันทึกเสียง 10 วินาทีเป็น WAV และประมวลผล
//    peak, rms, dB, sum of squares เพื่อใช้ส่งไปยัง server
//  - วงจรนี้มีการลบ DC offset และใช้ median-of-3 เพื่อลด noise
// ================================================================
void setupI2S() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8, .dma_buf_len = 512, .use_apll = true
  };
  i2s_pin_config_t pins = {
    .bck_io_num = I2S_SCK, .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = I2S_SD_PIN
  };
  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

bool testI2SMicrophone() {
  size_t b = 0;
  i2s_read(I2S_NUM_0, buf32, sizeof(buf32), &b, pdMS_TO_TICKS(500));
  return b > 0;
}

bool recordAudio_unsafe(const char *path, uint32_t durSec,
                        int32_t &outPeak, double &outSumSq, long &outCount) {
  File file = SD.open(path, FILE_WRITE);
  if (!file) return false;
  writeWAVHeader(file);
  uint32_t tEnd = millis() + durSec * 1000UL;
  outPeak = 0; outSumSq = 0; outCount = 0;
  static double dcEstimate = 0;
  const double DC_ALPHA = 0.01;
  int32_t histA = 0, histB = 0;
  int histCount = 0;
  while (millis() < tEnd) {
    size_t b = 0;
    i2s_read(I2S_NUM_0, buf32, sizeof(buf32), &b, portMAX_DELAY);
    int n = b / sizeof(int32_t);
    for (int i = 0; i < n; i++) {
      int32_t rawIn = constrain(buf32[i] >> 13, -32768, 32767);
      dcEstimate += DC_ALPHA * ((double)rawIn - dcEstimate);
      int32_t raw = constrain((int32_t)((double)rawIn - dcEstimate), -32768, 32767);
      // median-of-3
      int32_t outVal;
      if (histCount < 2) { outVal = raw; }
      else {
        int32_t a=histA, bb=histB, c=raw, med;
        if ((a<=bb&&bb<=c)||(c<=bb&&bb<=a)) med=bb;
        else if ((bb<=a&&a<=c)||(c<=a&&a<=bb)) med=a;
        else med=c;
        outVal = med;
      }
      histA = histB; histB = raw;
      if (histCount < 2) histCount++;
      buf16[i] = (int16_t)outVal;
      if (abs(outVal) > outPeak) outPeak = abs(outVal);
      outSumSq += (double)outVal * outVal; outCount++;
    }
    file.write((uint8_t*)buf16, n * 2);
  }
  updateWAVHeader(file); file.close();
  return true;
}

bool recordAudioNoSD(uint32_t durSec, int32_t &outPeak, double &outSumSq, long &outCount) {
  uint32_t tEnd = millis() + durSec * 1000UL;
  outPeak = 0; outSumSq = 0; outCount = 0;
  static double dcEstimate = 0;
  const double DC_ALPHA = 0.01;
  int32_t histA = 0, histB = 0;
  int histCount = 0;
  while (millis() < tEnd) {
    size_t b = 0;
    i2s_read(I2S_NUM_0, buf32, sizeof(buf32), &b, portMAX_DELAY);
    int n = b / sizeof(int32_t);
    for (int i = 0; i < n; i++) {
      int32_t rawIn = constrain(buf32[i] >> 13, -32768, 32767);
      dcEstimate += DC_ALPHA * ((double)rawIn - dcEstimate);
      int32_t raw = constrain((int32_t)((double)rawIn - dcEstimate), -32768, 32767);
      int32_t outVal;
      if (histCount < 2) { outVal = raw; }
      else {
        int32_t a=histA, bb=histB, c=raw, med;
        if ((a<=bb&&bb<=c)||(c<=bb&&bb<=a)) med=bb;
        else if ((bb<=a&&a<=c)||(c<=a&&a<=bb)) med=a;
        else med=c;
        outVal = med;
      }
      histA = histB; histB = raw;
      if (histCount < 2) histCount++;
      if (abs(outVal) > outPeak) outPeak = abs(outVal);
      outSumSq += (double)outVal * outVal; outCount++;
    }
  }
  return true;
}

void writeWAVHeader(File &f) {
  uint32_t sr=SAMPLE_RATE, z=0, sub=16, br=sr*2;
  uint16_t ch=1, bps=16, blk=2, fmt=1;
  f.write((uint8_t*)"RIFF",4); f.write((uint8_t*)&z,4);
  f.write((uint8_t*)"WAVE",4); f.write((uint8_t*)"fmt ",4);
  f.write((uint8_t*)&sub,4);   f.write((uint8_t*)&fmt,2);
  f.write((uint8_t*)&ch,2);    f.write((uint8_t*)&sr,4);
  f.write((uint8_t*)&br,4);    f.write((uint8_t*)&blk,2);
  f.write((uint8_t*)&bps,2);   f.write((uint8_t*)"data",4);
  f.write((uint8_t*)&z,4);
}

void updateWAVHeader(File &f) {
  uint32_t sz=f.size();
  uint32_t data=sz-44, chunk=sz-8;
  f.seek(4);  f.write((uint8_t*)&chunk,4);
  f.seek(40); f.write((uint8_t*)&data,4);
}