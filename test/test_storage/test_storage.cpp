#include <unity.h>
#include <Arduino.h>
#include <Preferences.h>
#include "../../src/state.h"
#include "../../src/storage.h"
#include "../../src/config.h"

void setUp(void) {
    mockReset();
    mockPrefsReset();
    setPoint    = DEFAULT_SETPOINT;
    offset      = DEFAULT_OFFSET;
    controlMode = MODE_HYSTERESIS;
    pidKp       = DEFAULT_PID_KP;
    pidKi       = DEFAULT_PID_KI;
    pidKd       = DEFAULT_PID_KD;
    pidWindowSize = DEFAULT_PID_WINDOW_MS;
    pidThreshold  = DEFAULT_PID_THRESHOLD;
    backlightTimeoutIndex = 0;
    timerSetMinutes  = 0;
    timerRemainingMs = 0;
    systemActive     = false;
    lcdFlipped       = false;
    activePresetIndex = -1;
    for (int i = 0; i < MAX_PRESETS; i++) {
        presets[i].used = false;
        presets[i].name[0] = '\0';
    }
}
void tearDown(void) {}

// ============================================================
// Load com NVS vazio aplica defaults (e os constraina)
// ============================================================

void test_load_settings_from_empty_nvs_uses_defaults(void) {
    storageLoadSettings();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, DEFAULT_SETPOINT, setPoint);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, DEFAULT_OFFSET, offset);
    TEST_ASSERT_EQUAL_INT(MODE_HYSTERESIS, controlMode);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, DEFAULT_PID_KP, pidKp);
}

// ============================================================
// Save → Load roundtrip
// ============================================================

void test_save_setpoint_then_load_returns_value(void) {
    setPoint = 75.5f;
    storageSaveSetPoint();
    setPoint = 0.0f;       // simular reboot
    storageLoadSettings();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 75.5f, setPoint);
}

void test_save_pid_params_roundtrip(void) {
    pidKp         = 3.45f;
    pidKi         = 0.123f;
    pidKd         = 67.8f;
    pidWindowSize = 15000UL;
    storageSavePidKp();
    storageSavePidKi();
    storageSavePidKd();
    storageSavePidWindow();

    pidKp = pidKi = pidKd = 0.0f;
    pidWindowSize = 0;
    storageLoadSettings();

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.45f, pidKp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.123f, pidKi);
    TEST_ASSERT_FLOAT_WITHIN(0.01f,  67.8f, pidKd);
    TEST_ASSERT_EQUAL_UINT32(15000UL, pidWindowSize);
}

// ============================================================
// CHECK-BEFORE-WRITE: validação principal pedida pelo usuário
// ============================================================

void test_save_unchanged_value_does_not_write(void) {
    setPoint = 80.0f;
    storageSaveSetPoint();          // primeira vez: escreve
    int writesAfterFirst = _mockPrefWriteCount;

    storageSaveSetPoint();          // mesmo valor: não deve escrever
    TEST_ASSERT_EQUAL_INT(writesAfterFirst, _mockPrefWriteCount);

    storageSaveSetPoint();          // mesma coisa de novo
    TEST_ASSERT_EQUAL_INT(writesAfterFirst, _mockPrefWriteCount);
}

void test_save_changed_value_writes(void) {
    setPoint = 80.0f;
    storageSaveSetPoint();
    int after1 = _mockPrefWriteCount;

    setPoint = 81.0f;
    storageSaveSetPoint();
    TEST_ASSERT_EQUAL_INT(after1 + 1, _mockPrefWriteCount);

    setPoint = 82.0f;
    storageSaveSetPoint();
    TEST_ASSERT_EQUAL_INT(after1 + 2, _mockPrefWriteCount);
}

void test_save_does_read_before_write(void) {
    // Deve haver pelo menos 1 read antes do write — para comparar.
    setPoint = 80.0f;
    storageSaveSetPoint();
    int writesAfter = _mockPrefWriteCount;
    int readsAfter  = _mockPrefReadCount;

    setPoint = 80.0f;            // valor IGUAL ao salvo
    int readsBefore = _mockPrefReadCount;
    storageSaveSetPoint();
    // Houve read (pra comparar) mas NENHUM write novo.
    TEST_ASSERT_TRUE(_mockPrefReadCount > readsBefore);
    TEST_ASSERT_EQUAL_INT(writesAfter, _mockPrefWriteCount);
}

void test_check_before_write_works_for_int(void) {
    controlMode = MODE_PID_ONOFF;
    storageSaveControlMode();
    int after1 = _mockPrefWriteCount;
    storageSaveControlMode();
    TEST_ASSERT_EQUAL_INT(after1, _mockPrefWriteCount);  // sem mudança
    controlMode = MODE_PID_WINDOW;
    storageSaveControlMode();
    TEST_ASSERT_EQUAL_INT(after1 + 1, _mockPrefWriteCount);
}

void test_check_before_write_works_for_ulong(void) {
    pidWindowSize = 12000UL;
    storageSavePidWindow();
    int after1 = _mockPrefWriteCount;
    storageSavePidWindow();
    TEST_ASSERT_EQUAL_INT(after1, _mockPrefWriteCount);
    pidWindowSize = 13000UL;
    storageSavePidWindow();
    TEST_ASSERT_EQUAL_INT(after1 + 1, _mockPrefWriteCount);
}

void test_float_epsilon_avoids_spurious_writes(void) {
    pidKp = 2.0f;
    storageSavePidKp();
    int after1 = _mockPrefWriteCount;
    pidKp = 2.0f + 0.00001f;     // diferença abaixo do epsilon
    storageSavePidKp();
    TEST_ASSERT_EQUAL_INT(after1, _mockPrefWriteCount);
}

