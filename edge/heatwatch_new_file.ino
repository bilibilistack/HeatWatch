#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_MLX90614.h>   // MLX90614 体表温度
#include "MAX30105.h"
#include "heartRate.h"
#include <math.h>
#include <axp20x.h>

// ======================================================
// HeatWatch ESP32 / T-Beam — Merged Firmware
//
// Sensors    : BME280, LIS3DH, MAX30102, MLX90614
// Features   : Fall detection (LIS3DH INT1 → GPIO25),
//              AXP192 power mgmt, FreeRTOS, LoRaWAN ABP
// Payload    : 9-byte binary
// ======================================================


// ================= ABP CREDENTIALS =================
static const PROGMEM u1_t NWKSKEY[16] = {
  0x65, 0x13, 0x36, 0x98, 0x71, 0x48, 0xC0, 0xBD,
  0x61, 0x7A, 0x85, 0x35, 0xDC, 0xFF, 0x0D, 0xF6
};
static const u1_t PROGMEM APPSKEY[16] = {
  0x05, 0x13, 0x55, 0xCC, 0x01, 0x8D, 0xD0, 0x40,
  0x3A, 0xB5, 0x08, 0x5A, 0xEB, 0x8A, 0x11, 0xF7
};
static const u4_t DEVADDR = 0x260D9A3E;

void os_getArtEui(u1_t* buf) {}
void os_getDevEui(u1_t* buf) {}
void os_getDevKey(u1_t* buf) {}


// ================= PIN MAP =================
const lmic_pinmap lmic_pins = {
  .nss  = 18,
  .rxtx = LMIC_UNUSED_PIN,
  .rst  = 23,
  .dio  = {26, 33, 32},
};

#define I2C_SDA     21
#define I2C_SCL     22
#define VIB_PIN     15   // ERM vibration motor
#define LIS_INT_PIN 25   // LIS3DH INT1 → GPIO25


// ================= USER CONFIG =================
#define DEMO_USB_POWER_ALWAYS_100  0


// ================= SENSOR OBJECTS =================
Adafruit_BME280    bme;
Adafruit_LIS3DH    lis = Adafruit_LIS3DH();
Adafruit_MLX90614  mlx;
MAX30105           particleSensor;
AXP20X_Class       axp;

bool bme_ok = false;
bool lis_ok = false;
bool mlx_ok = false;
bool axp_ok = false;


// ================= INTERRUPT FLAG =================
volatile bool lisInterruptFired = false;

void IRAM_ATTR onLisInterrupt() {
  lisInterruptFired = true;
}


// ================= RTOS =================
SemaphoreHandle_t i2cMutex;


// ================= GLOBAL SENSOR DATA =================
volatile float     airTemp       = 0;
volatile float     humidity      = 0;
volatile float     skinTemp      = 36.5f;
volatile float     localWBGT     = 0;
volatile float     effectiveWBGT = 0;
volatile float     externalWBGT  = 0;
volatile float     DI            = 0;
volatile int       beatAvg       = 0;

typedef enum { NORMAL, WARNING, CRITICAL } RiskLevel;
volatile RiskLevel currentRisk = NORMAL;

volatile unsigned long lastExternalWBGTTime = 0;
const    unsigned long EXTERNAL_WBGT_TIMEOUT = 30UL * 60 * 1000;

volatile bool          alarmMuted    = false;
volatile unsigned long muteStartTime = 0;
const    unsigned long MUTE_DURATION = 10UL * 60 * 1000;

volatile unsigned long txInterval  = 60;
unsigned long          lastTxTime  = 0;
unsigned long          loopStartTime = 0;


// ================= HEART RATE =================
const byte RATE_SIZE = 4;
byte  rates[RATE_SIZE];
byte  rateSpot       = 0;
long  lastBeat       = 0;
float beatsPerMinute = 0;


// ================= BATTERY =================
static float       battEma        = NAN;
static const float BATT_EMA_ALPHA = 0.15f;


