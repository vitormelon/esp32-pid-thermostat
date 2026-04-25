#include "state.h"

float currentTemp       = 0.0f;
bool  sensorFailed      = false;
bool  firstValidReading = false;
bool  newTempReading    = false;
unsigned long lastValidTempTime = 0;

float setPoint    = DEFAULT_SETPOINT;
float offset      = DEFAULT_OFFSET;
int   controlMode = MODE_HYSTERESIS;
bool  relayState  = false;
bool  systemActive = false;

float pidKp         = DEFAULT_PID_KP;
float pidKi         = DEFAULT_PID_KI;
float pidKd         = DEFAULT_PID_KD;
unsigned long pidWindowSize = DEFAULT_PID_WINDOW_MS;
float pidThreshold  = DEFAULT_PID_THRESHOLD;
float pidOutput     = 0.0f;

unsigned int  timerSetMinutes = 0;
unsigned long timerRemainingMs = 0;
bool          timerRunning = false;

unsigned long systemStartMs = 0;

bool hardCutoffActive = false;

SafetyError safetyError       = SAFETY_OK;
float       safetyTriggerTemp = 0.0f;

int backlightTimeoutIndex = 0;

bool lcdFlipped = false;

bool wifiConnected  = false;
bool blynkConnected = false;

Preset presets[MAX_PRESETS];
int    activePresetIndex = -1;

bool          recoveryPending        = false;
unsigned long recoveryStartMs        = 0;
bool          recoveryChoice         = true;
unsigned long recoveryTimerRem       = 0;
unsigned int  recoveryTimerSet       = 0;
bool          recoveryDecisionMade   = false;
bool          recoveryDecisionResume = true;

void recoveryReset() {
    recoveryPending        = false;
    recoveryStartMs        = 0;
    recoveryChoice         = true;
    recoveryTimerRem       = 0;
    recoveryTimerSet       = 0;
    recoveryDecisionMade   = false;
    recoveryDecisionResume = true;
}
