/**
 * @file watch_quick_settings.c
 * @brief 首页下拉快捷设置模块。
 *
 * 主要职责：
 * - 提供屏幕亮度滑杆并实时更新模拟屏幕亮度；
 * - 显示 Wi-Fi 网络名称和连接状态；
 * - 调节全局页面切换灵敏度；
 * - 快速切换深色与浅色模式；
 * - 提供关闭屏幕入口；
 * - 支持向上滑动收起设置面板。
 */
#include "watch_quick_settings.h"

#include <stdint.h>

static lv_obj_t * settings_panel;
static lv_obj_t * brightness_value_label;
static lv_obj_t * brightness_slider_obj;
static lv_obj_t * wifi_status_label;
static lv_obj_t * wifi_switch_obj;
static lv_obj_t * sensitivity_value_label;
static lv_point_t panel_swipe_start;
static bool panel_swipe_tracking;
static bool panel_dragged_since_press;
static uint8_t brightness_before_press;
static uint8_t sensitivity_before_press;
static bool wifi_before_press;

static void open_settings_panel(lv_obj_t * root, bool animate);

/* ---------------------------- 面板动画 ---------------------------- */

static void panel_y_anim_cb(void * object, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)object, value);
}

static void close_panel(void)
{
    if(settings_panel == NULL) return;

    lv_obj_t * panel = settings_panel;
    settings_panel = NULL;
    panel_swipe_tracking = false;
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, panel);
    lv_anim_set_exec_cb(&animation, panel_y_anim_cb);
    lv_anim_set_values(&animation, 0, -WATCH_VIEWPORT_HEIGHT);
    lv_anim_set_duration(&animation, 220);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in);
    lv_anim_start(&animation);
    lv_obj_fade_out(panel, 180, 40);
    lv_obj_delete_delayed(panel, 230);
}

static void close_button_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    if(panel_dragged_since_press) return;
    close_panel();
}

/* ---------------------------- 设置控制 ---------------------------- */

static void brightness_changed_cb(lv_event_t * event)
{
    lv_obj_t * slider = lv_event_get_target_obj(event);
    if(panel_dragged_since_press) {
        lv_slider_set_value(slider, watch_ui_get_brightness(), LV_ANIM_OFF);
        return;
    }
    const uint8_t value = (uint8_t)lv_slider_get_value(slider);
    watch_ui_set_brightness(value);
    if(brightness_value_label != NULL) {
        lv_label_set_text_fmt(brightness_value_label, "%u%%", value);
    }
}

static void wifi_changed_cb(lv_event_t * event)
{
    lv_obj_t * wifi_switch = lv_event_get_target_obj(event);
    if(panel_dragged_since_press) {
        if(watch_ui_get_wifi_connected()) lv_obj_add_state(wifi_switch, LV_STATE_CHECKED);
        else lv_obj_remove_state(wifi_switch, LV_STATE_CHECKED);
        return;
    }
    const bool connected = lv_obj_has_state(wifi_switch, LV_STATE_CHECKED);
    watch_ui_set_wifi_connected(connected);
    if(wifi_status_label != NULL) {
        lv_label_set_text(wifi_status_label, connected ? "已连接" : "未连接");
        lv_obj_set_style_text_color(
            wifi_status_label,
            lv_color_hex(connected ? WATCH_COLOR_GREEN : WATCH_COLOR_MUTED), 0);
    }
}

static const char * sensitivity_text(uint8_t level)
{
    static const char * names[] = { "灵敏", "标准", "稳健" };
    return names[level > 2U ? 2U : level];
}

static void sensitivity_clicked_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    if(panel_dragged_since_press) return;
    const uint8_t level = (uint8_t)((watch_ui_get_sensitivity() + 1U) % 3U);
    watch_ui_set_sensitivity(level);
    if(sensitivity_value_label != NULL) {
        lv_label_set_text(sensitivity_value_label, sensitivity_text(level));
    }
}

static void screen_off_clicked_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    if(panel_dragged_since_press) return;
    close_panel();
    watch_ui_turn_screen_off();
}

static void quick_theme_clicked_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    if(panel_dragged_since_press) return;
    const watch_theme_t theme = watch_ui_is_dark_theme() ?
                                WATCH_THEME_LIGHT : WATCH_THEME_DARK;

    /*
     * 全局主题切换会重建首页。重建后重新打开快捷面板，使用户仍停留在
     * 当前操作层，同时完整设置页会从同一主题状态读取最新值。
     */
    watch_ui_set_theme(theme);
    lv_obj_t * root = watch_ui_get_active_root();
    if(root != NULL && watch_ui_get_view() == WATCH_VIEW_HOME) {
        open_settings_panel(root, false);
    }
}

/* ---------------------------- 收起手势 ---------------------------- */