// ================= FALL DETECTION PARAMS =================
const float          G                     = 9.80665f;
const float          IMPACT_THRESHOLD_G    = 2.7f;
const float          FREEFALL_THRESHOLD_G  = 0.70f;
const unsigned long  LOWG_WINDOW_MS        = 900;
const float          STILL_LOWER_G         = 0.85f;
const float          STILL_UPPER_G         = 1.15f;
const float          STILL_DELTA_G         = 0.08f;
const unsigned long  CONFIRM_DURATION_MS   = 8000;
const unsigned long  STRUGGLE_GRACE_MS     = 2500;
const unsigned long  FINAL_STILL_MS        = 1200;
const unsigned long  MAX_STILL_MS          = 1500;
const float          STILL_RATIO_REQUIRED  = 0.30f;
const float          POSTURE_CHANGE_DEG    = 8.0f;
const bool           REQUIRE_POSTURE_CHANGE = false;
const float          VERY_STRONG_IMPACT_G  = 5.0f;
const unsigned long  ALARM_DURATION_MS     = 5000;
const unsigned long  SAMPLE_INTERVAL_MS    = 20;

enum FallState { STATE_IDLE, STATE_CONFIRMING, STATE_FALL_DETECTED };
FallState     fallState              = STATE_IDLE;
unsigned long stateEnterTime         = 0;
unsigned int  confirmTotalSamples    = 0;
unsigned int  confirmStillSamples    = 0;
unsigned long lastNonStillTime       = 0;
unsigned long currentStillStart      = 0;
unsigned long maxStillDuration       = 0;
bool          wasStill               = false;
float         lastAx=0, lastAy=0, lastAz=0;
float         lastSvmG=0, prevSvmG=1.0f, lastDeltaG=0;
bool          hasPrevSvm             = false;
unsigned long fallCount              = 0;
bool          pendingFallAlert       = false;
bool          baselineReady          = false;
float         baseAx=0, baseAy=0, baseAz=G;
float         postAxSum=0, postAySum=0, postAzSum=0;
unsigned int  postOrientationSamples = 0;
float         impactSvmG             = 0;
bool          impactHadRecentLowG    = false;
unsigned long lastLowGTime           = 0;
bool          latestFallAlarm        = false;


// ================= LORA =================
static osjob_t sendjob;
static uint8_t payloadBin[9];


// ================= FORWARD DECLARATIONS =================
void do_send(osjob_t* j);
void onEvent(ev_t ev);
void enterState(FallState s);
void readAccel();
void updateFallStateMachine();
void updateBaselineOrientation();
bool isLowMotionNow();
float angleBetweenVectors(float,float,float,float,float,float);
void triggerImmediateSend();
float calcStullWetBulb(float T, float RH);
float calcWBGT(float T, float RH);
float calcDI(float T, float RH);
void determineHeatStress();
void batteryBegin();
float readBatteryVoltageAdc();
uint8_t liionPctFromVoltage(float v);
float smoothBatteryVoltage(float v);
bool readUsbPower();
uint8_t readBatteryPercent();
void triggerVibration(int pattern);


// =====================================================
// HEAT STRESS
// =====================================================

float calcStullWetBulb(float T, float RH) {
  return T * atan(0.151977f * sqrt(RH + 8.313659f))
       + atan(T + RH)
       - atan(RH - 1.676331f)
       + 0.00391838f * pow(RH, 1.5f) * atan(0.023101f * RH)
       - 4.686035f;
}

float calcWBGT(float T, float RH) {
  return 0.7f * calcStullWetBulb(T, RH) + 0.3f * T;
}

float calcDI(float T, float RH) {
  return 0.5f * (T + calcStullWetBulb(T, RH));
}

