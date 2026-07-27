/**
 * @file watch_home.c
 * @brief 智能手表首页表盘模块。
 *
 * 主要职责：
 * - 显示时间、日期和天气信息；
 * - 提供流动极光、月夜星轨和量子核心三套动态壁纸；
 * - 定时刷新时间和动态背景；
 * - 处理左滑进入轮盘、右滑切换壁纸、上滑切换表盘、下滑打开快捷设置。
 */
#include "watch_home.h"

#include "watch_quick_settings.h"

#include <math.h>
#include <stdint.h>
#include <time.h>

#define HOME_WALLPAPER_COUNT 3U
#define HOME_WATCHFACE_COUNT 3U
#define HOME_GLOW_COUNT 3U
#define HOME_ARC_COUNT 3U
#define HOME_STAR_COUNT 14U
#define HOME_DIAL_CENTER 109
#define HOME_TWO_PI 6.2831853f

static lv_obj_t * time_label;
static lv_obj_t * date_label;
static lv_obj_t * heart_label;
static uint8_t displayed_heart_rate = 72;
static uint8_t background_index;
static uint8_t watchface_index;
static lv_point_t swipe_start;
static bool swipe_tracking;
static lv_timer_t * live_timer;
static lv_timer_t * wallpaper_timer;
static lv_obj_t * wallpaper_root;
static lv_obj_t * wallpaper_glows[HOME_GLOW_COUNT];
static lv_obj_t * wallpaper_arcs[HOME_ARC_COUNT];
static lv_obj_t * wallpaper_stars[HOME_STAR_COUNT];
static int16_t star_base_x[HOME_STAR_COUNT];
static int16_t star_base_y[HOME_STAR_COUNT];
static uint32_t wallpaper_phase;
static lv_obj_t * analog_hour_hand;
static lv_obj_t * analog_minute_hand;
static lv_obj_t * analog_second_hand;
static lv_point_precise_t analog_hour_points[2];
static lv_point_precise_t analog_minute_points[2];
static lv_point_precise_t analog_second_points[2];

/* ---------------------------- 实时数据 ---------------------------- */

static void time_strings(char * time_buf, size_t time_size, char * date_buf, size_t date_size)
{
    time_t now = time(NULL);
    struct tm local_now;
    localtime_s(&local_now, &now);
    strftime(time_buf, time_size, "%H:%M", &local_now);
    strftime(date_buf, date_size, "%m.%d  %a", &local_now);
}

static void set_hand_points(lv_obj_t * hand, lv_point_precise_t points[2],
                            float angle, int16_t length, int16_t tail)
{
    if(hand == NULL || !lv_obj_is_valid(hand)) return;

    points[0].x = (lv_value_precise_t)(HOME_DIAL_CENTER - (sinf(angle) * tail));
    points[0].y = (lv_value_precise_t)(HOME_DIAL_CENTER + (cosf(angle) * tail));
    points[1].x = (lv_value_precise_t)(HOME_DIAL_CENTER + (sinf(angle) * length));
    points[1].y = (lv_value_precise_t)(HOME_DIAL_CENTER - (cosf(angle) * length));
    lv_line_set_points_mutable(hand, points, 2);
    lv_obj_invalidate(hand);
}

static void update_analog_hands(const struct tm * local_now)
{
    if(analog_hour_hand == NULL || local_now == NULL) return;

    const float second_angle = ((float)local_now->tm_sec / 60.0f) * HOME_TWO_PI;
    const float minute_angle =
        (((float)local_now->tm_min + ((float)local_now->tm_sec / 60.0f)) / 60.0f) *
        HOME_TWO_PI;
    const float hour_angle =
        (((float)(local_now->tm_hour % 12) + ((float)local_now->tm_min / 60.0f)) /
         12.0f) * HOME_TWO_PI;

    set_hand_points(analog_hour_hand, analog_hour_points, hour_angle, 51, 6);
    set_hand_points(analog_minute_hand, analog_minute_points, minute_angle, 72, 8);
    set_hand_points(analog_second_hand, analog_second_points, second_angle, 83, 17);
}

