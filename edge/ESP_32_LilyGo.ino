#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <math.h>
#include <axp20x.h> // new AXP192 power management library

// ======================================================
// HeatWatch ESP32 / T-Beam Firmware
// BME280 + LIS3DH + LoRaWAN ABP + AXP192 Power Management
//
// Improvements:
// 1. 8-byte binary LoRaWAN payload
// 2. Demo-friendly fall detection
// 3. Allows post-fall struggle before final stillness
// 4. USB demo battery mode
// 5. Optional LED alarm
// 6. [NEW] AXP192 Integration & GPS Power Down
// ======================================================


// ================= USER CONFIG =================

// ---------- TTN ABP credentials ----------
// Replace with your real TTN ABP values.
static const PROGMEM u1_t NWKSKEY[16] = {
  0x65, 0x13, 0x36, 0x98, 0x71, 0x48, 0xC0, 0xBD, 0x61, 0x7A, 0x85, 0x35, 0xDC, 0xFF, 0x0D, 0xF6
};

static const u1_t PROGMEM APPSKEY[16] = {
  0x05, 0x13, 0x55, 0xCC, 0x01, 0x8D, 0xD0, 0x40, 0x3A, 0xB5, 0x08, 0x5A, 0xEB, 0x8A, 0x11, 0xF7
};

static const u4_t DEVADDR = 0x260D9A3E;


// ---------- Payload mode ----------
// 1 = compact 8-byte binary payload
// 0 = readable text payload for debugging
#define USE_BINARY_PAYLOAD 1


// ---------- Demo battery mode ----------
// 1 = USB demo mode: usb_power=1, battery=100%, vbat=4.20V
// 0 = try AXP192 real battery reading
// 【建议：如果要看真实耗电量，请改为 0】
#define DEMO_USB_POWER_ALWAYS_100 0


// ---------- LED / vibration indicator ----------
#define ENABLE_LED_ALARM 1

// External LED connected to GPIO4:
// GPIO4 -> resistor -> LED long leg (+)
// LED short leg (-) -> GND
#define LED_PIN 4
#define LED_ACTIVE_HIGH 1

// Vibration / movement indicator thresholds.
// These are only for LED display, not for fall detection.
const float LED_VIBRATION_SVM_G   = 1.35f;
const float LED_VIBRATION_DELTA_G = 0.12f;
const unsigned long LED_VIBRATION_HOLD_MS = 800;


// ================= FORWARD DECLARATIONS =================

enum FallState {
  STATE_IDLE,
  STATE_CONFIRMING,
  STATE_FALL_DETECTED
};

void do_send(osjob_t* j);
void onEvent(ev_t ev);

void enterState(FallState s);
void readAccel();
void updateFallStateMachine();
void updateBaselineOrientation();
bool isLowMotionNow();
float angleBetweenVectors(float ax1, float ay1, float az1,
                          float ax2, float ay2, float az2);
void triggerImmediateSend();
void printFallDecision(float stillRatio,
                       unsigned long quietTail,
                       float postureChangeDeg,
                       bool ratioOK,
                       bool tailOK,
                       bool maxStillOK,
                       bool postureOK,
                       bool impactStrong,
                       bool evidenceOK,
                       bool afterGrace,
                       bool confirmed);

float calcStullWetBulb(float T, float RH);
float calcWBGT(float T, float RH);
uint8_t hsiRiskLevel(float wbgt);

void batteryBegin();
float readBatteryVoltageAdc();
uint8_t liionPctFromVoltage(float v);
float smoothBatteryVoltage(float sampleV);
bool readUsbPower();
uint8_t readBatteryPercent();

uint8_t packPayload8(uint8_t out[8],
                     float tempC,
                     float humPct,
                     float wbgtC,
                     uint8_t risk,
                     uint8_t batteryPct,
                     bool fall,
                     bool usbPower,
                     bool activeSignal);

