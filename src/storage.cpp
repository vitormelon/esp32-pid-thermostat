#include "storage.h"
#include "state.h"
#include "config.h"
#include <Preferences.h>
#include <math.h>

static Preferences prefs;

// ============================================================
// CHECK-BEFORE-WRITE HELPERS
// ============================================================
// Lê valor atual do NVS e só escreve se diferente. Reduz desgaste do flash.
// O wear-leveling do flash é feito automaticamente pela camada NVS do ESP-IDF
// (escritas são distribuídas entre múltiplos setores físicos).

static const float NVS_FLOAT_EPSILON = 0.0001f;

// Retorna true se escreveu, false se sem mudança.
static bool saveFloatIfChanged(const char* ns, const char* key, float val) {
    prefs.begin(ns, true);
    bool exists = prefs.isKey(key);
    float cur = exists ? prefs.getFloat(key, 0.0f) : 0.0f;
    prefs.end();
    if (exists && fabsf(cur - val) < NVS_FLOAT_EPSILON) return false;
    prefs.begin(ns, false);
    prefs.putFloat(key, val);
    prefs.end();
    return true;
}

static bool saveIntIfChanged(const char* ns, const char* key, int val) {
    prefs.begin(ns, true);
    bool exists = prefs.isKey(key);
    int cur = exists ? prefs.getInt(key, 0) : 0;
    prefs.end();
    if (exists && cur == val) return false;
    prefs.begin(ns, false);
    prefs.putInt(key, val);
    prefs.end();
    return true;
}

static bool saveUIntIfChanged(const char* ns, const char* key, unsigned int val) {
    prefs.begin(ns, true);
    bool exists = prefs.isKey(key);
    unsigned int cur = exists ? prefs.getUInt(key, 0) : 0;
    prefs.end();
    if (exists && cur == val) return false;
    prefs.begin(ns, false);
    prefs.putUInt(key, val);
    prefs.end();
    return true;
}

static bool saveULongIfChanged(const char* ns, const char* key, unsigned long val) {
    prefs.begin(ns, true);
    bool exists = prefs.isKey(key);
    unsigned long cur = exists ? prefs.getULong(key, 0UL) : 0UL;
    prefs.end();
    if (exists && cur == val) return false;
    prefs.begin(ns, false);
    prefs.putULong(key, val);
    prefs.end();
    return true;
}

static bool saveBoolIfChanged(const char* ns, const char* key, bool val) {
    prefs.begin(ns, true);
    bool exists = prefs.isKey(key);
    bool cur = exists ? prefs.getBool(key, false) : false;
    prefs.end();
    if (exists && cur == val) return false;
    prefs.begin(ns, false);
    prefs.putBool(key, val);
    prefs.end();
    return true;
}

static bool saveStringIfChanged(const char* ns, const char* key, const char* val) {
    prefs.begin(ns, true);
    bool exists = prefs.isKey(key);
    String cur = exists ? prefs.getString(key, "") : String("");
    prefs.end();
    if (exists && cur.equals(val)) return false;
    prefs.begin(ns, false);
    prefs.putString(key, val);
    prefs.end();
    return true;
}

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

void storageSaveSetPoint() {
    if (saveFloatIfChanged("oven", "sp", setPoint))
        Serial.printf("[NVS] SP=%.1f\n", setPoint);
}

void storageSaveOffset() {
    if (saveFloatIfChanged("oven", "off", offset))
        Serial.printf("[NVS] Off=%.1f\n", offset);
}

void storageSaveControlMode() {
    if (saveIntIfChanged("oven", "mode", controlMode))
        Serial.printf("[NVS] Mode=%d\n", controlMode);
}

void storageSavePidKp() {
    if (saveFloatIfChanged("oven", "kp", pidKp))
        Serial.printf("[NVS] Kp=%.3f\n", pidKp);
}

void storageSavePidKi() {
    if (saveFloatIfChanged("oven", "ki", pidKi))
        Serial.printf("[NVS] Ki=%.4f\n", pidKi);
}

