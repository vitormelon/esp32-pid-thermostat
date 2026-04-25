#pragma once
#include <Arduino.h>
#include "config.h"

struct Preset {
    char name[PRESET_NAME_MAX_LEN + 1];
    float kp;
    float ki;
    float kd;
    unsigned long windowMs;
    bool used;
};

// --- Temperatura ---
extern float currentTemp;
extern bool  sensorFailed;
extern bool  firstValidReading;
extern bool  newTempReading;
extern unsigned long lastValidTempTime;

// --- Controle ---
extern float setPoint;
extern float offset;
extern int   controlMode;
extern bool  relayState;
extern bool  systemActive;

// --- PID ---
extern float pidKp, pidKi, pidKd;
extern unsigned long pidWindowSize;
extern float pidThreshold;
extern float pidOutput;

// --- Timer ---
extern unsigned int  timerSetMinutes;
extern unsigned long timerRemainingMs;
extern bool          timerRunning;

// --- Cronômetro ---
extern unsigned long systemStartMs;

// --- Segurança ---
extern bool hardCutoffActive;

enum SafetyError {
    SAFETY_OK,
    SAFETY_OVERTEMP,
    SAFETY_SENSOR_FAIL,
    SAFETY_RELAY_STUCK
};
extern SafetyError safetyError;
extern float       safetyTriggerTemp;

// --- Backlight ---
extern int backlightTimeoutIndex;

// --- Display ---
extern bool lcdFlipped;

// --- Conectividade ---
extern bool wifiConnected;
extern bool blynkConnected;

// --- Presets ---
extern Preset presets[MAX_PRESETS];
extern int    activePresetIndex;

// --- Recovery (não bloqueante) ---
extern bool          recoveryPending;
extern unsigned long recoveryStartMs;
extern bool          recoveryChoice;
extern unsigned long recoveryTimerRem;
extern unsigned int  recoveryTimerSet;
extern bool          recoveryDecisionMade;
extern bool          recoveryDecisionResume;

// Zera todas as variáveis de recovery aos valores padrão.
void recoveryReset();
