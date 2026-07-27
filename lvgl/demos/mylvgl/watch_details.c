/**
 * @file watch_details.c
 * @brief 健康、环境、运动、音乐和设置详情模块。
 *
 * 主要职责：
 * - 显示心率、血氧、环境、步数和姿态数据；
 * - 显示音乐播放器和设置卡片；
 * - 处理普通详情页的左右滑动返回手势。
 *
 * 当前桌面演示中的数值为模拟数据，接入硬件后可在本模块替换为传感器数据。
 */
#include "watch_details.h"
#include "watch_music.h"

static lv_point_t swipe_start;
static bool swipe_tracking;
static lv_obj_t * full_settings_list;
static bool restore_theme_scroll_position;

/* ---------------------------- 公共布局 ---------------------------- */

static void metric_page(lv_obj_t * root, const char * title, const char * icon,
                        const char * value, const char * unit, const char * status,
                        lv_color_t color, int32_t progress)
{
    watch_ui_add_detail_header(root, title);
    lv_obj_t * circle = watch_ui_make_card(root, 164, 164,
                                           lv_color_hex(WATCH_COLOR_SURFACE));
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 60);
    watch_ui_add_arc(circle, 148, 11, progress, color, 8, 8);
    watch_ui_make_label(circle, icon, &lv_font_montserrat_28, color,
                        LV_ALIGN_CENTER, 0, -37);
    watch_ui_make_label(circle, value, &lv_font_montserrat_38,
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_CENTER, 0, -4);
    watch_ui_make_label(circle, unit, &lv_font_montserrat_14,
                        lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_CENTER, 0, 32);

    lv_obj_t * status_card = watch_ui_make_card(root, 190, 46,
                                                lv_color_hex(WATCH_COLOR_SURFACE));
    lv_obj_align(status_card, LV_ALIGN_BOTTOM_MID, 0, -17);
    watch_ui_make_label(status_card, status, watch_ui_get_cn_font(), color,
                        LV_ALIGN_CENTER, 0, 0);
}

/* -------------------------- 健康与运动页面 -------------------------- */

static void heart_scale_anim_cb(void * object, int32_t value)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)object, value, 0);
}

static void start_heartbeat_animation(lv_obj_t * heart)
{
    /*
     * 115 ms 收缩、165 ms 回弹、550 ms 间隔，总周期约830 ms，
     * 对应约72 BPM，并保留真实心跳快速有力、回落稍慢的节奏。
     */
    lv_anim_t pulse;
    lv_anim_init(&pulse);
    lv_anim_set_var(&pulse, heart);
    lv_anim_set_exec_cb(&pulse, heart_scale_anim_cb);
    lv_anim_set_values(&pulse, 256, 292);
    lv_anim_set_duration(&pulse, 115);
    lv_anim_set_reverse_duration(&pulse, 165);
    lv_anim_set_repeat_delay(&pulse, 550);
    lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&pulse, lv_anim_path_ease_out);
    lv_anim_start(&pulse);
}

