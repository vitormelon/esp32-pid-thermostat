#include "control.h"
#include "state.h"
#include "config.h"

// Lock crítico para mudanças no estado do relé. setRelay/setRelayForce podem
// ser chamados de múltiplas tasks (control loop, display input, network).
// Em ESP32: portMUX_TYPE (spinlock leve, atomic entre cores).
// No host (testes): no-op.
#ifdef ARDUINO_ARCH_ESP32
  #include "freertos/FreeRTOS.h"
  static portMUX_TYPE relayMux = portMUX_INITIALIZER_UNLOCKED;
  #define RELAY_LOCK()    portENTER_CRITICAL(&relayMux)
  #define RELAY_UNLOCK()  portEXIT_CRITICAL(&relayMux)
#else
  #define RELAY_LOCK()    ((void)0)
  #define RELAY_UNLOCK()  ((void)0)
#endif

static float pidIntegral      = 0.0f;
static float pidLastError     = 0.0f;
static float pidFilteredDeriv = 0.0f;
static unsigned long lastPIDCompute    = 0;
static unsigned long relayPhaseStart   = 0;
static bool          lastRelayForPhase = false;

static const float DERIV_FILTER_ALPHA = 0.3f;

void controlInit() {
    pidIntegral      = 0.0f;
    pidLastError     = 0.0f;
    pidFilteredDeriv = 0.0f;
    pidOutput        = 0.0f;
    lastPIDCompute    = millis();
    relayPhaseStart   = millis();
    lastRelayForPhase = false;
}

void controlReset() {
    controlInit();
}

void setRelay(bool on) {
    bool changed;
    RELAY_LOCK();
    changed = (relayState != on);
    if (changed) {
        relayState = on;
        digitalWrite(RELAY_PIN, on ? HIGH : LOW);
    }
    RELAY_UNLOCK();
    if (changed) Serial.printf("[RELAY] %s\n", on ? "LIGADO" : "DESLIGADO");
}

// Em paths de segurança queremos garantir o estado físico do GPIO
// mesmo que o software ache que já está correto.
void setRelayForce(bool on) {
    RELAY_LOCK();
    relayState = on;
    digitalWrite(RELAY_PIN, on ? HIGH : LOW);
    RELAY_UNLOCK();
}

// --- Histerese simples ---
static void controlHysteresis() {
    if (currentTemp >= setPoint) {
        setRelay(false);
    } else if (currentTemp <= setPoint - offset) {
        setRelay(true);
    }
}

// --- PID: calcula output 0-100% ---
static void computePID() {
    if (!newTempReading) return;

    unsigned long now = millis();
    float dt = (now - lastPIDCompute) / 1000.0f;
    dt = constrain(dt, 0.001f, 10.0f);

    float error = setPoint - currentTemp;

    float rawDeriv = (error - pidLastError) / dt;
    pidFilteredDeriv = DERIV_FILTER_ALPHA * rawDeriv
                     + (1.0f - DERIV_FILTER_ALPHA) * pidFilteredDeriv;

    if (pidKi <= 0.0f) {
        pidIntegral = 0.0f;
    } else {
        bool satHigh = (pidOutput >= 100.0f);
        bool satLow  = (pidOutput <= 0.0f);
        if ((!satHigh && !satLow) ||
            (satHigh && error < 0.0f) ||
            (satLow  && error > 0.0f)) {
            pidIntegral += error * dt;
        }

        float maxInt = 50.0f / pidKi;
        pidIntegral = constrain(pidIntegral, -maxInt, maxInt);
    }

    pidOutput = constrain(
        pidKp * error + pidKi * pidIntegral + pidKd * pidFilteredDeriv,
        0.0f, 100.0f
    );

    pidLastError   = error;
    lastPIDCompute = now;
}

// --- PID On/Off: liga se output > threshold ---
static void controlPIDOnOff() {
    computePID();
    setRelay(pidOutput > pidThreshold);
}

// --- PID Janela: time-proportional relay ---
static void controlPIDWindow() {
    computePID();

    if (pidOutput <= 0.0f) {
        if (relayState) { setRelay(false); relayPhaseStart = millis(); lastRelayForPhase = false; }
        return;
    }
    if (pidOutput >= 100.0f) {
        if (!relayState) { setRelay(true); relayPhaseStart = millis(); lastRelayForPhase = true; }
        return;
    }

    unsigned long now = millis();
    float onTimeMs  = pidOutput * (float)pidWindowSize / 100.0f;
    float offTimeMs = (float)pidWindowSize - onTimeMs;

    if (relayState != lastRelayForPhase) {
        relayPhaseStart   = now;
        lastRelayForPhase = relayState;
    }

    unsigned long elapsed = now - relayPhaseStart;

    if (relayState) {
        if (elapsed >= (unsigned long)onTimeMs) {
            setRelay(false);
            relayPhaseStart   = now;
            lastRelayForPhase = false;
        }
    } else {
        if (elapsed >= (unsigned long)offTimeMs) {
            setRelay(true);
            relayPhaseStart   = now;
            lastRelayForPhase = true;
        }
    }
}

void controlRun() {
    if (!firstValidReading || sensorFailed || hardCutoffActive || !systemActive) return;

    switch (controlMode) {
        case MODE_HYSTERESIS: controlHysteresis(); break;
        case MODE_PID_ONOFF:  controlPIDOnOff();   break;
        case MODE_PID_WINDOW: controlPIDWindow();  break;
        default:              controlHysteresis();  break;
    }

    newTempReading = false;
}
