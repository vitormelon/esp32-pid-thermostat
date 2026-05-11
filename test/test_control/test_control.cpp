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
// Bumpless reparameterization: Ki change preserves output
// (replaces controlReset() on parameter changes)
// ============================================================

// Increasing Ki by 4x must NOT quadruple the output: the integral is
// rescaled to preserve the (Ki * integral) contribution.
void test_reparam_ki_preserves_output_when_increasing_ki(void) {
    controlMode    = MODE_PID_ONOFF;
    pidKp          = 0.0f;     // isolate the integral contribution
    pidKi          = 0.5f;
    pidKd          = 0.0f;
    setPoint       = 80.0f;
    currentTemp    = 78.0f;    // error = 2

    // Build integral over 50 cycles (dt=0.1s) -> integral ~9.8, output ~4.9
    for (int i = 0; i < 50; i++) {
        newTempReading = true;
        controlRun();
        mockAdvanceMs(100);
    }
    float outputBefore = pidOutput;
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 4.9f, outputBefore);  // sanity check

    // Bumpless transfer: 4x Ki + reparam. WITHOUT rescale the next compute
    // would output ~20 (4x). WITH rescale it stays at ~5. Tolerance 0.6
    // covers natural drift (~0.4) from one cycle of continued integration.
    float oldKi = pidKi;
    pidKi = 2.0f;
    controlReparamKi(oldKi);

    newTempReading = true;
    controlRun();    // no mockAdvanceMs: minimum dt, integral barely grows
    TEST_ASSERT_FLOAT_WITHIN(0.6f, outputBefore, pidOutput);
}

// Mirror of the previous test, decreasing Ki (0.25x). Without reparam the
// output would drop to ~1.25; with reparam it stays at ~5.
void test_reparam_ki_preserves_output_when_decreasing_ki(void) {
    controlMode    = MODE_PID_ONOFF;
    pidKp          = 0.0f;
    pidKi          = 2.0f;
    pidKd          = 0.0f;
    setPoint       = 80.0f;
    currentTemp    = 79.5f;    // error = 0.5 (avoid saturation)

    for (int i = 0; i < 50; i++) {
        newTempReading = true;
        controlRun();
        mockAdvanceMs(100);
    }
    float outputBefore = pidOutput;
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 4.9f, outputBefore);

    float oldKi = pidKi;
    pidKi = 0.5f;              // 0.25x
    controlReparamKi(oldKi);

    newTempReading = true;
    controlRun();
    TEST_ASSERT_FLOAT_WITHIN(0.3f, outputBefore, pidOutput);
}

// Edge case: oldKi=0 (the integral never accumulated). Reparam must not
// crash nor produce a spike -- it should just enable integration from zero.
// Passes with stub and with the real impl (documents the contract).
void test_reparam_ki_with_old_ki_zero_starts_fresh(void) {
    controlMode    = MODE_PID_ONOFF;
    pidKp          = 1.0f;
    pidKi          = 0.0f;     // integral stays at 0
    pidKd          = 0.0f;
    setPoint       = 80.0f;
    currentTemp    = 78.0f;    // error = 2

    for (int i = 0; i < 20; i++) {
        newTempReading = true;
        controlRun();
        mockAdvanceMs(100);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 2.0f, pidOutput);  // pure Kp*error

    float oldKi = pidKi;       // 0.0
    pidKi = 1.0f;
    controlReparamKi(oldKi);   // must not crash; integral=0 -> stays 0

    newTempReading = true;
    controlRun();              // integral grows from 0: ~0.2
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 2.2f, pidOutput);
}

// Guardian: changing pidKp directly (no reparam, no reset) must NOT
// disturb the integral. This is the contract that lets display.cpp and
// main.cpp skip controlReset() on Kp changes. If a future commit adds
// controlReset back to a Kp handler, this test stays green there but
// the contract remains documented at the control API level.
void test_kp_change_alone_preserves_integral(void) {
    controlMode    = MODE_PID_ONOFF;
    pidKp          = 0.0f;     // isolate the integral contribution first
    pidKi          = 1.0f;
    pidKd          = 0.0f;
    setPoint       = 80.0f;
    currentTemp    = 79.0f;    // error = 1

    // Build integral so its contribution dominates the output
    for (int i = 0; i < 50; i++) {
        newTempReading = true;
        controlRun();
        mockAdvanceMs(100);
    }
    float integralContrib = pidOutput;  // Ki * integral, Kp=Kd=0
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 4.9f, integralContrib);

    // Mutate pidKp directly, mimicking the production handler path.
    pidKp = 5.0f;

    newTempReading = true;
    controlRun();

    // Expected: NEW Kp*error + preserved Ki*integral = 5*1 + ~4.9 = ~9.9.
    // If a regression resets the integral, the output would collapse to
    // just Kp*error = 5 -- detectable as a 5+ unit drop.
    TEST_ASSERT_FLOAT_WITHIN(0.6f, integralContrib + 5.0f, pidOutput);
}

