// clang-format off
#include <lmic.h>
#include <hal/hal.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <Adafruit_BME280.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_MLX90614.h> // MLX90614 contact-less skin temperature sensor
#include <Adafruit_Sensor.h>
#include <SPI.h>
#include <Wire.h>
#include <axp20x.h>
#include <math.h>
// clang-format on

// ======================================================
// HeatWatch ESP32 / T-Beam — Merged Firmware
//
// Sensors    : BME280 (Air Temp/Humidity), LIS3DH (Accelerometer),
//              MAX30102 (Heart Rate/SpO2), MLX90614 (Infrared Skin Temp)
// Features   : Advanced Fall detection (Hardware interrupt + SVM state machine),
//              Moran Physiological Strain Index (PSI) calculation,
//              Stull Wet-Bulb & dual-mode heat stress risks estimation,
//              AXP192 power management, FreeRTOS tasks, LoRaWAN ABP
// Payload    : 20-byte optimized binary uplink
// ======================================================

// ================= LoRaWAN ABP CREDENTIALS =================
// Network Session Key (MSB format)
static const PROGMEM u1_t NWKSKEY[16] = {0x65, 0x13, 0x36, 0x98, 0x71, 0x48,
                                         0xC0, 0xBD, 0x61, 0x7A, 0x85, 0x35,
                                         0xDC, 0xFF, 0x0D, 0xF6};
// Application Session Key (MSB format)
static const u1_t PROGMEM APPSKEY[16] = {0x05, 0x13, 0x55, 0xCC, 0x01, 0x8D,
                                         0xD0, 0x40, 0x3A, 0xB5, 0x08, 0x5A,
                                         0xEB, 0x8A, 0x11, 0xF7};
// End-device Address (hex format)
static const u4_t DEVADDR = 0x260D9A3E;

// Dummy callback implementations required by the LMIC stack
void os_getArtEui(u1_t *buf) {}
void os_getDevEui(u1_t *buf) {}
void os_getDevKey(u1_t *buf) {}

// ================= ESP32 / T-BEAM PIN MAP =================
// Pin mapping matching the physical connections on the LilyGO T-Beam v1.1 LoRa board
const lmic_pinmap lmic_pins = {
    .nss = 18,                  // LoRa chip select
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 23,                  // LoRa chip reset
    .dio = {26, 33, 32},        // DIO0, DIO1, DIO2 pins
};

#define I2C_SDA 21              // Shared I2C Data pin
#define I2C_SCL 22              // Shared I2C Clock pin
#define VIB_PIN 15              // Pin connected to the ERM vibration motor driver (transistor)
#define LIS_INT_PIN 25          // LIS3DH INT1 pin routed to ESP32 GPIO25 for hardware-level fall detection

// ================= USER CONFIGURATION =================
// If enabled (1), system simulates constant 100% USB power regardless of AXP192 VBUS detection
#define DEMO_USB_POWER_ALWAYS_100 0

// ================= SENSOR DRIVER INSTANCES =================
Adafruit_BME280 bme;            // Environmental sensor
Adafruit_LIS3DH lis = Adafruit_LIS3DH(); // Accelerometer
Adafruit_MLX90614 mlx;          // Infrared temperature sensor
MAX30105 particleSensor;        // Heart rate sensor
AXP20X_Class axp;               // AXP192 PMU power controller

// Hardware detection status flags
bool bme_ok = false;
bool lis_ok = false;
bool mlx_ok = false;
bool axp_ok = false;

// ================= INTERRUPT FLAG =================
// Volatile flag toggled by the hardware LIS3DH INT1 ISR
volatile bool lisInterruptFired = false;

// Interrupt Service Routine (ISR) mapped to GPIO25 (keeps footprint minimal, runs in IRAM)
void IRAM_ATTR onLisInterrupt() { lisInterruptFired = true; }

// ================= RTOS MUTEX =================
// Mutex to protect the shared I2C bus between Core 0 and Core 1 tasks
SemaphoreHandle_t i2cMutex;

// ================= GLOBAL REAL-TIME SENSOR DATA =================
volatile float airTemp = 0;              // Ambient temperature (°C) from BME280
volatile float humidity = 0;             // Relative Humidity (%) from BME280
volatile float skinTemp = 33.0f;         // Skin temperature (°C) from MLX90614 object reading
volatile float localWBGT = 0;            // WBGT calculated locally from BME280 readings
volatile float effectiveWBGT = 0;        // Chosen index: local WBGT or gateway's external WBGT broadcast
volatile float externalWBGT = 0;         // Broadcasted external WBGT from LoRa Downlink
volatile float DI = 0;                   // Thom's Discomfort Index
volatile int beatAvg = 0;                // 4-reading moving average Heart Rate (bpm) from MAX30102

// Heat stress safety risk scale
typedef enum { NORMAL, WARNING, CRITICAL } RiskLevel;
volatile RiskLevel currentRisk = NORMAL;

// External data age trackers
volatile unsigned long lastExternalWBGTTime = 0;
const unsigned long EXTERNAL_WBGT_TIMEOUT = 30UL * 60 * 1000; // 30 minutes data expiration limit

// Force mute control fields (10 minutes silence window)
volatile bool alarmMuted = false;
volatile unsigned long muteStartTime = 0;
const unsigned long MUTE_DURATION = 10UL * 60 * 1000; // 10 minutes duration

// Advanced physiological strain index estimation state
volatile float estimatedTc = 37.0f;      // Core body temperature (°C) estimated from heart rate and skin temp
volatile float currentPSI = 0.0f;        // Physiological Strain Index (Moran's 0-10 scale)
volatile float cumulativeHeatStrain = 0.0f; // Long-term cumulative thermal stress (integrating PSI overtime)

