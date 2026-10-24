#pragma once
#include "data.h"

// The Sessions page (pages.h): how many Claude Code sessions are active, how
// busy they are, and what's waiting on you. It has its own LVGL screen, so
// while it's up the meters (and Clawd, parked by the director) cost nothing
// to draw. Positions and fonts come from the meters' layout (layout.h), so
// it follows the panel's shape the same way.

void sessions_page_init(void);                    // after ui_init()
void sessions_page_show(bool on);                 // its screen, or the meters' back
void sessions_page_update(const UsageData* d);    // `n` and `a` from a payload
void sessions_page_set_alert(const char* message);  // NULL: nothing waiting
void sessions_page_set_link(bool connected);
