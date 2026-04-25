#include "autotune.h"
#include "state.h"
#include "config.h"
#include "control.h"

// Melhorias incorporadas da lib jackw01/PIDAutotuner:
// 1. Média de Ku/Tu sobre todos os ciclos (mais estável)
// 2. Reset de min/max a cada zero-crossing (amplitude por semi-ciclo)
// 3. Constantes Tyreus-Luyben (menos overshoot, melhor para sistemas térmicos)

static AutotuneState atState = AT_IDLE;
static int atCompletedCycles = 0;

static float         atKuSum    = 0;
static float         atTuSum    = 0;
static int           atKuCount  = 0;

static float         atTrackMax, atTrackMin;
static bool          atAboveSP;
static bool          atFirstCross;
static unsigned long atLastCrossTime = 0;
static unsigned long atStartTime     = 0;
static unsigned long atTHigh         = 0;
static unsigned long atTLow          = 0;
static unsigned long atT1            = 0;
static unsigned long atT2            = 0;

static unsigned long atCycleDurations[AUTOTUNE_CYCLES + 1];
static int           atCycleDurCount = 0;
static unsigned long atLastCycleDur  = 0;

static float atSugKp = 0, atSugKi = 0, atSugKd = 0;

void autotuneStart() {
    atState          = AT_HEATING;
    atCompletedCycles = 0;
    atKuSum          = 0;
    atTuSum          = 0;
    atKuCount        = 0;
    atTrackMax       = currentTemp;
    atTrackMin       = currentTemp;
    atAboveSP        = (currentTemp > setPoint);
    atFirstCross     = false;
    atSugKp = atSugKi = atSugKd = 0;
    atLastCrossTime  = millis();
    atStartTime      = millis();
    atT1 = atT2      = micros();
    atTHigh = atTLow = 0;
    atCycleDurCount  = 0;
    atLastCycleDur   = 0;

    setRelay(currentTemp < setPoint);
    Serial.printf("[AUTOTUNE] Iniciado. SP=%.1f\n", setPoint);
}

void autotuneUpdate() {
    if (atState != AT_HEATING && atState != AT_COOLING) return;
    if (!newTempReading) return;
    newTempReading = false;

    float t = currentTemp;
    if (t > atTrackMax) atTrackMax = t;
    if (t < atTrackMin) atTrackMin = t;

    bool above = (t > setPoint);
    if (above == atAboveSP) return;

    unsigned long nowUs = micros();
    unsigned long nowMs = millis();

    if (!atFirstCross) {
        atFirstCross    = true;
        atAboveSP       = above;
        atTrackMax      = setPoint;
        atTrackMin      = setPoint;
        atLastCrossTime = nowMs;
        atT1 = atT2     = nowUs;
        setRelay(!above);
        atState = above ? AT_COOLING : AT_HEATING;
        return;
    }

    if (above) {
        // Subiu acima do SP → desliga relê
        atT1    = nowUs;
        atTHigh = atT1 - atT2;
        setRelay(false);
        atState = AT_COOLING;

        // Reset max para próximo semi-ciclo (como a biblioteca faz)
        atTrackMax = setPoint;
    } else {
        // Desceu abaixo do SP → liga relê
        atT2   = nowUs;
        atTLow = atT2 - atT1;
        setRelay(true);
        atState = AT_HEATING;

        // Calcular Ku e Tu deste ciclo (após primeiro semi-ciclo completo)
        if (atTHigh > 0 && atTLow > 0) {
            float amplitude = (atTrackMax - atTrackMin) / 2.0f;
            if (amplitude < 0.1f) amplitude = 0.1f;

            // Ku = 4d / (πa), d = amplitude do output / 2 = 50 (0-100%)
            float ku = 200.0f / (3.14159f * amplitude);
            float tu = (float)(atTHigh + atTLow) / 1000000.0f; // segundos

            if (tu > 0.1f) {
                atKuSum += ku;
                atTuSum += tu;
                atKuCount++;
            }

            atCompletedCycles = atKuCount;

            // Duração deste ciclo
            atLastCycleDur = nowMs - atLastCrossTime;
            if (atCycleDurCount <= AUTOTUNE_CYCLES) {
                atCycleDurations[atCycleDurCount++] = atLastCycleDur;
            }

            Serial.printf("[AUTOTUNE] Ciclo %d/%d Ku=%.2f Tu=%.1fs amp=%.2f\n",
                          atCompletedCycles, AUTOTUNE_CYCLES, ku, tu, amplitude);

            if (atCompletedCycles >= AUTOTUNE_CYCLES) {
                float avgKu = atKuSum / atKuCount;
                float avgTu = atTuSum / atKuCount;

                // Tyreus-Luyben (menos overshoot que Ziegler-Nichols clássico)
                atSugKp = 0.33f * avgKu;
                atSugKi = atSugKp / (0.5f * avgTu);
                atSugKd = 0.33f * atSugKp * avgTu;

                if (isnan(atSugKp) || isinf(atSugKp)) atSugKp = DEFAULT_PID_KP;
                if (isnan(atSugKi) || isinf(atSugKi)) atSugKi = DEFAULT_PID_KI;
                if (isnan(atSugKd) || isinf(atSugKd)) atSugKd = DEFAULT_PID_KD;

                setRelay(false);
                atState = AT_DONE;
                Serial.printf("[AUTOTUNE] Concluido! Kp=%.3f Ki=%.4f Kd=%.1f avgKu=%.2f avgTu=%.1fs\n",
                              atSugKp, atSugKi, atSugKd, avgKu, avgTu);
                return;
            }
        }

        // Reset min para próximo semi-ciclo
        atTrackMin = setPoint;
    }

    atLastCrossTime = nowMs;
    atAboveSP = above;
}

void autotuneCancel() {
    setRelay(false);
    atState = AT_CANCELLED;
    Serial.println("[AUTOTUNE] Cancelado");
}

void autotuneReset() {
    atState = AT_IDLE;
}

bool          autotuneIsRunning()     { return atState == AT_HEATING || atState == AT_COOLING; }
AutotuneState autotuneGetState()      { return atState; }
int           autotuneGetCycle()      { return atCompletedCycles; }
int           autotuneGetTotalCycles(){ return AUTOTUNE_CYCLES; }

unsigned long autotuneGetEtaMs() {
    if (atCycleDurCount < 1) return 0;
    unsigned long avgDur = 0;
    for (int i = 0; i < atCycleDurCount; i++) avgDur += atCycleDurations[i];
    avgDur /= atCycleDurCount;
    int remaining = AUTOTUNE_CYCLES - atCompletedCycles;
    if (remaining <= 0) return 0;
    return avgDur * remaining;
}

unsigned long autotuneGetLastCycleDurationMs() { return atLastCycleDur; }

unsigned long autotuneGetCurrentCycleElapsedMs() {
    if (!autotuneIsRunning()) return 0;
    return millis() - atLastCrossTime;
}

unsigned long autotuneGetTotalElapsedMs() {
    if (atStartTime == 0) return 0;
    return millis() - atStartTime;
}

float autotuneGetSuggestedKp() { return atSugKp; }
float autotuneGetSuggestedKi() { return atSugKi; }
float autotuneGetSuggestedKd() { return atSugKd; }