volatile unsigned long lastFallTriggerTime = 0; // Timestamp of the last confirmed fall event
bool max30102_is_sleeping = false;       // Flag tracking the sleep state of the MAX30102 sensor

// Determines whether the device should operate in low-power mode.
// In the first 30 minutes, or within 5 minutes after a fall detection, low power is disabled for continuous demo verification.
bool isPowerSavingMode() {
  if (millis() < 1800000) { // 30-minute Demo window
    return false;
  }
  if (lastFallTriggerTime > 0 && (millis() - lastFallTriggerTime < 300000)) { // 5-minute Fall recovery extension
    return false;
  }
  return true;
}

volatile unsigned long txInterval = 60; // Standard transmission interval in seconds
unsigned long lastTxTime = 0;           // Timestamp of the last transmission
unsigned long loopStartTime = 0;        // Timestamp tracking the device setup boot time

// ================= HEART RATE ALGORITHM BUFFER =================
const byte RATE_SIZE = 4;                // Small moving average window size to prevent memory overhead
byte rates[RATE_SIZE];                   // Array storing beat rates
byte rateSpot = 0;                       // Index pointer in the buffer
long lastBeat = 0;                       // Timestamp of the last detected pulse peak
float beatsPerMinute = 0;                // Calculated instaneous BPM value

// ================= BATTERY FILTERING =================
static float battEma = NAN;              // Exponential Moving Average filter accumulator for voltage stability
static const float BATT_EMA_ALPHA = 0.15f; // Smoothing factor for battery voltage EMA filter

// ================= FALL DETECTION CONSTRAINTS =================
const float G = 9.80665f;                // Standard gravity acceleration constant (m/s^2)
const float IMPACT_THRESHOLD_G = 2.7f;   // Threshold to detect primary collision event (g-force)
const float FREEFALL_THRESHOLD_G = 0.70f; // Threshold to detect start of free-fall phase (g-force)
const unsigned long LOWG_WINDOW_MS = 900; // Lookback window to verify free-fall weightlessness before impact
const float STILL_LOWER_G = 0.85f;       // Bottom g-force limit representing complete physical stillness
const float STILL_UPPER_G = 1.15f;       // Top g-force limit representing complete physical stillness
const float STILL_DELTA_G = 0.08f;       // Maximium allowable frame-to-frame change representing stillness
const unsigned long CONFIRM_DURATION_MS = 8000; // Entire stillness window duration following impact
const unsigned long STRUGGLE_GRACE_MS = 2500; // Initial delay to bypass potential post-fall immediate struggles
const unsigned long FINAL_STILL_MS = 1200; // Continuous stillness confirmation window
const unsigned long MAX_STILL_MS = 1500;   // Accumulative stillness confirmation window
const float STILL_RATIO_REQUIRED = 0.30f; // Required percentage of still frames in checking duration
const float POSTURE_CHANGE_DEG = 8.0f;   // Threshold representing posture deviation change (degrees)
const bool REQUIRE_POSTURE_CHANGE = false; // Posture validation override flag
const float VERY_STRONG_IMPACT_G = 5.0f;  // Severe impact bypass value (triggers fall easier)
const unsigned long ALARM_DURATION_MS = 5000; // Vibration trigger alarm duration (ms)
const unsigned long SAMPLE_INTERVAL_MS = 20; // 50Hz sampling frequency (20ms cycle)

// Fall detection state machine enumeration
enum FallState { STATE_IDLE, STATE_CONFIRMING, STATE_FALL_DETECTED };
FallState fallState = STATE_IDLE;        // Active fall detection state
unsigned long stateEnterTime = 0;        // Timestamp tracking the current state entry
unsigned int confirmTotalSamples = 0;    // Total count of accelerometer frames collected during confirmation
unsigned int confirmStillSamples = 0;    // Count of frames evaluated as "still" during confirmation
unsigned long lastNonStillTime = 0;      // Timestamp of the last frame showing movement
unsigned long currentStillStart = 0;     // Start timestamp of the current stillness segment
unsigned long maxStillDuration = 0;      // Longest contiguous stillness segment measured
bool wasStill = false;                   // State flag of the preceding frame
float lastAx = 0, lastAy = 0, lastAz = 0; // Raw tri-axial acceleration values
float lastSvmG = 0, prevSvmG = 1.0f, lastDeltaG = 0; // SVM tracking values
bool hasPrevSvm = false;                 // Initialization flag for SVM difference tracking
unsigned long fallCount = 0;             // Total count of confirmed falls
bool pendingFallAlert = false;           // Alert queue flag for upcoming LoRa packet
bool baselineReady = false;              // Baseline calculation status flag
float baseAx = 0, baseAy = 0, baseAz = G; // Tri-axial baseline reference vectors representing vertical rest
float postAxSum = 0, postAySum = 0, postAzSum = 0; // Accumulator fields for post-fall orientation calculation
unsigned int postOrientationSamples = 0; // Count of frames used to evaluate post-fall orientation
float impactSvmG = 0;                    // SVM force recorded during primary impact event
bool impactHadRecentLowG = false;        // Flag stating if a freefall preceded the collision
unsigned long lastLowGTime = 0;          // Timestamp of the last freefall event
bool latestFallAlarm = false;            // Current fall alarm state flag

// ================= LoRaWAN CONTEXTS =================
static osjob_t sendjob;                  // Structure tracking the periodic LoRa transmit task
static uint8_t payloadBin[20];           // Binary buffer holding the packaged uplink telemetry bytes

// ================= FUNCTION FORWARD DECLARATIONS =================
void do_send(osjob_t *j);
void onEvent(ev_t ev);
void enterState(FallState s);
void readAccel();
void updateFallStateMachine();
void updateBaselineOrientation();
bool isLowMotionNow();
float angleBetweenVectors(float, float, float, float, float, float);
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
// HEAT STRESS ENGINE
// =====================================================

