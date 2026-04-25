#pragma once
#include <Arduino.h>
#include "state.h"

// Callback chamado quando safety detecta erro. main.cpp registra para fazer
// cleanup (cancelar autotune, parar timer, salvar recovery, etc.).
typedef void (*SafetyTriggerCallback)(SafetyError err, float trigTemp);

void safetyInit();
void safetyCheck();
void safetyClear();
void safetySetTriggerCallback(SafetyTriggerCallback cb);

// Diagnóstico (útil para testes)
unsigned long safetyCutoffElapsedMs();
bool          safetyStuckChecked();
