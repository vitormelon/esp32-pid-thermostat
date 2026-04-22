#include "storage.h"
#include "state.h"
#include "config.h"
#include <Preferences.h>

static Preferences prefs;

// ============================================================
// SETTINGS
// ============================================================

void storageLoadSettings() {
    prefs.begin("oven", true);
    setPoint            = prefs.getFloat("sp",   DEFAULT_SETPOINT);
    offset              = prefs.getFloat("off",  DEFAULT_OFFSET);
    controlMode         = prefs.getInt("mode",   MODE_HYSTERESIS);
    pidKp               = prefs.getFloat("kp",   DEFAULT_PID_KP);
    pidKi               = prefs.getFloat("ki",   DEFAULT_PID_KI);
    pidKd               = prefs.getFloat("kd",   DEFAULT_PID_KD);
    pidWindowSize       = prefs.getULong("win",  DEFAULT_PID_WINDOW_MS);
    pidThreshold        = prefs.getFloat("thr",  DEFAULT_PID_THRESHOLD);
    backlightTimeoutIndex = prefs.getInt("bl",   0);
    timerSetMinutes     = prefs.getUInt("tmr",   0);
    lcdFlipped          = prefs.getBool("flip",  false);
    prefs.end();

    setPoint      = constrain(setPoint, SP_MIN, SP_MAX);
    offset        = constrain(offset, OFFSET_MIN, OFFSET_MAX);
    controlMode   = constrain(controlMode, 0, NUM_MODES - 1);
    pidKp         = constrain(pidKp, PID_KP_MIN, PID_KP_MAX);
    pidKi         = constrain(pidKi, PID_KI_MIN, PID_KI_MAX);
    pidKd         = constrain(pidKd, PID_KD_MIN, PID_KD_MAX);
    pidWindowSize = constrain(pidWindowSize,
                              (unsigned long)PID_WINDOW_MIN_S * 1000UL,
                              (unsigned long)PID_WINDOW_MAX_S * 1000UL);
    pidThreshold  = constrain(pidThreshold, PID_THRESHOLD_MIN, PID_THRESHOLD_MAX);
    backlightTimeoutIndex = constrain(backlightTimeoutIndex, 0, BL_TIMEOUT_COUNT - 1);
    timerSetMinutes = min(timerSetMinutes, (unsigned int)MAX_TIMER_MINUTES);

    Serial.printf("[NVS] SP=%.1f Off=%.1f Mode=%d BL=%d Timer=%dmin\n",
                  setPoint, offset, controlMode, backlightTimeoutIndex, timerSetMinutes);
    Serial.printf("[NVS] PID: Kp=%.3f Ki=%.4f Kd=%.1f Win=%lums Thr=%.0f%%\n",
                  pidKp, pidKi, pidKd, pidWindowSize, pidThreshold);
}

static void saveFloat(const char* key, float val) {
    prefs.begin("oven", false);
    prefs.putFloat(key, val);
    prefs.end();
}

void storageSaveSetPoint() {
    saveFloat("sp", setPoint);
    Serial.printf("[NVS] SP=%.1f\n", setPoint);
}

void storageSaveOffset() {
    saveFloat("off", offset);
    Serial.printf("[NVS] Off=%.1f\n", offset);
}

void storageSaveControlMode() {
    prefs.begin("oven", false);
    prefs.putInt("mode", controlMode);
    prefs.end();
    Serial.printf("[NVS] Mode=%d\n", controlMode);
}

void storageSavePidKp()  { saveFloat("kp", pidKp);  Serial.printf("[NVS] Kp=%.3f\n", pidKp); }
void storageSavePidKi()  { saveFloat("ki", pidKi);  Serial.printf("[NVS] Ki=%.4f\n", pidKi); }
void storageSavePidKd()  { saveFloat("kd", pidKd);  Serial.printf("[NVS] Kd=%.1f\n", pidKd); }

void storageSavePidWindow() {
    prefs.begin("oven", false);
    prefs.putULong("win", pidWindowSize);
    prefs.end();
    Serial.printf("[NVS] Win=%lums\n", pidWindowSize);
}

void storageSavePidThreshold() {
    saveFloat("thr", pidThreshold);
    Serial.printf("[NVS] Thr=%.0f%%\n", pidThreshold);
}

void storageSaveBacklightTimeout() {
    prefs.begin("oven", false);
    prefs.putInt("bl", backlightTimeoutIndex);
    prefs.end();
}

void storageSaveTimerMinutes() {
    prefs.begin("oven", false);
    prefs.putUInt("tmr", timerSetMinutes);
    prefs.end();
}