void printPayloadHex(const uint8_t* data, uint8_t len);
void updateLocalAlarm();


// ================= LMIC ABP FUNCTIONS =================

void os_getArtEui(u1_t* buf) {}
void os_getDevEui(u1_t* buf) {}
void os_getDevKey(u1_t* buf) {}


// ================= T-BEAM LORA PIN MAP =================

const lmic_pinmap lmic_pins = {
  .nss = 18,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 23,
  .dio = {26, 33, 32},
};


// ================= I2C / SENSOR CONFIG =================

#define I2C_SDA 21
#define I2C_SCL 22

#define LIS3DH_ADDR_PRIMARY 0x18
#define LIS3DH_ADDR_BACKUP  0x19

#define BME280_ADDR_PRIMARY 0x77
#define BME280_ADDR_BACKUP  0x76

Adafruit_LIS3DH lis = Adafruit_LIS3DH();
Adafruit_BME280 bme;
AXP20X_Class axp; // Initialize the object AXP

bool lis_ok = false;
bool bme_ok = false;
bool axp_ok = false; // Flag for AXP initialize


// ================= FALL DETECTION PARAMETERS =================

const float G = 9.80665f;

// Impact and low-g evidence.
const float IMPACT_THRESHOLD_G       = 2.7f;
const float FREEFALL_THRESHOLD_G     = 0.70f;
const unsigned long LOWG_WINDOW_MS   = 900;

// Stillness detection.
const float STILL_LOWER_G            = 0.85f;
const float STILL_UPPER_G            = 1.15f;
const float STILL_DELTA_G            = 0.08f;

// New struggle-friendly confirmation logic.
const unsigned long CONFIRM_DURATION_MS = 8000;  
const unsigned long STRUGGLE_GRACE_MS   = 2500;  
const unsigned long FINAL_STILL_MS      = 1200;  
const unsigned long MAX_STILL_MS        = 1500;  
const float STILL_RATIO_REQUIRED        = 0.30f; 

const float POSTURE_CHANGE_DEG          = 8.0f;
const bool REQUIRE_POSTURE_CHANGE       = false;

const float VERY_STRONG_IMPACT_G        = 5.0f;

const unsigned long ALARM_DURATION_MS   = 5000;
const unsigned long SAMPLE_INTERVAL_MS  = 20;


// ================= LORA TRANSMISSION =================

const unsigned TX_INTERVAL = 60;
static osjob_t sendjob;

#if USE_BINARY_PAYLOAD
static uint8_t payloadBin[8];
#else
static uint8_t payloadText[96];
#endif


// ================= FALL STATE VARIABLES =================
FallState fallState = STATE_IDLE;
unsigned long stateEnterTime = 0;
unsigned long lastSampleTime = 0;
unsigned int confirmTotalSamples = 0;
unsigned int confirmStillSamples = 0;
unsigned long lastNonStillTime = 0;
unsigned long currentStillStart = 0;
unsigned long maxStillDuration = 0;
bool wasStill = false;
float lastAx = 0, lastAy = 0, lastAz = 0;
float lastSvmG = 0, prevSvmG = 1.0f, lastDeltaG = 0;
bool hasPrevSvm = false;
unsigned long fallCount = 0;
bool pendingFallAlert = false;
bool baselineReady = false;
float baseAx = 0, baseAy = 0, baseAz = G;
float postAxSum = 0, postAySum = 0, postAzSum = 0;
unsigned int postOrientationSamples = 0;
float impactSvmG = 0;
bool impactHadRecentLowG = false;
unsigned long lastLowGTime = 0;
uint8_t latestRisk = 0;
bool latestFallAlarm = false;
// LED vibration indicator state
unsigned long lastVibrationLedTime = 0;

// ================= BATTERY VARIABLES =================
static float battEma = NAN;
static const float BATT_EMA_ALPHA = 0.15f;

