#include "timer_ctrl.h"
#include "state.h"

static unsigned long lastTick = 0;
static bool expiredFlag = false;

void timerInit() {
    timerRunning     = false;
    timerRemainingMs = 0;
    lastTick         = 0;
    expiredFlag      = false;
}

void timerUpdate() {
    if (!timerRunning || timerSetMinutes == 0) return;

    unsigned long now = millis();
    if (lastTick == 0) { lastTick = now; return; }

    unsigned long elapsed = now - lastTick;
    lastTick = now;

    if (elapsed >= timerRemainingMs) {
        timerRemainingMs = 0;
        timerRunning     = false;
        expiredFlag      = true;
        Serial.println("[TIMER] Expirado!");
    } else {
        timerRemainingMs -= elapsed;
    }
}

void timerStart() {
    if (timerSetMinutes == 0) return;
    timerRemainingMs = (unsigned long)timerSetMinutes * 60000UL;
    timerRunning     = true;
    lastTick         = millis();
    expiredFlag      = false;
    Serial.printf("[TIMER] Iniciado: %d min\n", timerSetMinutes);
}

void timerStop() {
    timerRunning = false;
    lastTick     = 0;
    expiredFlag  = false;
}

bool timerIsExpired() {
    if (expiredFlag) {
        expiredFlag = false;
        return true;
    }
    return false;
}

void timerReset() {
    timerRunning     = false;
    timerRemainingMs = 0;
    lastTick         = 0;
    expiredFlag      = false;
}

void timerResumeFromRecovery(unsigned long remainingMs) {
    if (timerSetMinutes == 0 || remainingMs == 0) return;
    timerRemainingMs = remainingMs;
    timerRunning     = true;
    lastTick         = millis();
    expiredFlag      = false;
    Serial.printf("[TIMER] Retomado: %lu ms restantes\n", remainingMs);
}
