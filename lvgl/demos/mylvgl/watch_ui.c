/**
 * @file watch_ui.c
 * @brief 智能手表界面公共核心模块。
 *
 * 主要职责：
 * - 初始化完整中文字体及备用字体链；
 * - 管理当前页面根对象和页面切换动画；
 * - 提供通用卡片、标签、按钮和圆弧控件；
 * - 统一调度首页、轮盘、课表和详情模块。
 */
#include "watch_ui.h"

#include "watch_carousel.h"
#include "watch_details.h"
#include "watch_home.h"
#include "watch_quick_settings.h"
#include "watch_schedule.h"
#include "watch_schedule_settings.h"

static lv_obj_t * active_root;
static lv_obj_t * brightness_overlay;
static lv_obj_t * screen_off_overlay;
static watch_view_t active_view = WATCH_VIEW_HOME;
static watch_app_t active_detail_app = WATCH_APP_SETTINGS;
static bool schedule_settings_page_active;
static watch_theme_t active_theme = WATCH_THEME_DARK;
static bool immediate_theme_refresh;
static uint8_t brightness_percent = 80;
static uint8_t sensitivity_level = 1;
static uint8_t crown_sensitivity_level = 1;
static bool wifi_connected = true;
static bool vibration_enabled = true;

static lv_font_t cn_font_primary;
static lv_font_t cn_font_secondary;
static lv_font_t * cn_font_runtime;
static lv_font_t * heart_font_runtime;
static const lv_font_t * cn_font = &cn_font_primary;
static const lv_font_t * heart_font = &lv_font_montserrat_48;

/* ---------------------------- 字体管理 ---------------------------- */

static void init_fonts(void)
{
    cn_font_secondary = lv_font_simsun_16_cjk;
    cn_font_secondary.fallback = &lv_font_montserrat_16;
    cn_font_primary = lv_font_source_han_sans_sc_16_cjk;
    cn_font_primary.fallback = &cn_font_secondary;

    cn_font_runtime = lv_tiny_ttf_create_file("A:assets/NotoSansSC-Watch.ttf", 16);
    if(cn_font_runtime != NULL) {
        cn_font_runtime->fallback = &cn_font_primary;
        cn_font = cn_font_runtime;
    }

    heart_font_runtime = lv_tiny_ttf_create_file("A:assets/NotoSansSC-Watch.ttf", 58);
    if(heart_font_runtime != NULL) {
        heart_font = heart_font_runtime;
    }
}

const lv_font_t * watch_ui_get_cn_font(void)
{
    return cn_font;
}

const lv_font_t * watch_ui_get_heart_font(void)
{
    return heart_font;
}

/* ---------------------------- 主题管理 ---------------------------- */

uint32_t watch_ui_theme_pick(uint32_t dark_color, uint32_t light_color)
{
    return active_theme == WATCH_THEME_DARK ? dark_color : light_color;
}

watch_theme_t watch_ui_get_theme(void)
{
    return active_theme;
}

bool watch_ui_is_dark_theme(void)
{
    return active_theme == WATCH_THEME_DARK;
}

void watch_ui_set_theme(watch_theme_t theme)
{
    if(theme != WATCH_THEME_DARK && theme != WATCH_THEME_LIGHT) return;
    if(active_theme == theme) return;
    active_theme = theme;
    immediate_theme_refresh = true;

    /*
     * 主题变化后重建当前页面，使固定样式、动态图层和控件状态一次性
     * 使用同一套色板，避免局部换色造成视觉割裂。
     */
    if(active_view == WATCH_VIEW_HOME) {
        watch_ui_show_home();
    }
    else if(active_view == WATCH_VIEW_CAROUSEL) {
        watch_ui_show_carousel();
    }
    else if(schedule_settings_page_active) {
        watch_ui_show_schedule_settings();
    }
    else {
        watch_ui_show_detail(active_detail_app);
    }
}

/* -------------------------- 页面切换管理 -------------------------- */

static void page_slide_x_cb(void * obj, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)obj, value);
}