void determineHeatStress() {
  // 异常值过滤：矿山环境合理范围
  if (isnan(airTemp)  || airTemp  < -10.0f || airTemp  > 60.0f) return;
  if (isnan(humidity) || humidity < 0.0f   || humidity > 100.0f) return;

  localWBGT = calcWBGT(airTemp, humidity);
  DI        = calcDI(airTemp, humidity);

  effectiveWBGT = (externalWBGT > 0 &&
                   millis() - lastExternalWBGTTime < EXTERNAL_WBGT_TIMEOUT)
                  ? externalWBGT : localWBGT;

  if (effectiveWBGT >= 30.0f || DI >= 32.0f) {
    currentRisk = CRITICAL;
    if (!alarmMuted) triggerVibration(2);
    Serial.println("[ALERT] CRITICAL");
  } else if (effectiveWBGT >= 23.0f || (DI >= 24.0f && DI < 29.0f)) {
    currentRisk = WARNING;
    if (!alarmMuted) triggerVibration(1);
    Serial.println("[WARN] WARNING");
  } else {
    currentRisk = NORMAL;
    alarmMuted  = false;
  }
}


// =====================================================
// BATTERY / POWER  (AXP192)
// =====================================================

void batteryBegin() {
  if (axp.begin(Wire, AXP192_SLAVE_ADDRESS) == AXP_FAIL) {
    Serial.println("AXP192 FAIL"); axp_ok = false; return;
  }
  axp_ok = true;
  Serial.println("AXP192 OK");
  axp.adc1Enable(AXP202_VBUS_VOL_ADC1 | AXP202_VBUS_CUR_ADC1 |
                 AXP202_BATT_VOL_ADC1  | AXP202_BATT_CUR_ADC1, true);
  axp.setPowerOutPut(AXP192_LDO3, false);  // GPS off
  axp.setPowerOutPut(AXP192_LDO2, true);   // LoRa on
  Serial.println("GPS off, LoRa on");
}

float readBatteryVoltageAdc() {
#if DEMO_USB_POWER_ALWAYS_100
  return 4.20f;
#else
  return axp_ok ? axp.getBattVoltage() / 1000.0f : 0.0f;
#endif
}

static float linearMap(float x, float a, float b, float c, float d) {
  return c + (x - a) * (d - c) / (b - a);
}

uint8_t liionPctFromVoltage(float v) {
  if (isnan(v) || v <= 3.30f) return 0;
  if (v >= 4.20f) return 100;
  if (v >= 3.95f) return (uint8_t)roundf(linearMap(v, 3.95f, 4.20f, 80, 100));
  if (v >= 3.80f) return (uint8_t)roundf(linearMap(v, 3.80f, 3.95f, 20,  80));
  if (v >= 3.60f) return (uint8_t)roundf(linearMap(v, 3.60f, 3.80f,  5,  20));
  return             (uint8_t)roundf(linearMap(v, 3.30f, 3.60f,  0,   5));
}

float smoothBatteryVoltage(float v) {
  battEma = isnan(battEma) ? v : battEma + BATT_EMA_ALPHA * (v - battEma);
  return battEma;
}

bool readUsbPower() {
#if DEMO_USB_POWER_ALWAYS_100
  return true;
#else
  return axp_ok && axp.isVBUSPlug();
#endif
}

uint8_t readBatteryPercent() {
  return readUsbPower() ? 100
       : liionPctFromVoltage(smoothBatteryVoltage(readBatteryVoltageAdc()));
}


// =====================================================
// VIBRATION  (ERM motor on GPIO15)
// =====================================================

void triggerVibration(int pattern) {
  // 0=stop  1=short pulse  2=continuous  3=3× pulse
  switch (pattern) {
    case 0:
      digitalWrite(VIB_PIN, LOW);
      break;
    case 1:
      digitalWrite(VIB_PIN, HIGH);
      vTaskDelay(pdMS_TO_TICKS(200));
      digitalWrite(VIB_PIN, LOW);
      break;
    case 2:
      digitalWrite(VIB_PIN, HIGH);  // caller responsible for stopping via pattern 0
      break;
    case 3:
      for (int i = 0; i < 3; i++) {
        digitalWrite(VIB_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(150));
        digitalWrite(VIB_PIN, LOW);
        vTaskDelay(pdMS_TO_TICKS(150));
      }
      break;
  }
  Serial.printf("[VIB] %d\n", pattern);
}


