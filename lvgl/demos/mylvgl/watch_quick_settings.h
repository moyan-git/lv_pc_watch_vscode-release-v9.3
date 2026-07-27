/**
 * @file watch_quick_settings.h
 * 首页下拉快捷设置模块接口。
 */
#ifndef WATCH_QUICK_SETTINGS_H
#define WATCH_QUICK_SETTINGS_H

#include "watch_ui.h"

void watch_quick_settings_open(lv_obj_t * root);
void watch_quick_settings_deactivate(void);
bool watch_quick_settings_is_open(void);

#endif /* WATCH_QUICK_SETTINGS_H */
