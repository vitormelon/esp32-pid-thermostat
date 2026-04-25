#include <unity.h>
#include "Arduino.h"
#include "../../src/state.h"
#include "../../src/control.h"
#include "../../src/config.h"

// ============================================================
// Helpers
// ============================================================

static void resetGlobals() {
    mockReset();
    setPoint          = 80.0f;
    offset            = 2.0f;
    controlMode       = MODE_HYSTERESIS;
    relayState        = false;
    systemActive      = true;
    firstValidReading = true;
    sensorFailed      = false;
    hardCutoffActive  = false;
    currentTemp       = 25.0f;
    newTempReading    = false;

    pidKp         = 2.0f;
    pidKi         = 0.0f;
    pidKd         = 0.0f;
    pidWindowSize = 10000UL;
    pidThreshold  = 50.0f;
    pidOutput     = 0.0f;

    controlInit();
}

void setUp(void)    { resetGlobals(); }
void tearDown(void) {}

// ============================================================
// setRelay
// ============================================================

void test_setRelay_writes_gpio_and_updates_state(void) {
    setRelay(true);
    TEST_ASSERT_TRUE(relayState);
    TEST_ASSERT_EQUAL_INT(HIGH, _mockGpioState[RELAY_PIN]);

    setRelay(false);
    TEST_ASSERT_FALSE(relayState);
    TEST_ASSERT_EQUAL_INT(LOW, _mockGpioState[RELAY_PIN]);
}

// ============================================================
// Hysteresis
// ============================================================

void test_hysteresis_above_setpoint_turns_relay_off(void) {
    controlMode = MODE_HYSTERESIS;
    relayState  = true;
    _mockGpioState[RELAY_PIN] = HIGH;
    currentTemp = 85.0f;  // SP=80
    controlRun();
    TEST_ASSERT_FALSE(relayState);
}

void test_hysteresis_below_setpoint_minus_offset_turns_relay_on(void) {
    controlMode = MODE_HYSTERESIS;
    relayState  = false;
    currentTemp = 77.0f;  // SP=80, offset=2 → liga abaixo de 78
    controlRun();
    TEST_ASSERT_TRUE(relayState);
    TEST_ASSERT_EQUAL_INT(HIGH, _mockGpioState[RELAY_PIN]);
}

void test_hysteresis_within_deadband_holds_state(void) {
    controlMode = MODE_HYSTERESIS;
    relayState  = false;
    currentTemp = 79.0f;  // dentro da banda morta (78-80)
    controlRun();
    TEST_ASSERT_FALSE(relayState);  // mantém OFF

    relayState  = true;
    currentTemp = 79.5f;
    controlRun();
    TEST_ASSERT_TRUE(relayState);   // mantém ON
}

// ============================================================
// PID Window (time-proportional)
// ============================================================

void test_pid_window_50pct_toggles_at_half_window(void) {
    controlMode    = MODE_PID_WINDOW;
    pidWindowSize  = 10000UL;
    pidKp          = 1.0f;
    pidKi          = 0.0f;
    pidKd          = 0.0f;
    setPoint       = 80.0f;
    currentTemp    = 30.0f;
    newTempReading = true;
    controlRun();
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, pidOutput);
    // Janela começa com relé OFF; precisa esperar offTime (5s) antes de ligar
    TEST_ASSERT_FALSE(relayState);

    // Avança 5s → completa offTime, deve ligar
    mockAdvanceMs(5000);
    newTempReading = true;
    controlRun();
    TEST_ASSERT_TRUE(relayState);

    // Avança +4s (total 9s) — ainda dentro dos 5s de ON
    mockAdvanceMs(4000);
    newTempReading = true;
    controlRun();
    TEST_ASSERT_TRUE(relayState);

    // Avança +2s (total 11s, 6s desde liga) — passou onTime, deve desligar
    mockAdvanceMs(2000);
    newTempReading = true;
    controlRun();
    TEST_ASSERT_FALSE(relayState);
}