// =====================================================
// FALL DETECTION  (LIS3DH + GPIO25 hardware interrupt)
// =====================================================

void enterState(FallState s) {
  fallState      = s;
  stateEnterTime = millis();
  if (s == STATE_CONFIRMING) {
    confirmTotalSamples = confirmStillSamples = 0;
    lastNonStillTime    = millis();
    currentStillStart   = maxStillDuration = 0;
    wasStill            = false;
    postAxSum = postAySum = postAzSum = 0;
    postOrientationSamples = 0;
  }
  if (s == STATE_FALL_DETECTED) latestFallAlarm = true;
  if (s == STATE_IDLE)          latestFallAlarm = false;
}

void readAccel() {
  if (!lis_ok) return;
  sensors_event_t e;
  if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(10))) {
    lis.getEvent(&e);
    xSemaphoreGive(i2cMutex);
  } else {
    return;
  }
  lastAx = e.acceleration.x;
  lastAy = e.acceleration.y;
  lastAz = e.acceleration.z;
  float mag  = sqrt(lastAx*lastAx + lastAy*lastAy + lastAz*lastAz);
  lastSvmG   = mag / G;
  lastDeltaG = hasPrevSvm ? fabs(lastSvmG - prevSvmG) : 0;
  hasPrevSvm = true;
  prevSvmG   = lastSvmG;
}

bool isLowMotionNow() {
  return (lastSvmG  >= STILL_LOWER_G && lastSvmG  <= STILL_UPPER_G)
      && (lastDeltaG <= STILL_DELTA_G);
}

void updateBaselineOrientation() {
  if (!lis_ok) return;
  if (!baselineReady) {
    baseAx = lastAx; baseAy = lastAy; baseAz = lastAz;
    baselineReady = true; return;
  }
  if (lastSvmG >= STILL_LOWER_G && lastSvmG <= STILL_UPPER_G
      && lastSvmG < IMPACT_THRESHOLD_G) {
    const float a = 0.03f;
    baseAx = (1-a)*baseAx + a*lastAx;
    baseAy = (1-a)*baseAy + a*lastAy;
    baseAz = (1-a)*baseAz + a*lastAz;
  }
}

float angleBetweenVectors(float x1,float y1,float z1,
                           float x2,float y2,float z2) {
  float dot = x1*x2 + y1*y2 + z1*z2;
  float n1  = sqrt(x1*x1 + y1*y1 + z1*z1);
  float n2  = sqrt(x2*x2 + y2*y2 + z2*z2);
  if (n1 < 1e-4f || n2 < 1e-4f) return 0;
  return acos(constrain(dot/(n1*n2), -1.0f, 1.0f)) * 180.0f / PI;
}

void triggerImmediateSend() {
  os_clearCallback(&sendjob);
  os_setCallback(&sendjob, do_send);
}

