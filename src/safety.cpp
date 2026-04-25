#include "safety.h"
#include "config.h"
#include "control.h"

static float                   sCutoffTemp   = 0.0f;
static unsigned long           sCutoffTime   = 0;
static bool                    sStuckChecked = false;
static SafetyTriggerCallback   sCallback     = nullptr;

void safetyInit() {
    sCutoffTemp   = 0.0f;
    sCutoffTime   = 0;
    sStuckChecked = false;
}

void safetySetTriggerCallback(SafetyTriggerCallback cb) {
    sCallback = cb;
}

unsigned long safetyCutoffElapsedMs() {
    return sCutoffTime ? (millis() - sCutoffTime) : 0;
}

bool safetyStuckChecked() { return sStuckChecked; }

static void doTrigger(SafetyError err, float trigTemp) {
    safetyError       = err;
    safetyTriggerTemp = trigTemp;
    setRelayForce(false);
    systemActive = false;
    if (sCallback) sCallback(err, trigTemp);
}

void safetyCheck() {
    if (safetyError != SAFETY_OK) return;
    if (!firstValidReading) return;

    // 1. Sensor fail
    if (sensorFailed) {
        doTrigger(SAFETY_SENSOR_FAIL, currentTemp);
        return;
    }

    // 2. Cutoff
    if (currentTemp >= HARD_CUTOFF_TEMP) {
        if (!hardCutoffActive) {
            hardCutoffActive = true;
            sCutoffTemp      = currentTemp;
            sCutoffTime      = millis();
            sStuckChecked    = false;
            setRelayForce(false);
        }

        // 3. Stuck relay (em SAFETY_STUCK_DELAY_MS, antes do overtemp)
        if (!sStuckChecked && millis() - sCutoffTime >= SAFETY_STUCK_DELAY_MS) {
            sStuckChecked = true;
            if (currentTemp > sCutoffTemp + SAFETY_STUCK_THRESHOLD) {
                doTrigger(SAFETY_RELAY_STUCK, sCutoffTemp);
                return;
            }
        }

        // 4. Overtemp dispara após OVERTEMP_DELAY_MS
        if (millis() - sCutoffTime >= OVERTEMP_DELAY_MS) {
            doTrigger(SAFETY_OVERTEMP, sCutoffTemp);
            return;
        }
    } else if (hardCutoffActive && currentTemp < CUTOFF_RECOVERY_TEMP) {
        hardCutoffActive = false;
        sCutoffTemp      = 0.0f;
        sCutoffTime      = 0;
        sStuckChecked    = false;
    }
}

bool safetyAllowsClickClear() {
    return safetyError != SAFETY_RELAY_STUCK;
}

void safetyClear() {
    safetyError       = SAFETY_OK;
    safetyTriggerTemp = 0.0f;
    hardCutoffActive  = false;
    sCutoffTemp       = 0.0f;
    sCutoffTime       = 0;
    sStuckChecked     = false;
}