// Guardian (sister of the Kp test): changing pidKd directly must not
// disturb the integral. With a stable error, the derivative is ~0 so
// Kd contributes ~0 regardless of magnitude, and the output should be
// essentially unchanged. A regression that resets the integral would
// drop the output by the integral contribution -- caught by tolerance.
void test_kd_change_alone_preserves_integral(void) {
    controlMode    = MODE_PID_ONOFF;
    pidKp          = 0.5f;
    pidKi          = 1.0f;
    pidKd          = 5.0f;
    setPoint       = 80.0f;
    currentTemp    = 79.0f;    // error = 1, derivative will settle to ~0

    for (int i = 0; i < 60; i++) {
        newTempReading = true;
        controlRun();
        mockAdvanceMs(100);
    }
    float outputBefore = pidOutput;
    TEST_ASSERT_TRUE(outputBefore > 1.0f);  // sanity: integral built

    // 10x Kd directly, no reset/reparam. With stable error the derivative
    // term is negligible, so the output must barely move. A regression
    // that resets the integral would drop the output by the Ki*integral
    // contribution (multiple units).
    pidKd = 50.0f;

    newTempReading = true;
    controlRun();

    TEST_ASSERT_FLOAT_WITHIN(0.6f, outputBefore, pidOutput);
}

// ============================================================
// Bumpless reparameterization: Window change restarts the phase
// ============================================================

// Changing pidWindowSize mid duty cycle WITHOUT reparam makes the relay
// switch state prematurely because the comparison uses elapsed-since-
// original. WITH reparam the phase restarts and the relay honors the new
// full onTime.
void test_reparam_window_restarts_phase_timer(void) {
    controlMode    = MODE_PID_WINDOW;
    pidWindowSize  = 10000UL;  // 10s window
    pidKp          = 1.0f;
    pidKi          = 0.0f;
    pidKd          = 0.0f;
    setPoint       = 80.0f;
    currentTemp    = 30.0f;    // error=50 -> output=50% (Kp=1)

    newTempReading = true;
    controlRun();
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, pidOutput);
    TEST_ASSERT_FALSE(relayState);

    // Wait offTime (5s) -> relay turns ON
    mockAdvanceMs(5000);
    newTempReading = true;
    controlRun();
    TEST_ASSERT_TRUE(relayState);

    // Advance 4s into ON phase (1s remaining at onTime=5s with window=10s)
    mockAdvanceMs(4000);
    newTempReading = true;
    controlRun();
    TEST_ASSERT_TRUE(relayState);

    // Change window to 20s and reparam:
    //   WITHOUT reparam: elapsed-since-original = 4s + 7s = 11s; new
    //                    onTime = 10s. 11 > 10 -> relay would have switched OFF.
    //   WITH reparam:    elapsed-since-reparam = 7s; onTime = 10s -> stays ON.
    pidWindowSize = 20000UL;
    controlReparamWindow();

    mockAdvanceMs(7000);
    newTempReading = true;
    controlRun();
    TEST_ASSERT_TRUE(relayState);   // requires reparam to pass
}

// Window reparam must not toggle the relay -- it only updates the internal
// phase bookkeeping. Documents the contract; passes with stub and impl.
void test_reparam_window_does_not_toggle_relay_state(void) {
    controlMode    = MODE_PID_WINDOW;
    pidWindowSize  = 10000UL;
    pidKp          = 1.0f;
    pidKi          = 0.0f;
    pidKd          = 0.0f;
    setPoint       = 80.0f;
    currentTemp    = 30.0f;

    newTempReading = true;
    controlRun();
    mockAdvanceMs(5000);
    newTempReading = true;
    controlRun();
    TEST_ASSERT_TRUE(relayState);
    int writesBefore = _mockGpioWriteCount[RELAY_PIN];

    pidWindowSize = 30000UL;
    controlReparamWindow();

    TEST_ASSERT_TRUE(relayState);
    TEST_ASSERT_EQUAL_INT(writesBefore, _mockGpioWriteCount[RELAY_PIN]);
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
    RUN_TEST(test_reparam_ki_preserves_output_when_increasing_ki);
    RUN_TEST(test_reparam_ki_preserves_output_when_decreasing_ki);
    RUN_TEST(test_reparam_ki_with_old_ki_zero_starts_fresh);
    RUN_TEST(test_kp_change_alone_preserves_integral);
    RUN_TEST(test_kd_change_alone_preserves_integral);
    RUN_TEST(test_reparam_window_restarts_phase_timer);
    RUN_TEST(test_reparam_window_does_not_toggle_relay_state);
    return UNITY_END();
}