static void heart_rate_page(lv_obj_t * root)
{
    watch_ui_add_detail_header(root, "心率");

    lv_obj_t * panel = watch_ui_make_card(root, 204, 202,
                                          lv_color_hex(WATCH_COLOR_SURFACE));
    lv_obj_set_pos(panel, 18, 60);
    lv_obj_set_style_radius(panel, 30, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(WATCH_COLOR_BORDER), 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);

    watch_ui_make_label(panel, "实时监测", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t * heart = watch_ui_make_card(panel, 72, 72,
                                          lv_color_hex(WATCH_COLOR_RED));
    lv_obj_set_pos(heart, 66, 32);
    lv_obj_set_style_bg_opa(heart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_transform_pivot_x(heart, 36, 0);
    lv_obj_set_style_transform_pivot_y(heart, 36, 0);
    lv_obj_add_flag(heart, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(heart, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

    /* 使用标准实心心形字形，避免多个几何控件拼接产生歪斜和接缝。 */
    watch_ui_make_label(heart, "\xE2\x99\xA5", watch_ui_get_heart_font(),
                        lv_color_hex(WATCH_COLOR_RED), LV_ALIGN_CENTER, 0, -2);

    watch_ui_make_label(panel, "72", &lv_font_montserrat_34,
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_TOP_MID, 0, 103);
    watch_ui_make_label(panel, "BPM", &lv_font_montserrat_12,
                        lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_TOP_MID, 0, 140);

    lv_obj_t * status = watch_ui_make_card(panel, 168, 34,
                                           lv_color_hex(WATCH_COLOR_SURFACE2));
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_radius(status, 17, 0);
    watch_ui_make_label(status, "静息心率正常", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_RED), LV_ALIGN_CENTER, 0, 0);

    start_heartbeat_animation(heart);
}

static void environment_page(lv_obj_t * root)
{
    watch_ui_add_detail_header(root, "环境温湿度");

    lv_obj_t * temperature = watch_ui_make_card(root, 98, 130,
                                                lv_color_hex(WATCH_COLOR_SURFACE));
    lv_obj_set_pos(temperature, 18, 72);
    watch_ui_make_label(temperature, "温度", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_TOP_MID, 0, 14);
    watch_ui_make_label(temperature, "24.6", &lv_font_montserrat_30,
                        lv_color_hex(WATCH_COLOR_ORANGE), LV_ALIGN_CENTER, 0, -8);
    watch_ui_make_label(temperature, "°C", &lv_font_montserrat_14,
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_BOTTOM_MID, 0, -17);

    lv_obj_t * humidity = watch_ui_make_card(root, 98, 130,
                                             lv_color_hex(WATCH_COLOR_SURFACE));
    lv_obj_set_pos(humidity, 124, 72);
    watch_ui_make_label(humidity, "湿度", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_TOP_MID, 0, 14);
    watch_ui_make_label(humidity, "58", &lv_font_montserrat_30,
                        lv_color_hex(WATCH_COLOR_CYAN), LV_ALIGN_CENTER, 0, -8);
    watch_ui_make_label(humidity, "%RH", &lv_font_montserrat_14,
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_BOTTOM_MID, 0, -17);

    lv_obj_t * note = watch_ui_make_card(root, 204, 58,
                                         lv_color_hex(WATCH_COLOR_SURFACE2));
    lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -25);
    watch_ui_make_label(note, "舒适 · 空气质量良好", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_GREEN), LV_ALIGN_CENTER, 0, 0);
}

static void steps_page(lv_obj_t * root)
{
    watch_ui_add_detail_header(root, "今日步数");
    lv_obj_t * circle = watch_ui_make_card(root, 166, 166,
                                           lv_color_hex(WATCH_COLOR_SURFACE));
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 57);
    watch_ui_add_arc(circle, 148, 12, 74, lv_color_hex(WATCH_COLOR_GREEN), 9, 9);
    watch_ui_add_arc(circle, 123, 7, 53, lv_color_hex(WATCH_COLOR_ORANGE), 21, 21);
    watch_ui_make_label(circle, "7,452", &lv_font_montserrat_32,
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_CENTER, 0, -9);
    watch_ui_make_label(circle, "步 / 10,000", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_CENTER, 0, 27);

    lv_obj_t * calories = watch_ui_make_card(root, 94, 43,
                                             lv_color_hex(WATCH_COLOR_SURFACE));
    lv_obj_set_pos(calories, 18, 232);
    watch_ui_make_label(calories, "312 千卡", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_ORANGE), LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * distance = watch_ui_make_card(root, 94, 43,
                                             lv_color_hex(WATCH_COLOR_SURFACE));
    lv_obj_set_pos(distance, 128, 232);
    watch_ui_make_label(distance, "5.1 公里", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_GREEN), LV_ALIGN_CENTER, 0, 0);
}