static void update_live_data(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    static int8_t direction = 1;
    char time_buf[8];
    char date_buf[32];

    displayed_heart_rate = (uint8_t)(displayed_heart_rate + direction);
    if(displayed_heart_rate >= 76 || displayed_heart_rate <= 70) direction = -direction;

    time_t now = time(NULL);
    struct tm local_now;
    localtime_s(&local_now, &now);
    time_strings(time_buf, sizeof(time_buf), date_buf, sizeof(date_buf));
    if(time_label != NULL) lv_label_set_text(time_label, time_buf);
    if(date_label != NULL) lv_label_set_text(date_label, date_buf);
    if(heart_label != NULL) lv_label_set_text_fmt(heart_label, "%u BPM", displayed_heart_rate);
    update_analog_hands(&local_now);
}

/* ---------------------------- 动态壁纸 ---------------------------- */

static lv_obj_t * make_wallpaper_glow(lv_obj_t * root, int16_t size,
                                      uint32_t color, lv_opa_t opacity)
{
    lv_obj_t * glow = watch_ui_make_card(root, size, size, lv_color_hex(color));
    lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(glow, opacity, 0);
    lv_obj_clear_flag(glow, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                            LV_OBJ_FLAG_EVENT_BUBBLE);
    return glow;
}

static lv_obj_t * make_wallpaper_arc(lv_obj_t * root, int16_t size, int16_t width,
                                     uint32_t color, int16_t start, int16_t end)
{
    lv_obj_t * arc = lv_arc_create(root);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_bg_angles(arc, start, end);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(arc, LV_OPA_40, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                            LV_OBJ_FLAG_EVENT_BUBBLE);
    return arc;
}

