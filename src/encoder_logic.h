// State machine pura do botão do encoder, isolada para teste no host.
// Sem hardware: recebe (now_ms, raw_state) e mantém estado interno entre chamadas.
//
// Uso: chamar encoderButtonTick(now, isPressed) a cada poll/ISR.
// Retorna eventos consumidos (click curto / long press) — flags one-shot.
#pragma once

#include <stdint.h>

struct EncoderButtonEvent {
    bool pressed;     // click curto detectado (no release, antes do long-press timeout)
    bool longPress;   // long press detectado (enquanto ainda segurado, dispara uma vez)
};

struct EncoderButtonState {
    bool          lastRaw     = true;   // pull-up: estado idle = HIGH = true
    bool          isDown      = false;
    bool          longFired   = false;
    unsigned long downAtMs    = 0;
    unsigned long lastEdgeMs  = 0;
};

// Processa uma "leitura" do botão. `rawHigh` = true quando NÃO está pressionado
// (pull-up ativo). Retorna evento consumido.
EncoderButtonEvent encoderButtonTick(EncoderButtonState& s,
                                     unsigned long now,
                                     bool rawHigh,
                                     unsigned long debounceMs,
                                     unsigned long longPressMs);
