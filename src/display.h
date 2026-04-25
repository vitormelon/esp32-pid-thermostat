#pragma once
#include <Arduino.h>
#include "encoder.h"

void displayInit();
void displayUpdate();
void displayHandleInput(EncoderInput in);
void displaySetBacklight(bool on);
bool displayIsBacklightOn();
void displayGraphSample();
void displayResetNavToScreen();
void displayResetAutotuneUI();
bool displayIsSafetyScreen();