// Stull formula for estimating wet-bulb temperature using air temperature and relative humidity
float calcStullWetBulb(float T, float RH) {
  return T * atan(0.151977f * sqrt(RH + 8.313659f)) + atan(T + RH) -
         atan(RH - 1.676331f) +
         0.00391838f * pow(RH, 1.5f) * atan(0.023101f * RH) - 4.686035f;
}

// Calculate Wet Bulb Globe Temperature (WBGT) using simplified outdoor approximation
float calcWBGT(float T, float RH) {
  return 0.7f * calcStullWetBulb(T, RH) + 0.3f * T;
}

// Calculate Discomfort Index (DI) using air and wet-bulb temperatures (Thom's Index)
float calcDI(float T, float RH) { return 0.5f * (T + calcStullWetBulb(T, RH)); }

// Evaluates physiological and environmental metrics to determine heat strain risk levels
void determineHeatStress() {
  // Outlier filtering: checks reasonable ranges for mining environments
  if (isnan(airTemp) || airTemp < -10.0f || airTemp > 70.0f)
    return;
  if (isnan(humidity) || humidity < 0.0f || humidity > 100.0f)
    return;

  // Compute environmental parameters
  localWBGT = calcWBGT(airTemp, humidity);
  DI = calcDI(airTemp, humidity);

  // Use broadcasted gateway WBGT if it's fresh (under 30 minutes), otherwise fallback to localized WBGT
  effectiveWBGT = (externalWBGT > 0 &&
                   millis() - lastExternalWBGTTime < EXTERNAL_WBGT_TIMEOUT)
                      ? externalWBGT
                      : localWBGT;

  // 1. Mute time check: implement 10-minute forced mute limit
  if (alarmMuted && (millis() - muteStartTime >= MUTE_DURATION)) {
    alarmMuted = false;
    Serial.println("[MUTE] 10-minute mute duration expired. Alarms unmuted.");
  }

  // 2. Heart rate data filtering: only accept readings of 60 ~ 180 bpm
  bool hrValid = (beatAvg >= 60 && beatAvg <= 180);
  float hrInput = hrValid
                      ? (float)beatAvg
                      : 70.0f; // Use resting heart rate of 70 bpm when heart rate is invalid or sensor is detached

  // 3. Calculate heat dissipation obstruction penalty coefficient (eta)
  float wetBulb = calcStullWetBulb(airTemp, humidity);
  float deltaEvap = estimatedTc - wetBulb;
  float eta =
      constrain(deltaEvap / 17.0f, 0.1f,
                1.0f); // 17.0f represents the temperature difference baseline between core temp (37°C) and comfortable wet-bulb temp (20°C)

  // 4. Update core body temperature using first-order differential equation (delta_t = 10s step size)
  float dTc = 0.000286f * hrInput - 0.005f * eta * (estimatedTc - skinTemp);
  estimatedTc += 10.0f * dTc;
  estimatedTc = constrain(estimatedTc, 35.0f,
                          42.0f); // Safety protection: limit core temperature to a physically reasonable range

  // 5. Moran Physiological Strain Index (PSI) calculation (dual-mode to prevent missing reports)
  float tcTerm = (estimatedTc - 37.0f) / 2.5f;
  if (tcTerm < 0.0f)
    tcTerm = 0.0f;

  if (hrValid) {
    // Normal estimation incorporating heart rate dynamics
    float hrTerm = (hrInput - 70.0f) / 110.0f;
    if (hrTerm < 0.0f)
      hrTerm = 0.0f;
    currentPSI = 5.0f * tcTerm + 5.0f * hrTerm;
  } else {
    // Single body temperature estimation mode when sensor is not worn / heart rate is invalid
    currentPSI = 10.0f * tcTerm;
  }
  currentPSI = constrain(currentPSI, 0.0f, 10.0f);

  // 6. Cumulative Heat Strain (CHS) leaky integration update
  float psiExcess = (currentPSI > 3.0f) ? (currentPSI - 3.0f) : 0.0f;
  float dCHS = 0.1f * psiExcess - 0.001f * cumulativeHeatStrain;
  cumulativeHeatStrain += 10.0f * dCHS;
  if (cumulativeHeatStrain < 0.0f)
    cumulativeHeatStrain = 0.0f;

  // 7. Upgraded dual risk determination (Physiological + Environmental thresholds)
  bool criticalEnv = (effectiveWBGT >= 30.0f || DI >= 32.0f);
  bool criticalPhys = (estimatedTc >= 38.5f || currentPSI >= 7.5f ||
                       cumulativeHeatStrain >= 50.0f);

  bool warningEnv = (effectiveWBGT >= 23.0f || DI >= 24.0f);
  bool warningPhys = (estimatedTc >= 37.8f || currentPSI >= 4.0f ||
                      cumulativeHeatStrain >= 20.0f);

  // Action state selection and vibration alarm triggers
  if (criticalEnv || criticalPhys) {
    currentRisk = CRITICAL;
    if (!alarmMuted)
      triggerVibration(2); // Continuous vibration mode
    Serial.printf("[ALERT] CRITICAL! Env(W=%.1f DI=%.1f) Phys(Tc=%.1f PSI=%.1f "
                  "CHS=%.1f)\n",
                  (float)effectiveWBGT, (float)DI, (float)estimatedTc,
                  (float)currentPSI, (float)cumulativeHeatStrain);
  } else if (warningEnv || warningPhys) {
    currentRisk = WARNING;
    if (!alarmMuted)
      triggerVibration(1); // Short pulse mode
    Serial.printf(
        "[WARN] WARNING! Env(W=%.1f DI=%.1f) Phys(Tc=%.1f PSI=%.1f CHS=%.1f)\n",
        (float)effectiveWBGT, (float)DI, (float)estimatedTc, (float)currentPSI,
        (float)cumulativeHeatStrain);
  } else {
    currentRisk = NORMAL;
    alarmMuted = false;
    triggerVibration(0); // Stop vibration when returning to normal risk level to prevent indefinite vibration
  }
}

