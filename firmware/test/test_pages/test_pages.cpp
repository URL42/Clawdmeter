// pages: page stepping, the side-button tap/hold gestures, and the idle
// return home.
//   pio test -d firmware -e native -f test_pages
#include <unity.h>
#include "pages.h"

static ButtonGesture g;

void setUp(void) { gesture_clear(&g); }
void tearDown(void) {}

static void test_page_step_wraps_both_ways(void) {
    TEST_ASSERT_EQUAL(PAGE_SESSIONS, page_step(PAGE_HOME, +1));
    TEST_ASSERT_EQUAL(PAGE_HOME, page_step(PAGE_SESSIONS, +1));
    TEST_ASSERT_EQUAL(PAGE_SESSIONS, page_step(PAGE_HOME, -1));
    TEST_ASSERT_EQUAL(PAGE_HOME, page_step(PAGE_SESSIONS, -1));
}

// Hold from t0 to t1 in 10 ms steps; count each event.
static int presses, taps, hold_starts, hold_ends;
static void reset_counts(void) { presses = taps = hold_starts = hold_ends = 0; }
static void count(GestureEvent e) {
    switch (e) {
    case GESTURE_PRESS: presses++; break;
    case GESTURE_TAP: taps++; break;
    case GESTURE_HOLD_START: hold_starts++; break;
    case GESTURE_HOLD_END: hold_ends++; break;
    default: break;
    }
}
static void hold(uint32_t t0, uint32_t t1) {
    for (uint32_t t = t0; t <= t1; t += 10) count(gesture_step(&g, true, t, BTN_PTT_HOLD_MS));
}
static void lift(uint32_t t) { count(gesture_step(&g, false, t, BTN_PTT_HOLD_MS)); }

static void test_quick_press_is_a_tap(void) {
    reset_counts();
    hold(1000, 1000 + BTN_PTT_HOLD_MS - 10);
    lift(1000 + BTN_PTT_HOLD_MS);
    TEST_ASSERT_EQUAL(1, presses);
    TEST_ASSERT_EQUAL(1, taps);
    TEST_ASSERT_EQUAL(0, hold_starts + hold_ends);
}

static void test_long_press_holds_once_then_ends(void) {
    reset_counts();
    hold(1000, 5000);
    TEST_ASSERT_EQUAL(1, presses);
    TEST_ASSERT_EQUAL(1, hold_starts);
    lift(5010);
    TEST_ASSERT_EQUAL(1, hold_ends);
    TEST_ASSERT_EQUAL(0, taps);
}

static void test_hold_starts_exactly_at_threshold(void) {
    TEST_ASSERT_EQUAL(GESTURE_PRESS, gesture_step(&g, true, 100, BTN_MODE_HOLD_MS));
    TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_step(&g, true, 100 + BTN_MODE_HOLD_MS - 1, BTN_MODE_HOLD_MS));
    TEST_ASSERT_EQUAL(GESTURE_HOLD_START, gesture_step(&g, true, 100 + BTN_MODE_HOLD_MS, BTN_MODE_HOLD_MS));
}

static void test_idle_button_reports_nothing(void) {
    for (uint32_t t = 0; t < 2000; t += 10)
        TEST_ASSERT_EQUAL(GESTURE_NONE, gesture_step(&g, false, t, BTN_PTT_HOLD_MS));
}

static void test_survives_millis_wraparound(void) {
    reset_counts();
    hold(0xFFFFFF00u, 0xFFFFFFF0u);       // press just before the wrap
    TEST_ASSERT_EQUAL(0, hold_starts);
    count(gesture_step(&g, true, 0x00000100u, BTN_PTT_HOLD_MS));  // 512 ms after the press, past the wrap
    TEST_ASSERT_EQUAL(1, hold_starts);
}

static void test_idle_home_only_off_home_and_quiet(void) {
    PageNav n;
    page_nav_init(&n, 0);
    TEST_ASSERT_FALSE(page_nav_idle_home_due(&n, PAGE_IDLE_HOME_MS * 2, false));   // already home
    TEST_ASSERT_TRUE(page_nav_go(&n, PAGE_SESSIONS, 1000));
    TEST_ASSERT_FALSE(page_nav_go(&n, PAGE_SESSIONS, 1000));                     // no change
    TEST_ASSERT_FALSE(page_nav_idle_home_due(&n, 1000 + PAGE_IDLE_HOME_MS - 1, false));
    TEST_ASSERT_TRUE(page_nav_idle_home_due(&n, 1000 + PAGE_IDLE_HOME_MS, false));
    TEST_ASSERT_FALSE(page_nav_idle_home_due(&n, 1000 + PAGE_IDLE_HOME_MS, true));  // something waits
    page_nav_note_input(&n, 2000);                                               // a press restarts the wait
    TEST_ASSERT_FALSE(page_nav_idle_home_due(&n, 1000 + PAGE_IDLE_HOME_MS, false));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_page_step_wraps_both_ways);
    RUN_TEST(test_quick_press_is_a_tap);
    RUN_TEST(test_long_press_holds_once_then_ends);
    RUN_TEST(test_hold_starts_exactly_at_threshold);
    RUN_TEST(test_idle_button_reports_nothing);
    RUN_TEST(test_survives_millis_wraparound);
    RUN_TEST(test_idle_home_only_off_home_and_quiet);
    return UNITY_END();
}