static void posture_page(lv_obj_t * root)
{
    watch_ui_add_detail_header(root, "姿态显示");
    lv_obj_t * body = watch_ui_make_card(root, 190, 120,
                                         lv_color_hex(WATCH_COLOR_SURFACE));
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 70);
    watch_ui_make_label(body, "P", &lv_font_montserrat_44,
                        lv_color_hex(WATCH_COLOR_PURPLE), LV_ALIGN_CENTER, 0, -20);
    watch_ui_make_label(body, "姿态良好", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_CENTER, 0, 27);

    lv_obj_t * balance = watch_ui_make_card(root, 190, 56,
                                            lv_color_hex(WATCH_COLOR_SURFACE2));
    lv_obj_align(balance, LV_ALIGN_BOTTOM_MID, 0, -28);
    watch_ui_make_label(balance, "身体平衡  96%", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_GREEN), LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t * bar = lv_bar_create(balance);
    lv_obj_set_size(bar, 140, 6);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -9);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 96, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(WATCH_COLOR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(WATCH_COLOR_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_PART_INDICATOR);
}

/* -------------------------- 音乐与设置页面 -------------------------- */

static void schedule_settings_clicked_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    watch_ui_show_schedule_settings();
}

static const char * setting_level_text(uint8_t level)
{
    static const char * levels[] = { "灵敏", "标准", "稳健" };
    return levels[level > 2U ? 2U : level];
}

static void full_brightness_changed_cb(lv_event_t * event)
{
    lv_obj_t * slider = lv_event_get_target_obj(event);
    lv_obj_t * value_label = lv_event_get_user_data(event);
    const uint8_t value = (uint8_t)lv_slider_get_value(slider);
    watch_ui_set_brightness(value);
    if(value_label != NULL) lv_label_set_text_fmt(value_label, "%u%%", value);
}

static void full_wifi_changed_cb(lv_event_t * event)
{
    lv_obj_t * wifi_switch = lv_event_get_target_obj(event);
    lv_obj_t * status_label = lv_event_get_user_data(event);
    const bool connected = lv_obj_has_state(wifi_switch, LV_STATE_CHECKED);
    watch_ui_set_wifi_connected(connected);
    if(status_label != NULL) {
        lv_label_set_text(status_label, connected ? "已连接" : "未连接");
        lv_obj_set_style_text_color(
            status_label,
            lv_color_hex(connected ? WATCH_COLOR_GREEN : WATCH_COLOR_MUTED), 0);
    }
}

static void full_sensitivity_clicked_cb(lv_event_t * event)
{
    lv_obj_t * value_label = lv_event_get_user_data(event);
    const uint8_t level = (uint8_t)((watch_ui_get_sensitivity() + 1U) % 3U);
    watch_ui_set_sensitivity(level);
    if(value_label != NULL) lv_label_set_text(value_label, setting_level_text(level));
}

static void full_vibration_changed_cb(lv_event_t * event)
{
    lv_obj_t * vibration_switch = lv_event_get_target_obj(event);
    lv_obj_t * status_label = lv_event_get_user_data(event);
    const bool enabled = lv_obj_has_state(vibration_switch, LV_STATE_CHECKED);
    watch_ui_set_vibration_enabled(enabled);
    if(status_label != NULL) {
        lv_label_set_text(status_label, enabled ? "开启" : "关闭");
        lv_obj_set_style_text_color(
            status_label,
            lv_color_hex(enabled ? WATCH_COLOR_GREEN : WATCH_COLOR_MUTED), 0);
    }
}

static void crown_sensitivity_clicked_cb(lv_event_t * event)
{
    lv_obj_t * value_label = lv_event_get_user_data(event);
    const uint8_t level = (uint8_t)((watch_ui_get_crown_sensitivity() + 1U) % 3U);
    watch_ui_set_crown_sensitivity(level);
    if(value_label != NULL) lv_label_set_text(value_label, setting_level_text(level));
}

static void full_screen_off_clicked_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    watch_ui_turn_screen_off();
}

static void apply_theme_after_input(void * user_data)
{
    const watch_theme_t theme = (watch_theme_t)(uintptr_t)user_data;
    watch_ui_set_theme(theme);
}

static void theme_pressed_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    if(full_settings_list == NULL || !lv_obj_is_valid(full_settings_list)) return;

    /*
     * 主题开关位于列表第一项，切换后应固定显示列表顶部。
     * 不采用 LVGL 自动计算的焦点位置，避免亮度卡片被滚到顶部。
    */
    lv_obj_stop_scroll_anim(full_settings_list);
}