// =====================================================
// AXP192 POWER MANAGEMENT CONTROLLER
// =====================================================

// Initialize power chip registers, configure peripheral Rails and enable ADCs
void batteryBegin() {
  if (axp.begin(Wire, AXP192_SLAVE_ADDRESS) == AXP_FAIL) {
    Serial.println("AXP192 FAIL");
    axp_ok = false;
    return;
  }
  axp_ok = true;
  Serial.println("AXP192 OK");
  
  // Enable internal AXP192 ADC parameters for telemetry tracing
  axp.adc1Enable(AXP202_VBUS_VOL_ADC1 | AXP202_VBUS_CUR_ADC1 |
                     AXP202_BATT_VOL_ADC1 | AXP202_BATT_CUR_ADC1,
                 true);
  
  axp.setPowerOutPut(AXP192_LDO3, false); // Turn GPS Rail OFF to save energy
  axp.setPowerOutPut(AXP192_LDO2, true);  // Turn LoRa Rail ON to enable communication
  Serial.println("GPS off, LoRa on");
}

// Read raw battery voltage from the AXP192 power management IC
float readBatteryVoltageAdc() {
#if DEMO_USB_POWER_ALWAYS_100
  return 4.20f;
#else
  return axp_ok ? axp.getBattVoltage() / 1000.0f : 0.0f;
#endif
}

// Helper utility for segmented linear mappings
static float linearMap(float x, float a, float b, float c, float d) {
  return c + (x - a) * (d - c) / (b - a);
}

// Estimate Li-ion percentage from voltage curve segments matching battery chemistry
uint8_t liionPctFromVoltage(float v) {
  if (isnan(v) || v <= 3.30f)
    return 0;
  if (v >= 4.20f)
    return 100;
  if (v >= 3.95f)
    return (uint8_t)roundf(linearMap(v, 3.95f, 4.20f, 80, 100));
  if (v >= 3.80f)
    return (uint8_t)roundf(linearMap(v, 3.80f, 3.95f, 20, 80));
  if (v >= 3.60f)
    return (uint8_t)roundf(linearMap(v, 3.60f, 3.80f, 5, 20));
  return (uint8_t)roundf(linearMap(v, 3.30f, 3.60f, 0, 5));
}

// Smooth voltage readings using exponential moving average (EMA) to prevent spike readings
float smoothBatteryVoltage(float v) {
  battEma = isnan(battEma) ? v : battEma + BATT_EMA_ALPHA * (v - battEma);
  return battEma;
}

// Detect USB (VBUS) presence to report charging status
bool readUsbPower() {
#if DEMO_USB_POWER_ALWAYS_100
  return true;
#else
  return axp_ok && axp.isVBUSPlug();
#endif
}

// Reads and filters the current battery state percentage
uint8_t readBatteryPercent() {
  return readUsbPower() ? 100
                        : liionPctFromVoltage(
                              smoothBatteryVoltage(readBatteryVoltageAdc()));
}

// =====================================================
// VIBRATION MOTOR DRIVER (ERM motor on GPIO15)
// =====================================================

// Manages the physical ERM vibration motor according to selected risk/notification patterns
void triggerVibration(int pattern) {
  // 0 = stop vibration immediately
  // 1 = short alert pulse for WARNING risks
  // 2 = continuous vibration for CRITICAL risks (must be terminated by pattern 0)
  // 3 = three quick alert pulses for hydration reminder notifications
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
    digitalWrite(VIB_PIN, HIGH); // Continuous drive mode
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
// ACCELEROMETER & STATE MACHINE FALL DETECTION (LIS3DH)
// =====================================================

// Handles state transition events inside the fall detection engine
void enterState(FallState s) {
  fallState = s;
  stateEnterTime = millis();
  
  if (s == STATE_CONFIRMING) {
    // Clear accumulators and timestamps when starting confirmation window
    confirmTotalSamples = confirmStillSamples = 0;
    lastNonStillTime = millis();
    currentStillStart = maxStillDuration = 0;
    wasStill = false;
    postAxSum = postAySum = postAzSum = 0;
    postOrientationSamples = 0;
  }
  
  if (s == STATE_FALL_DETECTED) {
    latestFallAlarm = true;
    lastFallTriggerTime = millis(); // Save timestamp to keep board in full-power mode
    Serial.println("[POWER] Fall detected! Switching back to Demo Mode for 5 minutes.");
  }
  
  if (s == STATE_IDLE)
    latestFallAlarm = false;
}

// Collect raw 3D acceleration data from I2C, compute vector magnitude, and delta deviation
void readAccel() {
  if (!lis_ok)
    return;
  sensors_event_t e;
  
  // Try taking I2C mutex quickly to prevent blocking critical sensor threads
  if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(10))) {
    lis.getEvent(&e);
    xSemaphoreGive(i2cMutex);
  } else {
    return; // Fast escape if mutex is locked
  }
  
  lastAx = e.acceleration.x;
  lastAy = e.acceleration.y;
  lastAz = e.acceleration.z;
  
  // Calculate Signal Vector Magnitude (SVM) in physical units (G-force)
  float mag = sqrt(lastAx * lastAx + lastAy * lastAy + lastAz * lastAz);
  lastSvmG = mag / G;
  
  // Track consecutive absolute variance to verify dynamic stillness
  lastDeltaG = hasPrevSvm ? fabs(lastSvmG - prevSvmG) : 0;
  hasPrevSvm = true;
  prevSvmG = lastSvmG;
}

