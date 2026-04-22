#include "control.h"
#include "state.h"
#include "config.h"

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
    if (relayState != on) {
        relayState = on;
        digitalWrite(RELAY_PIN, on ? HIGH : LOW);
        Serial.printf("[RELAY] %s\n", on ? "LIGADO" : "DESLIGADO");
    }
}

void addToMovingAverage(float temp) {
    movingAvgBuffer[movingAvgIndex] = temp;
    movingAvgIndex = (movingAvgIndex + 1) % MOVING_AVG_SAMPLES;
    if (movingAvgCount < MOVING_AVG_SAMPLES) movingAvgCount++;
}

float getMovingAverage() {
    if (movingAvgCount == 0) return currentTemp;
    float sum = 0;
    for (int i = 0; i < movingAvgCount; i++) sum += movingAvgBuffer[i];
    return sum / movingAvgCount;
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

    bool satHigh = (pidOutput >= 100.0f);
    bool satLow  = (pidOutput <= 0.0f);
    if ((!satHigh && !satLow) ||
        (satHigh && error < 0.0f) ||
        (satLow  && error > 0.0f)) {
        pidIntegral += error * dt;
    }

    if (pidKi > 0.0f) {
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