// ================= HEAT STRESS FUNCTIONS =================
float calcStullWetBulb(float T, float RH) {
  return T * atan(0.151977f * sqrt(RH + 8.313659f)) + atan(T + RH) - atan(RH - 1.676331f) + 0.00391838f * pow(RH, 1.5f) * atan(0.023101f * RH) - 4.686035f;
}

float calcWBGT(float T, float RH) {
  return 0.7f * calcStullWetBulb(T, RH) + 0.3f * T;
}

uint8_t hsiRiskLevel(float wbgt) {
  if (wbgt < 28.0f) return 0;
  if (wbgt < 30.0f) return 1;
  if (wbgt < 32.0f) return 2;
  return 3;
}


// ================= BATTERY / POWER FUNCTIONS (MODIFIED) =================

void batteryBegin() {
  // Initialize AXP192
  int ret = axp.begin(Wire, AXP192_SLAVE_ADDRESS);
  if (ret == AXP_FAIL) {
    Serial.println("AXP192 Initialization FAILED!");
    axp_ok = false;
  } else {
    Serial.println("AXP192 Initialization OK!");
    axp_ok = true;
    
    // Enable ADC sampling
    axp.adc1Enable(AXP202_VBUS_VOL_ADC1 | AXP202_VBUS_CUR_ADC1 | AXP202_BATT_VOL_ADC1 | AXP202_BATT_CUR_ADC1, true);

    // Turn off GPS to save power
    axp.setPowerOutPut(AXP192_LDO3, false);
    Serial.println(">>> GPS Power (LDO3) is turned OFF for power saving.");

    // Make sure the power of LoRA is on
    axp.setPowerOutPut(AXP192_LDO2, true);
    Serial.println(">>> LoRa Power (LDO2) is enabled.");
  }
}

float readBatteryVoltageAdc() {
#if DEMO_USB_POWER_ALWAYS_100
  return 4.20f;
#else
  if (!axp_ok) return 0.0f;
  return axp.getBattVoltage() / 1000.0f; // AXP read mV and convert to voltage
#endif
}

float linearMapFloat(float x, float inMin, float inMax, float outMin, float outMax) {
  return outMin + (x - inMin) * (outMax - outMin) / (inMax - inMin);
}

uint8_t liionPctFromVoltage(float v) {
  if (isnan(v)) return 0;
  if (v >= 4.20f) return 100;
  if (v <= 3.30f) return 0;

  if (v >= 3.95f) return (uint8_t)roundf(linearMapFloat(v, 3.95f, 4.20f, 80.0f, 100.0f));
  if (v >= 3.80f) return (uint8_t)roundf(linearMapFloat(v, 3.80f, 3.95f, 20.0f, 80.0f));
  if (v >= 3.60f) return (uint8_t)roundf(linearMapFloat(v, 3.60f, 3.80f, 5.0f, 20.0f));
  return (uint8_t)roundf(linearMapFloat(v, 3.30f, 3.60f, 0.0f, 5.0f));
}

float smoothBatteryVoltage(float sampleV) {
  if (isnan(battEma)) battEma = sampleV;
  else battEma = battEma + BATT_EMA_ALPHA * (sampleV - battEma);
  return battEma;
}

bool readUsbPower() {
#if DEMO_USB_POWER_ALWAYS_100
  return true;
#else
  if (!axp_ok) return false;
  return axp.isVBUSPlug();
#endif
}

uint8_t readBatteryPercent() {
  bool usbPower = readUsbPower();
  float vbat = readBatteryVoltageAdc();
  float smoothV = smoothBatteryVoltage(vbat);
  uint8_t engineeringPct = liionPctFromVoltage(smoothV);

  if (usbPower) return 100;
  return engineeringPct;
}


static inline int16_t clampI16(long v) {
  if (v > 32767) return 32767;
  if (v < -32768) return -32768;
  return (int16_t)v;
}

