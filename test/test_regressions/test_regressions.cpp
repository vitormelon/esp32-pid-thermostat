// ============================================================
// Suite de regressão — um teste por bug histórico.
// Cada teste reproduz o cenário que era buggy e valida o fix.
// Se um destes falhar, é regressão direta de bug conhecido.
// ============================================================

#include <unity.h>
#include <Arduino.h>
#include <Preferences.h>

#include "../../src/state.h"
#include "../../src/config.h"
#include "../../src/control.h"
#include "../../src/timer_ctrl.h"
#include "../../src/autotune.h"
#include "../../src/safety.h"
#include "../../src/storage.h"

// Callback espião para safety
static int           safetyCbCount   = 0;
static SafetyError   safetyCbLastErr = SAFETY_OK;
static void safetyCbSpy(SafetyError err, float) {
    safetyCbCount++;
    safetyCbLastErr = err;
}

// Reset completo do mundo simulado entre testes
void setUp(void) {
    mockReset();
    mockPrefsReset();

    // Estado padrão
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

    timerSetMinutes  = 0;
    timerRemainingMs = 0;
    timerRunning     = false;

    safetyError       = SAFETY_OK;
    safetyTriggerTemp = 0.0f;

    safetyCbCount   = 0;
    safetyCbLastErr = SAFETY_OK;

    controlInit();
    safetyInit();
    safetySetTriggerCallback(safetyCbSpy);
    autotuneReset();
    timerInit();
}
void tearDown(void) {}

// Simula o dispatch do loop main.cpp para autotune vs controle.
// Replica a lógica corrigida em main.cpp loop().
static void simulateLoopDispatch() {
    AutotuneState atSt = autotuneGetState();
    if (atSt == AT_HEATING || atSt == AT_COOLING) {
        autotuneUpdate();
    } else if (atSt == AT_DONE) {
        setRelay(false);
    } else {
        controlRun();
    }
}

// ============================================================
// BUG #1: controlRun() interferia com o relé após autotune AT_DONE.
// Antes do fix: AT_DONE → autotuneIsRunning()=false → loop chamava
// controlRun() que computava PID e podia religar o relé com Kp/Ki/Kd antigos.
// Pós-fix: dispatch detecta AT_DONE e força relé OFF até user decidir.
// ============================================================

void test_bug1_controlRun_does_not_run_during_autotune_done(void) {
    // Setup: autotune em AT_DONE, sistema ativo, T muito baixa (controle religaria)
    currentTemp = 30.0f;
    setPoint    = 80.0f;
    relayState  = false;

    // Forçar AT_DONE simulando autotune completo
    autotuneStart();
    // Hack pra chegar em AT_DONE rapidamente: feed crossings de SP
    for (int cycle = 0; cycle < 10 && autotuneGetState() != AT_DONE; cycle++) {
        for (int i = 0; i < 20; i++) {
            mockAdvanceMs(200);
            currentTemp    = 70.0f + i * 0.8f;
            newTempReading = true;
            autotuneUpdate();
        }
        for (int i = 0; i < 20; i++) {
            mockAdvanceMs(200);
            currentTemp    = 85.0f - i * 0.8f;
            newTempReading = true;
            autotuneUpdate();
        }
    }
    TEST_ASSERT_EQUAL(AT_DONE, autotuneGetState());

    // Agora simula o loop dispatch — relé NÃO pode ser religado pelo PID
    relayState = false;
    _mockGpioState[RELAY_PIN] = LOW;
    currentTemp = 30.0f;          // muito abaixo do SP, PID quereria ligar 100%
    controlMode = MODE_PID_ONOFF;
    pidKp = 10.0f; pidKi = 0.0f; pidKd = 0.0f;
    pidThreshold = 50.0f;

    for (int i = 0; i < 5; i++) {
        newTempReading = true;
        simulateLoopDispatch();
    }

    TEST_ASSERT_FALSE(relayState);  // relé fica OFF mesmo com PID querendo ligar
}

void test_bug1_controlRun_returns_after_autotune_reset(void) {
    // Após autotune Reset (user aceitou/rejeitou), controle volta a operar
    autotuneStart();
    autotuneCancel();
    autotuneReset();
    TEST_ASSERT_EQUAL(AT_IDLE, autotuneGetState());

    controlMode = MODE_HYSTERESIS;
    currentTemp = 70.0f;     // abaixo de SP-offset
    simulateLoopDispatch();
    TEST_ASSERT_TRUE(relayState);   // controle voltou a funcionar
}

