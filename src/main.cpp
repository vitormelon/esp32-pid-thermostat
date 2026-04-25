// ============================================================
// ESP32 PID THERMOSTAT
// Controle de temperatura com LCD 20x4 + Rotary Encoder
// Arquitetura: modular, não bloqueante, tolerante a falhas
// ============================================================

#include "credentials.h"

#define BLYNK_TEMPLATE_ID   BLYNK_TMPL_ID
#define BLYNK_TEMPLATE_NAME BLYNK_TMPL_NAME
#define BLYNK_AUTH_TOKEN    BLYNK_TOKEN

#include <Arduino.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "state.h"
#include "encoder.h"
#include "display.h"
#include "storage.h"
#include "control.h"
#include "timer_ctrl.h"
#include "autotune.h"
#include "safety.h"

// --- Sensor ---
static OneWire oneWire(SENSOR_PIN);
static DallasTemperature sensors(&oneWire);

// --- Temperatura (estado interno) ---
enum TempState { TEMP_IDLE, TEMP_CONVERTING };
static TempState     tempState        = TEMP_IDLE;
static unsigned long tempConvStart    = 0;
static unsigned long lastTempRead     = 0;

// --- WiFi (estado interno) ---
enum WiFiSt { WF_INIT, WF_CONNECTING, WF_CONNECTED, WF_RETRY_WAIT, WF_FAILED };
static WiFiSt        wfState          = WF_INIT;
static int           wfRetryCount     = 0;
static unsigned long wfStateTime      = 0;

// --- Blynk ---
static bool          blynkConfigured    = false;
static unsigned long lastBlynkRetry     = 0;
static unsigned long lastBlynkSend      = 0;
static unsigned long blynkRetryInterval = BLYNK_RETRY_INTERVAL_MIN;

// --- Backlight ---
static bool          blOn             = true;
static unsigned long lastInteraction  = 0;
static bool          wakeDelay        = false;
static unsigned long wakeDelayStart   = 0;

// --- Recovery ---
static unsigned long lastRecoverySave = 0;

// ============================================================
// TEMPERATURA (DS18B20)
// ============================================================
static void initSensor() {
    sensors.begin();
    sensors.setResolution(12);
    sensors.setWaitForConversion(false);
    Serial.println("[TEMP] DS18B20 init (12-bit, non-blocking)");
}

static void manageTemperature() {
    switch (tempState) {
        case TEMP_IDLE:
            if (millis() - lastTempRead >= TEMP_READ_INTERVAL) {
                sensors.requestTemperatures();
                tempConvStart = millis();
                tempState = TEMP_CONVERTING;
            }
            break;

        case TEMP_CONVERTING:
            if (millis() - tempConvStart >= TEMP_CONVERSION_DELAY) {
                float t = sensors.getTempCByIndex(0);
                lastTempRead = millis();
                tempState = TEMP_IDLE;

                bool valid = !isnan(t)
                          && t != DEVICE_DISCONNECTED_C
                          && t > TEMP_MIN_VALID
                          && t < TEMP_MAX_VALID;

                if (!valid) {
                    Serial.printf("[TEMP] Leitura invalida: %.2f\n", t);
                    if (firstValidReading &&
                        millis() - lastValidTempTime >= SENSOR_FAIL_TIMEOUT) {
                        if (!sensorFailed) {
                            sensorFailed = true;
                            setRelay(false);
                            Serial.println("[SEG] Sensor falhou >5s — rele OFF");
                        }
                    }
                } else {
                    currentTemp     = t;
                    lastValidTempTime = millis();
                    sensorFailed    = false;
                    newTempReading  = true;

                    if (!firstValidReading) {
                        firstValidReading = true;
                        Serial.printf("[TEMP] Primeira leitura: %.2f%cC\n", t, 0xB0);
                    }
                    Serial.printf("[TEMP] %.2f%cC\n", t, 0xB0);
                }
            }
            break;
    }
}

// ============================================================
// SEGURANÇA — cleanup orquestrado quando safety dispara
// ============================================================
static void tryUnstickRelay() {
    Serial.println("[SEG] Tentando desgrudar rele...");
    for (int i = 0; i < SAFETY_UNSTICK_CYCLES; i++) {
        digitalWrite(RELAY_PIN, HIGH);
        delay(50);
        digitalWrite(RELAY_PIN, LOW);
        delay(50);
        esp_task_wdt_reset();
    }
    relayState = false;
    Serial.println("[SEG] Rele toggled. Desligado.");
}