uint8_t packPayload8(uint8_t out[8], float tempC, float humPct, float wbgtC, uint8_t risk, uint8_t batteryPct, bool fall, bool usbPower, bool activeSignal) {
  int16_t temp_x10 = clampI16(lroundf(tempC * 10.0f));
  uint8_t hum_x2   = (uint8_t)constrain((int)lroundf(humPct * 2.0f), 0, 200);
  int16_t wbgt_x10 = clampI16(lroundf(wbgtC * 10.0f));
  const uint8_t protoVer = 1;
  uint8_t flags = 0;
  flags |= (fall ? 1u : 0u) << 0;
  flags |= (usbPower ? 1u : 0u) << 1;
  flags |= (activeSignal ? 1u : 0u) << 2;
  flags |= (protoVer & 0x07u) << 5;

  out[0] = (uint8_t)((uint16_t)temp_x10 >> 8);
  out[1] = (uint8_t)((uint16_t)temp_x10 & 0xFF);
  out[2] = hum_x2;
  out[3] = (uint8_t)((uint16_t)wbgt_x10 >> 8);
  out[4] = (uint8_t)((uint16_t)wbgt_x10 & 0xFF);
  out[5] = risk;
  out[6] = batteryPct;
  out[7] = flags;
  return 8;
}

void printPayloadHex(const uint8_t* data, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(data[i], HEX);
    if (i < len - 1) Serial.print(" ");
  }
  Serial.println();
}

void enterState(FallState s) {
  fallState = s;
  stateEnterTime = millis();
  if (s == STATE_CONFIRMING) {
    confirmTotalSamples = 0; confirmStillSamples = 0; lastNonStillTime = millis();
    currentStillStart = 0; maxStillDuration = 0; wasStill = false;
    postAxSum = 0; postAySum = 0; postAzSum = 0; postOrientationSamples = 0;
  }
  if (s == STATE_FALL_DETECTED) latestFallAlarm = true;
  if (s == STATE_IDLE) latestFallAlarm = false;
}

void readAccel() {
  if (!lis_ok) return;

  sensors_event_t e;
  lis.getEvent(&e);

  lastAx = e.acceleration.x;
  lastAy = e.acceleration.y;
  lastAz = e.acceleration.z;

  float mag = sqrt(lastAx * lastAx + lastAy * lastAy + lastAz * lastAz);
  lastSvmG = mag / G;

  if (hasPrevSvm) {
    lastDeltaG = fabs(lastSvmG - prevSvmG);
  } else {
    lastDeltaG = 0;
    hasPrevSvm = true;
  }

  prevSvmG = lastSvmG;

  // LED-only vibration indicator.
  // This does not affect fall detection.
  if (lastSvmG >= LED_VIBRATION_SVM_G || lastDeltaG >= LED_VIBRATION_DELTA_G) {
    lastVibrationLedTime = millis();
  }
}

bool isLowMotionNow() {
  bool nearOneG = (lastSvmG >= STILL_LOWER_G && lastSvmG <= STILL_UPPER_G);
  bool smallChange = (lastDeltaG <= STILL_DELTA_G);
  return nearOneG && smallChange;
}

void updateBaselineOrientation() {
  if (!lis_ok) return;
  if (!baselineReady) {
    baseAx = lastAx; baseAy = lastAy; baseAz = lastAz; baselineReady = true; return;
  }
  bool safeToUpdate = (lastSvmG >= STILL_LOWER_G) && (lastSvmG <= STILL_UPPER_G) && (lastSvmG < IMPACT_THRESHOLD_G);
  if (safeToUpdate) {
    const float alpha = 0.03f;
    baseAx = (1.0f - alpha) * baseAx + alpha * lastAx;
    baseAy = (1.0f - alpha) * baseAy + alpha * lastAy;
    baseAz = (1.0f - alpha) * baseAz + alpha * lastAz;
  }
}