// Evaluates current frame variance to verify stillness
bool isLowMotionNow() {
  return (lastSvmG >= STILL_LOWER_G && lastSvmG <= STILL_UPPER_G) &&
         (lastDeltaG <= STILL_DELTA_G);
}

// Accumulates a slow, moving reference vector representing vertical gravity orientation at rest
void updateBaselineOrientation() {
  if (!lis_ok)
    return;
  
  if (!baselineReady) {
    baseAx = lastAx;
    baseAy = lastAy;
    baseAz = lastAz;
    baselineReady = true;
    return;
  }
  
  // Integrate active frame into baseline averages using a low pass filter coefficient (3%)
  if (lastSvmG >= STILL_LOWER_G && lastSvmG <= STILL_UPPER_G &&
      lastSvmG < IMPACT_THRESHOLD_G) {
    const float a = 0.03f;
    baseAx = (1 - a) * baseAx + a * lastAx;
    baseAy = (1 - a) * baseAy + a * lastAy;
    baseAz = (1 - a) * baseAz + a * lastAz;
  }
}

// Computes standard angle (degrees) between two 3D vectors using dot product formula
float angleBetweenVectors(float x1, float y1, float z1, float x2, float y2,
                          float z2) {
  float dot = x1 * x2 + y1 * y2 + z1 * z2;
  float n1 = sqrt(x1 * x1 + y1 * y1 + z1 * z1);
  float n2 = sqrt(x2 * x2 + y2 * y2 + z2 * z2);
  if (n1 < 1e-4f || n2 < 1e-4f)
    return 0;
  return acos(constrain(dot / (n1 * n2), -1.0f, 1.0f)) * 180.0f / PI;
}

// Commands LoRa WAN engine to trigger immediate packet transmission
void triggerImmediateSend() {
  os_clearCallback(&sendjob);
  os_setCallback(&sendjob, do_send);
}

// Main state-driven fall detection engine executing at 50Hz
void updateFallStateMachine() {
  if (!lis_ok)
    return;
  
  unsigned long now = millis();
  
  // Monitor weightlessness indicators (low gravity states representing freefalls)
  if (lastSvmG < FREEFALL_THRESHOLD_G)
    lastLowGTime = now;
  bool lowMotion = isLowMotionNow();

  switch (fallState) {

  case STATE_IDLE:
    // Update reference vector while resting
    updateBaselineOrientation();
    
    // Switch to confirmation state if hardware ISR is fired or SVM exceeds primary impact limit
    if (lisInterruptFired || lastSvmG > IMPACT_THRESHOLD_G) {
      lisInterruptFired = false;
      impactSvmG = lastSvmG;
      impactHadRecentLowG = (now - lastLowGTime <= LOWG_WINDOW_MS); // Verify freefall lookback
      Serial.printf("!IMPACT %.2fg lowG=%d\n", lastSvmG, impactHadRecentLowG);
      enterState(STATE_CONFIRMING);
    }
    break;

  case STATE_CONFIRMING: {
    lisInterruptFired = false;
    confirmTotalSamples++;
    
    if (lowMotion) {
      confirmStillSamples++;
      if (!wasStill) {
        currentStillStart = now;
        wasStill = true;
      }
      unsigned long dur = now - currentStillStart;
      if (dur > maxStillDuration)
        maxStillDuration = dur;
        
      // Accumulate post-fall vertical reference orientation
      postAxSum += lastAx;
      postAySum += lastAy;
      postAzSum += lastAz;
      postOrientationSamples++;
    } else {
      wasStill = false;
      lastNonStillTime = now;
    }

    // Verify confirmation constraints
    float stillRatio = confirmTotalSamples > 0
                           ? (float)confirmStillSamples / confirmTotalSamples
                           : 0;
    unsigned long quietTail = now - lastNonStillTime;
    float postureChangeDeg = 0;
    bool postureOK = false;
    
    if (postOrientationSamples > 0) {
      // Calculate angular deviation from baseline representing body posture change
      postureChangeDeg = angleBetweenVectors(
          baseAx, baseAy, baseAz, postAxSum / postOrientationSamples,
          postAySum / postOrientationSamples,
          postAzSum / postOrientationSamples);
      postureOK = (postureChangeDeg >= POSTURE_CHANGE_DEG);
    }

    bool afterGrace = (now - stateEnterTime >= STRUGGLE_GRACE_MS);
    bool ratioOK = (stillRatio >= STILL_RATIO_REQUIRED);
    bool tailOK = (quietTail >= FINAL_STILL_MS);
    bool maxStillOK = (maxStillDuration >= MAX_STILL_MS);
    bool impactStrong = (impactSvmG >= VERY_STRONG_IMPACT_G);
    
    // Evaluate combined evidence: freefall presence, orientation changes, and stillness ratios
    bool evidenceOK = impactHadRecentLowG || postureOK ||
                      (impactStrong && stillRatio >= 0.20f);
    if (REQUIRE_POSTURE_CHANGE)
      evidenceOK = evidenceOK && postureOK;
    bool stillEvidOK = maxStillOK || (tailOK && ratioOK);
    bool confirmed = afterGrace && evidenceOK && stillEvidOK;

    if (confirmed) {
      // Fall officially confirmed
      Serial.printf("FALL! ratio=%.2f tail=%lums impact=%.2fg\n", stillRatio,
                    quietTail, impactSvmG);
      fallCount++;
      pendingFallAlert = true;
      enterState(STATE_FALL_DETECTED);
      triggerImmediateSend(); // Transmit alert packet as fast as possible
    } else if (now - stateEnterTime >= CONFIRM_DURATION_MS) {
      // Return to resting state if verification constraints timeout
      Serial.println("Not a fall, reset");
      enterState(STATE_IDLE);
    }
    break;
  }

  case STATE_FALL_DETECTED:
    // Retain state for set duration to allow display alerts or local alarms
    if (now - stateEnterTime >= ALARM_DURATION_MS) {
      enterState(STATE_IDLE);
    }
    break;
  }
}