static void restore_settings_before_drag(void)
{
    watch_ui_set_brightness(brightness_before_press);
    if(brightness_slider_obj != NULL) {
        lv_slider_set_value(brightness_slider_obj, brightness_before_press, LV_ANIM_OFF);
    }
    if(brightness_value_label != NULL) {
        lv_label_set_text_fmt(brightness_value_label, "%u%%", brightness_before_press);
    }

    watch_ui_set_wifi_connected(wifi_before_press);
    if(wifi_switch_obj != NULL) {
        if(wifi_before_press) lv_obj_add_state(wifi_switch_obj, LV_STATE_CHECKED);
        else lv_obj_remove_state(wifi_switch_obj, LV_STATE_CHECKED);
    }
    if(wifi_status_label != NULL) {
        lv_label_set_text(wifi_status_label, wifi_before_press ? "已连接" : "未连接");
        lv_obj_set_style_text_color(
            wifi_status_label,
            lv_color_hex(wifi_before_press ? WATCH_COLOR_GREEN : WATCH_COLOR_MUTED), 0);
    }

    watch_ui_set_sensitivity(sensitivity_before_press);
    if(sensitivity_value_label != NULL) {
        lv_label_set_text(sensitivity_value_label, sensitivity_text(sensitivity_before_press));
    }

}

static void mark_panel_vertical_drag(void)
{
    if(panel_dragged_since_press) return;
    panel_dragged_since_press = true;
    restore_settings_before_drag();
}

static void panel_touch_cb(lv_event_t * event)
{
    lv_indev_t * indev = lv_indev_active();
    if(indev == NULL) return;

    const lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &panel_swipe_start);
        panel_swipe_tracking = true;
        panel_dragged_since_press = false;
        brightness_before_press = watch_ui_get_brightness();
        sensitivity_before_press = watch_ui_get_sensitivity();
        wifi_before_press = watch_ui_get_wifi_connected();
        return;
    }

    if(code == LV_EVENT_PRESSING && panel_swipe_tracking) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        const int16_t dx = point.x - panel_swipe_start.x;
        const int16_t dy = point.y - panel_swipe_start.y;
        const int16_t abs_dx = dx < 0 ? -dx : dx;
        const int16_t abs_dy = dy < 0 ? -dy : dy;
        if(abs_dy >= 8 && abs_dy > abs_dx) mark_panel_vertical_drag();
        return;
    }

    if((code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) ||
       !panel_swipe_tracking) return;
    panel_swipe_tracking = false;

    lv_point_t end_point;
    lv_indev_get_point(indev, &end_point);
    const int16_t dx = end_point.x - panel_swipe_start.x;
    const int16_t dy = end_point.y - panel_swipe_start.y;
    const int16_t abs_dx = dx < 0 ? -dx : dx;
    const int16_t abs_dy = dy < 0 ? -dy : dy;
    if(abs_dy >= 8 && abs_dy > abs_dx) mark_panel_vertical_drag();
    if(dy < 0 && abs_dy >= watch_ui_get_swipe_threshold() && abs_dy > abs_dx) {
        close_panel();
    }
}

/* ---------------------------- 页面布局 ---------------------------- */

static lv_obj_t * make_setting_card_sized(int16_t x, int16_t y,
                                          int16_t width, int16_t height)
{
    lv_obj_t * card = watch_ui_make_card(
        settings_panel, width, height,
        lv_color_hex(watch_ui_theme_pick(0x252B35, 0xFFFFFF)));
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_radius(card, 16, 0);
    return card;
}

static lv_obj_t * make_setting_card(int16_t y, int16_t height)
{
    return make_setting_card_sized(14, y, 212, height);
}