void updateFallStateMachine() {
  if (!lis_ok) return;
  unsigned long now = millis();
  if (lastSvmG < FREEFALL_THRESHOLD_G) lastLowGTime = now;
  bool lowMotion = isLowMotionNow();

  switch (fallState) {

    case STATE_IDLE:
      updateBaselineOrientation();
      if (lisInterruptFired || lastSvmG > IMPACT_THRESHOLD_G) {
        lisInterruptFired   = false;
        impactSvmG          = lastSvmG;
        impactHadRecentLowG = (now - lastLowGTime <= LOWG_WINDOW_MS);
        Serial.printf("!IMPACT %.2fg lowG=%d\n", lastSvmG, impactHadRecentLowG);
        enterState(STATE_CONFIRMING);
      }
      break;

    case STATE_CONFIRMING: {
      lisInterruptFired = false;
      confirmTotalSamples++;
      if (lowMotion) {
        confirmStillSamples++;
        if (!wasStill) { currentStillStart = now; wasStill = true; }
        unsigned long dur = now - currentStillStart;
        if (dur > maxStillDuration) maxStillDuration = dur;
        postAxSum += lastAx; postAySum += lastAy; postAzSum += lastAz;
        postOrientationSamples++;
      } else {
        wasStill = false; lastNonStillTime = now;
      }

      float stillRatio        = confirmTotalSamples > 0
                              ? (float)confirmStillSamples / confirmTotalSamples : 0;
      unsigned long quietTail = now - lastNonStillTime;
      float postureChangeDeg  = 0;
      bool  postureOK         = false;
      if (postOrientationSamples > 0) {
        postureChangeDeg = angleBetweenVectors(
          baseAx, baseAy, baseAz,
          postAxSum/postOrientationSamples,
          postAySum/postOrientationSamples,
          postAzSum/postOrientationSamples);
        postureOK = (postureChangeDeg >= POSTURE_CHANGE_DEG);
      }

      bool afterGrace   = (now - stateEnterTime >= STRUGGLE_GRACE_MS);
      bool ratioOK      = (stillRatio >= STILL_RATIO_REQUIRED);
      bool tailOK       = (quietTail  >= FINAL_STILL_MS);
      bool maxStillOK   = (maxStillDuration >= MAX_STILL_MS);
      bool impactStrong = (impactSvmG >= VERY_STRONG_IMPACT_G);
      bool evidenceOK   = impactHadRecentLowG || postureOK
                        || (impactStrong && stillRatio >= 0.20f);
      if (REQUIRE_POSTURE_CHANGE) evidenceOK = evidenceOK && postureOK;
      bool stillEvidOK  = maxStillOK || (tailOK && ratioOK);
      bool confirmed    = afterGrace && evidenceOK && stillEvidOK;

      if (confirmed) {
        Serial.printf("FALL! ratio=%.2f tail=%lums impact=%.2fg\n",
                      stillRatio, quietTail, impactSvmG);
        fallCount++; pendingFallAlert = true;
        enterState(STATE_FALL_DETECTED);
        triggerImmediateSend();
      } else if (now - stateEnterTime >= CONFIRM_DURATION_MS) {
        Serial.println("Not a fall, reset");
        enterState(STATE_IDLE);
      }
      break;
    }

    case STATE_FALL_DETECTED:
      if (now - stateEnterTime >= ALARM_DURATION_MS) {
        enterState(STATE_IDLE);
      }
      break;
  }
}


// =====================================================
// LORA TX / RX
// =====================================================

void do_send(osjob_t* j) {
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println("TXRXPEND, retry");
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(5), do_send);
    return;
  }

  bool    usb  = readUsbPower();
  uint8_t bat  = readBatteryPercent();
  bool    fall = pendingFallAlert;
  pendingFallAlert = false;

  // 9-byte binary payload:
  // [0-1] temp  ×10  int16
  // [2]   hum   ×2   uint8
  // [3-4] WBGT  ×10  int16
  // [5]   risk       uint8  0=normal 1=warn 2=critical
  // [6]   bat%       uint8
  // [7]   flags      uint8  b0=fall b1=usb b2=active b[7:5]=ver(1)
  // [8]   BPM        uint8

  int16_t t16 = (int16_t)constrain(lroundf(airTemp       * 10), -32768, 32767);
  int16_t w16 = (int16_t)constrain(lroundf(effectiveWBGT * 10), -32768, 32767);
  uint8_t h8  = (uint8_t)constrain((int)lroundf(humidity  * 2),       0,   200);
  uint8_t fl  = (fall?1:0) | (usb?2:0) | 0x04 | (1u<<5);

  payloadBin[0] = (uint8_t)((uint16_t)t16 >> 8);
  payloadBin[1] = (uint8_t)((uint16_t)t16 & 0xFF);
  payloadBin[2] = h8;
  payloadBin[3] = (uint8_t)((uint16_t)w16 >> 8);
  payloadBin[4] = (uint8_t)((uint16_t)w16 & 0xFF);
  payloadBin[5] = (uint8_t)currentRisk;
  payloadBin[6] = bat;
  payloadBin[7] = fl;
  payloadBin[8] = (uint8_t)constrain(beatAvg, 0, 255);

  Serial.printf("TX T=%.1f H=%.1f W=%.1f (LocW=%.1f ExtW=%.1f) Sk=%.1f R=%d F=%d BPM=%d BAT=%d%%\n",
                (float)airTemp, (float)humidity, (float)effectiveWBGT,
                (float)localWBGT, (float)externalWBGT, (float)skinTemp, (int)currentRisk, fall, (int)beatAvg, bat);
  if (axp_ok) {
    if (usb) Serial.printf("[USB] %.2fV %.1fmA\n",
                            axp.getVbusVoltage()/1000.0f, axp.getVbusCurrent());
    else      Serial.printf("[BAT] %.2fV %.1fmA\n",
                            axp.getBattVoltage()/1000.0f, axp.getBattDischargeCurrent());
  }

  LMIC_setTxData2(1, payloadBin, sizeof(payloadBin), 0);
  Serial.println("Queued 9B");
}

