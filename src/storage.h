#pragma once
#include <Arduino.h>

void storageLoadSettings();
void storageSaveSetPoint();
void storageSaveOffset();
void storageSaveControlMode();
void storageSavePidKp();
void storageSavePidKi();
void storageSavePidKd();
void storageSavePidWindow();
void storageSavePidThreshold();
void storageSaveBacklightTimeout();
void storageSaveTimerMinutes();

void storageLoadPresets();
void storageSavePreset(int index);
void storageDeletePreset(int index);

void storageSaveGraphScale(int scale);
int  storageLoadGraphScale();
void storageSaveFlip();


void storageSaveRecoveryState();
bool storageHasRecoveryState();
void storageLoadRecoveryState(unsigned long &timerRemaining, unsigned int &timerSet);
void storageClearRecoveryState();