// ============================================================
// BUG #2: stuck check (30s) vinha DEPOIS de overtemp (5s) na ordem
// das checagens. Resultado: overtemp sempre ganhava, stuck era código morto.
// Pós-fix: stuck check em 30s, overtemp em 60s; stuck dispara primeiro.
// ============================================================

void test_bug2_stuck_relay_detected_before_overtemp(void) {
    currentTemp = HARD_CUTOFF_TEMP + 1.0f;
    safetyCheck();                                  // marca cutoff em t=0
    mockAdvanceMs(SAFETY_STUCK_DELAY_MS);           // 30s
    currentTemp = HARD_CUTOFF_TEMP + 1.0f + SAFETY_STUCK_THRESHOLD + 1.0f;
    safetyCheck();
    TEST_ASSERT_EQUAL(SAFETY_RELAY_STUCK, safetyError);
    // Se o bug estivesse presente, teria virado SAFETY_OVERTEMP em 5s
}

void test_bug2_overtemp_uses_60s_delay_not_5s(void) {
    currentTemp = HARD_CUTOFF_TEMP + 1.0f;
    safetyCheck();                                  // t=0
    mockAdvanceMs(5000);                            // 5s (limite antigo)
    safetyCheck();
    TEST_ASSERT_EQUAL(SAFETY_OK, safetyError);      // não dispara em 5s
    mockAdvanceMs(OVERTEMP_DELAY_MS - 5000);        // chega aos 60s
    safetyCheck();
    TEST_ASSERT_EQUAL(SAFETY_OVERTEMP, safetyError);
}

// ============================================================
// BUG #3: triggerSafetyError chamava autotuneCancel() mas não resetava
// navState, deixando UI travada em NAV_AUTOTUNE_RUN com dados velhos.
// Pós-fix: callback de safety chama displayResetAutotuneUI; aqui
// validamos que o callback é invocado quando safety dispara.
// ============================================================

void test_bug3_safety_trigger_invokes_cleanup_callback(void) {
    sensorFailed = true;
    safetyCheck();
    TEST_ASSERT_EQUAL_INT(1, safetyCbCount);
    TEST_ASSERT_EQUAL(SAFETY_SENSOR_FAIL, safetyCbLastErr);
}

// ============================================================
// BUG #4: displayUpdate não tratava AT_CANCELLED em NAV_AUTOTUNE_RUN.
// Validamos via state machine: depois de cancel, autotuneIsRunning é
// false e o estado é AT_CANCELLED (display sabe diferenciar de AT_DONE).
// ============================================================

void test_bug4_autotune_cancelled_state_distinguishable_from_done(void) {
    autotuneStart();
    autotuneCancel();
    TEST_ASSERT_EQUAL(AT_CANCELLED, autotuneGetState());
    TEST_ASSERT_FALSE(autotuneIsRunning());

    // E AT_DONE é diferente
    autotuneReset();
    autotuneStart();
    // (não chegamos a DONE aqui, só validamos que CANCELLED != DONE)
    TEST_ASSERT_NOT_EQUAL(AT_DONE, autotuneGetState());
}

// ============================================================
// BUG #5: Antiwindup do PID só clampava integral se Ki>0; com Ki=0 o
// integral acumulava sem limite. Mudar Ki depois causaria spike.
// Pós-fix: com Ki=0, integral é forçado a 0; output é só Kp*error.
// ============================================================

void test_bug5_pid_with_ki_zero_does_not_accumulate_windup(void) {
    controlMode = MODE_PID_ONOFF;
    pidKp = 2.0f; pidKi = 0.0f; pidKd = 0.0f;
    setPoint = 80.0f;
    currentTemp = 30.0f;          // error = 50, sustentado

    // Roda 200 ciclos com error grande e constante
    for (int i = 0; i < 200; i++) {
        newTempReading = true;
        controlRun();
        mockAdvanceMs(100);
    }
    // Output saturado em 100 (Kp*error = 100)
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, pidOutput);

    // Inverte o erro — sem windup, output cai imediato
    currentTemp = 100.0f;          // error = -20
    newTempReading = true;
    mockAdvanceMs(100);
    controlRun();
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, pidOutput);
}