// =====================================================
// LoRaWAN TELEMETRY COMMUNICATION ENGINE
// =====================================================

// Packs current localized measurements and physiological states into optimized 20-byte payload
void do_send(osjob_t *j) {
  // Fast escape if transmission stack is already occupied with pending requests
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println("TXRXPEND, retry");
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(5), do_send);
    return;
  }

  bool usb = readUsbPower();
  uint8_t bat = readBatteryPercent();
  bool fall = pendingFallAlert;
  pendingFallAlert = false; // Reset queued state flag

  // 20-byte binary payload description:
  // [0-1]   Air Temp   ×10  int16
  // [2]     Humidity   ×2   uint8
  // [3-4]   Eff. WBGT  ×10  int16
  // [5]     Risk Level      uint8 (0=normal 1=warn 2=critical)
  // [6]     Battery %       uint8
  // [7]     Flags           uint8 (b0=fall b1=usb b2=active)
  // [8]     BPM             uint8
  // [9-10]  Local WBGT ×10  int16
  // [11-12] Ext. WBGT  ×10  int16
  // [13-14] Skin Temp  ×10  int16
  // [15-16] Est. Tc    ×10  int16
  // [17]    PSI        ×10  uint8
  // [18-19] CHS        ×10  uint16

  int16_t t16 = (int16_t)constrain(lroundf(airTemp * 10), -32768, 32767);
  int16_t w16 = (int16_t)constrain(lroundf(effectiveWBGT * 10), -32768, 32767);
  uint8_t h8 = (uint8_t)constrain((int)lroundf(humidity * 2), 0, 200);
  uint8_t fl = (fall ? 1 : 0) | (usb ? 2 : 0) | 0x04;

  int16_t locW16 = (int16_t)constrain(lroundf(localWBGT * 10), -32768, 32767);
  int16_t extW16 =
      (int16_t)constrain(lroundf(externalWBGT * 10), -32768, 32767);
  int16_t sk16 = (int16_t)constrain(lroundf(skinTemp * 10), -32768, 32767);
  int16_t tc16 = (int16_t)constrain(lroundf(estimatedTc * 10), -32768, 32767);
  uint8_t psi8 = (uint8_t)constrain(lroundf(currentPSI * 10), 0, 255);
  uint16_t chs16 =
      (uint16_t)constrain(lroundf(cumulativeHeatStrain * 10), 0, 65535);

  payloadBin[0] = (uint8_t)((uint16_t)t16 >> 8);
  payloadBin[1] = (uint8_t)((uint16_t)t16 & 0xFF);
  payloadBin[2] = h8;
  payloadBin[3] = (uint8_t)((uint16_t)w16 >> 8);
  payloadBin[4] = (uint8_t)((uint16_t)w16 & 0xFF);
  payloadBin[5] = (uint8_t)currentRisk;
  payloadBin[6] = bat;
  payloadBin[7] = fl;
  payloadBin[8] = (uint8_t)constrain(beatAvg, 0, 255);

  payloadBin[9] = (uint8_t)((uint16_t)locW16 >> 8);
  payloadBin[10] = (uint8_t)((uint16_t)locW16 & 0xFF);

  payloadBin[11] = (uint8_t)((uint16_t)extW16 >> 8);
  payloadBin[12] = (uint8_t)((uint16_t)extW16 & 0xFF);

  payloadBin[13] = (uint8_t)((uint16_t)sk16 >> 8);
  payloadBin[14] = (uint8_t)((uint16_t)sk16 & 0xFF);

  payloadBin[15] = (uint8_t)((uint16_t)tc16 >> 8);
  payloadBin[16] = (uint8_t)((uint16_t)tc16 & 0xFF);

  payloadBin[17] = psi8;

  payloadBin[18] = (uint8_t)(chs16 >> 8);
  payloadBin[19] = (uint8_t)(chs16 & 0xFF);

  Serial.printf("TX T=%.1f H=%.1f W=%.1f (LocW=%.1f ExtW=%.1f) Skin=%.1f Risk=%d "
                "Fall=%d BPM=%d BAT=%d%% Tc=%.1f PSI=%.1f CHS=%.1f\n",
                (float)airTemp, (float)humidity, (float)effectiveWBGT,
                (float)localWBGT, (float)externalWBGT, (float)skinTemp,
                (int)currentRisk, fall, (int)beatAvg, bat, (float)estimatedTc,
                (float)currentPSI, (float)cumulativeHeatStrain);
  if (axp_ok) {
    if (usb)
      Serial.printf("[USB] %.2fV %.1fmA\n", axp.getVbusVoltage() / 1000.0f,
                    axp.getVbusCurrent());
    else
      Serial.printf("[BAT] %.2fV %.1fmA\n", axp.getBattVoltage() / 1000.0f,
                    axp.getBattDischargeCurrent());
  }

  // Queue packet inside the LMIC radio buffer
  LMIC_setTxData2(1, payloadBin, sizeof(payloadBin), 0);
  Serial.println("Queued 20B");
}

