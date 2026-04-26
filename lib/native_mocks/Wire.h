// Mock mínimo de Wire (TwoWire) para testes nativos.
// Apenas o necessário para satisfazer setClock().
#pragma once
#include "Arduino.h"

extern unsigned long _mockWireClock;

class TwoWire {
public:
    void begin() {}
    void setClock(unsigned long hz) { _mockWireClock = hz; }
};

extern TwoWire Wire;