static void theme_changed_cb(lv_event_t * event)
{
    lv_obj_t * theme_switch = lv_event_get_target_obj(event);
    const watch_theme_t theme = lv_obj_has_state(theme_switch, LV_STATE_CHECKED) ?
                                WATCH_THEME_LIGHT : WATCH_THEME_DARK;

    /*
     * 等待本次触摸彻底释放，并在下一个 LVGL 周期应用主题。
     * 避免回调内立即重建页面后，新列表继承当前触摸而产生自动滚动。
     */
    lv_indev_t * indev = lv_indev_active();
    if(indev != NULL) lv_indev_wait_release(indev);
    if(full_settings_list != NULL && lv_obj_is_valid(full_settings_list)) {
        lv_obj_stop_scroll_anim(full_settings_list);
        lv_obj_scroll_to_y(full_settings_list, 0, LV_ANIM_OFF);
    }
    restore_theme_scroll_position = true;
    lv_async_call(apply_theme_after_input, (void *)(uintptr_t)theme);
}

static lv_obj_t * make_full_setting_card(lv_obj_t * list, int16_t y, int16_t height,
                                         uint32_t color)
{
    lv_obj_t * card = watch_ui_make_card(list, 204, height, lv_color_hex(color));
    lv_obj_set_pos(card, 0, y);
    lv_obj_set_style_radius(card, 14, 0);
    return card;
}

