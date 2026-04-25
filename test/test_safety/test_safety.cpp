#include <unity.h>
#include <Arduino.h>
#include "../../src/state.h"
#include "../../src/safety.h"
#include "../../src/control.h"
#include "../../src/config.h"

// Capturador de callback
static int                 cbCallCount = 0;
static SafetyError         cbLastErr   = SAFETY_OK;
static float               cbLastTemp  = 0.0f;

static void testCallback(SafetyError err, float temp) {
    cbCallCount++;
    cbLastErr  = err;
    cbLastTemp = temp;
}

void setUp(void) {
    mockReset();
    safetyError       = SAFETY_OK;
    safetyTriggerTemp = 0.0f;
    sensorFailed      = false;
    firstValidReading = true;
    hardCutoffActive  = false;
    currentTemp       = 25.0f;
    relayState        = false;
    systemActive      = true;

    cbCallCount = 0;
    cbLastErr   = SAFETY_OK;
    cbLastTemp  = 0.0f;

    safetyInit();
    safetySetTriggerCallback(testCallback);
}
void tearDown(void) {}

// ============================================================
// Pré-condições
// ============================================================

void test_no_op_when_no_first_valid_reading(void) {
    firstValidReading = false;
    currentTemp       = 200.0f;
    safetyCheck();
    TEST_ASSERT_EQUAL(SAFETY_OK, safetyError);
    TEST_ASSERT_EQUAL_INT(0, cbCallCount);
}

void test_no_op_when_already_in_error(void) {
    safetyError       = SAFETY_OVERTEMP;
    currentTemp       = 25.0f;
    safetyCheck();
    TEST_ASSERT_EQUAL_INT(0, cbCallCount);
}

// ============================================================
// Sensor failed
// ============================================================

void test_sensor_failed_triggers_immediately(void) {
    sensorFailed = true;
    safetyCheck();
    TEST_ASSERT_EQUAL(SAFETY_SENSOR_FAIL, safetyError);
    TEST_ASSERT_EQUAL_INT(1, cbCallCount);
    TEST_ASSERT_EQUAL(SAFETY_SENSOR_FAIL, cbLastErr);
    TEST_ASSERT_FALSE(systemActive);
}

// ============================================================
// Cutoff (HARD_CUTOFF_TEMP)
// ============================================================

void test_below_cutoff_does_nothing(void) {
    currentTemp = HARD_CUTOFF_TEMP - 5.0f;
    safetyCheck();
    TEST_ASSERT_EQUAL(SAFETY_OK, safetyError);
    TEST_ASSERT_FALSE(hardCutoffActive);
}

void test_at_cutoff_marks_hardCutoff_and_relay_off(void) {
    relayState  = true;
    _mockGpioState[RELAY_PIN] = HIGH;
    currentTemp = HARD_CUTOFF_TEMP;
    safetyCheck();
    TEST_ASSERT_TRUE(hardCutoffActive);
    TEST_ASSERT_FALSE(relayState);
    TEST_ASSERT_EQUAL_INT(LOW, _mockGpioState[RELAY_PIN]);
    TEST_ASSERT_EQUAL(SAFETY_OK, safetyError);  // ainda não disparou erro
}

void test_temp_drops_below_recovery_clears_cutoff(void) {
    currentTemp = HARD_CUTOFF_TEMP + 1.0f;
    safetyCheck();
    TEST_ASSERT_TRUE(hardCutoffActive);
    currentTemp = CUTOFF_RECOVERY_TEMP - 1.0f;
    safetyCheck();
    TEST_ASSERT_FALSE(hardCutoffActive);
    TEST_ASSERT_EQUAL(SAFETY_OK, safetyError);
}

// ============================================================
// Stuck relay (BUG #2 — corrigido: stuck em SAFETY_STUCK_DELAY_MS)
// ============================================================

void test_stuck_relay_detected_at_30s_with_temp_rising(void) {
    currentTemp = HARD_CUTOFF_TEMP + 1.0f;
    safetyCheck();                        // marca hardCutoff em t=0

    // Avança 30s e simula temperatura subindo +10°C (relé travado mecanicamente)
    mockAdvanceMs(SAFETY_STUCK_DELAY_MS);
    currentTemp = (HARD_CUTOFF_TEMP + 1.0f) + SAFETY_STUCK_THRESHOLD + 1.0f;
    safetyCheck();

    TEST_ASSERT_EQUAL(SAFETY_RELAY_STUCK, safetyError);
    TEST_ASSERT_EQUAL_INT(1, cbCallCount);
}

void test_no_stuck_if_temp_did_not_rise(void) {
    currentTemp = HARD_CUTOFF_TEMP + 1.0f;
    safetyCheck();
    mockAdvanceMs(SAFETY_STUCK_DELAY_MS);
    // Mesma temperatura — relé funcionou
    safetyCheck();
    TEST_ASSERT_NOT_EQUAL(SAFETY_RELAY_STUCK, safetyError);
}

