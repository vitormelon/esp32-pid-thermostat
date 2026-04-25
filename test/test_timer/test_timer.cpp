#include <unity.h>
#include <Arduino.h>
#include "../../src/state.h"
#include "../../src/timer_ctrl.h"
#include "../../src/config.h"

void setUp(void) {
    mockReset();
    timerSetMinutes  = 0;
    timerRemainingMs = 0;
    timerRunning     = false;
    timerInit();
}
void tearDown(void) {}

// ============================================================
// Init / Start / Stop
// ============================================================

void test_init_resets_state(void) {
    timerRunning     = true;
    timerRemainingMs = 12345;
    timerInit();
    TEST_ASSERT_FALSE(timerRunning);
    TEST_ASSERT_EQUAL_UINT32(0, timerRemainingMs);
    TEST_ASSERT_FALSE(timerIsExpired());
}

void test_start_with_zero_minutes_does_nothing(void) {
    timerSetMinutes = 0;
    timerStart();
    TEST_ASSERT_FALSE(timerRunning);
    TEST_ASSERT_EQUAL_UINT32(0, timerRemainingMs);
}

void test_start_with_minutes_initializes_remaining(void) {
    timerSetMinutes = 5;
    timerStart();
    TEST_ASSERT_TRUE(timerRunning);
    TEST_ASSERT_EQUAL_UINT32(5UL * 60000UL, timerRemainingMs);
}

void test_stop_clears_running(void) {
    timerSetMinutes = 5;
    timerStart();
    timerStop();
    TEST_ASSERT_FALSE(timerRunning);
}

// ============================================================
// Update / countdown / expiration
// ============================================================

void test_update_decrements_remaining(void) {
    timerSetMinutes = 1;
    timerStart();           // 60000 ms
    mockAdvanceMs(10000);
    timerUpdate();
    TEST_ASSERT_TRUE(timerRunning);
    // Tolerância: primeiro update inicializa lastTick e ainda não decrementa
    // (por design — evita pular tempo entre setUp e start). Vou avançar de novo.
    mockAdvanceMs(10000);
    timerUpdate();
    TEST_ASSERT_UINT32_WITHIN(100UL, 50000UL, timerRemainingMs);
}

void test_update_expires_when_remaining_runs_out(void) {
    timerSetMinutes = 1;
    timerStart();           // 60000 ms
    mockAdvanceMs(1);
    timerUpdate();          // primeiro update inicializa lastTick
    mockAdvanceMs(60000);
    timerUpdate();          // deve expirar
    TEST_ASSERT_FALSE(timerRunning);
    TEST_ASSERT_EQUAL_UINT32(0, timerRemainingMs);
    TEST_ASSERT_TRUE(timerIsExpired());
}

void test_isExpired_consumes_flag(void) {
    timerSetMinutes = 1;
    timerStart();
    mockAdvanceMs(1);
    timerUpdate();
    mockAdvanceMs(60001);
    timerUpdate();
    TEST_ASSERT_TRUE(timerIsExpired());
    TEST_ASSERT_FALSE(timerIsExpired());  // segunda chamada já consumiu
}

void test_update_no_op_when_not_running(void) {
    timerRunning     = false;
    timerRemainingMs = 30000;
    mockAdvanceMs(10000);
    timerUpdate();
    TEST_ASSERT_EQUAL_UINT32(30000, timerRemainingMs);
}

void test_update_no_op_when_set_zero(void) {
    timerSetMinutes  = 0;
    timerRunning     = true;     // estado inválido proposital
    timerRemainingMs = 30000;
    mockAdvanceMs(10000);
    timerUpdate();
    TEST_ASSERT_EQUAL_UINT32(30000, timerRemainingMs);
}

// ============================================================
// Recovery
// ============================================================

void test_resumeFromRecovery_applies_remaining(void) {
    timerSetMinutes = 10;       // qualquer valor não-zero
    timerResumeFromRecovery(45000UL);
    TEST_ASSERT_TRUE(timerRunning);
    TEST_ASSERT_EQUAL_UINT32(45000UL, timerRemainingMs);
}

void test_resumeFromRecovery_ignored_if_set_zero(void) {
    timerSetMinutes = 0;
    timerResumeFromRecovery(45000UL);
    TEST_ASSERT_FALSE(timerRunning);
}

// ============================================================
// Runner
// ============================================================

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_init_resets_state);
    RUN_TEST(test_start_with_zero_minutes_does_nothing);
    RUN_TEST(test_start_with_minutes_initializes_remaining);
    RUN_TEST(test_stop_clears_running);
    RUN_TEST(test_update_decrements_remaining);
    RUN_TEST(test_update_expires_when_remaining_runs_out);
    RUN_TEST(test_isExpired_consumes_flag);
    RUN_TEST(test_update_no_op_when_not_running);
    RUN_TEST(test_update_no_op_when_set_zero);
    RUN_TEST(test_resumeFromRecovery_applies_remaining);
    RUN_TEST(test_resumeFromRecovery_ignored_if_set_zero);
    return UNITY_END();
}