static void onSafetyTrigger(SafetyError err, float trigTemp) {
    if (autotuneIsRunning()) autotuneCancel();
    displayResetAutotuneUI();
    timerStop();
    if (err == SAFETY_RELAY_STUCK) tryUnstickRelay();
    storageSaveRecoveryState();
    Serial.printf("[SEG] ERRO %d! Trigger=%.1f Atual=%.1f\n",
                  err, trigTemp, currentTemp);
}

// ============================================================
// WIFI (não bloqueante, SEM restart automático)
// ============================================================
static void manageWiFi() {
    switch (wfState) {
        case WF_INIT:
            Serial.println("[WIFI] Iniciando...");
            WiFi.mode(WIFI_STA);
            WiFi.setAutoReconnect(true);
            WiFi.begin(WIFI_SSID, WIFI_PASS);
            wfRetryCount = 0;
            wfStateTime  = millis();
            wfState      = WF_CONNECTING;
            break;

        case WF_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[WIFI] Conectado! IP: %s\n",
                              WiFi.localIP().toString().c_str());
                wfState       = WF_CONNECTED;
                wifiConnected = true;
            } else if (millis() - wfStateTime >= WIFI_CONNECT_TIMEOUT) {
                wfRetryCount++;
                Serial.printf("[WIFI] Tentativa %d/%d falhou\n",
                              wfRetryCount, WIFI_MAX_RETRIES);
                if (wfRetryCount >= WIFI_MAX_RETRIES) {
                    wfState     = WF_FAILED;
                    wfStateTime = millis();
                    Serial.println("[WIFI] Tentativas esgotadas — continuando offline");
                } else {
                    WiFi.disconnect();
                    wfState     = WF_RETRY_WAIT;
                    wfStateTime = millis();
                }
            }
            break;

        case WF_CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WIFI] Desconectado!");
                wifiConnected = false;
                wfState       = WF_INIT;
            }
            break;

        case WF_RETRY_WAIT:
            if (millis() - wfStateTime >= WIFI_RETRY_PAUSE) {
                WiFi.begin(WIFI_SSID, WIFI_PASS);
                wfStateTime = millis();
                wfState     = WF_CONNECTING;
            }
            break;

        case WF_FAILED:
            wifiConnected = false;
            if (millis() - wfStateTime >= WIFI_RETRY_CYCLE_PAUSE) {
                wfRetryCount = 0;
                wfState      = WF_INIT;
            }
            break;
    }
}

// ============================================================
// BLYNK
// ============================================================
static void sendBlynkData() {
    if (!Blynk.connected()) return;
    if (millis() - lastBlynkSend < BLYNK_SEND_INTERVAL) return;
    lastBlynkSend = millis();
    Blynk.virtualWrite(V0,  currentTemp);
    Blynk.virtualWrite(V1,  setPoint);
    Blynk.virtualWrite(V2,  relayState ? 1 : 0);
    Blynk.virtualWrite(V3,  offset);
    Blynk.virtualWrite(V4,  controlMode);
    Blynk.virtualWrite(V5,  pidKp);
    Blynk.virtualWrite(V6,  pidKi);
    Blynk.virtualWrite(V7,  pidKd);
    Blynk.virtualWrite(V8,  (int)(pidWindowSize / 1000));
    Blynk.virtualWrite(V9,  pidOutput);
    Blynk.virtualWrite(V11, systemActive ? 1 : 0);
}

static void manageBlynk() {
    if (WiFi.status() != WL_CONNECTED) {
        // Reset backoff: assim que WiFi voltar, primeira tentativa é rápida.
        blynkRetryInterval = BLYNK_RETRY_INTERVAL_MIN;
        return;
    }

    if (!blynkConfigured) {
        Blynk.config(BLYNK_AUTH_TOKEN);
        blynkConfigured = true;
    }

    if (!Blynk.connected()) {
        if (millis() - lastBlynkRetry >= blynkRetryInterval) {
            Serial.printf("[BLYNK] Tentando conectar... (proxima retry em %lus)\n",
                          blynkRetryInterval / 1000);
            Blynk.connect(BLYNK_CONNECT_TIMEOUT_MS);
            lastBlynkRetry = millis();
            // Backoff exponencial: dobra até o máximo
            unsigned long next = blynkRetryInterval * 2;
            blynkRetryInterval = (next > BLYNK_RETRY_INTERVAL_MAX)
                                  ? BLYNK_RETRY_INTERVAL_MAX
                                  : next;
        }
    } else {
        blynkConnected     = true;
        blynkRetryInterval = BLYNK_RETRY_INTERVAL_MIN;  // reset enquanto conectado
        Blynk.run();
        sendBlynkData();
    }
}

