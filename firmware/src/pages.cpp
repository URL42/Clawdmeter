#include "pages.h"

Page page_step(Page p, int dir) {
    int i = ((int)p + dir) % PAGE_COUNT;
    if (i < 0) i += PAGE_COUNT;
    return (Page)i;
}

void gesture_clear(ButtonGesture* g) {
    g->down = false;
    g->holding = false;
    g->down_at = 0;
}

GestureEvent gesture_step(ButtonGesture* g, bool held, uint32_t now_ms, uint32_t hold_ms) {
    if (held && !g->down) {
        g->down = true;
        g->holding = false;
        g->down_at = now_ms;
        return GESTURE_PRESS;
    }
    if (!held && g->down) {
        const bool was_holding = g->holding;
        gesture_clear(g);
        return was_holding ? GESTURE_HOLD_END : GESTURE_TAP;
    }
    if (held && !g->holding && now_ms - g->down_at >= hold_ms) {
        g->holding = true;
        return GESTURE_HOLD_START;
    }
    return GESTURE_NONE;
}

void page_nav_init(PageNav* n, uint32_t now_ms) {
    n->page = PAGE_HOME;
    n->last_input = now_ms;
}

bool page_nav_go(PageNav* n, Page p, uint32_t now_ms) {
    n->last_input = now_ms;
    if (p == n->page) return false;
    n->page = p;
    return true;
}

void page_nav_note_input(PageNav* n, uint32_t now_ms) { n->last_input = now_ms; }

bool page_nav_idle_home_due(const PageNav* n, uint32_t now_ms, bool waiting) {
    return n->page != PAGE_HOME && !waiting && now_ms - n->last_input >= PAGE_IDLE_HOME_MS;
}
