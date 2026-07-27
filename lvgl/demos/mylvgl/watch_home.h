/**
 * @file watch_home.h
 * 智能手表首页表盘模块接口。
 */
#ifndef WATCH_HOME_H
#define WATCH_HOME_H

#include "watch_ui.h"

void watch_home_init(void);
void watch_home_build(lv_obj_t * root);
void watch_home_deactivate(void);

#endif /* WATCH_HOME_H */
