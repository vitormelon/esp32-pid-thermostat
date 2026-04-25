#include "Arduino.h"

unsigned long _mockMillis = 0;
unsigned long _mockMicros = 0;
int           _mockGpioState[64] = {0};
FakeSerial    Serial;

void mockReset() {
    _mockMillis = 0;
    _mockMicros = 0;
    for (int i = 0; i < 64; i++) _mockGpioState[i] = 0;
}

void mockAdvanceMs(unsigned long ms) {
    _mockMillis += ms;
    _mockMicros += ms * 1000UL;
}