float angleBetweenVectors(float ax1, float ay1, float az1, float ax2, float ay2, float az2) {
  float dot = ax1 * ax2 + ay1 * ay2 + az1 * az2;
  float n1 = sqrt(ax1 * ax1 + ay1 * ay1 + az1 * az1);
  float n2 = sqrt(ax2 * ax2 + ay2 * ay2 + az2 * az2);
  if (n1 <= 0.0001f || n2 <= 0.0001f) return 0;
  float c = dot / (n1 * n2);
  if (c > 1.0f) c = 1.0f;
  if (c < -1.0f) c = -1.0f;
  return acos(c) * 180.0f / PI;
}

void triggerImmediateSend() {
  os_clearCallback(&sendjob);
  os_setCallback(&sendjob, do_send);
}

void printFallDecision(float stillRatio, unsigned long quietTail, float postureChangeDeg, bool ratioOK, bool tailOK, bool maxStillOK, bool postureOK, bool impactStrong, bool evidenceOK, bool afterGrace, bool confirmed) {
  Serial.println("\n---- Fall confirmation result ----");
  Serial.printf("Impact SVM: %.2f g\n", impactSvmG);
  Serial.printf("Recent low-g: %d\n", impactHadRecentLowG ? 1 : 0);
  Serial.printf("Still ratio: %.2f\n", stillRatio);
  Serial.printf("Quiet tail: %lu ms\n", quietTail);
  Serial.printf("Max still duration: %lu ms\n", maxStillDuration);
  Serial.printf("Posture change: %.1f deg\n", postureChangeDeg);
  Serial.printf("afterGrace=%d ratioOK=%d tailOK=%d maxStillOK=%d postureOK=%d impactStrong=%d evidenceOK=%d confirmed=%d\n", afterGrace, ratioOK, tailOK, maxStillOK, postureOK, impactStrong, evidenceOK, confirmed);
  Serial.println("----------------------------------");
}

void updateFallStateMachine() {
  if (!lis_ok) return;
  unsigned long now = millis();
  if (lastSvmG < FREEFALL_THRESHOLD_G) lastLowGTime = now;
  bool lowMotion = isLowMotionNow();

  switch (fallState) {
    case STATE_IDLE: {
      updateBaselineOrientation();
      if (lastSvmG > IMPACT_THRESHOLD_G) {
        impactSvmG = lastSvmG;
        impactHadRecentLowG = (now - lastLowGTime <= LOWG_WINDOW_MS);
        Serial.printf("\n!!! IMPACT detected. SVM = %.2f g | %s\n", lastSvmG, impactHadRecentLowG ? "recent low-g detected" : "no recent low-g");
        enterState(STATE_CONFIRMING);
      }
      break;
    }
    case STATE_CONFIRMING: {
      confirmTotalSamples++;
      if (lowMotion) {
        confirmStillSamples++;
        if (!wasStill) { currentStillStart = now; wasStill = true; }
        unsigned long currentStillDuration = now - currentStillStart;
        if (currentStillDuration > maxStillDuration) maxStillDuration = currentStillDuration;
        postAxSum += lastAx; postAySum += lastAy; postAzSum += lastAz; postOrientationSamples++;
      } else {
        wasStill = false; lastNonStillTime = now;
      }
      float stillRatio = confirmTotalSamples > 0 ? (float)confirmStillSamples / confirmTotalSamples : 0;
      unsigned long quietTail = now - lastNonStillTime;
      float postureChangeDeg = 0.0f;
      bool postureOK = false;
      if (postOrientationSamples > 0) {
        postureChangeDeg = angleBetweenVectors(baseAx, baseAy, baseAz, postAxSum/postOrientationSamples, postAySum/postOrientationSamples, postAzSum/postOrientationSamples);
        postureOK = (postureChangeDeg >= POSTURE_CHANGE_DEG);
      }
      bool afterGrace = (now - stateEnterTime >= STRUGGLE_GRACE_MS);
      bool ratioOK = (stillRatio >= STILL_RATIO_REQUIRED);
      bool tailOK = (quietTail >= FINAL_STILL_MS);
      bool maxStillOK = (maxStillDuration >= MAX_STILL_MS);
      bool impactStrong = (impactSvmG >= VERY_STRONG_IMPACT_G);
      bool evidenceOK = impactHadRecentLowG || postureOK || (impactStrong && stillRatio >= 0.20f);
      if (REQUIRE_POSTURE_CHANGE) evidenceOK = evidenceOK && postureOK;
      
      bool stillEvidenceOK = maxStillOK || (tailOK && ratioOK);
      bool confirmed = afterGrace && evidenceOK && stillEvidenceOK;

      if (confirmed) {
        printFallDecision(stillRatio, quietTail, postureChangeDeg, ratioOK, tailOK, maxStillOK, postureOK, impactStrong, evidenceOK, afterGrace, confirmed);
        fallCount++; pendingFallAlert = true;
        Serial.println("\n!!!! FALL DETECTED — sending emergency uplink !!!!\n");
        enterState(STATE_FALL_DETECTED);
        triggerImmediateSend();
      } else if (now - stateEnterTime >= CONFIRM_DURATION_MS) {
        printFallDecision(stillRatio, quietTail, postureChangeDeg, ratioOK, tailOK, maxStillOK, postureOK, impactStrong, evidenceOK, afterGrace, confirmed);
        Serial.println("--> Not a real fall. Reset to IDLE.");
        enterState(STATE_IDLE);
      }
      break;
    }
    case STATE_FALL_DETECTED: {
      if (now - stateEnterTime >= ALARM_DURATION_MS) enterState(STATE_IDLE);
      break;
    }
  }
}