static void settings_page(lv_obj_t * root)
{
    watch_ui_add_detail_header(root, "设置");

    lv_obj_t * list = lv_obj_create(root);
    full_settings_list = list;
    lv_obj_set_size(list, 220, 224);
    lv_obj_set_pos(list, 10, 56);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_left(list, 8, 0);
    lv_obj_set_style_pad_right(list, 8, 0);
    lv_obj_set_style_pad_top(list, 0, 0);
    lv_obj_set_style_pad_bottom(list, 8, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list, lv_color_hex(WATCH_COLOR_CYAN), LV_PART_SCROLLBAR);
    lv_obj_add_flag(list, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);

    lv_obj_t * theme = make_full_setting_card(list, 0, 52, WATCH_COLOR_SURFACE2);
    watch_ui_make_label(theme, "外观模式", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_LEFT_MID, 12, -9);
    watch_ui_make_label(theme, watch_ui_is_dark_theme() ? "深色" : "浅色",
                        watch_ui_get_cn_font(), lv_color_hex(WATCH_COLOR_MUTED),
                        LV_ALIGN_LEFT_MID, 12, 11);
    lv_obj_t * theme_switch = lv_switch_create(theme);
    lv_obj_set_size(theme_switch, 40, 22);
    lv_obj_align(theme_switch, LV_ALIGN_RIGHT_MID, -10, 0);
    if(!watch_ui_is_dark_theme()) lv_obj_add_state(theme_switch, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(theme_switch, lv_color_hex(WATCH_COLOR_CYAN),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_flag(theme_switch, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(theme_switch,
                      LV_OBJ_FLAG_SCROLL_ON_FOCUS | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(theme_switch, theme_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(theme_switch, theme_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * brightness = make_full_setting_card(list, 58, 62, WATCH_COLOR_SURFACE);
    watch_ui_make_label(brightness, "屏幕亮度", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_TOP_LEFT, 12, 7);
    lv_obj_t * brightness_value = watch_ui_make_label(
        brightness, "", &lv_font_montserrat_12, lv_color_hex(WATCH_COLOR_ORANGE),
        LV_ALIGN_TOP_RIGHT, -12, 9);
    lv_label_set_text_fmt(brightness_value, "%u%%", watch_ui_get_brightness());
    lv_obj_t * brightness_slider = lv_slider_create(brightness);
    lv_obj_set_size(brightness_slider, 178, 7);
    lv_obj_align(brightness_slider, LV_ALIGN_BOTTOM_MID, 0, -11);
    lv_slider_set_range(brightness_slider, 10, 100);
    lv_slider_set_value(brightness_slider, watch_ui_get_brightness(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(WATCH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(WATCH_COLOR_ORANGE),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(WATCH_COLOR_TEXT), LV_PART_KNOB);
    lv_obj_add_flag(brightness_slider, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(brightness_slider,
                      LV_OBJ_FLAG_SCROLL_ON_FOCUS | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(brightness_slider, full_brightness_changed_cb,
                        LV_EVENT_VALUE_CHANGED, brightness_value);

    lv_obj_t * wifi = make_full_setting_card(list, 126, 50, WATCH_COLOR_SURFACE);
    watch_ui_make_label(wifi, "Wi-Fi", &lv_font_montserrat_16,
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_LEFT_MID, 12, -9);
    lv_obj_t * wifi_status = watch_ui_make_label(
        wifi, watch_ui_get_wifi_connected() ? "已连接" : "未连接",
        watch_ui_get_cn_font(),
        lv_color_hex(watch_ui_get_wifi_connected() ? WATCH_COLOR_GREEN : WATCH_COLOR_MUTED),
        LV_ALIGN_LEFT_MID, 12, 10);
    lv_obj_t * wifi_switch = lv_switch_create(wifi);
    lv_obj_set_size(wifi_switch, 38, 21);
    lv_obj_align(wifi_switch, LV_ALIGN_RIGHT_MID, -10, 0);
    if(watch_ui_get_wifi_connected()) lv_obj_add_state(wifi_switch, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(wifi_switch, lv_color_hex(WATCH_COLOR_GREEN),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_flag(wifi_switch, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(wifi_switch,
                      LV_OBJ_FLAG_SCROLL_ON_FOCUS | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(wifi_switch, full_wifi_changed_cb,
                        LV_EVENT_VALUE_CHANGED, wifi_status);

    lv_obj_t * sensitivity = make_full_setting_card(list, 182, 50, WATCH_COLOR_SURFACE);
    lv_obj_add_flag(sensitivity, LV_OBJ_FLAG_CLICKABLE);
    watch_ui_make_label(sensitivity, "页面切换灵敏度", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t * sensitivity_value = watch_ui_make_label(
        sensitivity, setting_level_text(watch_ui_get_sensitivity()),
        watch_ui_get_cn_font(), lv_color_hex(WATCH_COLOR_CYAN),
        LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_event_cb(sensitivity, full_sensitivity_clicked_cb,
                        LV_EVENT_CLICKED, sensitivity_value);

    lv_obj_t * vibration = make_full_setting_card(list, 238, 50, WATCH_COLOR_SURFACE);
    watch_ui_make_label(vibration, "振动反馈", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t * vibration_status = watch_ui_make_label(
        vibration, watch_ui_get_vibration_enabled() ? "开启" : "关闭",
        watch_ui_get_cn_font(),
        lv_color_hex(watch_ui_get_vibration_enabled() ? WATCH_COLOR_GREEN : WATCH_COLOR_MUTED),
        LV_ALIGN_RIGHT_MID, -58, 0);
    lv_obj_t * vibration_switch = lv_switch_create(vibration);
    lv_obj_set_size(vibration_switch, 38, 21);
    lv_obj_align(vibration_switch, LV_ALIGN_RIGHT_MID, -10, 0);
    if(watch_ui_get_vibration_enabled()) {
        lv_obj_add_state(vibration_switch, LV_STATE_CHECKED);
    }
    lv_obj_set_style_bg_color(vibration_switch, lv_color_hex(WATCH_COLOR_GREEN),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_flag(vibration_switch, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(vibration_switch,
                      LV_OBJ_FLAG_SCROLL_ON_FOCUS | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(vibration_switch, full_vibration_changed_cb,
                        LV_EVENT_VALUE_CHANGED, vibration_status);

    lv_obj_t * crown = make_full_setting_card(list, 294, 50, WATCH_COLOR_SURFACE);
    lv_obj_add_flag(crown, LV_OBJ_FLAG_CLICKABLE);
    watch_ui_make_label(crown, "表冠灵敏度", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t * crown_value = watch_ui_make_label(
        crown, setting_level_text(watch_ui_get_crown_sensitivity()),
        watch_ui_get_cn_font(), lv_color_hex(WATCH_COLOR_CYAN),
        LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_event_cb(crown, crown_sensitivity_clicked_cb,
                        LV_EVENT_CLICKED, crown_value);

    lv_obj_t * schedule = make_full_setting_card(list, 350, 50, WATCH_COLOR_SURFACE2);
    lv_obj_set_style_border_width(schedule, 1, 0);
    lv_obj_set_style_border_color(schedule, lv_color_hex(WATCH_COLOR_BORDER), 0);
    lv_obj_add_flag(schedule, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(schedule, schedule_settings_clicked_cb, LV_EVENT_CLICKED, NULL);
    watch_ui_make_label(schedule, "电子课表", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_LEFT_MID, 14, 0);
    watch_ui_make_label(schedule, "管理 >", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_CYAN), LV_ALIGN_RIGHT_MID, -14, 0);

    lv_obj_t * screen_off =
        make_full_setting_card(list, 406, 44, WATCH_COLOR_DANGER_SURFACE);
    lv_obj_add_flag(screen_off, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen_off, full_screen_off_clicked_cb, LV_EVENT_CLICKED, NULL);
    watch_ui_make_label(screen_off, "关闭屏幕", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_DANGER_TEXT), LV_ALIGN_CENTER, 0, 0);

    if(restore_theme_scroll_position) {
        /*
         * 子控件全部完成布局后恢复旧位置，且明确关闭动画，避免主题切换
         * 造成列表回到顶部或继续执行旧的滚动惯性。
         */
        lv_obj_update_layout(list);
        lv_obj_stop_scroll_anim(list);
        lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
        restore_theme_scroll_position = false;
    }
}

/* ---------------------------- 详情导航 ---------------------------- */

static void detail_touch_cb(lv_event_t * event)
{
    lv_indev_t * indev = lv_indev_active();
    if(indev == NULL) return;

    const lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_PRESSED) {
        lv_obj_t * target = lv_event_get_target_obj(event);
        /*
         * 滑杆和开关需要独占拖动手势，禁止它们冒泡后被详情页识别为
         * 横向返回。页面卡片和空白区域仍保留左右滑动返回。
         */
        if(lv_obj_check_type(target, &lv_slider_class) ||
           lv_obj_check_type(target, &lv_switch_class)) {
            swipe_tracking = false;
            return;
        }
        lv_indev_get_point(indev, &swipe_start);
        swipe_tracking = true;
        return;
    }

    if((code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) || !swipe_tracking) return;
    swipe_tracking = false;

    lv_point_t end_point;
    lv_indev_get_point(indev, &end_point);
    const int16_t dx = end_point.x - swipe_start.x;
    const int16_t dy = end_point.y - swipe_start.y;
    const int16_t abs_dx = dx < 0 ? -dx : dx;
    const int16_t abs_dy = dy < 0 ? -dy : dy;
    if(abs_dx >= watch_ui_get_swipe_threshold() && abs_dx > abs_dy) {
        watch_ui_show_carousel();
    }
}

void watch_details_build(lv_obj_t * root, watch_app_t app)
{
    swipe_tracking = false;
    full_settings_list = NULL;
    if(app != WATCH_APP_MUSIC) {
        lv_obj_add_event_cb(root, detail_touch_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(root, detail_touch_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(root, detail_touch_cb, LV_EVENT_PRESS_LOST, NULL);
    }

    switch(app) {
        case WATCH_APP_HEART_RATE:
            heart_rate_page(root);
            break;
        case WATCH_APP_SPO2:
            metric_page(root, "血氧", "O2", "98", "% SpO2", "血氧状态正常",
                        lv_color_hex(WATCH_COLOR_BLUE), 98);
            break;
        case WATCH_APP_ENVIRONMENT:
            environment_page(root);
            break;
        case WATCH_APP_STEPS:
            steps_page(root);
            break;
        case WATCH_APP_POSTURE:
            posture_page(root);
            break;
        case WATCH_APP_MUSIC:
            watch_music_build(root);
            break;
        case WATCH_APP_SETTINGS:
            settings_page(root);
            break;
        case WATCH_APP_SCHEDULE:
            /* 课表页面由 watch_schedule.c 单独构建。 */
            break;
    }
}