// LoRaWAN MAC controller event handler
void onEvent(ev_t ev) {
  switch (ev) {
  case EV_TXSTART:
    Serial.println("EV_TXSTART");
    break;

  case EV_TXCOMPLETE:
    Serial.println("EV_TXCOMPLETE");
    lastTxTime = millis();

    // Check for downlink payload command streams from gateway
    if (LMIC.dataLen) {
      uint8_t cmd = LMIC.frame[LMIC.dataBeg];
      Serial.printf("[DL] 0x%02X len=%d\n", cmd, LMIC.dataLen);

      if (cmd == 0xF1) {
        // Trigger manual override critical continuous alarm
        triggerVibration(2);
      } else if (cmd == 0xF2 && LMIC.dataLen >= 3) {
        // Decode external environmental WBGT broadcast
        uint16_t raw = ((uint16_t)LMIC.frame[LMIC.dataBeg + 1] << 8) |
                       LMIC.frame[LMIC.dataBeg + 2];
        externalWBGT = raw / 10.0f;
        lastExternalWBGTTime = millis();
        Serial.printf("Ext WBGT=%.1f\n", externalWBGT);
        determineHeatStress();
      } else if (cmd == 0xF3) {
        // Mute alarms locally for 10 minutes
        alarmMuted = true;
        muteStartTime = millis();
        triggerVibration(0);
        Serial.println("Alarm muted (10 minutes force mute)");
      } else if (cmd == 0xF4) {
        // Hydration reminder vibration pattern
        triggerVibration(3);
        Serial.println("Hydration reminder");
      }
    }
    // Schedule the next periodic transmission
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(txInterval),
                        do_send);
    break;

  default:
    Serial.printf("EV %u\n", (unsigned)ev);
    break;
  }
}

// =====================================================
// FREERTOS THREAD TASKS
// =====================================================

// Core 1 Task — MAX30102 Heart Rate + LIS3DH Fall Detection (Runs at 50Hz / 20ms cycles)
void TaskBioMotion(void *pv) {
  for (;;) {
    bool heartRateActive = true;
    unsigned long delayMs = SAMPLE_INTERVAL_MS;

    // Check if system is working under low-power configurations
    if (isPowerSavingMode()) {
      unsigned long cycleTime = millis() % 60000;
      if (cycleTime >= 15000) {
        // Sleep window: 15s to 60s (45 seconds duration). Shut down heart rate detection to conserve current
        heartRateActive = false;
        if (!max30102_is_sleeping) {
          if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50))) {
            particleSensor.shutDown();
            xSemaphoreGive(i2cMutex);
            max30102_is_sleeping = true;
            Serial.println("[POWER] MAX30102 entered Low-Power shutdown.");
          }
        }
        
        // Scale thread cycle time if device is idling without any fall suspicion
        if (fallState == STATE_IDLE) {
          delayMs = 200;
        }
      } else {
        // Active window: 0s to 15s. Restore sensor activity
        if (max30102_is_sleeping) {
          if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50))) {
            particleSensor.wakeUp();
            // Clear rates array to avoid noise spikes or legacy registers values from wakeup
            for (byte x = 0; x < RATE_SIZE; x++) rates[x] = 0;
            rateSpot = 0;
            beatsPerMinute = 0;
            xSemaphoreGive(i2cMutex);
            max30102_is_sleeping = false;
            Serial.println("[POWER] MAX30102 woke up for active sampling.");
          }
        }
      }
    } else {
      // Normal/Demo override: Keep sensor alive continuously
      if (max30102_is_sleeping) {
        if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50))) {
          particleSensor.wakeUp();
          xSemaphoreGive(i2cMutex);
          max30102_is_sleeping = false;
          Serial.println("[POWER] Demo Mode: MAX30102 forced active.");
        }
      }
    }

    if (heartRateActive) {
      // Heart rate peak-to-peak estimation logic
      long irValue = 0;
      if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(10))) {
        irValue = particleSensor.getIR();
        xSemaphoreGive(i2cMutex);
      }
      
      if (irValue < 50000) {
        // Reset buffers if raw IR measurement drops representing sensor detachment
        beatAvg = 0;
        beatsPerMinute = 0;
        for (byte x = 0; x < RATE_SIZE; x++)
          rates[x] = 0;
        rateSpot = 0;
      } else if (checkForBeat(irValue)) {
        // Pulse peak recognized
        long delta = millis() - lastBeat;
        lastBeat = millis();
        beatsPerMinute = 60.0f / (delta / 1000.0f);
        
        // Filter out physiological outlier readings
        if (beatsPerMinute > 20 && beatsPerMinute < 255) {
          rates[rateSpot++] = (byte)beatsPerMinute;
          rateSpot %= RATE_SIZE;
          beatAvg = 0;
          for (byte x = 0; x < RATE_SIZE; x++)
            beatAvg += rates[x];
          beatAvg /= RATE_SIZE; // Compute moving average
        }
      }
    }

    // Read accelerometer coordinates and evaluate fall detection state transitions
    readAccel();
    updateFallStateMachine();

    vTaskDelay(pdMS_TO_TICKS(delayMs));
  }
}

// Core 0 Task — BME280 + MLX90614 + Heat Stress Estimation (Runs every 10s)
void TaskEnvironment(void *pv) {
  for (;;) {
    bool readSensors = true;
    
    // Scale down sampling frequency if operating in low power settings
    if (isPowerSavingMode()) {
      unsigned long cycleTime = millis() % 60000;
      if (cycleTime >= 10000) {
        readSensors = false;
      }
    }

    if (readSensors) {
      if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100))) {
        if (bme_ok) {
          float t = bme.readTemperature();
          float h = bme.readHumidity();
          if (!isnan(t) && t > -10.0f && t < 60.0f)
            airTemp = t;
          if (!isnan(h) && h >= 0.0f && h <= 100.0f)
            humidity = h;
        }
        if (mlx_ok) {
          float obj = mlx.readObjectTempC();
          // Filter skin temperatures: normal range is 30–42°C
          if (!isnan(obj) && obj > 30.0f && obj < 42.0f) {
            skinTemp = obj;
          }
        }
        xSemaphoreGive(i2cMutex);
      }
    }

    // Run physiological thermal index estimation and check alarms
    determineHeatStress();

    Serial.printf("ENV T=%.1f H=%.1f Sk=%.1f W=%.1f (LocW=%.1f ExtW=%.1f) "
                  "DI=%.1f R=%d BPM=%d Tc=%.1f PSI=%.1f CHS=%.1f\n",
                  (float)airTemp, (float)humidity, (float)skinTemp,
                  (float)effectiveWBGT, (float)localWBGT, (float)externalWBGT,
                  (float)DI, (int)currentRisk, (int)beatAvg, (float)estimatedTc,
                  (float)currentPSI, (float)cumulativeHeatStrain);

    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

