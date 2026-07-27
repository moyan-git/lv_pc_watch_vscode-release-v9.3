/**
 * @file watch_ui.h
 * 智能手表界面的公共类型、颜色、控件和导航接口。
 */
#ifndef WATCH_UI_H
#define WATCH_UI_H

#include "lvgl.h"
#include <stdint.h>

typedef enum {
    WATCH_APP_HEART_RATE,
    WATCH_APP_SPO2,
    WATCH_APP_ENVIRONMENT,
    WATCH_APP_STEPS,
    WATCH_APP_POSTURE,
    WATCH_APP_SCHEDULE,
    WATCH_APP_MUSIC,
    WATCH_APP_SETTINGS,
} watch_app_t;

typedef enum {
    WATCH_VIEW_HOME,
    WATCH_VIEW_CAROUSEL,
    WATCH_VIEW_DETAIL,
} watch_view_t;

typedef enum {
    WATCH_THEME_DARK,
    WATCH_THEME_LIGHT,
} watch_theme_t;

/*
 * 公共主题色全部在运行时选择。深色模式采用蓝黑底与青紫高光，
 * 浅色模式采用暖白底、雾蓝表面与低饱和珊瑚色点缀。
 */
uint32_t watch_ui_theme_pick(uint32_t dark_color, uint32_t light_color);

#define WATCH_COLOR_BG       watch_ui_theme_pick(0x05070B, 0xF5F2EC)
#define WATCH_COLOR_SURFACE  watch_ui_theme_pick(0x111722, 0xFFFFFF)
#define WATCH_COLOR_SURFACE2 watch_ui_theme_pick(0x1A2230, 0xE8EEF4)
#define WATCH_COLOR_TEXT     watch_ui_theme_pick(0xF4F7FB, 0x172333)
#define WATCH_COLOR_MUTED    watch_ui_theme_pick(0x96A1B2, 0x667687)
#define WATCH_COLOR_BORDER   watch_ui_theme_pick(0x344255, 0xCBD6E0)
#define WATCH_COLOR_CYAN     watch_ui_theme_pick(0x33D9FF, 0x087F9C)
#define WATCH_COLOR_GREEN    watch_ui_theme_pick(0x53E28C, 0x238A5A)
#define WATCH_COLOR_RED      watch_ui_theme_pick(0xFF6174, 0xD9475D)
#define WATCH_COLOR_ORANGE   watch_ui_theme_pick(0xFFAA4C, 0xD86F2B)
#define WATCH_COLOR_PURPLE   watch_ui_theme_pick(0xB58CFF, 0x7857C2)
#define WATCH_COLOR_BLUE     watch_ui_theme_pick(0x5E9CFF, 0x3378C5)
#define WATCH_COLOR_DANGER_SURFACE watch_ui_theme_pick(0x61333B, 0xF8DDE1)
#define WATCH_COLOR_DANGER_TEXT    watch_ui_theme_pick(0xFFF2F2, 0x982C3D)

#define WATCH_VIEWPORT_WIDTH  240
#define WATCH_VIEWPORT_HEIGHT 280

void watch_ui_init(void);
void watch_ui_show_home(void);
void watch_ui_show_carousel(void);
void watch_ui_show_detail(watch_app_t app);
void watch_ui_show_schedule_settings(void);

watch_view_t watch_ui_get_view(void);
lv_obj_t * watch_ui_get_active_root(void);
const lv_font_t * watch_ui_get_cn_font(void);
const lv_font_t * watch_ui_get_heart_font(void);
watch_theme_t watch_ui_get_theme(void);
bool watch_ui_is_dark_theme(void);
void watch_ui_set_theme(watch_theme_t theme);
uint8_t watch_ui_get_brightness(void);
void watch_ui_set_brightness(uint8_t percent);
uint8_t watch_ui_get_sensitivity(void);
void watch_ui_set_sensitivity(uint8_t level);
bool watch_ui_get_wifi_connected(void);
void watch_ui_set_wifi_connected(bool connected);
bool watch_ui_get_vibration_enabled(void);
void watch_ui_set_vibration_enabled(bool enabled);
uint8_t watch_ui_get_crown_sensitivity(void);
void watch_ui_set_crown_sensitivity(uint8_t level);
int16_t watch_ui_get_swipe_threshold(void);
void watch_ui_turn_screen_off(void);

lv_obj_t * watch_ui_make_label(lv_obj_t * parent, const char * text, const lv_font_t * font,
                               lv_color_t color, lv_align_t align, int16_t x, int16_t y);
lv_obj_t * watch_ui_make_card(lv_obj_t * parent, int16_t width, int16_t height, lv_color_t color);
lv_obj_t * watch_ui_round_button(lv_obj_t * parent, int16_t size, lv_color_t color,
                                 const char * icon, lv_event_cb_t callback, intptr_t id);
void watch_ui_add_arc(lv_obj_t * parent, int16_t size, int16_t width, int32_t value,
                      lv_color_t indicator, int16_t x, int16_t y);
void watch_ui_add_detail_header(lv_obj_t * root, const char * title);

#endif /* WATCH_UI_H */