void storageSavePidKd() {
    if (saveFloatIfChanged("oven", "kd", pidKd))
        Serial.printf("[NVS] Kd=%.1f\n", pidKd);
}

void storageSavePidWindow() {
    if (saveULongIfChanged("oven", "win", pidWindowSize))
        Serial.printf("[NVS] Win=%lums\n", pidWindowSize);
}

void storageSavePidThreshold() {
    if (saveFloatIfChanged("oven", "thr", pidThreshold))
        Serial.printf("[NVS] Thr=%.0f%%\n", pidThreshold);
}

void storageSaveBacklightTimeout() {
    saveIntIfChanged("oven", "bl", backlightTimeoutIndex);
}

void storageSaveTimerMinutes() {
    saveUIntIfChanged("oven", "tmr", timerSetMinutes);
}

// ============================================================
// GRAPH SCALE
// ============================================================

void storageSaveGraphScale(int scale) {
    saveIntIfChanged("oven", "gscale", scale);
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
    saveBoolIfChanged("oven", "flip", lcdFlipped);
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
    bool anyChange = false;

    snprintf(key, sizeof(key), "p%du", index);
    if (saveBoolIfChanged("presets", key, p.used)) anyChange = true;
    snprintf(key, sizeof(key), "p%dn", index);
    if (saveStringIfChanged("presets", key, p.name)) anyChange = true;
    snprintf(key, sizeof(key), "p%dkp", index);
    if (saveFloatIfChanged("presets", key, p.kp)) anyChange = true;
    snprintf(key, sizeof(key), "p%dki", index);
    if (saveFloatIfChanged("presets", key, p.ki)) anyChange = true;
    snprintf(key, sizeof(key), "p%dkd", index);
    if (saveFloatIfChanged("presets", key, p.kd)) anyChange = true;
    snprintf(key, sizeof(key), "p%dw", index);
    if (saveULongIfChanged("presets", key, p.windowMs)) anyChange = true;

    if (anyChange) Serial.printf("[NVS] Preset %d salvo: '%s'\n", index, p.name);
}

void storageDeletePreset(int index) {
    if (index < 0 || index >= MAX_PRESETS) return;
    presets[index].used = false;
    memset(presets[index].name, 0, sizeof(presets[index].name));

    char key[8];
    snprintf(key, sizeof(key), "p%du", index);
    saveBoolIfChanged("presets", key, false);

    if (activePresetIndex == index) activePresetIndex = -1;
    Serial.printf("[NVS] Preset %d removido\n", index);
}

// ============================================================
// RECOVERY
// ============================================================
// Salva estado para recuperação após queda de energia.
// Tolerância no timer remaining (60s) evita escritas frequentes durante
// countdown sem perder muita precisão na recuperação.

#define RECOVERY_TREM_TOLERANCE_MS 60000UL

void storageSaveRecoveryState() {
    prefs.begin("recov", true);
    bool curActive = prefs.getBool("active", false);
    unsigned long curRem = prefs.getULong("trem", 0);
    unsigned int curSet = prefs.getUInt("tset", 0);
    prefs.end();

    bool activeChanged = (curActive != systemActive);
    bool setChanged    = (curSet != timerSetMinutes);
    unsigned long diff = (curRem > timerRemainingMs)
                         ? (curRem - timerRemainingMs)
                         : (timerRemainingMs - curRem);
    bool remChanged    = (diff > RECOVERY_TREM_TOLERANCE_MS);

    // Sempre persistir transições críticas (ativar/desativar) imediatamente,
    // mesmo que trem ainda esteja dentro da tolerância.
    if (!activeChanged && !setChanged && !remChanged) return;

    prefs.begin("recov", false);
    if (activeChanged) prefs.putBool("active", systemActive);
    if (setChanged)    prefs.putUInt("tset", timerSetMinutes);
    if (activeChanged || setChanged || remChanged) prefs.putULong("trem", timerRemainingMs);
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
    saveBoolIfChanged("recov", "active", false);
}