// --- Blynk Callbacks ---

BLYNK_CONNECTED() {
    Serial.println("[BLYNK] Conectado — sync");
    Blynk.virtualWrite(V0,  currentTemp);
    Blynk.virtualWrite(V1,  setPoint);
    Blynk.virtualWrite(V2,  relayState ? 1 : 0);
    Blynk.virtualWrite(V3,  offset);
    Blynk.virtualWrite(V4,  controlMode);
    Blynk.virtualWrite(V5,  pidKp);
    Blynk.virtualWrite(V6,  pidKi);
    Blynk.virtualWrite(V7,  pidKd);
    Blynk.virtualWrite(V8,  (int)(pidWindowSize / 1000));
    Blynk.virtualWrite(V9,  pidOutput);
    Blynk.virtualWrite(V11, systemActive ? 1 : 0);
}

BLYNK_WRITE(V1) {
    float v = param.asFloat();
    if (v >= SP_MIN && v <= SP_MAX) {
        setPoint = v;
        storageSaveSetPoint();
        Serial.printf("[BLYNK] SP=%.1f\n", setPoint);
    }
}

BLYNK_WRITE(V3) {
    float v = param.asFloat();
    if (v >= OFFSET_MIN && v <= OFFSET_MAX) {
        offset = v;
        storageSaveOffset();
        Serial.printf("[BLYNK] Off=%.1f\n", offset);
    }
}

BLYNK_WRITE(V4) {
    int v = param.asInt();
    if (v >= 0 && v < NUM_MODES) {
        controlMode = v;
        storageSaveControlMode();
        controlReset();
        Serial.printf("[BLYNK] Mode=%d\n", controlMode);
    }
}

BLYNK_WRITE(V5) {
    float v = param.asFloat();
    if (v >= PID_KP_MIN && v <= PID_KP_MAX) {
        pidKp = v;
        storageSavePidKp();
        controlReset();
        Serial.printf("[BLYNK] Kp=%.3f\n", pidKp);
    }
}

BLYNK_WRITE(V6) {
    float v = param.asFloat();
    if (v >= PID_KI_MIN && v <= PID_KI_MAX) {
        pidKi = v;
        storageSavePidKi();
        controlReset();
        Serial.printf("[BLYNK] Ki=%.4f\n", pidKi);
    }
}

BLYNK_WRITE(V7) {
    float v = param.asFloat();
    if (v >= PID_KD_MIN && v <= PID_KD_MAX) {
        pidKd = v;
        storageSavePidKd();
        controlReset();
        Serial.printf("[BLYNK] Kd=%.1f\n", pidKd);
    }
}

BLYNK_WRITE(V8) {
    unsigned long ms = (unsigned long)(param.asInt()) * 1000UL;
    if (ms >= (unsigned long)PID_WINDOW_MIN_S * 1000UL &&
        ms <= (unsigned long)PID_WINDOW_MAX_S * 1000UL) {
        pidWindowSize = ms;
        storageSavePidWindow();
        controlReset();
        Serial.printf("[BLYNK] Win=%lums\n", pidWindowSize);
    }
}

BLYNK_WRITE(V11) {
    bool v = param.asInt() != 0;
    if (v != systemActive) {
        systemActive = v;
        if (systemActive) {
            systemStartMs = millis();
            controlReset();
            if (timerSetMinutes > 0) timerStart();
        } else {
            setRelay(false);
            timerStop();
        }
        storageSaveRecoveryState();
        Serial.printf("[BLYNK] Active=%d\n", systemActive);
    }
}

// ============================================================
// BACKLIGHT + INPUT
// ============================================================
static void handleInputAndBacklight() {
    EncoderInput in = encoderRead();
    bool any = (in.delta != 0 || in.pressed || in.longPress);

    if (any) lastInteraction = millis();

    // Backlight auto-off
    unsigned long timeout = BL_TIMEOUTS[backlightTimeoutIndex];
    if (timeout > 0 && blOn && millis() - lastInteraction >= timeout) {
        displaySetBacklight(false);
        displayResetNavToScreen();
        blOn = false;
    }

    if (!any) return;

    // Wake backlight
    if (!blOn) {
        displaySetBacklight(true);
        blOn = true;
        wakeDelay = true;
        wakeDelayStart = millis();
        return;
    }

    // Ignore input during wake delay
    if (wakeDelay) {
        if (millis() - wakeDelayStart < BL_WAKE_DELAY_MS) return;
        wakeDelay = false;
    }

    displayHandleInput(in);
}