static lv_obj_t * make_white_icon_part(lv_obj_t * parent,
                                       int16_t width, int16_t height,
                                       int16_t x, int16_t y)
{
    lv_obj_t * part = watch_ui_make_card(parent, width, height, lv_color_white());
    lv_obj_set_pos(part, x, y);
    lv_obj_set_style_radius(part, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(part, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                            LV_OBJ_FLAG_EVENT_BUBBLE);
    return part;
}

/* 白色简笔太阳：空心圆和八条短射线。 */
static void add_sun_icon(lv_obj_t * button)
{
    lv_obj_t * center = watch_ui_make_card(button, 16, 16, lv_color_white());
    lv_obj_set_pos(center, 44, 17);
    lv_obj_set_style_bg_opa(center, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center, 2, 0);
    lv_obj_set_style_border_color(center, lv_color_white(), 0);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(center, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

    make_white_icon_part(button, 2, 6, 51, 5);
    make_white_icon_part(button, 2, 6, 51, 39);
    make_white_icon_part(button, 6, 2, 33, 24);
    make_white_icon_part(button, 6, 2, 65, 24);

    lv_obj_t * ray = make_white_icon_part(button, 2, 7, 39, 10);
    lv_obj_set_style_transform_rotation(ray, 3150, 0);
    ray = make_white_icon_part(button, 2, 7, 63, 10);
    lv_obj_set_style_transform_rotation(ray, 450, 0);
    ray = make_white_icon_part(button, 2, 7, 39, 33);
    lv_obj_set_style_transform_rotation(ray, 450, 0);
    ray = make_white_icon_part(button, 2, 7, 63, 33);
    lv_obj_set_style_transform_rotation(ray, 3150, 0);
}

/* 白色月牙：白色圆面叠加与按钮同色的偏移圆。 */
static void add_moon_icon(lv_obj_t * button, uint32_t button_color)
{
    lv_obj_t * moon = make_white_icon_part(button, 25, 25, 39, 12);
    lv_obj_set_style_shadow_width(moon, 9, 0);
    lv_obj_set_style_shadow_color(moon, lv_color_white(), 0);
    lv_obj_set_style_shadow_opa(moon, LV_OPA_30, 0);

    lv_obj_t * cutout = watch_ui_make_card(
        button, 22, 22, lv_color_hex(button_color));
    lv_obj_set_pos(cutout, 47, 7);
    lv_obj_set_style_radius(cutout, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(cutout, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
}

/*
 * 白色电源图标。
 * 不使用圆弧角度：先画完整圆环，再以 x=50 为轴切出等宽缺口，
 * 最后添加同轴竖杠，从几何上保证左右完全镜像。
 */
static void add_power_icon(lv_obj_t * button, uint32_t button_color)
{
    lv_obj_t * ring = watch_ui_make_card(button, 28, 28, lv_color_white());
    lv_obj_set_pos(ring, 36, 13);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_set_style_border_color(ring, lv_color_white(), 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

    /* 12像素缺口：x=44..55，中心线两侧各6像素。 */
    lv_obj_t * cutout = watch_ui_make_card(
        button, 12, 13, lv_color_hex(button_color));
    lv_obj_set_pos(cutout, 44, 7);
    lv_obj_set_style_radius(cutout, 0, 0);
    lv_obj_clear_flag(cutout, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

    /* 4像素竖杠：x=48..51，中心线两侧各2像素。 */
    make_white_icon_part(button, 4, 19, 48, 5);
}

static void open_settings_panel(lv_obj_t * root, bool animate)
{
    if(settings_panel != NULL) return;

    settings_panel = lv_obj_create(root);
    lv_obj_set_size(settings_panel, WATCH_VIEWPORT_WIDTH, WATCH_VIEWPORT_HEIGHT);
    lv_obj_set_pos(settings_panel, 0, animate ? -WATCH_VIEWPORT_HEIGHT : 0);
    lv_obj_set_style_bg_color(
        settings_panel, lv_color_hex(watch_ui_theme_pick(0x171B24, 0xF7F2EA)), 0);
    lv_obj_set_style_bg_grad_color(
        settings_panel, lv_color_hex(watch_ui_theme_pick(0x302936, 0xE3EDF3)), 0);
    lv_obj_set_style_bg_grad_dir(settings_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(settings_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(settings_panel, 0, 0);
    lv_obj_set_style_pad_all(settings_panel, 0, 0);
    lv_obj_clear_flag(settings_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(settings_panel, panel_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(settings_panel, panel_touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(settings_panel, panel_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(settings_panel, panel_touch_cb, LV_EVENT_PRESS_LOST, NULL);

    lv_obj_t * handle = watch_ui_make_card(settings_panel, 42, 5,
                                           lv_color_hex(WATCH_COLOR_TEXT));
    lv_obj_set_style_radius(handle, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(handle, close_button_cb, LV_EVENT_CLICKED, NULL);

    watch_ui_make_label(settings_panel, "快捷设置", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t * brightness_card = make_setting_card(43, 52);
    watch_ui_make_label(brightness_card, "亮度", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_TOP_LEFT, 13, 7);
    brightness_value_label = watch_ui_make_label(
        brightness_card, "80%", &lv_font_montserrat_12,
        lv_color_hex(WATCH_COLOR_ORANGE), LV_ALIGN_TOP_RIGHT, -13, 9);
    lv_label_set_text_fmt(brightness_value_label, "%u%%", watch_ui_get_brightness());

    brightness_slider_obj = lv_slider_create(brightness_card);
    lv_obj_set_size(brightness_slider_obj, 184, 7);
    lv_obj_align(brightness_slider_obj, LV_ALIGN_BOTTOM_MID, 0, -11);
    lv_slider_set_range(brightness_slider_obj, 10, 100);
    lv_slider_set_value(brightness_slider_obj, watch_ui_get_brightness(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightness_slider_obj,
                              lv_color_hex(WATCH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider_obj, lv_color_hex(WATCH_COLOR_ORANGE),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider_obj,
                              lv_color_hex(WATCH_COLOR_TEXT), LV_PART_KNOB);
    lv_obj_add_flag(brightness_slider_obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(brightness_slider_obj, brightness_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * wifi_card = make_setting_card(101, 46);
    watch_ui_make_label(wifi_card, "Wi-Fi", &lv_font_montserrat_16,
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_TOP_LEFT, 13, 7);
    watch_ui_make_label(wifi_card, "Campus_WiFi", &lv_font_montserrat_12,
                        lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_BOTTOM_LEFT, 13, -7);
    wifi_status_label = watch_ui_make_label(
        wifi_card, watch_ui_get_wifi_connected() ? "已连接" : "未连接",
        watch_ui_get_cn_font(),
        lv_color_hex(watch_ui_get_wifi_connected() ? WATCH_COLOR_GREEN : WATCH_COLOR_MUTED),
        LV_ALIGN_RIGHT_MID, -48, 0);

    wifi_switch_obj = lv_switch_create(wifi_card);
    lv_obj_set_size(wifi_switch_obj, 36, 20);
    lv_obj_align(wifi_switch_obj, LV_ALIGN_RIGHT_MID, -9, 0);
    if(watch_ui_get_wifi_connected()) lv_obj_add_state(wifi_switch_obj, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(wifi_switch_obj, lv_color_hex(WATCH_COLOR_GREEN),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_flag(wifi_switch_obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(wifi_switch_obj, wifi_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * sensitivity_card = make_setting_card(153, 44);
    lv_obj_add_flag(sensitivity_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sensitivity_card, sensitivity_clicked_cb, LV_EVENT_CLICKED, NULL);
    watch_ui_make_label(sensitivity_card, "页面切换灵敏度", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_LEFT_MID, 13, 0);
    sensitivity_value_label = watch_ui_make_label(
        sensitivity_card, sensitivity_text(watch_ui_get_sensitivity()), watch_ui_get_cn_font(),
        lv_color_hex(WATCH_COLOR_CYAN), LV_ALIGN_RIGHT_MID, -14, 0);

    const uint32_t theme_button_color =
        watch_ui_theme_pick(0x1B2734, 0x79AFC0);
    lv_obj_t * theme_card = watch_ui_make_card(
        settings_panel, 104, 50, lv_color_hex(theme_button_color));
    lv_obj_set_pos(theme_card, 14, 203);
    lv_obj_set_style_radius(theme_card, 16, 0);
    lv_obj_set_style_border_width(theme_card, 1, 0);
    lv_obj_set_style_border_color(theme_card, lv_color_white(), 0);
    lv_obj_set_style_border_opa(theme_card, LV_OPA_30, 0);
    lv_obj_add_flag(theme_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(theme_card, quick_theme_clicked_cb, LV_EVENT_CLICKED, NULL);
    if(watch_ui_is_dark_theme()) add_moon_icon(theme_card, theme_button_color);
    else add_sun_icon(theme_card);

    const uint32_t screen_button_color =
        watch_ui_theme_pick(0x61333B, 0xC85F70);
    lv_obj_t * screen_off = watch_ui_make_card(
        settings_panel, 100, 50, lv_color_hex(screen_button_color));
    lv_obj_set_pos(screen_off, 126, 203);
    lv_obj_set_style_radius(screen_off, 16, 0);
    lv_obj_set_style_border_width(screen_off, 1, 0);
    lv_obj_set_style_border_color(screen_off, lv_color_white(), 0);
    lv_obj_set_style_border_opa(screen_off, LV_OPA_30, 0);
    lv_obj_add_flag(screen_off, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen_off, screen_off_clicked_cb, LV_EVENT_CLICKED, NULL);
    add_power_icon(screen_off, screen_button_color);

    if(animate) {
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, settings_panel);
        lv_anim_set_exec_cb(&animation, panel_y_anim_cb);
        lv_anim_set_values(&animation, -WATCH_VIEWPORT_HEIGHT, 0);
        lv_anim_set_duration(&animation, 260);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_start(&animation);
        lv_obj_fade_in(settings_panel, 180, 0);
    }
    lv_obj_move_foreground(settings_panel);

    /* 重新将亮度遮罩置顶，使设置面板也体现当前亮度。 */
    watch_ui_set_brightness(watch_ui_get_brightness());
}

void watch_quick_settings_open(lv_obj_t * root)
{
    open_settings_panel(root, true);
}

bool watch_quick_settings_is_open(void)
{
    return settings_panel != NULL;
}

void watch_quick_settings_deactivate(void)
{
    settings_panel = NULL;
    brightness_value_label = NULL;
    brightness_slider_obj = NULL;
    wifi_status_label = NULL;
    wifi_switch_obj = NULL;
    sensitivity_value_label = NULL;
    panel_swipe_tracking = false;
    panel_dragged_since_press = false;
}
