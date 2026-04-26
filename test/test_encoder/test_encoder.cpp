#include <unity.h>
#include "../../src/encoder_logic.h"

static const unsigned long DEBOUNCE_MS  = 20;
static const unsigned long LONG_MS      = 1000;

// Helper: feed a sequence (now, rawHigh) → return last event (último tick decide)
static EncoderButtonState st;
static unsigned long T = 0;

void setUp(void) {
    st = EncoderButtonState();   // reset
    T = 0;
}
void tearDown(void) {}

static EncoderButtonEvent feed(unsigned long advance, bool rawHigh) {
    T += advance;
    return encoderButtonTick(st, T, rawHigh, DEBOUNCE_MS, LONG_MS);
}

// ============================================================
// Idle state — sem ações
// ============================================================
void test_idle_no_event(void) {
    EncoderButtonEvent e = feed(100, true);   // idle pull-up high
    TEST_ASSERT_FALSE(e.pressed);
    TEST_ASSERT_FALSE(e.longPress);
}

// ============================================================
// Click curto válido (acima do debounce, abaixo do long-press)
// ============================================================
void test_short_click_fires_pressed_on_release(void) {
    feed(100, true);              // idle
    feed(50, false);              // press at T=150
    EncoderButtonEvent e1 = feed(50, false);  // ainda apertado, T=200
    TEST_ASSERT_FALSE(e1.pressed);
    TEST_ASSERT_FALSE(e1.longPress);
    EncoderButtonEvent e2 = feed(30, true);   // release at T=230
    TEST_ASSERT_TRUE(e2.pressed);             // click registrado
    TEST_ASSERT_FALSE(e2.longPress);
}

// ============================================================
// Long press dispara antes do release (uma vez só)
// ============================================================
void test_long_press_fires_while_held(void) {
    feed(100, true);
    feed(50, false);                          // press at T=150
    EncoderButtonEvent e1 = feed(LONG_MS + 10, false);  // ainda apertado após 1s
    TEST_ASSERT_TRUE(e1.longPress);
    TEST_ASSERT_FALSE(e1.pressed);
    EncoderButtonEvent e2 = feed(50, false);  // mais 50ms apertado: NÃO refire
    TEST_ASSERT_FALSE(e2.longPress);
    EncoderButtonEvent e3 = feed(50, true);   // release: NÃO conta como click
    TEST_ASSERT_FALSE(e3.pressed);
}

// ============================================================
// Debounce: pulse < debounce é ignorado
// ============================================================
void test_debounce_rejects_short_pulse(void) {
    feed(100, true);
    feed(5, false);                  // press at T=105
    EncoderButtonEvent e = feed(3, true);   // release T=108 (3ms após press, < debounce 20ms)
    TEST_ASSERT_FALSE(e.pressed);    // ignorado
}

// ============================================================
// Bouncy press: várias bordas em < debounce → 1 click só
// ============================================================
void test_bounce_filtered_to_single_click(void) {
    feed(100, true);
    // Bouncy press: down/up/down/up rapidamente
    feed(2, false);
    feed(2, true);
    feed(2, false);
    feed(2, true);
    feed(2, false);                  // T=110, press final
    feed(50, false);                  // T=160, ainda apertado (estável)
    EncoderButtonEvent e = feed(20, true);  // T=180, release
    TEST_ASSERT_TRUE(e.pressed);     // exatamente 1 click
}

// ============================================================
// Press + release rápidos, depois novo press: contado como 2 clicks
// ============================================================
void test_two_separate_clicks(void) {
    feed(100, true);
    feed(50, false);                  // T=150 press 1
    EncoderButtonEvent e1 = feed(50, true);   // T=200 release 1
    TEST_ASSERT_TRUE(e1.pressed);

    feed(100, false);                 // T=300 press 2
    EncoderButtonEvent e2 = feed(50, true);   // T=350 release 2
    TEST_ASSERT_TRUE(e2.pressed);
}

// ============================================================
// Hold passes long-press threshold + release: pressed NÃO dispara
// ============================================================
void test_hold_then_release_does_not_fire_pressed(void) {
    feed(100, true);
    feed(50, false);                  // press
    feed(LONG_MS + 100, false);       // hold past long
    EncoderButtonEvent e = feed(50, true);  // release
    TEST_ASSERT_FALSE(e.pressed);     // long já consumiu, não vira click
    TEST_ASSERT_FALSE(e.longPress);   // long já fired antes, não refire
}

// ============================================================
// Release sem press anterior (estado limpo): nada acontece
// ============================================================
void test_release_without_press_is_noop(void) {
    EncoderButtonEvent e = feed(100, true);  // sempre high
    TEST_ASSERT_FALSE(e.pressed);
    TEST_ASSERT_FALSE(e.longPress);
}

// ============================================================
// Bounce no release: depois de click válido, bounces extras não geram outros
// ============================================================
void test_bounce_on_release_does_not_create_extra_clicks(void) {
    feed(100, true);
    feed(50, false);                  // press estável T=150
    feed(100, false);                  // segura T=250
    EncoderButtonEvent e1 = feed(50, true);   // release T=300
    TEST_ASSERT_TRUE(e1.pressed);
    // Bounces no release (rápidos)
    feed(2, false);
    feed(2, true);
    EncoderButtonEvent e3 = feed(50, true);  // estável após bounces
    TEST_ASSERT_FALSE(e3.pressed);    // não dispara novo click
}

// ============================================================
// Runner
// ============================================================
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_no_event);
    RUN_TEST(test_short_click_fires_pressed_on_release);
    RUN_TEST(test_long_press_fires_while_held);
    RUN_TEST(test_debounce_rejects_short_pulse);
    RUN_TEST(test_bounce_filtered_to_single_click);
    RUN_TEST(test_two_separate_clicks);
    RUN_TEST(test_hold_then_release_does_not_fire_pressed);
    RUN_TEST(test_release_without_press_is_noop);
    RUN_TEST(test_bounce_on_release_does_not_create_extra_clicks);
    return UNITY_END();
}
