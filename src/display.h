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
// Marca o buffer interno como inválido — próxima escrita redesenha tudo.
// Usado em init e em troca de tela (Screen).
void displayInvalidate();

// Exposto para testes: força um flush direto de uma linha (formato interno).
// Em produção é chamado via lcdLine() após formatação.
void displayLcdLineForTest(int row, const char* text);
