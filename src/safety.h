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

// Indica se o erro atual pode ser limpo por click simples (true) ou
// se exige long press (false — caso de RELAY_STUCK, evita loop de clear).
bool safetyAllowsClickClear();

// Diagnóstico (útil para testes)
unsigned long safetyCutoffElapsedMs();
bool          safetyStuckChecked();
