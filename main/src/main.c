/**
 * @file main.c
 * @brief 桌面模拟器主程序。
 *
 * 主程序只负责初始化 LVGL 和桌面硬件抽象层，
 * 完整手表界面由 watch_app_start() 汇总接口启动。
 */
#define _DEFAULT_SOURCE
#define WATCH_LCD_WIDTH  240
#define WATCH_LCD_HEIGHT 280

#include <unistd.h>

#include "lvgl/lvgl.h"
#include "lvgl/demos/mylvgl/watch_app.h"
#include LV_SDL_INCLUDE_PATH

static lv_display_t * hal_init(int32_t width, int32_t height);
static lv_obj_t * cursor_obj;

extern void freertos_main(void);

int main(int argc, char ** argv)
{
    (void)argc;
    (void)argv;

    /* 初始化 LVGL 和 240×280 桌面模拟器。 */
    lv_init();
    hal_init(WATCH_LCD_WIDTH, WATCH_LCD_HEIGHT);

    /* 通过唯一汇总接口启动完整手表应用。 */
    watch_app_start();
    lv_obj_move_foreground(cursor_obj);

#if LV_USE_OS == LV_OS_NONE
    while(1) {
        /* 周期性处理 LVGL 的刷新、输入和动画任务。 */
        const uint32_t timer_period = lv_timer_handler();
        usleep(timer_period * 1000);
    }
#elif LV_USE_OS == LV_OS_FREERTOS
    /* 启动 FreeRTOS 下的 LVGL 任务。 */
    freertos_main();
#endif

    return 0;
}

/**
 * 初始化桌面显示、鼠标、表冠滚轮和键盘输入设备。
 */
static lv_display_t * hal_init(int32_t width, int32_t height)
{
    lv_group_set_default(lv_group_create());

    lv_display_t * display = lv_sdl_window_create(width, height);

    lv_indev_t * mouse = lv_sdl_mouse_create();
    lv_indev_set_group(mouse, lv_group_get_default());
    lv_indev_set_display(mouse, display);
    lv_display_set_default(display);

    /* 创建并绑定桌面模拟器鼠标指针。 */
    LV_IMAGE_DECLARE(mouse_cursor_icon);
    cursor_obj = lv_image_create(lv_screen_active());
    lv_image_set_src(cursor_obj, &mouse_cursor_icon);
    lv_indev_set_cursor(mouse, cursor_obj);

    lv_indev_t * mousewheel = lv_sdl_mousewheel_create();
    lv_indev_set_display(mousewheel, display);
    lv_indev_set_group(mousewheel, lv_group_get_default());

    lv_indev_t * keyboard = lv_sdl_keyboard_create();
    lv_indev_set_display(keyboard, display);
    lv_indev_set_group(keyboard, lv_group_get_default());

    return display;
}
