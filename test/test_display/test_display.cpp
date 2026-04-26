// Testes para o display: foco em diff redraw e edge cases.
// O LCD é mockado em lib/native_mocks/LiquidCrystal_I2C.{h,cpp}.
#include <unity.h>
#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include "../../src/display.h"
#include "../../src/state.h"
#include "../../src/config.h"

void setUp(void) {
    mockReset();
    mockLcdReset();
    lcdFlipped = false;
    safetyError = SAFETY_OK;
    recoveryPending = false;
    displayInit();        // Inicializa LCD + invalida buffer
    mockLcdReset();        // Limpa contadores depois do init (que já escreve splash)
}

void tearDown(void) {}

// Helper: monta uma string de tamanho LCD_COLS preenchida com ' ' depois conteúdo
static void writeLine(int row, const char* text) {
    displayLcdLineForTest(row, text);
}

// ============================================================
// 1. Primeira escrita após invalidate: escreve a linha inteira
// ============================================================
void test_first_write_after_invalidate_writes_full_line(void) {
    displayInvalidate();
    mockLcdReset();
    writeLine(0, "ABC");
    // Linha inteira de 20 chars deve ser escrita: "ABC" + 17 espaços
    TEST_ASSERT_EQUAL_INT(LCD_COLS, _mockLcdWriteCharCount);
    TEST_ASSERT_EQUAL_INT(1, _mockLcdSetCursorCount);
}

// ============================================================
// 2. Segunda escrita IGUAL: zero writes
// ============================================================
void test_identical_write_produces_zero_writes(void) {
    writeLine(0, "Status: OK");
    int writesAfterFirst = _mockLcdWriteCharCount;
    int cursorsAfterFirst = _mockLcdSetCursorCount;

    writeLine(0, "Status: OK");
    TEST_ASSERT_EQUAL_INT(writesAfterFirst, _mockLcdWriteCharCount);
    TEST_ASSERT_EQUAL_INT(cursorsAfterFirst, _mockLcdSetCursorCount);
}

// ============================================================
// 3. Mudança em 1 caractere no meio: 1 setCursor + 1 write
// ============================================================
void test_single_char_change_writes_one_char(void) {
    writeLine(0, "Temp: 80.0 C");
    mockLcdReset();
    writeLine(0, "Temp: 85.0 C");        // muda só o '0' para '5' na pos 7
    TEST_ASSERT_EQUAL_INT(1, _mockLcdWriteCharCount);
    TEST_ASSERT_EQUAL_INT(1, _mockLcdSetCursorCount);
}

// ============================================================
// 4. Mudança em range contíguo: 1 setCursor + N writes
// ============================================================
void test_contiguous_change_uses_one_setCursor(void) {
    writeLine(0, "ABCDEF");
    mockLcdReset();
    writeLine(0, "ABXYZF");              // 3 chars contíguos diferentes (pos 2-4)
    TEST_ASSERT_EQUAL_INT(3, _mockLcdWriteCharCount);
    TEST_ASSERT_EQUAL_INT(1, _mockLcdSetCursorCount);
}

// ============================================================
// 5. Mudanças em posições NÃO contíguas: 1 setCursor por bloco
// ============================================================
void test_noncontiguous_changes_use_multiple_setCursor(void) {
    writeLine(0, "AAAAAAAAAAAAAAAAAAAA");
    mockLcdReset();
    writeLine(0, "BAAAAAAAAAAAAAAAAAAB");  // muda pos 0 e pos 19
    TEST_ASSERT_EQUAL_INT(2, _mockLcdWriteCharCount);
    TEST_ASSERT_EQUAL_INT(2, _mockLcdSetCursorCount);
}

// ============================================================
// 6. Linha mais curta cobre o que era escrito antes (com espaços)
// ============================================================
void test_shorter_line_overwrites_with_spaces(void) {
    writeLine(0, "ABCDEFGHIJKLMNOPQRST");  // 20 chars, sem espaços
    mockLcdReset();
    writeLine(0, "Bye");                   // 3 chars + 17 espaços
    // Todos os 20 chars mudam (texto antigo não tinha espaços)
    TEST_ASSERT_EQUAL_INT(20, _mockLcdWriteCharCount);
    TEST_ASSERT_EQUAL_INT(1, _mockLcdSetCursorCount);
}

