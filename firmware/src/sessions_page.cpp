#include "sessions_page.h"
#include "pages.h"
#include "layout.h"
#include "theme.h"
#include "hal/board_caps.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

static Layout L = {};
static lv_obj_t* meters_scr = nullptr;
static lv_obj_t* scr = nullptr;
static lv_obj_t* dots = nullptr;           // which page this is; "Not connected" takes its place
static lv_obj_t* lbl_link = nullptr;

// First card: how many sessions and how busy, or the alert while one is up.
static lv_obj_t* card_top = nullptr;
static lv_obj_t* grp_count = nullptr;
static lv_obj_t* lbl_count = nullptr;
static lv_obj_t* lbl_count_desc = nullptr;
static lv_obj_t* lbl_activity = nullptr;
static lv_obj_t* grp_alert = nullptr;
static lv_obj_t* lbl_alert = nullptr;
static char alert_shown[128] = "";

// Second card: the session list (`ss`), what waits on you first.
static lv_obj_t* lbl_rows_empty = nullptr;
static lv_obj_t* row_name[SESSION_ROWS_MAX] = {};
static lv_obj_t* row_state[SESSION_ROWS_MAX] = {};
static int rows_fit = 0;                   // rows the card has room for
static SessionRowData rows[SESSION_ROWS_MAX];
static int row_count = -1;                 // -1: the app sends no list
static bool have_n = false;                // the app sends `n`: hooks are in
static uint32_t rows_at = 0;               // lv_tick when they arrived; ages count on from there