void ledWrite(bool on) {
#if ENABLE_LED_ALARM
  if (LED_ACTIVE_HIGH) {
    digitalWrite(LED_PIN, on ? HIGH : LOW);
  } else {
    digitalWrite(LED_PIN, on ? LOW : HIGH);
  }
#endif
}

void updateLocalAlarm() {
#if ENABLE_LED_ALARM
  static unsigned long lastBlink = 0;
  static bool ledState = false;

  unsigned long now = millis();

  // Power-saving design:
  // LED stays off during normal operation.
  // LED only blinks rapidly after a confirmed fall.
  if (!latestFallAlarm) {
    digitalWrite(LED_PIN, LED_ACTIVE_HIGH ? LOW : HIGH);
    ledState = false;
    return;
  }

  // Confirmed fall: fast blink
  const unsigned long interval = 120;

  if (now - lastBlink >= interval) {
    lastBlink = now;
    ledState = !ledState;

    digitalWrite(
      LED_PIN,
      ledState ? (LED_ACTIVE_HIGH ? HIGH : LOW)
               : (LED_ACTIVE_HIGH ? LOW : HIGH)
    );
  }
#endif
}

void onEvent(ev_t ev) {
  Serial.print(os_getTime());
  Serial.print(": ");
  switch (ev) {
    case EV_TXSTART: Serial.println("EV_TXSTART"); break;
    case EV_TXCOMPLETE:
      Serial.println("EV_TXCOMPLETE — packet sent");
      os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
      break;
    case EV_JOINED: Serial.println("EV_JOINED"); break;
    case EV_JOIN_FAILED: Serial.println("EV_JOIN_FAILED"); break;
    case EV_JOIN_TXCOMPLETE: Serial.println("EV_JOIN_TXCOMPLETE: no JoinAccept"); break;
    default: Serial.printf("Event: %u\n", (unsigned)ev); break;
  }
}


// ================= SEND TELEMETRY (Modified to log power consumption) =================