void test_pid_window_zero_output_keeps_relay_off(void) {
    controlMode    = MODE_PID_WINDOW;
    pidKp          = 1.0f;
    pidKi          = 0.0f;
    pidKd          = 0.0f;
    setPoint       = 80.0f;
    currentTemp    = 100.0f;    // error negativo → output saturado em 0
    newTempReading = true;
    controlRun();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pidOutput);
    TEST_ASSERT_FALSE(relayState);
}

// ============================================================
// Sistema inativo / sensor falho
// ============================================================

void test_control_run_does_nothing_when_system_inactive(void) {
    systemActive = false;
    relayState   = true;
    _mockGpioState[RELAY_PIN] = HIGH;
    currentTemp  = 200.0f;  // muito acima do SP
    controlRun();
    // Não deve mexer no relé porque sistema inativo
    TEST_ASSERT_TRUE(relayState);
}

void test_control_run_does_nothing_when_sensor_failed(void) {
    sensorFailed = true;
    relayState   = false;
    currentTemp  = 30.0f;   // abaixo do SP, normalmente ligaria
    controlRun();
    TEST_ASSERT_FALSE(relayState);  // sensor falho → não controla
}

// ============================================================
// PID anti-windup com Ki=0 (regressão do bug #5)
// ============================================================

void test_pid_with_ki_zero_does_not_grow_output_unboundedly(void) {
    // Ki=0 deve manter integral=0; output deve ser apenas Kp*error
    controlMode    = MODE_PID_ONOFF;
    pidKp          = 2.0f;
    pidKi          = 0.0f;
    pidKd          = 0.0f;
    setPoint       = 80.0f;
    currentTemp    = 30.0f;     // error = 50
    pidThreshold   = 50.0f;

    // Roda 100 ciclos de PID com error constante; sem antiwindup correto,
    // integral cresceria mesmo com Ki=0 e poderia bagunçar mudança posterior.
    for (int i = 0; i < 100; i++) {
        newTempReading = true;
        controlRun();
        mockAdvanceMs(100);
    }
    // Output esperado: Kp * error = 2 * 50 = 100, saturado em 100.
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, pidOutput);

    // Inverte temp acima do SP → error negativo, output deve cair imediatamente
    currentTemp    = 100.0f;    // error = -20
    newTempReading = true;
    mockAdvanceMs(100);
    controlRun();
    // Sem windup, output saturado em 0 (Kp*error = -40)
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, pidOutput);
}

// ============================================================
// Moving average
// ============================================================

void test_moving_average_returns_currentTemp_when_empty(void) {
    currentTemp     = 42.5f;
    movingAvgIndex  = 0;
    movingAvgCount  = 0;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 42.5f, getMovingAverage());
}

void test_moving_average_computes_correctly(void) {
    movingAvgIndex = 0;
    movingAvgCount = 0;
    addToMovingAverage(10.0f);
    addToMovingAverage(20.0f);
    addToMovingAverage(30.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, getMovingAverage());
}

// ============================================================
// Runner
// ============================================================

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_setRelay_writes_gpio_and_updates_state);
    RUN_TEST(test_hysteresis_above_setpoint_turns_relay_off);
    RUN_TEST(test_hysteresis_below_setpoint_minus_offset_turns_relay_on);
    RUN_TEST(test_hysteresis_within_deadband_holds_state);
    RUN_TEST(test_pid_window_50pct_toggles_at_half_window);
    RUN_TEST(test_pid_window_zero_output_keeps_relay_off);
    RUN_TEST(test_control_run_does_nothing_when_system_inactive);
    RUN_TEST(test_control_run_does_nothing_when_sensor_failed);
    RUN_TEST(test_pid_with_ki_zero_does_not_grow_output_unboundedly);
    RUN_TEST(test_moving_average_returns_currentTemp_when_empty);
    RUN_TEST(test_moving_average_computes_correctly);
    return UNITY_END();
}