static void create_wallpaper_stars(lv_obj_t * root, bool warm)
{
    static const int16_t x_positions[HOME_STAR_COUNT] =
        { 15, 39, 66, 91, 121, 151, 181, 214, 26, 57, 105, 143, 194, 224 };
    static const int16_t y_positions[HOME_STAR_COUNT] =
        { 28, 65, 19, 92, 35, 76, 18, 58, 222, 253, 234, 262, 216, 246 };

    for(uint8_t i = 0U; i < HOME_STAR_COUNT; i++) {
        star_base_x[i] = x_positions[i];
        star_base_y[i] = y_positions[i];
        const int16_t size = (int16_t)(2 + (i % 3U));
        wallpaper_stars[i] = watch_ui_make_card(
            root, size, size,
            lv_color_hex(
                warm && (i % 4U == 0U) ?
                watch_ui_theme_pick(0xFFE3A1, 0xB66B24) :
                watch_ui_theme_pick(0xD8F5FF, 0x456A7D)));
        lv_obj_set_pos(wallpaper_stars[i], star_base_x[i], star_base_y[i]);
        lv_obj_set_style_radius(wallpaper_stars[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_opa(wallpaper_stars[i], LV_OPA_70, 0);
        lv_obj_clear_flag(wallpaper_stars[i], LV_OBJ_FLAG_CLICKABLE |
                                               LV_OBJ_FLAG_EVENT_BUBBLE);
    }
}

static void build_aurora_wallpaper(lv_obj_t * root)
{
    lv_obj_set_style_bg_color(
        root, lv_color_hex(watch_ui_theme_pick(0x06111D, 0xF8F3EA)), 0);
    lv_obj_set_style_bg_grad_color(
        root, lv_color_hex(watch_ui_theme_pick(0x17283C, 0xDCECEF)), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);

    wallpaper_glows[0] = make_wallpaper_glow(root, 190, 0x1DD6C7, LV_OPA_20);
    wallpaper_glows[1] = make_wallpaper_glow(root, 172, 0x426BFF, LV_OPA_20);
    wallpaper_glows[2] = make_wallpaper_glow(root, 148, 0xB05CFF, LV_OPA_20);
    lv_obj_set_pos(wallpaper_glows[0], -92, -38);
    lv_obj_set_pos(wallpaper_glows[1], 116, 74);
    lv_obj_set_pos(wallpaper_glows[2], -30, 184);

    wallpaper_arcs[0] = make_wallpaper_arc(root, 286, 18, 0x37E8C9, 198, 334);
    wallpaper_arcs[1] = make_wallpaper_arc(root, 250, 13, 0x5A83FF, 205, 332);
    wallpaper_arcs[2] = make_wallpaper_arc(root, 214, 9, 0xC87CFF, 210, 326);
    lv_obj_set_pos(wallpaper_arcs[0], -55, -25);
    lv_obj_set_pos(wallpaper_arcs[1], 30, 54);
    lv_obj_set_pos(wallpaper_arcs[2], -42, 130);
    create_wallpaper_stars(root, false);
}

static void build_moon_wallpaper(lv_obj_t * root)
{
    lv_obj_set_style_bg_color(
        root, lv_color_hex(watch_ui_theme_pick(0x080B19, 0xF7F2FA)), 0);
    lv_obj_set_style_bg_grad_color(
        root, lv_color_hex(watch_ui_theme_pick(0x2A1D42, 0xDDE6F2)), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);

    wallpaper_glows[0] = make_wallpaper_glow(root, 104, 0x758CFF, LV_OPA_20);
    wallpaper_glows[1] = make_wallpaper_glow(root, 72, 0xFFF2C4, LV_OPA_90);
    wallpaper_glows[2] = make_wallpaper_glow(
        root, 54, watch_ui_theme_pick(0x10172E, 0xE1E8F1), LV_OPA_COVER);
    lv_obj_set_pos(wallpaper_glows[0], 137, 10);
    lv_obj_set_pos(wallpaper_glows[1], 154, 26);
    lv_obj_set_pos(wallpaper_glows[2], 135, 13);

    wallpaper_arcs[0] = make_wallpaper_arc(root, 270, 2, 0x758CFF, 185, 350);
    wallpaper_arcs[1] = make_wallpaper_arc(root, 224, 2, 0xB889FF, 192, 342);
    wallpaper_arcs[2] = make_wallpaper_arc(root, 178, 1, 0xD7C5FF, 205, 332);
    lv_obj_set_pos(wallpaper_arcs[0], -92, 74);
    lv_obj_set_pos(wallpaper_arcs[1], 68, 119);
    lv_obj_set_pos(wallpaper_arcs[2], -56, 170);
    create_wallpaper_stars(root, true);
}

static void build_quantum_wallpaper(lv_obj_t * root)
{
    lv_obj_set_style_bg_color(
        root, lv_color_hex(watch_ui_theme_pick(0x02060D, 0xF1F6F6)), 0);
    lv_obj_set_style_bg_grad_color(
        root, lv_color_hex(watch_ui_theme_pick(0x081B2F, 0xD6E6EB)), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);

    wallpaper_glows[0] = make_wallpaper_glow(root, 226, 0x00E5FF, LV_OPA_10);
    wallpaper_glows[1] = make_wallpaper_glow(root, 154, 0x1D6DFF, LV_OPA_20);
    wallpaper_glows[2] = make_wallpaper_glow(root, 84, 0xBE3CFF, LV_OPA_20);
    lv_obj_set_pos(wallpaper_glows[0], 7, 27);
    lv_obj_set_pos(wallpaper_glows[1], 43, 63);
    lv_obj_set_pos(wallpaper_glows[2], 78, 98);

    wallpaper_arcs[0] = make_wallpaper_arc(root, 274, 3, 0x00E7FF, 24, 146);
    wallpaper_arcs[1] = make_wallpaper_arc(root, 252, 2, 0x397BFF, 176, 318);
    wallpaper_arcs[2] = make_wallpaper_arc(root, 232, 2, 0xD154FF, 44, 236);
    lv_obj_set_pos(wallpaper_arcs[0], -17, 3);
    lv_obj_set_pos(wallpaper_arcs[1], -6, 14);
    lv_obj_set_pos(wallpaper_arcs[2], 4, 24);
    create_wallpaper_stars(root, false);

    for(uint8_t i = 0U; i < HOME_STAR_COUNT; i++) {
        lv_obj_set_style_bg_color(
            wallpaper_stars[i],
            lv_color_hex(i % 3U == 0U ?
                         watch_ui_theme_pick(0xD54FFF, 0x8F3BA6) :
                         (i % 2U == 0U ?
                          watch_ui_theme_pick(0x00E7FF, 0x087F9C) :
                          watch_ui_theme_pick(0x4B86FF, 0x315FAD))), 0);
        lv_obj_set_style_radius(wallpaper_stars[i], i % 4U == 0U ? 0 : LV_RADIUS_CIRCLE, 0);
    }
}

static void update_wallpaper_motion(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    if(watch_ui_get_view() != WATCH_VIEW_HOME || wallpaper_root == NULL ||
       !lv_obj_is_valid(wallpaper_root)) return;

    wallpaper_phase += 2U;
    const float phase = (float)wallpaper_phase;
    if(background_index == 0U) {
        lv_obj_set_pos(wallpaper_glows[0],
                       (int16_t)(-92 + (sinf(phase * 0.025f) * 18.0f)),
                       (int16_t)(-38 + (cosf(phase * 0.019f) * 12.0f)));
        lv_obj_set_pos(wallpaper_glows[1],
                       (int16_t)(116 + (cosf(phase * 0.021f) * 17.0f)),
                       (int16_t)(74 + (sinf(phase * 0.017f) * 20.0f)));
        lv_obj_set_pos(wallpaper_glows[2],
                       (int16_t)(-30 + (sinf(phase * 0.016f) * 24.0f)),
                       (int16_t)(184 + (cosf(phase * 0.023f) * 12.0f)));
        lv_arc_set_rotation(wallpaper_arcs[0], (int32_t)(wallpaper_phase % 360U));
        lv_arc_set_rotation(
            wallpaper_arcs[1],
            (int32_t)((360U - (wallpaper_phase % 360U)) % 360U));
        lv_arc_set_rotation(wallpaper_arcs[2], (int32_t)((wallpaper_phase / 2U) % 360U));
    }
    else if(background_index == 1U) {
        const int16_t moon_y = (int16_t)(26 + (sinf(phase * 0.018f) * 5.0f));
        lv_obj_set_y(wallpaper_glows[0], moon_y - 16);
        lv_obj_set_y(wallpaper_glows[1], moon_y);
        lv_obj_set_y(wallpaper_glows[2], moon_y - 13);
        lv_arc_set_rotation(wallpaper_arcs[0], (int32_t)((wallpaper_phase / 3U) % 360U));
        lv_arc_set_rotation(
            wallpaper_arcs[1],
            (int32_t)((360U - ((wallpaper_phase / 4U) % 360U)) % 360U));
        lv_arc_set_rotation(wallpaper_arcs[2], (int32_t)((wallpaper_phase / 5U) % 360U));
    }
    else {
        const float pulse = (sinf(phase * 0.055f) + 1.0f) * 0.5f;
        const int16_t core_shift = (int16_t)(pulse * 8.0f);
        lv_obj_set_pos(wallpaper_glows[0], 7 - core_shift / 2, 27 - core_shift / 2);
        lv_obj_set_size(wallpaper_glows[0], 226 + core_shift, 226 + core_shift);
        lv_obj_set_style_bg_opa(
            wallpaper_glows[1], (lv_opa_t)(35 + (int32_t)(pulse * 35.0f)), 0);
        lv_obj_set_style_bg_opa(
            wallpaper_glows[2], (lv_opa_t)(28 + (int32_t)((1.0f - pulse) * 42.0f)), 0);
        lv_arc_set_rotation(wallpaper_arcs[0], (int32_t)((wallpaper_phase * 2U) % 360U));
        lv_arc_set_rotation(
            wallpaper_arcs[1],
            (int32_t)((360U - ((wallpaper_phase * 3U / 2U) % 360U)) % 360U));
        lv_arc_set_rotation(wallpaper_arcs[2], (int32_t)((wallpaper_phase / 2U) % 360U));
    }

    for(uint8_t i = 0U; i < HOME_STAR_COUNT; i++) {
        const float wave = sinf((phase * (0.025f + (i * 0.001f))) + (i * 1.47f));
        const int32_t opacity = (int32_t)(115.0f + ((wave + 1.0f) * 65.0f));
        lv_obj_set_style_opa(wallpaper_stars[i], (lv_opa_t)opacity, 0);
        lv_obj_set_x(wallpaper_stars[i],
                     (int16_t)(star_base_x[i] + sinf(phase * 0.009f + i) * 3.0f));
        if(background_index == 2U) {
            lv_obj_set_y(
                wallpaper_stars[i],
                (int16_t)(star_base_y[i] + cosf(phase * 0.018f + i * 0.7f) * 4.0f));
        }
    }
}

void watch_home_init(void)
{
    if(live_timer == NULL) live_timer = lv_timer_create(update_live_data, 1000, NULL);
    if(wallpaper_timer == NULL) {
        wallpaper_timer = lv_timer_create(update_wallpaper_motion, 40, NULL);
    }
}

void watch_home_deactivate(void)
{
    time_label = NULL;
    date_label = NULL;
    heart_label = NULL;
    wallpaper_root = NULL;
    for(uint8_t i = 0U; i < HOME_GLOW_COUNT; i++) wallpaper_glows[i] = NULL;
    for(uint8_t i = 0U; i < HOME_ARC_COUNT; i++) wallpaper_arcs[i] = NULL;
    for(uint8_t i = 0U; i < HOME_STAR_COUNT; i++) wallpaper_stars[i] = NULL;
    analog_hour_hand = NULL;
    analog_minute_hand = NULL;
    analog_second_hand = NULL;
    swipe_tracking = false;
}

/* ---------------------------- 触摸控制 ---------------------------- */

static void home_touch_cb(lv_event_t * event)
{
    lv_indev_t * indev = lv_indev_active();
    if(indev == NULL) return;

    const lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_PRESSED) {
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
    const int16_t threshold = watch_ui_get_swipe_threshold();

    if(abs_dy >= threshold && abs_dy > abs_dx) {
        if(dy > 0) {
            watch_quick_settings_open(lv_event_get_current_target_obj(event));
        }
        else {
            watchface_index = (uint8_t)((watchface_index + 1U) % HOME_WATCHFACE_COUNT);
            if(watchface_index == 2U) background_index = 2U;
            watch_ui_show_home();
        }
        return;
    }
    if(abs_dx < threshold || abs_dx <= abs_dy) return;

    if(dx < 0) {
        watch_ui_show_carousel();
    }
    else {
        background_index = (uint8_t)((background_index + 1U) % HOME_WALLPAPER_COUNT);
        watch_ui_show_home();
    }
}

/* ---------------------------- 页面布局 ---------------------------- */

static void build_glass_watchface(lv_obj_t * root)
{
    lv_obj_t * glass = watch_ui_make_card(
        root, 198, 134,
        lv_color_hex(watch_ui_theme_pick(0x02040A, 0xFFFFFF)));
    lv_obj_set_style_bg_opa(glass,
                            watch_ui_is_dark_theme() ? LV_OPA_50 : LV_OPA_80, 0);
    lv_obj_set_style_radius(glass, 48, 0);
    lv_obj_set_style_border_width(glass, 1, 0);
    lv_obj_set_style_border_color(
        glass, lv_color_hex(watch_ui_theme_pick(0x8BA9CC, 0xB6C6D0)), 0);
    lv_obj_set_style_shadow_width(glass, 24, 0);
    lv_obj_set_style_shadow_color(
        glass, lv_color_hex(watch_ui_theme_pick(0x183C5E, 0x8CAAB4)), 0);
    lv_obj_set_style_shadow_opa(glass, LV_OPA_30, 0);
    lv_obj_align(glass, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(glass, LV_OBJ_FLAG_CLICKABLE);

    time_label = watch_ui_make_label(glass, "10:24", &lv_font_montserrat_48,
                                     lv_color_hex(WATCH_COLOR_TEXT),
                                     LV_ALIGN_TOP_MID, 0, 19);
    date_label = watch_ui_make_label(glass, "07.24  THU", &lv_font_montserrat_14,
                                     lv_color_hex(WATCH_COLOR_MUTED),
                                     LV_ALIGN_TOP_MID, 0, 72);
    watch_ui_make_label(glass, "SUNNY   28°C", &lv_font_montserrat_16,
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_BOTTOM_MID, 0, -17);
}

static void build_round_watchface(lv_obj_t * root)
{
    lv_obj_t * dial = watch_ui_make_card(
        root, 208, 208,
        lv_color_hex(watch_ui_theme_pick(0x030712, 0xFBFDFE)));
    lv_obj_set_style_bg_opa(dial,
                            watch_ui_is_dark_theme() ? LV_OPA_40 : LV_OPA_80, 0);
    lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dial, 2, 0);
    lv_obj_set_style_border_color(
        dial, lv_color_hex(background_index == 0U ? 0x59E4D2 : 0xC7B3FF), 0);
    lv_obj_set_style_shadow_width(dial, 28, 0);
    lv_obj_set_style_shadow_color(
        dial, lv_color_hex(background_index == 0U ? 0x1C7A84 : 0x60458F), 0);
    lv_obj_set_style_shadow_opa(dial, LV_OPA_40, 0);
    lv_obj_set_pos(dial, 16, 36);
    lv_obj_clear_flag(dial, LV_OBJ_FLAG_CLICKABLE);

    for(uint8_t i = 0U; i < 12U; i++) {
        const float angle = ((float)i * 6.2831853f / 12.0f) - 1.5707963f;
        const int16_t size = (i % 3U == 0U) ? 5 : 3;
        lv_obj_t * tick = watch_ui_make_card(
            dial, size, size,
            lv_color_hex(i % 3U == 0U ? WATCH_COLOR_TEXT : WATCH_COLOR_MUTED));
        lv_obj_set_style_radius(tick, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(tick,
                       (int16_t)(104.0f + (sinf(angle) * 89.0f) - (size / 2)),
                       (int16_t)(104.0f - (cosf(angle) * 89.0f) - (size / 2)));
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    lv_obj_t * progress = lv_arc_create(dial);
    lv_obj_set_size(progress, 188, 188);
    lv_obj_align(progress, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_range(progress, 0, 100);
    lv_arc_set_bg_angles(progress, 0, 300);
    lv_arc_set_rotation(progress, 210);
    lv_arc_set_value(progress, 78);
    lv_obj_remove_style(progress, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(progress, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(progress, lv_color_hex(WATCH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_arc_width(progress, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(
        progress, lv_color_hex(background_index == 0U ? WATCH_COLOR_CYAN :
                              WATCH_COLOR_PURPLE), LV_PART_INDICATOR);
    lv_obj_clear_flag(progress, LV_OBJ_FLAG_CLICKABLE);

    date_label = watch_ui_make_label(dial, "07.24  THU", &lv_font_montserrat_14,
                                     lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_CENTER, 0, -46);
    time_label = watch_ui_make_label(dial, "10:24", &lv_font_montserrat_44,
                                     lv_color_hex(WATCH_COLOR_TEXT),
                                     LV_ALIGN_CENTER, 0, -5);
    watch_ui_make_label(dial, "28°C  SUNNY", &lv_font_montserrat_14,
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_CENTER, 0, 43);
}

static lv_obj_t * make_analog_hand(lv_obj_t * dial, int16_t width, uint32_t color,
                                   lv_point_precise_t points[2])
{
    lv_obj_t * hand = lv_line_create(dial);
    lv_obj_remove_style_all(hand);
    lv_obj_set_size(hand, 218, 218);
    lv_obj_set_pos(hand, 0, 0);
    lv_obj_set_style_line_width(hand, width, 0);
    lv_obj_set_style_line_color(hand, lv_color_hex(color), 0);
    lv_obj_set_style_line_rounded(hand, true, 0);
    lv_line_set_points_mutable(hand, points, 2);
    lv_obj_clear_flag(hand, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    return hand;
}

static void build_analog_watchface(lv_obj_t * root)
{
    lv_obj_t * dial = watch_ui_make_card(
        root, 218, 218,
        lv_color_hex(watch_ui_theme_pick(0x020811, 0xF8FCFD)));
    lv_obj_set_pos(dial, 11, 31);
    lv_obj_set_style_bg_opa(dial,
                            watch_ui_is_dark_theme() ? LV_OPA_70 : LV_OPA_90, 0);
    lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dial, 2, 0);
    lv_obj_set_style_border_color(dial, lv_color_hex(WATCH_COLOR_CYAN), 0);
    lv_obj_set_style_shadow_width(dial, 30, 0);
    lv_obj_set_style_shadow_color(
        dial, lv_color_hex(watch_ui_theme_pick(0x067A9F, 0x6E9DAC)), 0);
    lv_obj_set_style_shadow_opa(dial, LV_OPA_50, 0);
    lv_obj_clear_flag(dial, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * outer_ring = lv_arc_create(dial);
    lv_obj_set_size(outer_ring, 206, 206);
    lv_obj_align(outer_ring, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(outer_ring, 202, 518);
    lv_obj_remove_style(outer_ring, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(outer_ring, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(outer_ring, lv_color_hex(WATCH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_arc_width(outer_ring, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(outer_ring, lv_color_hex(WATCH_COLOR_CYAN), LV_PART_INDICATOR);
    lv_arc_set_range(outer_ring, 0, 100);
    lv_arc_set_value(outer_ring, 72);
    lv_obj_clear_flag(outer_ring, LV_OBJ_FLAG_CLICKABLE);

    for(uint8_t i = 0U; i < 60U; i++) {
        const float angle = ((float)i / 60.0f) * HOME_TWO_PI;
        const bool major = (i % 5U) == 0U;
        const int16_t width = major ? 3 : 2;
        const int16_t height = major ? 9 : 4;
        lv_obj_t * tick = watch_ui_make_card(
            dial, width, height,
            lv_color_hex(major ?
                         (i % 15U == 0U ? WATCH_COLOR_TEXT : WATCH_COLOR_CYAN) :
                         WATCH_COLOR_MUTED));
        lv_obj_set_style_radius(tick, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(tick,
                       (int16_t)(HOME_DIAL_CENTER + sinf(angle) * 91.0f - width / 2),
                       (int16_t)(HOME_DIAL_CENTER - cosf(angle) * 91.0f - height / 2));
        lv_obj_set_style_transform_rotation(tick, (int32_t)((angle * 1800.0f) / 3.14159265f), 0);
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    watch_ui_make_label(dial, "NEXUS", &lv_font_montserrat_12,
                        lv_color_hex(WATCH_COLOR_CYAN), LV_ALIGN_CENTER, 0, -54);
    date_label = watch_ui_make_label(dial, "07.24  THU", &lv_font_montserrat_12,
                                     lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_CENTER, 0, 53);
    heart_label = watch_ui_make_label(dial, "72 BPM", &lv_font_montserrat_12,
                                      lv_color_hex(WATCH_COLOR_PURPLE), LV_ALIGN_CENTER, 0, 70);

    analog_hour_hand = make_analog_hand(
        dial, 6, watch_ui_theme_pick(0xE8F9FF, 0x183345), analog_hour_points);
    analog_minute_hand = make_analog_hand(
        dial, 4, WATCH_COLOR_CYAN, analog_minute_points);
    analog_second_hand = make_analog_hand(
        dial, 2, watch_ui_theme_pick(0xE44DFF, 0x963CAE), analog_second_points);

    lv_obj_t * hub_glow = watch_ui_make_card(
        dial, 18, 18,
        lv_color_hex(watch_ui_theme_pick(0x09364A, 0xD7EBEF)));
    lv_obj_align(hub_glow, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(hub_glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(hub_glow, 2, 0);
    lv_obj_set_style_border_color(hub_glow, lv_color_hex(WATCH_COLOR_CYAN), 0);
    lv_obj_set_style_shadow_width(hub_glow, 12, 0);
    lv_obj_set_style_shadow_color(hub_glow, lv_color_hex(WATCH_COLOR_CYAN), 0);
    lv_obj_set_style_shadow_opa(hub_glow, LV_OPA_70, 0);
    lv_obj_clear_flag(hub_glow, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t * hub = watch_ui_make_card(dial, 7, 7, lv_color_hex(WATCH_COLOR_TEXT));
    lv_obj_align(hub, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(hub, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
}

void watch_home_build(lv_obj_t * root)
{
    wallpaper_root = root;
    lv_obj_add_event_cb(root, home_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(root, home_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(root, home_touch_cb, LV_EVENT_PRESS_LOST, NULL);

    if(background_index == 0U) build_aurora_wallpaper(root);
    else if(background_index == 1U) build_moon_wallpaper(root);
    else build_quantum_wallpaper(root);

    if(watchface_index == 0U) build_glass_watchface(root);
    else if(watchface_index == 1U) build_round_watchface(root);
    else build_analog_watchface(root);

    update_live_data(NULL);
    update_wallpaper_motion(NULL);
}