void onEvent(ev_t ev) {
  switch (ev) {
    case EV_TXSTART:
      Serial.println("EV_TXSTART");
      break;

    case EV_TXCOMPLETE:
      Serial.println("EV_TXCOMPLETE");
      lastTxTime = millis();

      if (LMIC.dataLen) {
        uint8_t cmd = LMIC.frame[LMIC.dataBeg];
        Serial.printf("[DL] 0x%02X len=%d\n", cmd, LMIC.dataLen);

        if (cmd == 0xF1) {
          triggerVibration(2);
        } else if (cmd == 0xF2 && LMIC.dataLen >= 3) {
          uint16_t raw = ((uint16_t)LMIC.frame[LMIC.dataBeg+1] << 8)
                                  | LMIC.frame[LMIC.dataBeg+2];
          externalWBGT         = raw / 10.0f;
          lastExternalWBGTTime = millis();
          Serial.printf("Ext WBGT=%.1f\n", externalWBGT);
          determineHeatStress();
        } else if (cmd == 0xF3) {
          alarmMuted    = true;
          muteStartTime = millis();
          externalWBGT  = 0;
          triggerVibration(0);
          Serial.println("Alarm muted");
        } else if (cmd == 0xF4) {
          triggerVibration(3);
          Serial.println("Hydration reminder");
        }
      }
      os_setTimedCallback(&sendjob,
                          os_getTime() + sec2osticks(txInterval), do_send);
      break;

    default:
      Serial.printf("EV %u\n", (unsigned)ev);
      break;
  }
}


// =====================================================
// FREERTOS TASKS
// =====================================================

// Core 1 — MAX30102 心率 + LIS3DH 跌倒检测 (20ms)
void TaskBioMotion(void* pv) {
  for (;;) {
    // 心率
    long irValue = 0;
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(10))) {
      irValue = particleSensor.getIR();
      xSemaphoreGive(i2cMutex);
    }
    if (irValue < 50000) {
      beatAvg = 0; beatsPerMinute = 0;
      for (byte x = 0; x < RATE_SIZE; x++) rates[x] = 0;
      rateSpot = 0;
    } else if (checkForBeat(irValue)) {
      long delta     = millis() - lastBeat;
      lastBeat       = millis();
      beatsPerMinute = 60.0f / (delta / 1000.0f);
      if (beatsPerMinute > 20 && beatsPerMinute < 255) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;
        beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
      }
    }

    // 跌倒检测 — lisInterruptFired 由 GPIO25 ISR 设置
    readAccel();
    updateFallStateMachine();

    vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
  }
}

