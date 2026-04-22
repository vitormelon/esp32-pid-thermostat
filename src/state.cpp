#include "state.h"

float currentTemp       = 0.0f;
bool  sensorFailed      = false;
bool  firstValidReading = false;
bool  newTempReading    = false;
unsigned long lastValidTempTime = 0;

float movingAvgBuffer[MOVING_AVG_SAMPLES] = {0};
int   movingAvgIndex = 0;
int   movingAvgCount = 0;

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
