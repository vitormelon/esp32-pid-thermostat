#pragma once
#include <Arduino.h>

struct EncoderInput {
    int  delta;      // rotação acumulada (positivo = CW, negativo = CCW)
    bool pressed;    // click curto
    bool longPress;  // click longo
};

void encoderInit();
EncoderInput encoderRead();
