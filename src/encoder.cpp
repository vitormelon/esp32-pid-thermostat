#include "encoder.h"
#include "encoder_logic.h"
#include "config.h"
#include <ESP32Encoder.h>

static ESP32Encoder enc;
static int accumSteps = 0;

// Estado do botão (state machine pura, definida em encoder_logic.{h,cpp})
static EncoderButtonState btnState;

// ISR captura cada borda no pino do botão e atualiza essas variáveis volatile.
// O loop/task lê e processa via encoderButtonTick(), evitando depender de polling
// para detectar pulsos curtos quando o loop está ocupado.
static volatile bool          btnRawIsr     = true;        // estado mais recente do pino
static volatile unsigned long btnIsrEdgeMs  = 0;           // timestamp da última borda

static void IRAM_ATTR onButtonChange() {
    btnRawIsr    = (digitalRead(ENCODER_SW_PIN) != 0);
    btnIsrEdgeMs = millis();
}

void encoderInit() {
    ESP32Encoder::useInternalWeakPullResistors = UP;
    enc.attachFullQuad(ENCODER_DT_PIN, ENCODER_CLK_PIN);
    enc.setFilter(1023);
    enc.clearCount();

    pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
    btnRawIsr = (digitalRead(ENCODER_SW_PIN) != 0);
    attachInterrupt(digitalPinToInterrupt(ENCODER_SW_PIN), onButtonChange, CHANGE);

    Serial.println("[ENC] ESP32Encoder (PCNT) + botao via ISR");
}

EncoderInput encoderRead() {
    EncoderInput in = {0, false, false};

    // --- Rotação (PCNT hardware) ---
    int raw = (int)enc.getCount();
    enc.clearCount();
    accumSteps += raw;
    in.delta = accumSteps / ENCODER_STEPS_PER_DETENT;
    accumSteps %= ENCODER_STEPS_PER_DETENT;

    // --- Botão (state machine pura) ---
    // Lê estado capturado pela ISR. Em ESP32, leitura de um word volatile é atômica.
    bool rawHigh = btnRawIsr;
    EncoderButtonEvent ev = encoderButtonTick(btnState, millis(), rawHigh,
                                              ENCODER_DEBOUNCE_MS, LONG_PRESS_MS);
    in.pressed   = ev.pressed;
    in.longPress = ev.longPress;

    return in;
}