// ============================================================
// Recovery state — comportamento crítico do save periódico
// ============================================================

void test_recovery_save_writes_when_active_changes(void) {
    systemActive     = false;
    timerSetMinutes  = 0;
    timerRemainingMs = 0;
    storageSaveRecoveryState();
    int after1 = _mockPrefWriteCount;

    systemActive = true;
    storageSaveRecoveryState();
    TEST_ASSERT_TRUE(_mockPrefWriteCount > after1);
}

void test_recovery_save_writes_when_timerSet_changes(void) {
    systemActive = true;
    timerSetMinutes = 10;
    storageSaveRecoveryState();
    int after1 = _mockPrefWriteCount;

    timerSetMinutes = 20;
    storageSaveRecoveryState();
    TEST_ASSERT_TRUE(_mockPrefWriteCount > after1);
}

void test_recovery_save_skips_small_trem_change(void) {
    // Tolerância: trem só conta como "mudado" se diff > 60s
    systemActive     = true;
    timerSetMinutes  = 10;
    timerRemainingMs = 600000UL;     // 10 min
    storageSaveRecoveryState();
    int after1 = _mockPrefWriteCount;

    timerRemainingMs = 590000UL;     // diff = 10s, dentro da tolerância
    storageSaveRecoveryState();
    TEST_ASSERT_EQUAL_INT(after1, _mockPrefWriteCount);
}

void test_recovery_save_writes_on_large_trem_change(void) {
    systemActive     = true;
    timerSetMinutes  = 10;
    timerRemainingMs = 600000UL;
    storageSaveRecoveryState();
    int after1 = _mockPrefWriteCount;

    timerRemainingMs = 500000UL;     // diff = 100s > 60s
    storageSaveRecoveryState();
    TEST_ASSERT_TRUE(_mockPrefWriteCount > after1);
}

void test_recovery_save_unchanged_state_no_writes(void) {
    systemActive     = true;
    timerSetMinutes  = 10;
    timerRemainingMs = 600000UL;
    storageSaveRecoveryState();
    int after1 = _mockPrefWriteCount;

    storageSaveRecoveryState();      // tudo igual
    TEST_ASSERT_EQUAL_INT(after1, _mockPrefWriteCount);
}

void test_storageHasRecoveryState_reflects_saved_active(void) {
    systemActive = true;
    storageSaveRecoveryState();
    TEST_ASSERT_TRUE(storageHasRecoveryState());

    storageClearRecoveryState();
    TEST_ASSERT_FALSE(storageHasRecoveryState());
}

void test_storageLoadRecoveryState_returns_saved_values(void) {
    systemActive     = true;
    timerSetMinutes  = 15;
    timerRemainingMs = 250000UL;
    storageSaveRecoveryState();

    unsigned long rem = 0;
    unsigned int  set = 0;
    storageLoadRecoveryState(rem, set);
    TEST_ASSERT_EQUAL_UINT32(250000UL, rem);
    TEST_ASSERT_EQUAL_UINT(15, set);
}

// ============================================================
// Presets
// ============================================================

void test_save_and_load_preset(void) {
    presets[3].used = true;
    strcpy(presets[3].name, "Forno Tinta");
    presets[3].kp = 5.5f;
    presets[3].ki = 0.05f;
    presets[3].kd = 100.0f;
    presets[3].windowMs = 8000UL;
    storageSavePreset(3);

    // Limpa e reload
    presets[3].used = false;
    presets[3].name[0] = '\0';
    presets[3].kp = presets[3].ki = presets[3].kd = 0.0f;
    presets[3].windowMs = 0;
    storageLoadPresets();

    TEST_ASSERT_TRUE(presets[3].used);
    TEST_ASSERT_EQUAL_STRING("Forno Tinta", presets[3].name);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.5f, presets[3].kp);
    TEST_ASSERT_EQUAL_UINT32(8000UL, presets[3].windowMs);
}

void test_delete_preset_marks_unused(void) {
    presets[2].used = true;
    strcpy(presets[2].name, "X");
    storageSavePreset(2);
    storageDeletePreset(2);
    presets[2].used = true;          // simular release-old in memory
    storageLoadPresets();
    TEST_ASSERT_FALSE(presets[2].used);
}

// ============================================================
// Runner
// ============================================================

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_load_settings_from_empty_nvs_uses_defaults);
    RUN_TEST(test_save_setpoint_then_load_returns_value);
    RUN_TEST(test_save_pid_params_roundtrip);
    RUN_TEST(test_save_unchanged_value_does_not_write);
    RUN_TEST(test_save_changed_value_writes);
    RUN_TEST(test_save_does_read_before_write);
    RUN_TEST(test_check_before_write_works_for_int);
    RUN_TEST(test_check_before_write_works_for_ulong);
    RUN_TEST(test_float_epsilon_avoids_spurious_writes);
    RUN_TEST(test_recovery_save_writes_when_active_changes);
    RUN_TEST(test_recovery_save_writes_when_timerSet_changes);
    RUN_TEST(test_recovery_save_skips_small_trem_change);
    RUN_TEST(test_recovery_save_writes_on_large_trem_change);
    RUN_TEST(test_recovery_save_unchanged_state_no_writes);
    RUN_TEST(test_storageHasRecoveryState_reflects_saved_active);
    RUN_TEST(test_storageLoadRecoveryState_returns_saved_values);
    RUN_TEST(test_save_and_load_preset);
    RUN_TEST(test_delete_preset_marks_unused);
    return UNITY_END();
}
