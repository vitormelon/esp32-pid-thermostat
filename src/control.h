#pragma once
#include <Arduino.h>

void controlInit();
void controlRun();
void controlReset();
void controlReparamKi(float oldKi);   // bumpless: preserves Ki * integral across Ki changes
void controlReparamWindow();          // restarts only the duty-cycle phase (does not touch the relay)
void setRelay(bool on);
void setRelayForce(bool on);   // always writes the GPIO; use in safety paths