// ============================================================
// Overtemp dispara após OVERTEMP_DELAY_MS (BUG #2 corrigido)
// ============================================================

void test_overtemp_does_not_trigger_before_60s(void) {
    currentTemp = HARD_CUTOFF_TEMP + 1.0f;
    safetyCheck();                        // t=0 marca cutoff
    mockAdvanceMs(OVERTEMP_DELAY_MS - 1000);  // 59s
    safetyCheck();
    TEST_ASSERT_NOT_EQUAL(SAFETY_OVERTEMP, safetyError);
}

void test_overtemp_triggers_at_60s_if_temp_stayed_high(void) {
    currentTemp = HARD_CUTOFF_TEMP + 1.0f;
    safetyCheck();                        // t=0
    mockAdvanceMs(OVERTEMP_DELAY_MS);     // exatamente 60s
    safetyCheck();
    TEST_ASSERT_EQUAL(SAFETY_OVERTEMP, safetyError);
    TEST_ASSERT_EQUAL_INT(1, cbCallCount);
}

void test_overtemp_does_not_trigger_if_temp_recovers_in_time(void) {
    currentTemp = HARD_CUTOFF_TEMP + 1.0f;
    safetyCheck();
    mockAdvanceMs(30000);                 // 30s — passou stuck check (sem subir)
    safetyCheck();                        // marca stuckChecked=true mas sem disparar
    currentTemp = CUTOFF_RECOVERY_TEMP - 1.0f;
    safetyCheck();                        // libera cutoff antes do overtemp
    TEST_ASSERT_EQUAL(SAFETY_OK, safetyError);
    TEST_ASSERT_FALSE(hardCutoffActive);
}

// ============================================================
// Stuck check roda ANTES do overtemp (regressão do bug original)
// ============================================================

void test_stuck_check_runs_before_overtemp(void) {
    currentTemp = HARD_CUTOFF_TEMP + 1.0f;
    safetyCheck();                              // t=0
    mockAdvanceMs(SAFETY_STUCK_DELAY_MS);       // 30s
    currentTemp = (HARD_CUTOFF_TEMP + 1.0f) + SAFETY_STUCK_THRESHOLD + 1.0f;
    safetyCheck();
    // Deve ser STUCK (30s), não OVERTEMP (60s ainda não chegou)
    TEST_ASSERT_EQUAL(SAFETY_RELAY_STUCK, safetyError);
}

// ============================================================
// Bug G: RELAY_STUCK não pode ser limpo por click simples (evita
// loop safety→clear→safety se o relé continuar travado fisicamente).
// safetyAllowsClickClear() informa ao display qual gesto é necessário.
// ============================================================

void test_allowsClickClear_true_for_sensor_fail(void) {
    safetyError = SAFETY_SENSOR_FAIL;
    TEST_ASSERT_TRUE(safetyAllowsClickClear());
}

void test_allowsClickClear_true_for_overtemp(void) {
    safetyError = SAFETY_OVERTEMP;
    TEST_ASSERT_TRUE(safetyAllowsClickClear());
}

void test_allowsClickClear_false_for_relay_stuck(void) {
    safetyError = SAFETY_RELAY_STUCK;
    TEST_ASSERT_FALSE(safetyAllowsClickClear());
}

// ============================================================
// safetyClear
// ============================================================

void test_clear_resets_all_state(void) {
    sensorFailed = true;
    safetyCheck();
    TEST_ASSERT_EQUAL(SAFETY_SENSOR_FAIL, safetyError);

    safetyClear();
    TEST_ASSERT_EQUAL(SAFETY_OK, safetyError);
    TEST_ASSERT_FALSE(hardCutoffActive);
}

// ============================================================
// Runner
// ============================================================

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_no_op_when_no_first_valid_reading);
    RUN_TEST(test_no_op_when_already_in_error);
    RUN_TEST(test_sensor_failed_triggers_immediately);
    RUN_TEST(test_below_cutoff_does_nothing);
    RUN_TEST(test_at_cutoff_marks_hardCutoff_and_relay_off);
    RUN_TEST(test_temp_drops_below_recovery_clears_cutoff);
    RUN_TEST(test_stuck_relay_detected_at_30s_with_temp_rising);
    RUN_TEST(test_no_stuck_if_temp_did_not_rise);
    RUN_TEST(test_overtemp_does_not_trigger_before_60s);
    RUN_TEST(test_overtemp_triggers_at_60s_if_temp_stayed_high);
    RUN_TEST(test_overtemp_does_not_trigger_if_temp_recovers_in_time);
    RUN_TEST(test_stuck_check_runs_before_overtemp);
    RUN_TEST(test_allowsClickClear_true_for_sensor_fail);
    RUN_TEST(test_allowsClickClear_true_for_overtemp);
    RUN_TEST(test_allowsClickClear_false_for_relay_stuck);
    RUN_TEST(test_clear_resets_all_state);
    return UNITY_END();
}
