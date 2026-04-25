#pragma once
#include <Arduino.h>

void controlInit();
void controlRun();
void controlReset();
void setRelay(bool on);
void setRelayForce(bool on);   // sempre escreve no GPIO; usar em paths de safety