void do_send(osjob_t* j) {
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println("OP_TXRXPEND, will retry");
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(5), do_send);
    return;
  }

  float temp = bme_ok ? bme.readTemperature() : 0.0f;
  float hum  = bme_ok ? bme.readHumidity()    : 0.0f;
  float wbgt = bme_ok ? calcWBGT(temp, hum)   : 0.0f;

  uint8_t risk = hsiRiskLevel(wbgt);
  latestRisk = risk;

  bool fallFlag = pendingFallAlert;
  pendingFallAlert = false;

  bool usbPower = readUsbPower();
  uint8_t batteryPct = readBatteryPercent();

  bool activeSignal = true;

  Serial.println("\n---- Telemetry ----");
  Serial.printf("T=%.1f C, H=%.1f %%, eWBGT=%.1f C, risk=%d, fall=%d, fallCount=%lu, usb=%d, battery=%d%%\n",
                temp, hum, wbgt, risk, fallFlag ? 1 : 0, fallCount, usbPower ? 1 : 0, batteryPct);

  // Using AXP to print the current and power consumption
if (axp_ok) {
    if (usbPower) {
      // Read USB current and voltage
      float vbus_v = axp.getVbusVoltage() / 1000.0f;
      float vbus_c = axp.getVbusCurrent(); 
      Serial.printf("[USB Powered] Voltage: %.2f V | Current: %.2f mA\n", vbus_v, vbus_c);
    } else {
      // Read battery voltage and current
      float vbat = axp.getBattVoltage() / 1000.0f;
      float batt_c = axp.getBattDischargeCurrent();
      Serial.printf("[Battery Powered] Voltage: %.2f V | Current: %.2f mA\n", vbat, batt_c);
    }
  }
  Serial.println("-------------------");

#if USE_BINARY_PAYLOAD
  uint8_t len = packPayload8(payloadBin, temp, hum, wbgt, risk, batteryPct, fallFlag, usbPower, activeSignal);
  Serial.printf("Sending binary payload, len=%d, hex=", len);
  printPayloadHex(payloadBin, len);
  LMIC_setTxData2(1, payloadBin, len, 0);
  Serial.println("Packet queued");
#else
  snprintf((char*)payloadText, sizeof(payloadText), "T:%.1f H:%.1f WBGT:%.1f HSI:%d FALL:%d CNT:%lu BAT:%d USB:%d", temp, hum, wbgt, risk, fallFlag ? 1 : 0, fallCount, batteryPct, usbPower ? 1 : 0);
  Serial.printf("Sending text payload: %s\n", payloadText);
  LMIC_setTxData2(1, payloadText, strlen((char*)payloadText), 0);
  Serial.println("Packet queued");
#endif
}


// ================= SETUP AND LOOP =================

void setup() {
  Serial.begin(115200);
  delay(2500);

  Serial.println("\n>>> HeatWatch + AXP192 Power Management + GPS Off <<<");

#if ENABLE_LED_ALARM
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ACTIVE_HIGH ? LOW : HIGH);
#endif

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  // Initialize battery and power management
  batteryBegin();

  lis_ok = lis.begin(LIS3DH_ADDR_PRIMARY) || lis.begin(LIS3DH_ADDR_BACKUP);
  if (lis_ok) {
    lis.setRange(LIS3DH_RANGE_8_G);
    lis.setDataRate(LIS3DH_DATARATE_100_HZ);
    Serial.println("LIS3DH OK");
  } else Serial.println("LIS3DH NOT FOUND");

  bme_ok = bme.begin(BME280_ADDR_PRIMARY) || bme.begin(BME280_ADDR_BACKUP);
  if (bme_ok) Serial.println("BME280 OK");
  else Serial.println("BME280 NOT FOUND");

  if (lis_ok) { readAccel(); updateBaselineOrientation(); }

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
  LMIC.dn2Dr = DR_SF9;
  LMIC_setDrTxpow(DR_SF7, 14);
  LMIC_setClockError(MAX_CLOCK_ERROR * 1 / 100);

  Serial.println("Setup complete. Sending first uplink...");
  do_send(&sendjob);
}

void loop() {
  os_runloop_once();
  updateLocalAlarm();

  unsigned long now = millis();
  if (now - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = now;
    readAccel();
    updateFallStateMachine();
  }
}