// =====================================================
// SYSTEM SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n>>> HeatWatch Merged Firmware <<<");

  // Configure vibration pin output driver
  pinMode(VIB_PIN, OUTPUT);
  digitalWrite(VIB_PIN, LOW);

  // LIS3DH INT1 hardware interrupt configuration (routed to GPIO25, triggered on rising edge)
  pinMode(LIS_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(LIS_INT_PIN), onLisInterrupt, RISING);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000); // Standard I2C Clock speed (100kHz)

  i2cMutex = xSemaphoreCreateMutex();

  // Initialize the AXP192 controller to disable GPS rail and turn on LoRa rail
  batteryBegin();

  // Try BME280 initialization using standard addresses (0x77 or 0x76)
  bme_ok = bme.begin(0x77) || bme.begin(0x76);
  Serial.println(bme_ok ? "BME280 OK" : "BME280 FAIL");

  // Initialize MLX90614 sharing standard I2C wires
  mlx_ok = mlx.begin(0x5A, &Wire);
  Serial.println(mlx_ok ? "MLX90614 OK" : "MLX90614 FAIL");

  // Try LIS3DH initialization using standard addresses (0x18 or 0x19)
  lis_ok = lis.begin(0x18) || lis.begin(0x19);
  if (lis_ok) {
    lis.setRange(LIS3DH_RANGE_8_G);
    lis.setDataRate(LIS3DH_DATARATE_100_HZ);
    
    // --- LIS3DH INT1 register configurations to route thresholds to GPIO25 ---
    
    // Register 0x36: INT1_THS (Interrupt 1 Threshold)
    // Range is set to 8G, where 1 LSB = 62mg. Writing 40 gives 40 * 62mg = 2.48G (triggers above ~2.5G)
    Wire.beginTransmission(0x18);
    Wire.write(0x36);
    Wire.write(40);
    Wire.endTransmission();
    
    // Register 0x37: INT1_DURATION (Interrupt 1 Duration)
    // Sets duration threshold. At 100Hz, 1 LSB = 10ms. Writing 5 gives 50ms of continuous G override
    Wire.beginTransmission(0x18);
    Wire.write(0x37);
    Wire.write(5);
    Wire.endTransmission();
    
    // Register 0x30: INT1_CFG (Interrupt 1 Configuration)
    // Writing 0x2A enables high event interrupts on X, Y, and Z axes (XHigh, YHigh, ZHigh)
    Wire.beginTransmission(0x18);
    Wire.write(0x30);
    Wire.write(0x2A);
    Wire.endTransmission();
    
    // Register 0x22: CTRL_REG3
    // Writing 0x40 routes the IA1 (Interrupt Activity 1) signal to the INT1 pin
    Wire.beginTransmission(0x18);
    Wire.write(0x22);
    Wire.write(0x40);
    Wire.endTransmission();
    
    Serial.println("LIS3DH OK — INT1 → GPIO25");
    readAccel();
    updateBaselineOrientation();
  } else {
    Serial.println("LIS3DH FAIL");
  }

  // Initialize the MAX30102 particle/photodiode heart rate sensor
  if (particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x0A); // Reduce LED drive current to save energy
    Serial.println("MAX30102 OK");
  } else {
    Serial.println("MAX30102 FAIL");
  }

  // Initialize LoRa SPI and LMIC components
  SPI.begin(5, 19, 27, 18);
  os_init();
  LMIC_reset();

  // Load LoRaWAN session ABP credentials
  uint8_t appskey[sizeof(APPSKEY)];
  uint8_t nwkskey[sizeof(NWKSKEY)];
  memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
  memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
  LMIC_setSession(0x1, DEVADDR, nwkskey, appskey);
  LMIC_selectSubBand(1);
  LMIC_setLinkCheckMode(0);
  LMIC.dn2Dr = DR_SF9;
  LMIC.rxDelay = 1;
  LMIC_setAdrMode(0);
  LMIC_setDrTxpow(DR_SF7, 14);
  LMIC_setClockError(MAX_CLOCK_ERROR * 80 / 100);

  // Spawn RTOS concurrent threads pinned to cores to maintain sensor responsiveness
  xTaskCreatePinnedToCore(TaskBioMotion, "Bio", 4096, NULL, 2, NULL, 1); // Core 1 executes Bio/Motion
  xTaskCreatePinnedToCore(TaskEnvironment, "Env", 4096, NULL, 1, NULL, 0); // Core 0 executes Environment/Sensors

  Serial.println("Setup done, first TX in 20s");
}

// =====================================================
// MAIN CORE 1 CONCURRENT RUNLOOP
// =====================================================

void loop() {
  os_runloop_once(); // Service standard LoRa state machine callback queue

  unsigned long now = millis();
  if (loopStartTime == 0)
    loopStartTime = now;

  // Trigger the very first transmission packet after a 20-second startup boot window
  if (lastTxTime == 0 && (now - loopStartTime > 20000)) {
    lastTxTime = now;
    do_send(&sendjob);
  }
}
