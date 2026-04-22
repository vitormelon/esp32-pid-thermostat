#pragma once
#include <Arduino.h>

void controlInit();
void controlRun();
void controlReset();
void setRelay(bool on);

void addToMovingAverage(float temp);
float getMovingAverage();