// ============================================================
// GRAPH SCALE
// ============================================================

void storageSaveGraphScale(int scale) {
    prefs.begin("oven", false);
    prefs.putInt("gscale", scale);
    prefs.end();
}

int storageLoadGraphScale() {
    prefs.begin("oven", true);
    int v = prefs.getInt("gscale", 0);
    prefs.end();
    return constrain(v, 0, GRAPH_SCALE_COUNT - 1);
}

// ============================================================
// FLIP
// ============================================================

void storageSaveFlip() {
    prefs.begin("oven", false);
    prefs.putBool("flip", lcdFlipped);
    prefs.end();
}

// ============================================================
// PRESETS
// ============================================================

void storageLoadPresets() {
    prefs.begin("presets", true);
    for (int i = 0; i < MAX_PRESETS; i++) {
        char key[8];
        snprintf(key, sizeof(key), "p%du", i);
        presets[i].used = prefs.getBool(key, false);
        if (!presets[i].used) {
            memset(presets[i].name, 0, sizeof(presets[i].name));
            continue;
        }
        snprintf(key, sizeof(key), "p%dn", i);
        String name = prefs.getString(key, "");
        strncpy(presets[i].name, name.c_str(), PRESET_NAME_MAX_LEN);
        presets[i].name[PRESET_NAME_MAX_LEN] = '\0';

        snprintf(key, sizeof(key), "p%dkp", i);
        presets[i].kp = prefs.getFloat(key, DEFAULT_PID_KP);
        snprintf(key, sizeof(key), "p%dki", i);
        presets[i].ki = prefs.getFloat(key, DEFAULT_PID_KI);
        snprintf(key, sizeof(key), "p%dkd", i);
        presets[i].kd = prefs.getFloat(key, DEFAULT_PID_KD);
        snprintf(key, sizeof(key), "p%dw", i);
        presets[i].windowMs = prefs.getULong(key, DEFAULT_PID_WINDOW_MS);

        Serial.printf("[NVS] Preset %d: '%s' Kp=%.3f Ki=%.4f Kd=%.1f W=%lums\n",
                      i, presets[i].name, presets[i].kp, presets[i].ki,
                      presets[i].kd, presets[i].windowMs);
    }
    prefs.end();
}

void storageSavePreset(int index) {
    if (index < 0 || index >= MAX_PRESETS) return;
    Preset& p = presets[index];
    char key[8];

    prefs.begin("presets", false);
    snprintf(key, sizeof(key), "p%du", index);
    prefs.putBool(key, p.used);
    snprintf(key, sizeof(key), "p%dn", index);
    prefs.putString(key, p.name);
    snprintf(key, sizeof(key), "p%dkp", index);
    prefs.putFloat(key, p.kp);
    snprintf(key, sizeof(key), "p%dki", index);
    prefs.putFloat(key, p.ki);
    snprintf(key, sizeof(key), "p%dkd", index);
    prefs.putFloat(key, p.kd);
    snprintf(key, sizeof(key), "p%dw", index);
    prefs.putULong(key, p.windowMs);
    prefs.end();

    Serial.printf("[NVS] Preset %d salvo: '%s'\n", index, p.name);
}

void storageDeletePreset(int index) {
    if (index < 0 || index >= MAX_PRESETS) return;
    presets[index].used = false;
    memset(presets[index].name, 0, sizeof(presets[index].name));

    char key[8];
    prefs.begin("presets", false);
    snprintf(key, sizeof(key), "p%du", index);
    prefs.putBool(key, false);
    prefs.end();

    if (activePresetIndex == index) activePresetIndex = -1;
    Serial.printf("[NVS] Preset %d removido\n", index);
}

// ============================================================
// RECOVERY
// ============================================================

void storageSaveRecoveryState() {
    prefs.begin("recov", false);
    prefs.putBool("active", systemActive);
    prefs.putULong("trem", timerRemainingMs);
    prefs.putUInt("tset", timerSetMinutes);
    prefs.end();
}

bool storageHasRecoveryState() {
    prefs.begin("recov", true);
    bool active = prefs.getBool("active", false);
    prefs.end();
    return active;
}

void storageLoadRecoveryState(unsigned long &timerRemaining, unsigned int &timerSet) {
    prefs.begin("recov", true);
    timerRemaining = prefs.getULong("trem", 0);
    timerSet       = prefs.getUInt("tset", 0);
    prefs.end();
}

void storageClearRecoveryState() {
    prefs.begin("recov", false);
    prefs.putBool("active", false);
    prefs.end();
}
