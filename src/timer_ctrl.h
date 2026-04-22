#pragma once
#include <Arduino.h>

void timerInit();
void timerUpdate();
void timerStart();
void timerStop();
bool timerIsExpired();
void timerReset();
void timerResumeFromRecovery(unsigned long remainingMs);