// ============================================================
// 7. displayInvalidate força próxima escrita ser completa
// ============================================================
void test_invalidate_forces_full_redraw_on_next_write(void) {
    writeLine(0, "Same text here");
    mockLcdReset();
    displayInvalidate();
    writeLine(0, "Same text here");  // mesmo texto, mas após invalidate
    TEST_ASSERT_EQUAL_INT(LCD_COLS, _mockLcdWriteCharCount);
}

// ============================================================
// 8. Linhas independentes: escrever linha 0 não afeta diff de linha 1
// ============================================================
void test_lines_have_independent_buffers(void) {
    writeLine(0, "Line 0");
    writeLine(1, "Line 1");
    mockLcdReset();
    writeLine(0, "Line 0");           // igual → 0 writes
    writeLine(1, "Line 1");           // igual → 0 writes
    TEST_ASSERT_EQUAL_INT(0, _mockLcdWriteCharCount);
}

// ============================================================
// 9. Conteúdo correto na "tela" mockada após escrita
// ============================================================
void test_screen_content_correct_after_write(void) {
    writeLine(0, "Hello");
    TEST_ASSERT_EQUAL_CHAR('H', _mockLcdScreen[0][0]);
    TEST_ASSERT_EQUAL_CHAR('e', _mockLcdScreen[0][1]);
    TEST_ASSERT_EQUAL_CHAR('l', _mockLcdScreen[0][2]);
    TEST_ASSERT_EQUAL_CHAR('l', _mockLcdScreen[0][3]);
    TEST_ASSERT_EQUAL_CHAR('o', _mockLcdScreen[0][4]);
    TEST_ASSERT_EQUAL_CHAR(' ', _mockLcdScreen[0][5]);   // padding
}

// ============================================================
// 10. I2C clock setado em displayInit
// ============================================================
void test_displayInit_sets_i2c_clock(void) {
    mockLcdReset();
    displayInit();
    TEST_ASSERT_EQUAL_UINT32((unsigned long)I2C_FREQ_HZ, _mockWireClock);
}

// ============================================================
// 11. Display flipped: conteúdo aparece invertido na tela física
// ============================================================
void test_flipped_display_inverts_horizontal_and_vertical(void) {
    lcdFlipped = true;
    displayInvalidate();
    mockLcdReset();
    writeLine(0, "ABC");
    // Em tela flipada, linha 0 lógica vira linha 3 física
    // E "ABC" + 17 espaços é invertido: 17 espaços + "CBA"
    TEST_ASSERT_EQUAL_CHAR('A', _mockLcdScreen[3][LCD_COLS - 1]);
    TEST_ASSERT_EQUAL_CHAR('B', _mockLcdScreen[3][LCD_COLS - 2]);
    TEST_ASSERT_EQUAL_CHAR('C', _mockLcdScreen[3][LCD_COLS - 3]);
}

// ============================================================
// 12. Diff funciona corretamente também com flipped
// ============================================================
void test_flipped_diff_writes_only_changed_chars(void) {
    lcdFlipped = true;
    displayInvalidate();
    writeLine(0, "Temp: 80.0");
    mockLcdReset();
    writeLine(0, "Temp: 85.0");        // muda 1 char
    TEST_ASSERT_EQUAL_INT(1, _mockLcdWriteCharCount);
}

// ============================================================
// Runner
// ============================================================
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_write_after_invalidate_writes_full_line);
    RUN_TEST(test_identical_write_produces_zero_writes);
    RUN_TEST(test_single_char_change_writes_one_char);
    RUN_TEST(test_contiguous_change_uses_one_setCursor);
    RUN_TEST(test_noncontiguous_changes_use_multiple_setCursor);
    RUN_TEST(test_shorter_line_overwrites_with_spaces);
    RUN_TEST(test_invalidate_forces_full_redraw_on_next_write);
    RUN_TEST(test_lines_have_independent_buffers);
    RUN_TEST(test_screen_content_correct_after_write);
    RUN_TEST(test_displayInit_sets_i2c_clock);
    RUN_TEST(test_flipped_display_inverts_horizontal_and_vertical);
    RUN_TEST(test_flipped_diff_writes_only_changed_chars);
    return UNITY_END();
}