static lv_obj_t* make_card(lv_obj_t* parent, const LayoutRect& r) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, r.x, r.y);
    lv_obj_set_size(card, r.w, r.h);
    lv_obj_set_style_bg_color(card, THEME_PANEL, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_border_color(card, THEME_ACCENT, 0);
    lv_obj_set_style_pad_left(card, L.pad_x, 0);
    lv_obj_set_style_pad_right(card, L.pad_x, 0);
    lv_obj_set_style_pad_top(card, L.pad_y, 0);
    lv_obj_set_style_pad_bottom(card, L.pad_y, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

// A transparent full-size layer in a card, to swap a set of labels as one.
static lv_obj_t* make_group(lv_obj_t* card) {
    lv_obj_t* g = lv_obj_create(card);
    lv_obj_set_size(g, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(g, 0, 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g, 0, 0);
    lv_obj_set_style_pad_all(g, 0, 0);
    lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);
    return g;
}

static lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

// Which page you're on: a dot per page, centred on the status line's band
// at the bottom, this one lit.
static void build_dots(void) {
    const int d = 8, gap = 10;
    const int total = PAGE_COUNT * d + (PAGE_COUNT - 1) * gap;
    dots = lv_obj_create(scr);
    lv_obj_set_size(dots, total, d);
    lv_obj_set_style_bg_opa(dots, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dots, 0, 0);
    lv_obj_set_style_pad_all(dots, 0, 0);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, L.status_y - (lv_font_get_line_height(L.status_font) - d) / 2);
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t* dot = lv_obj_create(dots);
        lv_obj_set_size(dot, d, d);
        lv_obj_set_pos(dot, i * (d + gap), 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, i == PAGE_SESSIONS ? THEME_TEXT : THEME_BAR_BG, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    }
}

static const char* state_word(char s) {
    switch (s) {
    case 'w': return "Waiting";
    case 'r': return "Working";
    case 'd': return "Done";
    default:  return "Idle";
    }
}

static lv_color_t state_color(char s) {
    switch (s) {
    case 'w': return THEME_ACCENT;
    case 'r': return THEME_TEXT;
    case 'd': return THEME_GREEN;
    default:  return THEME_DIM;
    }
}

// As many rows as the card holds (up to SESSION_ROWS_MAX): the name on the
// left, cut with "..."; the state and its age on the right.
static void build_rows(lv_obj_t* card, const LayoutRect& r) {
    const int inner_w = r.w - 2 * L.pad_x;
    const int line_h = lv_font_get_line_height(L.reset_font);
    const int gap = line_h / 3;
    rows_fit = (r.h - 2 * L.pad_y + gap) / (line_h + gap);
    if (rows_fit > SESSION_ROWS_MAX) rows_fit = SESSION_ROWS_MAX;
    if (rows_fit < 1) rows_fit = 1;
    static const char* const WIDEST[] = { "Waiting 59m", "Working 59m" };
    int right_w = 0;
    for (const char* w : WIDEST) {
        const int tw = layout_text_width(L.reset_font, w);
        if (tw > right_w) right_w = tw;
    }
    for (int i = 0; i < rows_fit; i++) {
        const int y = i * (line_h + gap);
        row_name[i] = make_label(card, L.reset_font, THEME_TEXT);
        lv_label_set_long_mode(row_name[i], LV_LABEL_LONG_DOT);
        lv_obj_set_size(row_name[i], inner_w - right_w - line_h / 2, line_h);
        lv_obj_set_pos(row_name[i], 0, y);
        row_state[i] = make_label(card, L.reset_font, THEME_DIM);
        lv_obj_align(row_state[i], LV_ALIGN_TOP_RIGHT, 0, y);
    }
    lbl_rows_empty = make_label(card, L.reset_font, THEME_DIM);
    lv_label_set_long_mode(lbl_rows_empty, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_rows_empty, inner_w);
    lv_obj_set_pos(lbl_rows_empty, 0, 0);
}

static void format_age(char* buf, size_t n, uint32_t s) {
    if (s < 60)        snprintf(buf, n, "%lus", (unsigned long)s);
    else if (s < 3600) snprintf(buf, n, "%lum", (unsigned long)(s / 60));
    else               snprintf(buf, n, "%luh", (unsigned long)(s / 3600));
}

// Setting a label's text (or colour) redraws it even when nothing changed;
// the list is redrawn every few seconds for its ages, so only touch what did.
static bool set_text(lv_obj_t* l, const char* text) {
    if (strcmp(lv_label_get_text(l), text) == 0) return false;
    lv_label_set_text(l, text);
    return true;
}

static void draw_rows(void) {
    const uint32_t since = lv_tick_elaps(rows_at) / 1000;
    for (int i = 0; i < rows_fit; i++) {
        if (i >= row_count) {
            set_text(row_name[i], "");
            set_text(row_state[i], "");
            continue;
        }
        const SessionRowData& r = rows[i];
        char age[8], right[24];
        format_age(age, sizeof(age), r.age_s + since);
        snprintf(right, sizeof(right), "%s %s", state_word(r.state), age);
        set_text(row_name[i], r.name);
        if (set_text(row_state[i], right)) lv_obj_set_style_text_color(row_state[i], state_color(r.state), 0);
    }
    if (row_count > 0) {
        lv_obj_add_flag(lbl_rows_empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        // No list: the hooks aren't installed, or the app predates the list.
        set_text(lbl_rows_empty, row_count == 0 ? "No sessions in the last 30 min"
                                 : have_n       ? "This app sends no session list"
                                                : "Install the hooks in the app");
        lv_obj_clear_flag(lbl_rows_empty, LV_OBJ_FLAG_HIDDEN);
    }
}

void sessions_page_init(void) {
    const BoardCaps& caps = board_caps();
    L = layout_compute({ caps.width, caps.height, caps.round_panel });
    meters_scr = lv_screen_active();

    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, THEME_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    if (L.title_font[0]) {
        lv_obj_t* title = make_label(scr, L.title_font[0], THEME_TEXT);
        lv_label_set_text(title, "Sessions");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, L.title_y[0]);
    }

    card_top = make_card(scr, L.card[0]);
    grp_count = make_group(card_top);
    lbl_count = make_label(grp_count, L.pct_font, THEME_TEXT);
    lv_label_set_text(lbl_count, "--");
    lv_obj_set_pos(lbl_count, 0, 0);
    lbl_activity = make_label(grp_count, L.pill_font, THEME_DIM);
    lv_obj_align(lbl_activity, LV_ALIGN_TOP_RIGHT, 0, 1);
    lbl_count_desc = make_label(grp_count, L.reset_font, THEME_DIM);
    lv_label_set_text(lbl_count_desc, "Waiting for the app");
    lv_obj_set_pos(lbl_count_desc, 0, L.reset_y);

    // The alert wraps in the space above the dismiss hint and ends in "..."
    // where it runs out.
    grp_alert = make_group(card_top);
    lbl_alert = make_label(grp_alert, L.reset_font, THEME_ACCENT);
    lv_label_set_long_mode(lbl_alert, LV_LABEL_LONG_DOT);
    lv_obj_set_size(lbl_alert, L.card[0].w - 2 * L.pad_x, L.reset_y - 4);
    lv_obj_set_pos(lbl_alert, 0, 0);
    lv_obj_t* hint = make_label(grp_alert, L.reset_font, THEME_DIM);
    // Without a touchscreen BOOT dismisses, or PWR where it's the only button
    // (director_button).
    lv_label_set_text(hint, caps.has_touch ? "Tap to dismiss"
                          : caps.button_count > 0 ? "Press BOOT to dismiss"
                                                  : "Press PWR to dismiss");
    lv_obj_set_pos(hint, 0, L.reset_y);

    build_rows(make_card(scr, L.card[1]), L.card[1]);
    draw_rows();

    lbl_link = make_label(scr, L.status_font, THEME_DIM);
    lv_label_set_text(lbl_link, "Not connected");
    lv_obj_align(lbl_link, LV_ALIGN_BOTTOM_MID, 0, L.status_y);
    lv_obj_add_flag(lbl_link, LV_OBJ_FLAG_HIDDEN);
    build_dots();

    sessions_page_set_alert(NULL);
}

void sessions_page_show(bool on) {
    if (!scr) return;
    lv_obj_t* want = on ? scr : meters_scr;
    if (lv_screen_active() == want) return;
    if (on) draw_rows();                     // ages moved on while it was hidden
    lv_screen_load(want);
}

// The ages count on between payloads; a redraw every few seconds is plenty
// for "45s" / "12m" / "3h".
void sessions_page_tick(void) {
    static uint32_t last = 0;
    if (!scr || lv_screen_active() != scr || row_count <= 0) return;
    if (lv_tick_elaps(last) < 5000) return;
    last = lv_tick_get();
    draw_rows();
}

static const char* activity_word(int a) {
    switch (a) {
    case 0:  return "Quiet";
    case 1:  return "Working";
    case 2:  return "Busy";
    case 3:  return "Very busy";
    default: return "";
    }
}

void sessions_page_update(const UsageData* d) {
    if (!scr) return;
    have_n = d->sessions >= 0;
    if (d->session_rows >= 0) {
        row_count = d->session_rows;
        memcpy(rows, d->session_list, sizeof(rows));
        rows_at = lv_tick_get();
    }
    draw_rows();
    if (d->sessions < 0) {
        // No `n`: an older app, or the hooks aren't installed.
        lv_label_set_text(lbl_count, "--");
        lv_label_set_text(lbl_count_desc, "Install the hooks in the app");
        lv_label_set_text(lbl_activity, "");
        return;
    }
    lv_label_set_text_fmt(lbl_count, "%d", d->sessions);
    lv_label_set_text(lbl_count_desc, d->sessions == 1 ? "active session" : "active sessions");
    lv_label_set_text(lbl_activity, d->sessions > 0 ? activity_word(d->activity) : "");
}

void sessions_page_set_alert(const char* message) {
    if (!scr) return;
    static bool drawn = false;
    const char* m = message ? message : "";
    if (drawn && strcmp(m, alert_shown) == 0) return;
    drawn = true;
    snprintf(alert_shown, sizeof(alert_shown), "%s", m);
    lv_label_set_text(lbl_alert, m);
    lv_obj_set_style_border_width(card_top, m[0] ? 2 : 0, 0);
    if (m[0]) {
        lv_obj_add_flag(grp_count, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(grp_alert, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(grp_alert, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(grp_count, LV_OBJ_FLAG_HIDDEN);
    }
}

void sessions_page_set_link(bool connected) {
    if (!scr) return;
    lv_obj_t* shown  = connected ? dots : lbl_link;
    lv_obj_t* hidden = connected ? lbl_link : dots;
    lv_obj_clear_flag(shown, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hidden, LV_OBJ_FLAG_HIDDEN);
}