// Core 0 — BME280 + MLX90614 + 热应力判定 (10s)
void TaskEnvironment(void* pv) {
  for (;;) {
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100))) {
      if (bme_ok) {
        float t = bme.readTemperature();
        float h = bme.readHumidity();
        if (!isnan(t) && t > -10.0f && t < 60.0f)  airTemp  = t;
        if (!isnan(h) && h >= 0.0f  && h <= 100.0f) humidity = h;
      }
      if (mlx_ok) {
        float obj = mlx.readObjectTempC();
        // 基本有效性过滤：正常体表温度范围 30–42°C
        if (!isnan(obj) && obj > 30.0f && obj < 42.0f) {
          skinTemp = obj;
        }
      }
      xSemaphoreGive(i2cMutex);
    }

    determineHeatStress();

    Serial.printf("ENV T=%.1f H=%.1f Sk=%.1f W=%.1f (LocW=%.1f ExtW=%.1f) DI=%.1f R=%d BPM=%d\n",
                  (float)airTemp, (float)humidity, (float)skinTemp,
                  (float)effectiveWBGT, (float)localWBGT, (float)externalWBGT, (float)DI, (int)currentRisk, (int)beatAvg);

    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}


// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n>>> HeatWatch Merged Firmware <<<");

  // 振动马达
  pinMode(VIB_PIN, OUTPUT);
  digitalWrite(VIB_PIN, LOW);

  // LIS3DH INT1 中断，GPIO25，上升沿触发
  pinMode(LIS_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(LIS_INT_PIN), onLisInterrupt, RISING);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  i2cMutex = xSemaphoreCreateMutex();

  batteryBegin();  // AXP192: GPS off, LoRa on

  bme_ok = bme.begin(0x77) || bme.begin(0x76);
  Serial.println(bme_ok ? "BME280 OK" : "BME280 FAIL");

  // MLX90614 默认 I2C 地址 0x5A，共用 SDA/SCL
  mlx_ok = mlx.begin(0x5A, &Wire);
  Serial.println(mlx_ok ? "MLX90614 OK" : "MLX90614 FAIL");

  lis_ok = lis.begin(0x18) || lis.begin(0x19);
  if (lis_ok) {
    lis.setRange(LIS3DH_RANGE_8_G);
    lis.setDataRate(LIS3DH_DATARATE_100_HZ);
    // INT1: 任意轴超 ~2.5g 持续 50ms 触发
    Wire.beginTransmission(0x18); Wire.write(0x36); Wire.write(40);   Wire.endTransmission();
    Wire.beginTransmission(0x18); Wire.write(0x37); Wire.write(5);    Wire.endTransmission();
    Wire.beginTransmission(0x18); Wire.write(0x30); Wire.write(0x2A); Wire.endTransmission();
    Wire.beginTransmission(0x18); Wire.write(0x22); Wire.write(0x40); Wire.endTransmission();
    Serial.println("LIS3DH OK — INT1 → GPIO25");
    readAccel(); updateBaselineOrientation();
  } else {
    Serial.println("LIS3DH FAIL");
  }

  if (particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x0A);
    Serial.println("MAX30102 OK");
  } else {
    Serial.println("MAX30102 FAIL");
  }

  SPI.begin(5, 19, 27, 18);
  os_init();
  LMIC_reset();

  uint8_t appskey[sizeof(APPSKEY)];
  uint8_t nwkskey[sizeof(NWKSKEY)];
  memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
  memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
  LMIC_setSession(0x1, DEVADDR, nwkskey, appskey);
  LMIC_selectSubBand(1);
  LMIC_setLinkCheckMode(0);
  LMIC.dn2Dr   = DR_SF9;
  LMIC.rxDelay = 1;
  LMIC_setAdrMode(0);
  LMIC_setDrTxpow(DR_SF7, 14);
  LMIC_setClockError(MAX_CLOCK_ERROR * 80 / 100);

  xTaskCreatePinnedToCore(TaskBioMotion,   "Bio", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskEnvironment, "Env", 4096, NULL, 1, NULL, 0);

  Serial.println("Setup done, first TX in 20s");
}


// =====================================================
// LOOP  (Core 1 — LoRa runloop only)
// =====================================================

void loop() {
  os_runloop_once();

  unsigned long now = millis();
  if (loopStartTime == 0) loopStartTime = now;

  if (lastTxTime == 0 && (now - loopStartTime > 20000)) {
    lastTxTime = now;
    do_send(&sendjob);
  }
}
