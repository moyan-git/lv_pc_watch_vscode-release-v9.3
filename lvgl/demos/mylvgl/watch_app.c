/**
 * @file watch_app.c
 * @brief 智能手表应用汇总入口。
 *
 * 各功能分别由以下模块实现：
 * - watch_ui.c：公共控件、字体、页面动画和导航；
 * - watch_home.c：首页表盘；
 * - watch_quick_settings.c：首页下拉快捷设置；
 * - watch_carousel.c：旋转卡片菜单；
 * - watch_schedule.c：电子课表；
 * - watch_schedule_data.c：课表数据、日期换算和持久化；
 * - watch_schedule_settings.c：课表设置、课程编辑和屏幕键盘；
 * - watch_details.c：健康、运动、音乐和设置详情页。
 */
#include "watch_app.h"

#include "watch_schedule_data.h"
#include "watch_ui.h"

void watch_app_start(void)
{
    watch_schedule_data_init();
    watch_ui_init();
}
