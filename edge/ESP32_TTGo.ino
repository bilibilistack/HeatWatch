#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <math.h>

// ==================== LoRaWAN 密钥 ====================
static const PROGMEM u1_t NWKSKEY[16] = { 0x3C, 0x55, 0x06, 0xB7, 0xAC, 0x1B, 0xD5, 0x85, 0x77, 0x8B, 0xA0, 0x22, 0xE9, 0x22, 0x08, 0xC9 };
static const u1_t PROGMEM APPSKEY[16] = { 0x2A, 0x3B, 0x5B, 0x7B, 0x85, 0x40, 0x58, 0x1C, 0xFD, 0xCD, 0x5E, 0x19, 0x55, 0x46, 0x7F, 0x4C };
static const u4_t DEVADDR = 0x260D38CB;

void os_getArtEui (u1_t* buf) { }
void os_getDevEui (u1_t* buf) { }
void os_getDevKey (u1_t* buf) { }
#define LED_PIN 25
// ==================== 硬件对象 ====================
Adafruit_BME280 bme;
MAX30105 particleSensor;

// ==================== 风险等级 ====================
typedef enum { NORMAL, WARNING, CRITICAL } RiskLevel;
volatile RiskLevel currentRisk = NORMAL;

// ==================== 全局传感器数据 ====================
volatile int beatAvg = 0;
volatile float airTemp = 0;
volatile float humidity = 0;
volatile float skinTemp = 0;      // TODO: MLX90614 到货后替换
volatile float localWBGT = 0;
volatile float externalWBGT = 0;
volatile float effectiveWBGT = 0;
volatile float DI = 0;

// ==================== 心率计算 ====================
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;

// ==================== 上报间隔 ====================
volatile unsigned long txInterval = 30;
unsigned long lastTxTime = 0;

// ==================== FreeRTOS ====================
SemaphoreHandle_t i2cMutex;
TaskHandle_t taskCommHandle;

// ==================== LoRaWAN ====================
static osjob_t sendjob;

const lmic_pinmap lmic_pins = {
    .nss = 18,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 23,
    .dio = {26, 33, 32},
};

// ==================== 模拟 MLX90614 体表温度 ====================
// TODO: 传感器到货后删除此函数，替换为真实读取
float simulateSkinTemp() {
    static float lastSkinTemp = 36.5;
    lastSkinTemp += random(-10, 10) / 100.0;
    lastSkinTemp = constrain(lastSkinTemp, 35.5, 37.8);
    return lastSkinTemp;
}

// ==================== WBGT 计算 ====================
float calculateWBGT(float temp, float rh) {
    return 0.567 * temp + 0.393 * (rh / 100.0 * 6.105 *
           exp(17.27 * temp / (237.7 + temp))) + 3.94;
}

// ==================== 不适指数 DI ====================
float calculateDI(float temp, float rh) {
    return temp - 0.55 * (1 - 0.01 * rh) * (temp - 14.5);
}
// =================LED========================
void updateLED() {
    static unsigned long lastBlink = 0;
    static bool ledState = false;
    unsigned long now = millis();

    switch (currentRisk) {
        case NORMAL:
            if (now - lastBlink > 3000) {
                lastBlink = now;
                ledState = !ledState;
                digitalWrite(LED_PIN, ledState);
            }
            break;
        case WARNING:
            if (now - lastBlink > 500) {
                lastBlink = now;
                ledState = !ledState;
                digitalWrite(LED_PIN, ledState);
            }
            break;
        case CRITICAL:
            digitalWrite(LED_PIN, HIGH);
            break;
    }
}

void blinkOnSend() {
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(100);
        digitalWrite(LED_PIN, LOW);
        delay(100);
    }
}
// ==================== 震动控制 ====================
void triggerVibration(int pattern) {
    // TODO: ERM 震动马达到货后实现
    Serial.print("[VIB] Pattern: ");
    Serial.println(pattern == 1 ? "Short (Warning)" : "Continuous (Critical)");
}

// ==================== 热应力判定 ====================
void determineHeatStress() {
    effectiveWBGT = max(localWBGT, externalWBGT);

    if (effectiveWBGT > 33.0 || beatsPerMinute > 160) {
        currentRisk = CRITICAL;
        txInterval = 30;
        triggerVibration(2);
        Serial.println("[ALERT] CRITICAL!");
    } else if (effectiveWBGT > 30.0) {
        currentRisk = WARNING;
        txInterval = 30;
        triggerVibration(1);
        Serial.println("[WARN] WARNING!");
    } else {
        currentRisk = NORMAL;
        txInterval = 30;
    }
}