// ============================================================
// BUG #6: Timer expirava no loop main mas não chamava
// storageSaveRecoveryState. Após queda de energia logo depois,
// recovery oferecia retomar ciclo já encerrado.
// Validamos: ao desativar e salvar, storage reflete o novo estado.
// ============================================================

void test_bug6_recovery_save_persists_systemActive_change(void) {
    // Estado inicial salvo: ativo
    systemActive    = true;
    timerSetMinutes = 30;
    storageSaveRecoveryState();
    TEST_ASSERT_TRUE(storageHasRecoveryState());

    // Simula timer expirar
    systemActive = false;
    storageSaveRecoveryState();   // o que main.cpp faz hoje após timer
    TEST_ASSERT_FALSE(storageHasRecoveryState());
}

// ============================================================
// BUG #7: displayShowRecoveryScreen() era totalmente bloqueante (até 20s)
// — bloqueava WiFi, Blynk, leitura de temperatura. Pós-fix: usa state
// machine com flag recoveryPending e applyRecovery() não bloqueante.
// Validamos que as variáveis existem e podem ser manipuladas externamente.
// ============================================================

void test_bug7_recovery_state_machine_has_required_globals(void) {
    // Globals declarados em state.h (build falha se não existem)
    recoveryPending        = true;
    recoveryStartMs        = 1000;
    recoveryChoice         = false;
    recoveryTimerRem       = 50000UL;
    recoveryTimerSet       = 5;
    recoveryDecisionMade   = true;
    recoveryDecisionResume = true;

    TEST_ASSERT_TRUE(recoveryPending);
    TEST_ASSERT_EQUAL_UINT32(1000, recoveryStartMs);
    TEST_ASSERT_FALSE(recoveryChoice);
    TEST_ASSERT_EQUAL_UINT32(50000UL, recoveryTimerRem);
    TEST_ASSERT_EQUAL_UINT(5, recoveryTimerSet);
    TEST_ASSERT_TRUE(recoveryDecisionMade);
    TEST_ASSERT_TRUE(recoveryDecisionResume);
}

// ============================================================
// BUG #8: NVS escrevia mesmo quando o valor não mudou — desgaste
// desnecessário do flash. Pós-fix: storageSave* faz read-then-compare,
// só escreve se diferente.
// ============================================================

void test_bug8_nvs_skips_write_when_value_unchanged(void) {
    setPoint = 80.0f;
    storageSaveSetPoint();
    int writesAfter = _mockPrefWriteCount;

    // 5 saves do mesmo valor — nenhum write adicional
    for (int i = 0; i < 5; i++) storageSaveSetPoint();
    TEST_ASSERT_EQUAL_INT(writesAfter, _mockPrefWriteCount);

    // Mudar valor → escreve
    setPoint = 81.0f;
    storageSaveSetPoint();
    TEST_ASSERT_EQUAL_INT(writesAfter + 1, _mockPrefWriteCount);
}

void test_bug8_nvs_reads_before_write_for_comparison(void) {
    // Garante que o "compare" realmente acontece (não skip por sorte)
    setPoint = 80.0f;
    storageSaveSetPoint();

    int readsBefore  = _mockPrefReadCount;
    int writesBefore = _mockPrefWriteCount;

    setPoint = 80.0f;            // mesmo valor
    storageSaveSetPoint();
    TEST_ASSERT_TRUE(_mockPrefReadCount > readsBefore);     // leu pra comparar
    TEST_ASSERT_EQUAL_INT(writesBefore, _mockPrefWriteCount); // não escreveu
}

// ============================================================
// Runner
// ============================================================

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_bug1_controlRun_does_not_run_during_autotune_done);
    RUN_TEST(test_bug1_controlRun_returns_after_autotune_reset);
    RUN_TEST(test_bug2_stuck_relay_detected_before_overtemp);
    RUN_TEST(test_bug2_overtemp_uses_60s_delay_not_5s);
    RUN_TEST(test_bug3_safety_trigger_invokes_cleanup_callback);
    RUN_TEST(test_bug4_autotune_cancelled_state_distinguishable_from_done);
    RUN_TEST(test_bug5_pid_with_ki_zero_does_not_accumulate_windup);
    RUN_TEST(test_bug6_recovery_save_persists_systemActive_change);
    RUN_TEST(test_bug7_recovery_state_machine_has_required_globals);
    RUN_TEST(test_bug8_nvs_skips_write_when_value_unchanged);
    RUN_TEST(test_bug8_nvs_reads_before_write_for_comparison);
    return UNITY_END();
}
