#include "encoder_logic.h"

EncoderButtonEvent encoderButtonTick(EncoderButtonState& s,
                                     unsigned long now,
                                     bool rawHigh,
                                     unsigned long debounceMs,
                                     unsigned long longPressMs) {
    EncoderButtonEvent ev = {false, false};

    // Debounce: ignora bordas dentro da janela.
    // Se o raw mudou recentemente, descarta a leitura.
    bool stable = rawHigh;
    if (rawHigh != s.lastRaw) {
        if (now - s.lastEdgeMs < debounceMs) {
            stable = s.lastRaw;          // descarta — bouncing
        } else {
            s.lastEdgeMs = now;          // borda estável — registra
        }
    }

    // Press: HIGH → LOW estável
    if (!stable && s.lastRaw) {
        s.isDown    = true;
        s.downAtMs  = now;
        s.longFired = false;
    }

    // Long press: dispara uma vez quando segurado por longPressMs
    if (!stable && s.isDown && !s.longFired
        && (now - s.downAtMs >= longPressMs)) {
        ev.longPress = true;
        s.longFired  = true;
    }

    // Release: LOW → HIGH estável
    if (stable && !s.lastRaw && s.isDown) {
        s.isDown = false;
        if (!s.longFired) {
            ev.pressed = true;
        }
    }

    s.lastRaw = stable;
    return ev;
}
