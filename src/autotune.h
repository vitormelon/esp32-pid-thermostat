#pragma once
#include <Arduino.h>

enum AutotuneState {
    AT_IDLE,
    AT_HEATING,
    AT_COOLING,
    AT_DONE,
    AT_CANCELLED
};

void autotuneStart();
void autotuneUpdate();
void autotuneCancel();
void autotuneReset();

bool           autotuneIsRunning();
AutotuneState  autotuneGetState();
int            autotuneGetCycle();
int            autotuneGetTotalCycles();
unsigned long  autotuneGetEtaMs();
unsigned long  autotuneGetLastCycleDurationMs();
unsigned long  autotuneGetCurrentCycleElapsedMs();
unsigned long  autotuneGetTotalElapsedMs();
float          autotuneGetSuggestedKp();
float          autotuneGetSuggestedKi();
float          autotuneGetSuggestedKd();
