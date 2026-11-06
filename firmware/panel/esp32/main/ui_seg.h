#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "panel.h"
void ui_seg_init(void);
void ui_seg_show(const ui_view_t *v, uint32_t now_ms);
void ui_seg_beep(uint32_t ms);
int  ui_seg_key(uint32_t now_ms, bool *long_press);   /* pkey_t or -1 */
