#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_quick_panel_show(void);
void ui_quick_panel_hide(void);
void ui_quick_panel_close_now(void);
bool ui_quick_panel_is_open(void);
void ui_quick_panel_refresh(void);

#ifdef __cplusplus
}
#endif
