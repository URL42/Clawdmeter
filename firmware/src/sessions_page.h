#pragma once
#include "data.h"

// The Sessions page (pages.h): how many Claude Code sessions are active and
// how busy they are (or the alert, while one is up), then the session list
// from the app (`ss`), what waits on you first. It has its own LVGL screen, so
// while it's up the meters (and Clawd, parked by the director) cost nothing
// to draw. Positions and fonts come from the meters' layout (layout.h), so
// it follows the panel's shape the same way.

void sessions_page_init(void);                    // after ui_init()
void sessions_page_show(bool on);                 // its screen, or the meters' back
void sessions_page_update(const UsageData* d);    // `n`, `a` and `ss` from a payload
void sessions_page_tick(void);                    // the list's ages count on while it's up
void sessions_page_set_alert(const char* message);  // NULL: nothing waiting
void sessions_page_set_link(bool connected);
