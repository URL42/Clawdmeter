#pragma once
// Pages: the meters with Clawd (home) and the pages beside it, and the
// side-button gestures that move between them.
//
//   BOOT  tap: previous page   hold: Control twice (macOS Dictation on / off)
//   KEY   tap: next page       hold: Shift+Tab once (Claude Code mode toggle)
//   PWR   press: home; already home, brightness (main.cpp)
//
// A tap is a release before the hold threshold; dictation toggles
// BTN_PTT_HOLD_MS into a press.
//
// Pure (no Arduino / LVGL) so the native tests cover it:
//   pio test -d firmware -e native -f test_pages
#include <stdint.h>

#define BTN_PTT_HOLD_MS     250UL            // BOOT held this long → Dictation toggle
#define BTN_MODE_HOLD_MS    600UL            // KEY held this long → Shift+Tab
#define PAGE_IDLE_HOME_MS   (5UL * 60UL * 1000UL)  // off home, nothing waiting, no input → home

enum Page {
    PAGE_HOME,       // the meters and Clawd
    PAGE_SESSIONS,   // Claude Code sessions and what's waiting on you
    PAGE_COUNT,
};

// The page `dir` steps from `p` (+1 next, -1 previous), wrapping around.
Page page_step(Page p, int dir);

// ---- One side button's gesture ----
enum GestureEvent {
    GESTURE_NONE,
    GESTURE_PRESS,       // the press edge (wake / "you're here" bookkeeping)
    GESTURE_TAP,         // released before the hold threshold
    GESTURE_HOLD_START,  // held past the threshold (once per press)
    GESTURE_HOLD_END,    // released after HOLD_START
};

struct ButtonGesture {
    bool     down;
    bool     holding;    // HOLD_START was reported for this press
    uint32_t down_at;
};

void gesture_clear(ButtonGesture* g);

// One step per loop: `held` is the button's level now. At most one event per
// step; a press and its hold never land on the same step.
GestureEvent gesture_step(ButtonGesture* g, bool held, uint32_t now_ms, uint32_t hold_ms);

// ---- Which page is up ----
struct PageNav {
    Page     page;
    uint32_t last_input;   // last button or touch while off home
};

void page_nav_init(PageNav* n, uint32_t now_ms);
// Show page `p`. True when it changed. Counts as input either way.
bool page_nav_go(PageNav* n, Page p, uint32_t now_ms);
void page_nav_note_input(PageNav* n, uint32_t now_ms);
// Off home with nothing waiting and no input for PAGE_IDLE_HOME_MS: time to
// go home, so the screensaver and Clawd get the panel back.
bool page_nav_idle_home_due(const PageNav* n, uint32_t now_ms, bool waiting);