// ============================================================
// RECOVERY SAVE
// ============================================================
static void manageRecoverySave() {
    if (millis() - lastRecoverySave < RECOVERY_SAVE_INTERVAL) return;
    lastRecoverySave = millis();
    storageSaveRecoveryState();
}

// ============================================================
// RECOVERY APPLY (não bloqueante)
// ============================================================
static void applyRecovery(bool resume) {
    if (resume) {
        systemActive    = true;
        timerSetMinutes = recoveryTimerSet;
        if (recoveryTimerSet > 0 && recoveryTimerRem > 0) {
            timerResumeFromRecovery(recoveryTimerRem);
        }
        controlReset();
        systemStartMs = millis();
        Serial.println("[RECOVERY] Ciclo retomado");
    } else {
        Serial.println("[RECOVERY] Ciclo descartado");
    }
    storageClearRecoveryState();
    recoveryReset();
}

static void manageRecovery() {
    if (!recoveryPending) return;
    if (recoveryDecisionMade) {
        applyRecovery(recoveryDecisionResume);
        return;
    }
    if (millis() - recoveryStartMs >= RECOVERY_TIMEOUT_MS) {
        applyRecovery(true);  // timeout → SIM por padrão
    }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("========================================");
    Serial.println("  ESP32 PID THERMOSTAT");
    Serial.println("========================================");

    // 1. Relê OFF (boot seguro)
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    relayState = false;
    Serial.println("[BOOT] Rele OFF");

    // 2. Watchdog
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);
    Serial.printf("[BOOT] WDT %ds\n", WDT_TIMEOUT_SEC);

    // 3. Carregar configurações
    storageLoadSettings();
    storageLoadPresets();

    // 4. Sensor
    initSensor();

    // 5. Encoder
    encoderInit();

    // 6. LCD
    displayInit();

    // 7. Timer
    timerInit();

    // 8. Controle
    controlInit();
    safetyInit();
    safetySetTriggerCallback(onSafetyTrigger);

    // 9. Recovery check (não bloqueante: tela e decisão acontecem no loop)
    if (storageHasRecoveryState()) {
        storageLoadRecoveryState(recoveryTimerRem, recoveryTimerSet);
        recoveryPending        = true;
        recoveryStartMs        = millis();
        recoveryChoice         = true;
        recoveryDecisionMade   = false;
        recoveryDecisionResume = true;
        Serial.printf("[RECOVERY] Pendente — timerSet=%u min, trem=%lu ms\n",
                      recoveryTimerSet, recoveryTimerRem);
    }

    // 10. WiFi deferred
    wfState = WF_INIT;
    lastInteraction = millis();

    Serial.println("[BOOT] Setup completo");
    Serial.println("========================================");
}

// ============================================================
// LOOP PRINCIPAL
// ============================================================
void loop() {
    // 1. Input + backlight
    handleInputAndBacklight();

    // 2. Temperatura
    manageTemperature();

    // 3. Segurança
    safetyCheck();

    // 4. Controle ou Auto-tune
    //    AT_HEATING/AT_COOLING: autotune ativo → autotuneUpdate()
    //    AT_DONE: aguardando confirmação do usuário → não controlar (relé fica OFF)
    //    AT_IDLE/AT_CANCELLED: controle normal
    AutotuneState atSt = autotuneGetState();
    if (atSt == AT_HEATING || atSt == AT_COOLING) {
        autotuneUpdate();
    } else if (atSt == AT_DONE) {
        setRelay(false);
    } else if (!recoveryPending) {
        controlRun();
    }

    // 5. Timer
    if (systemActive) timerUpdate();
    if (timerIsExpired()) {
        systemActive = false;
        setRelay(false);
        storageSaveRecoveryState();
        Serial.println("[TIMER] Sistema desativado por timer");
    }

    // 6. Display
    displayUpdate();
    displayGraphSample();

    // 7. Recovery (decisão / timeout)
    manageRecovery();
    manageRecoverySave();

    // 8. WiFi + Blynk
    manageWiFi();
    manageBlynk();

    // 9. Watchdog
    esp_task_wdt_reset();
}