static lv_obj_t * begin_page(watch_view_t view)
{
    lv_obj_t * old_root = active_root;
    const bool animate_transition = !immediate_theme_refresh;
    immediate_theme_refresh = false;
    active_view = view;
    watch_home_deactivate();
    watch_carousel_deactivate();
    watch_quick_settings_deactivate();
    watch_schedule_deactivate();
    watch_schedule_settings_deactivate();
    brightness_overlay = NULL;

    active_root = lv_obj_create(lv_screen_active());
    lv_obj_set_size(active_root, WATCH_VIEWPORT_WIDTH, WATCH_VIEWPORT_HEIGHT);
    lv_obj_set_style_bg_color(active_root, lv_color_hex(WATCH_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(active_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(active_root, 0, 0);
    lv_obj_set_style_pad_all(active_root, 0, 0);
    lv_obj_clear_flag(active_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(active_root,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_PRESS_LOCK);

    if(animate_transition) {
        lv_obj_set_x(active_root, 16);
        lv_obj_fade_in(active_root, 220, 0);

        lv_anim_t slide;
        lv_anim_init(&slide);
        lv_anim_set_var(&slide, active_root);
        lv_anim_set_exec_cb(&slide, page_slide_x_cb);
        lv_anim_set_values(&slide, 16, 0);
        lv_anim_set_duration(&slide, 230);
        lv_anim_set_path_cb(&slide, lv_anim_path_ease_out);
        lv_anim_start(&slide);
    }
    else {
        lv_obj_set_x(active_root, 0);
    }

    if(old_root != NULL) {
        lv_obj_clear_flag(old_root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
        if(animate_transition) {
            lv_obj_fade_out(old_root, 170, 0);
            lv_obj_delete_delayed(old_root, 240);
        }
        else {
            /*
             * 主题切换发生在旧页面按钮事件内，不能立即删除事件对象；
             * 延迟一个 LVGL 周期回收即可，视觉上不执行任何转场。
             */
            lv_obj_delete_delayed(old_root, 1);
        }
    }

    return active_root;
}

/* -------------------------- 显示与交互设置 -------------------------- */

static void apply_brightness(lv_obj_t * root)
{
    brightness_overlay = lv_obj_create(root);
    lv_obj_set_size(brightness_overlay, WATCH_VIEWPORT_WIDTH, WATCH_VIEWPORT_HEIGHT);
    lv_obj_set_pos(brightness_overlay, 0, 0);
    lv_obj_set_style_bg_color(brightness_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(brightness_overlay,
                            (lv_opa_t)((100U - brightness_percent) * 2U), 0);
    lv_obj_set_style_border_width(brightness_overlay, 0, 0);
    lv_obj_set_style_pad_all(brightness_overlay, 0, 0);
    lv_obj_clear_flag(brightness_overlay,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_move_foreground(brightness_overlay);
}

uint8_t watch_ui_get_brightness(void)
{
    return brightness_percent;
}

void watch_ui_set_brightness(uint8_t percent)
{
    if(percent < 10U) percent = 10U;
    if(percent > 100U) percent = 100U;
    brightness_percent = percent;
    if(brightness_overlay != NULL) {
        lv_obj_set_style_bg_opa(brightness_overlay,
                                (lv_opa_t)((100U - brightness_percent) * 2U), 0);
        lv_obj_move_foreground(brightness_overlay);
    }
}

uint8_t watch_ui_get_sensitivity(void)
{
    return sensitivity_level;
}

void watch_ui_set_sensitivity(uint8_t level)
{
    sensitivity_level = level > 2U ? 2U : level;
}

bool watch_ui_get_wifi_connected(void)
{
    return wifi_connected;
}

void watch_ui_set_wifi_connected(bool connected)
{
    wifi_connected = connected;
}

bool watch_ui_get_vibration_enabled(void)
{
    return vibration_enabled;
}

void watch_ui_set_vibration_enabled(bool enabled)
{
    vibration_enabled = enabled;
}

uint8_t watch_ui_get_crown_sensitivity(void)
{
    return crown_sensitivity_level;
}

void watch_ui_set_crown_sensitivity(uint8_t level)
{
    crown_sensitivity_level = level > 2U ? 2U : level;
}

int16_t watch_ui_get_swipe_threshold(void)
{
    static const int16_t thresholds[] = { 20, 28, 36 };
    return thresholds[sensitivity_level];
}

static void screen_wake_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    if(screen_off_overlay == NULL) return;
    lv_obj_t * overlay = screen_off_overlay;
    screen_off_overlay = NULL;
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_fade_out(overlay, 180, 0);
    lv_obj_delete_delayed(overlay, 190);
}

void watch_ui_turn_screen_off(void)
{
    if(screen_off_overlay != NULL) return;

    screen_off_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(screen_off_overlay, WATCH_VIEWPORT_WIDTH, WATCH_VIEWPORT_HEIGHT);
    lv_obj_set_pos(screen_off_overlay, 0, 0);
    lv_obj_set_style_bg_color(screen_off_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_off_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen_off_overlay, 0, 0);
    lv_obj_set_style_pad_all(screen_off_overlay, 0, 0);
    lv_obj_clear_flag(screen_off_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen_off_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(screen_off_overlay, screen_wake_cb, LV_EVENT_CLICKED, NULL);
    watch_ui_make_label(screen_off_overlay, "屏幕已关闭", watch_ui_get_cn_font(),
                        lv_color_hex(0x5A6470), LV_ALIGN_CENTER, 0, -8);
    watch_ui_make_label(screen_off_overlay, "点击唤醒", watch_ui_get_cn_font(),
                        lv_color_hex(0x343B44), LV_ALIGN_CENTER, 0, 18);
    lv_obj_fade_in(screen_off_overlay, 180, 0);
    lv_obj_move_foreground(screen_off_overlay);
}

watch_view_t watch_ui_get_view(void)
{
    return active_view;
}

lv_obj_t * watch_ui_get_active_root(void)
{
    return active_root;
}

void watch_ui_show_home(void)
{
    schedule_settings_page_active = false;
    lv_obj_t * root = begin_page(WATCH_VIEW_HOME);
    watch_home_build(root);
    apply_brightness(root);
}

void watch_ui_show_carousel(void)
{
    schedule_settings_page_active = false;
    lv_obj_t * root = begin_page(WATCH_VIEW_CAROUSEL);
    watch_carousel_build(root);
    apply_brightness(root);
}

void watch_ui_show_detail(watch_app_t app)
{
    active_detail_app = app;
    schedule_settings_page_active = false;
    lv_obj_t * root = begin_page(WATCH_VIEW_DETAIL);
    if(app == WATCH_APP_SCHEDULE) watch_schedule_build(root);
    else watch_details_build(root, app);
    apply_brightness(root);
}

void watch_ui_show_schedule_settings(void)
{
    schedule_settings_page_active = true;
    lv_obj_t * root = begin_page(WATCH_VIEW_DETAIL);
    watch_schedule_settings_build(root);
    apply_brightness(root);
}

/* -------------------------- 通用界面控件 -------------------------- */

lv_obj_t * watch_ui_make_label(lv_obj_t * parent, const char * text, const lv_font_t * font,
                               lv_color_t color, lv_align_t align, int16_t x, int16_t y)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, align, x, y);
    return label;
}

lv_obj_t * watch_ui_make_card(lv_obj_t * parent, int16_t width, int16_t height, lv_color_t color)
{
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_bg_color(card, color, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    return card;
}

lv_obj_t * watch_ui_round_button(lv_obj_t * parent, int16_t size, lv_color_t color,
                                 const char * icon, lv_event_cb_t callback, intptr_t id)
{
    lv_obj_t * button = watch_ui_make_card(parent, size, size, color);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    if(callback != NULL) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, (void *)id);
    watch_ui_make_label(button, icon, &lv_font_montserrat_24,
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_CENTER, 0, 0);
    return button;
}

void watch_ui_add_arc(lv_obj_t * parent, int16_t size, int16_t width, int32_t value,
                      lv_color_t indicator, int16_t x, int16_t y)
{
    lv_obj_t * arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_set_pos(arc, x, y);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_value(arc, value);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(WATCH_COLOR_SURFACE2), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, indicator, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
}

static void header_back_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    watch_ui_show_carousel();
}

void watch_ui_add_detail_header(lv_obj_t * root, const char * title)
{
    lv_obj_t * back = watch_ui_round_button(root, 36, lv_color_hex(WATCH_COLOR_SURFACE),
                                            "<", header_back_cb, 0);
    lv_obj_set_pos(back, 14, 13);
    watch_ui_make_label(root, title, watch_ui_get_cn_font(), lv_color_hex(WATCH_COLOR_TEXT),
                        LV_ALIGN_TOP_MID, 0, 20);
}

/* ---------------------------- 模块启动 ---------------------------- */

void watch_ui_init(void)
{
    init_fonts();
    watch_home_init();
    watch_ui_show_home();
}
