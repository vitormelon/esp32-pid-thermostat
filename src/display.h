#pragma once
#include <Arduino.h>
#include "encoder.h"

void displayInit();
void displayUpdate();
void displayHandleInput(EncoderInput in);
void displaySetBacklight(bool on);
bool displayIsBacklightOn();
bool displayShowRecoveryScreen();
void displayGraphSample();
void displayResetNavToScreen();
bool displayIsSafetyScreen();