// ==================== LoRaWAN 二进制打包发送 ====================
void do_send(osjob_t* j) {
    if (LMIC.opmode & OP_TXRXPEND) {
        Serial.println("OP_TXRXPEND, not sending");
        return;
    }

    uint8_t payload[9];
    
    // Byte 0-1: 温度 int16 x10
    int16_t tempRaw = (int16_t)(airTemp * 10);
    payload[0] = (tempRaw >> 8) & 0xFF;
    payload[1] = tempRaw & 0xFF;
    payload[2] = (uint8_t)constrain(humidity * 2, 0, 255);
    int16_t wbgtRaw = (int16_t)(effectiveWBGT * 10);
    payload[3] = (wbgtRaw >> 8) & 0xFF;
    payload[4] = wbgtRaw & 0xFF;
    payload[5] = (uint8_t)currentRisk;
    payload[6] = 0;  // battery，没有传感器填0
    payload[7] = 0x04; // 状态位 active_signal=1
    payload[8] = (uint8_t)constrain(beatAvg, 0, 255); // 心率

    Serial.print("TX: T:"); Serial.print(airTemp);
    Serial.print(" H:"); Serial.print(humidity);
    Serial.print(" W:"); Serial.print(effectiveWBGT);
    Serial.print(" BPM:"); Serial.print(beatAvg);
    Serial.print(" R:"); Serial.println(currentRisk);
    blinkOnSend();
    LMIC_setTxData2(1, payload, sizeof(payload), 0);
    Serial.println("Packet queued (8 bytes)");
    lastTxTime = millis();
}
// ==================== LoRaWAN 回调 ====================
void onEvent(ev_t ev) {
    switch(ev) {
        case EV_TXSTART:
            Serial.println("EV_TXSTART");
            break;
        case EV_TXCOMPLETE:
            Serial.println("EV_TXCOMPLETE");
            if (LMIC.dataLen) {
                uint8_t cmd = LMIC.frame[LMIC.dataBeg];
                if (cmd == 0xF1) {
                    triggerVibration(2);
                    Serial.println("Force vibrate command!");
                } else if (cmd == 0xF2) {
                    externalWBGT = LMIC.frame[LMIC.dataBeg + 1] / 10.0;
                    Serial.print("External WBGT: ");
                    Serial.println(externalWBGT);
                }
            }
            break;
        default:
            break;
    }
}

// ==================== Task 1: 心率采样 (Core 1) ====================
void TaskBioMotion(void *pv) {
    for(;;) {
        long irValue = particleSensor.getIR();

        if (irValue < 50000) {
            beatAvg = 0;
            beatsPerMinute = 0;
        } else {
            if (checkForBeat(irValue)) {
                long delta = millis() - lastBeat;
                lastBeat = millis();
                beatsPerMinute = 60 / (delta / 1000.0);
                if (beatsPerMinute < 255 && beatsPerMinute > 20) {
                    rates[rateSpot++] = (byte)beatsPerMinute;
                    rateSpot %= RATE_SIZE;
                    beatAvg = 0;
                    for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
                    beatAvg /= RATE_SIZE;
                }
            }
        }
        updateLED();
        vTaskDelay(pdMS_TO_TICKS(20)); // 50Hz
    }
}

// ==================== Task 2: 环境监测 (Core 0) ====================
void TaskEnvironment(void *pv) {
    for(;;) {
        if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100))) {
            airTemp = bme.readTemperature();
            humidity = bme.readHumidity();
            skinTemp = simulateSkinTemp(); // TODO: 替换为真实 MLX90614
            xSemaphoreGive(i2cMutex);
        }

        localWBGT = calculateWBGT(airTemp, humidity);
        DI = calculateDI(airTemp, humidity);
        determineHeatStress();

        Serial.print("ENV - T:"); Serial.print(airTemp);
        Serial.print(" H:"); Serial.print(humidity);
        Serial.print(" SkinT:"); Serial.print(skinTemp);
        Serial.print(" WBGT:"); Serial.print(localWBGT);
        Serial.print(" DI:"); Serial.print(DI);
        Serial.print(" Risk:"); Serial.println(currentRisk);

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// ==================== Task 3: LoRaWAN 通信 (Core 0) ====================
void TaskCommunication(void *pv) {
    unsigned long startTime = millis();

    for(;;) {
        os_runloop_once();

        unsigned long now = millis();

        if (lastTxTime == 0 && now - startTime > 20000) {
            lastTxTime = now;
            do_send(&sendjob);
        }

        if (lastTxTime > 0 && now - lastTxTime >= txInterval * 1000) {
            do_send(&sendjob);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ==================== Setup ====================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Wire.begin(21, 22);
    Wire.setClock(100000);

    i2cMutex = xSemaphoreCreateMutex();

    if (!bme.begin(0x77)) {
        Serial.println("BME280 not found!");
    } else {
        Serial.println("BME280 OK");
    }

    if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
        Serial.println("MAX30102 not found!");
    } else {
        particleSensor.setup();
        particleSensor.setPulseAmplitudeRed(0x0A);
        Serial.println("MAX30102 OK");
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
    LMIC.dn2Dr = DR_SF9;

    xTaskCreatePinnedToCore(TaskBioMotion,     "BioMotion",     4096, NULL, 2, NULL,            1);
    xTaskCreatePinnedToCore(TaskEnvironment,   "Environment",   4096, NULL, 1, NULL,            0);
    xTaskCreatePinnedToCore(TaskCommunication, "Communication", 8192, NULL, 3, &taskCommHandle, 0);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    Serial.println("Setup complete!");
}

void loop() {}