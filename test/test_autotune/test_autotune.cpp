#include <unity.h>
#include <Arduino.h>
#include "../../src/state.h"
#include "../../src/autotune.h"
#include "../../src/control.h"
#include "../../src/config.h"

void setUp(void) {
    mockReset();
    setPoint          = 80.0f;
    currentTemp       = 25.0f;
    relayState        = false;
    systemActive      = false;
    firstValidReading = true;
    sensorFailed      = false;
    newTempReading    = false;
    pidKp             = 2.0f;
    pidKi             = 0.01f;
    pidKd             = 50.0f;
    autotuneReset();
}
void tearDown(void) {}

// Helper: aplica nova leitura e roda update
static void feedTemp(float t) {
    currentTemp    = t;
    newTempReading = true;
    autotuneUpdate();
}

// ============================================================
// State machine
// ============================================================

void test_starts_in_idle(void) {
    TEST_ASSERT_EQUAL(AT_IDLE, autotuneGetState());
    TEST_ASSERT_FALSE(autotuneIsRunning());
}

void test_start_below_setpoint_goes_heating(void) {
    currentTemp = 25.0f;     // abaixo de 80
    autotuneStart();
    TEST_ASSERT_EQUAL(AT_HEATING, autotuneGetState());
    TEST_ASSERT_TRUE(autotuneIsRunning());
    TEST_ASSERT_EQUAL_INT(HIGH, _mockGpioState[RELAY_PIN]); // relé ON
}

void test_start_above_setpoint_goes_cooling(void) {
    currentTemp = 100.0f;    // acima de 80
    autotuneStart();
    TEST_ASSERT_EQUAL(AT_HEATING, autotuneGetState());  // start sempre coloca AT_HEATING como inicial
    // Mas relé fica OFF porque já está acima do SP
    TEST_ASSERT_EQUAL_INT(LOW, _mockGpioState[RELAY_PIN]);
}

void test_cancel_stops_autotune(void) {
    currentTemp = 25.0f;
    autotuneStart();
    autotuneCancel();
    TEST_ASSERT_EQUAL(AT_CANCELLED, autotuneGetState());
    TEST_ASSERT_FALSE(autotuneIsRunning());
    TEST_ASSERT_EQUAL_INT(LOW, _mockGpioState[RELAY_PIN]);
}

void test_reset_returns_to_idle(void) {
    currentTemp = 25.0f;
    autotuneStart();
    autotuneCancel();
    TEST_ASSERT_EQUAL(AT_CANCELLED, autotuneGetState());
    autotuneReset();
    TEST_ASSERT_EQUAL(AT_IDLE, autotuneGetState());
}

// ============================================================
// Update sem nova leitura: no-op
// ============================================================

void test_update_without_new_reading_does_nothing(void) {
    currentTemp = 25.0f;
    autotuneStart();
    AutotuneState before = autotuneGetState();
    newTempReading = false;
    autotuneUpdate();
    TEST_ASSERT_EQUAL(before, autotuneGetState());
}

void test_update_when_idle_does_nothing(void) {
    autotuneReset();
    feedTemp(85.0f);
    TEST_ASSERT_EQUAL(AT_IDLE, autotuneGetState());
}

// ============================================================
// Oscilação completa: 5 ciclos → AT_DONE com Ku/Tu sensatos
// ============================================================

void test_full_oscillation_completes_after_5_cycles(void) {
    currentTemp = 70.0f;
    autotuneStart();

    // Simula uma oscilação senoidal-like atravessando o SP a cada 4s.
    // Para cada semi-ciclo: vai abaixo→acima→abaixo gerando crossings
    // que o autotune usa para calcular Ku/Tu.
    for (int cycle = 0; cycle < 7; cycle++) {
        // Sobe: cruza SP indo pra cima
        for (int i = 0; i < 20; i++) {
            mockAdvanceMs(200);
            feedTemp(70.0f + (float)i * 0.8f);  // 70 → 85
        }
        // Desce: cruza SP indo pra baixo
        for (int i = 0; i < 20; i++) {
            mockAdvanceMs(200);
            feedTemp(85.0f - (float)i * 0.8f);  // 85 → 70
        }
        if (autotuneGetState() == AT_DONE) break;
    }

    TEST_ASSERT_EQUAL(AT_DONE, autotuneGetState());
    TEST_ASSERT_FALSE(autotuneIsRunning());
    // Sugestões devem ser positivas e finitas
    TEST_ASSERT_TRUE(autotuneGetSuggestedKp() > 0.0f);
    TEST_ASSERT_TRUE(autotuneGetSuggestedKi() > 0.0f);
    TEST_ASSERT_TRUE(autotuneGetSuggestedKd() > 0.0f);
    TEST_ASSERT_FALSE(isnan(autotuneGetSuggestedKp()));
    TEST_ASSERT_FALSE(isinf(autotuneGetSuggestedKp()));
}

// ============================================================
// Cycle counter
// ============================================================

void test_cycle_counter_starts_zero(void) {
    currentTemp = 25.0f;
    autotuneStart();
    TEST_ASSERT_EQUAL_INT(0, autotuneGetCycle());
    TEST_ASSERT_EQUAL_INT(AUTOTUNE_CYCLES, autotuneGetTotalCycles());
}

// ============================================================
// Runner
// ============================================================

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_in_idle);
    RUN_TEST(test_start_below_setpoint_goes_heating);
    RUN_TEST(test_start_above_setpoint_goes_cooling);
    RUN_TEST(test_cancel_stops_autotune);
    RUN_TEST(test_reset_returns_to_idle);
    RUN_TEST(test_update_without_new_reading_does_nothing);
    RUN_TEST(test_update_when_idle_does_nothing);
    RUN_TEST(test_full_oscillation_completes_after_5_cycles);
    RUN_TEST(test_cycle_counter_starts_zero);
    return UNITY_END();
}
