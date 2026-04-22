#include "encoder.h"
#include "config.h"
#include <ESP32Encoder.h>

static ESP32Encoder enc;
static int accumSteps = 0;

// Button state (manual, sem OneButton)
static bool     btnLast       = true;
static bool     btnDown       = false;
static bool     longFired     = false;
static unsigned long btnDownTime     = 0;
static unsigned long lastBtnChange   = 0;

void encoderInit() {
    ESP32Encoder::useInternalWeakPullResistors = UP;
    enc.attachFullQuad(ENCODER_DT_PIN, ENCODER_CLK_PIN);
    enc.setFilter(1023);
    enc.clearCount();

    pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
    Serial.println("[ENC] ESP32Encoder (PCNT) + botao direto");
}

EncoderInput encoderRead() {
    EncoderInput in = {0, false, false};

    // --- Rotação (PCNT hardware) ---
    int raw = (int)enc.getCount();
    enc.clearCount();
    accumSteps += raw;
    in.delta = accumSteps / ENCODER_STEPS_PER_DETENT;
    accumSteps %= ENCODER_STEPS_PER_DETENT;

    // --- Botão (leitura direta, click instantâneo na soltura) ---
    bool rawBtn = digitalRead(ENCODER_SW_PIN);

    // Debounce
    if (rawBtn != btnLast && millis() - lastBtnChange < ENCODER_DEBOUNCE_MS) {
        rawBtn = btnLast;
    }
    if (rawBtn != btnLast) {
        lastBtnChange = millis();
    }

    // Press down
    if (!rawBtn && btnLast) {
        btnDownTime = millis();
        btnDown     = true;
        longFired   = false;
    }

    // Long press (enquanto segurado)
    if (!rawBtn && btnDown && !longFired && millis() - btnDownTime >= LONG_PRESS_MS) {
        in.longPress = true;
        longFired    = true;
    }

    // Release → click instantâneo
    if (rawBtn && !btnLast && btnDown) {
        btnDown = false;
        if (!longFired) {
            in.pressed = true;
        }
    }

    btnLast = rawBtn;
    return in;
}
