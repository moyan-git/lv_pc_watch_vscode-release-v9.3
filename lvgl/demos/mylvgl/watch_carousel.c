/**
 * @file watch_carousel.c
 * @brief 无限循环卡片轮盘菜单模块。
 *
 * 主要职责：
 * - 按左侧圆心曲线排列五张可见卡片；
 * - 支持纵向拖拽、惯性、吸附和表冠输入；
 * - 区分横向返回手势与纵向轮盘手势；
 * - 点击非中心卡片时居中，点击中心卡片时进入功能页。
 */
#include "watch_carousel.h"

#include <math.h>
#include <stdint.h>

#define WHEEL_ITEM_COUNT 8
#define WHEEL_PI 3.14159265f
#define WHEEL_TAU (2.0f * WHEEL_PI)
#define WHEEL_FRONT_ANGLE (WHEEL_PI / 2.0f)
#define WHEEL_CENTER_LOCK_SLOT 0.62f
#define WHEEL_FRONT_CARD_WIDTH 234.0f
#define WHEEL_CARD_WIDTH_STEP 14.0f
#define WHEEL_FRONT_CARD_HEIGHT 64.0f
#define WHEEL_CARD_HEIGHT_STEP 12.0f
#define WHEEL_FRONT_CARD_X 3.0f
#define WHEEL_CARD_GAP 4.0f

typedef struct {
    watch_app_t app;
    const char * icon;
    const char * title;
    uint32_t dark_color;
    uint32_t light_color;
} wheel_item_t;

typedef enum {
    WHEEL_AXIS_UNDECIDED,
    WHEEL_AXIS_VERTICAL,
    WHEEL_AXIS_HORIZONTAL,
} wheel_drag_axis_t;

static const wheel_item_t wheel_items[WHEEL_ITEM_COUNT] = {
    { WATCH_APP_STEPS,       "RUN", "运动", 0x1E5944, 0x70A98B },
    { WATCH_APP_HEART_RATE,  "HR",  "心率", 0x713743, 0xC77782 },
    { WATCH_APP_SPO2,        "O2",  "血氧", 0x2C4D72, 0x7996BF },
    { WATCH_APP_ENVIRONMENT, "WX",  "天气", 0x704B28, 0xC2925E },
    { WATCH_APP_MUSIC,       "MUS", "音乐", 0x4D3B72, 0x9180B7 },
    { WATCH_APP_POSTURE,     "POS", "姿态", 0x275962, 0x6E9FA8 },
    { WATCH_APP_SCHEDULE,    "CAL", "课表", 0x66442E, 0xB5896B },
    { WATCH_APP_SETTINGS,    "SET", "设置", 0x354454, 0x7A8796 },
};

static lv_obj_t * wheel_cards[WHEEL_ITEM_COUNT];
static lv_obj_t * wheel_icon_labels[WHEEL_ITEM_COUNT];
static lv_obj_t * wheel_titles[WHEEL_ITEM_COUNT];
static float wheel_rotation;
static float wheel_velocity;
static float wheel_target_rotation;
static bool wheel_dragging;
static bool wheel_dragged_since_press;
static bool wheel_click_blocked;
static bool wheel_horizontal_return_ready;
static wheel_drag_axis_t wheel_drag_axis;
static lv_point_t wheel_drag_start;
static lv_point_t wheel_drag_latest;
static bool wheel_settling;
static bool wheel_motion_active;
static uint8_t wheel_selected;
static lv_timer_t * wheel_motion_timer;

/* ---------------------------- 轮盘几何 ---------------------------- */

static float wheel_wrap_delta(float delta)
{
    while(delta > WHEEL_PI) delta -= WHEEL_TAU;
    while(delta < -WHEEL_PI) delta += WHEEL_TAU;
    return delta;
}

/*
 * 根据槽位距离和卡片递减高度累计纵向位置。
 * 相邻卡片边缘始终保留 WHEEL_CARD_GAP，旋转过程中也不会互相覆盖。
 */
static float wheel_slot_center_y(float relative_slot)
{
    const float distance = fabsf(relative_slot);
    const float offset =
        ((WHEEL_FRONT_CARD_HEIGHT + WHEEL_CARD_GAP) * distance) -
        ((WHEEL_CARD_HEIGHT_STEP * 0.5f) * distance * distance);
    return 140.0f + (relative_slot < 0.0f ? -offset : offset);
}

static uint8_t wheel_nearest_center(void)
{
    uint8_t selected = 0;
    float best_distance_sq = 1.0e9f;
    const float step = WHEEL_TAU / WHEEL_ITEM_COUNT;

    for(uint8_t i = 0; i < WHEEL_ITEM_COUNT; i++) {
        const float angle = WHEEL_FRONT_ANGLE + ((float)i * step) + wheel_rotation;
        const float relative_slot = wheel_wrap_delta(angle - WHEEL_FRONT_ANGLE) / step;
        const float distance = fabsf(relative_slot);
        const float card_width = WHEEL_FRONT_CARD_WIDTH - (WHEEL_CARD_WIDTH_STEP * distance);
        const float card_x = WHEEL_FRONT_CARD_X - (1.5f * distance * distance);
        const float card_center_x = card_x + (card_width * 0.5f);
        const float card_center_y = wheel_slot_center_y(relative_slot);
        const float dx = card_center_x - (WATCH_VIEWPORT_WIDTH * 0.5f);
        const float dy = card_center_y - (WATCH_VIEWPORT_HEIGHT * 0.5f);
        const float distance_sq = (dx * dx) + (dy * dy);
        if(distance_sq < best_distance_sq) {
            best_distance_sq = distance_sq;
            selected = i;
        }
    }

    /* 中心滞回区可防止卡片在槽位边界反复切换。 */
    const float current_angle = WHEEL_FRONT_ANGLE + ((float)wheel_selected * step) + wheel_rotation;
    const float current_slot = wheel_wrap_delta(current_angle - WHEEL_FRONT_ANGLE) / step;
    if(fabsf(current_slot) <= WHEEL_CENTER_LOCK_SLOT) selected = wheel_selected;
    return selected;
}

static void wheel_render(void)
{
    if(watch_ui_get_view() != WATCH_VIEW_CAROUSEL) return;

    const float step = WHEEL_TAU / WHEEL_ITEM_COUNT;
    const uint8_t selected = wheel_nearest_center();
    wheel_selected = selected;

    for(uint8_t i = 0; i < WHEEL_ITEM_COUNT; i++) {
        const float angle = WHEEL_FRONT_ANGLE + ((float)i * step) + wheel_rotation;
        const float relative_slot = wheel_wrap_delta(angle - WHEEL_FRONT_ANGLE) / step;
        const float distance = fabsf(relative_slot);
        const int16_t width =
            (int16_t)(WHEEL_FRONT_CARD_WIDTH - (WHEEL_CARD_WIDTH_STEP * distance));
        const int16_t height =
            (int16_t)(WHEEL_FRONT_CARD_HEIGHT - (WHEEL_CARD_HEIGHT_STEP * distance));
        const int16_t x =
            (int16_t)(WHEEL_FRONT_CARD_X - (1.5f * distance * distance));
        const int16_t y =
            (int16_t)(wheel_slot_center_y(relative_slot) - (height / 2));
        int32_t opa_value = (int32_t)(255.0f - (76.0f * distance));
        if(opa_value < 0) opa_value = 0;
        if(opa_value > 255) opa_value = 255;
        const lv_opa_t opacity = (lv_opa_t)opa_value;

        /* 卡片完全滑出屏幕后才隐藏，避免在边缘突然消失。 */
        if((y + height) <= 0 || y >= WATCH_VIEWPORT_HEIGHT || opacity == LV_OPA_TRANSP) {
            lv_obj_add_flag(wheel_cards[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_clear_flag(wheel_cards[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(wheel_cards[i], width, height);
        lv_obj_set_pos(wheel_cards[i], x, y);
        lv_obj_set_style_bg_color(
            wheel_cards[i],
            lv_color_hex(watch_ui_theme_pick(wheel_items[i].dark_color,
                                             wheel_items[i].light_color)), 0);
        lv_obj_set_style_opa(wheel_cards[i], opacity, 0);
        lv_obj_set_style_radius(wheel_cards[i], i == selected ? 23 : 18, 0);
        lv_obj_set_style_border_width(wheel_cards[i], i == selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(
            wheel_cards[i],
            lv_color_hex(i == selected ?
                         watch_ui_theme_pick(0xDDFBFF, 0x8A5D48) :
                         watch_ui_theme_pick(0x547081, 0xD5BBA8)), 0);
        lv_obj_set_style_text_opa(wheel_icon_labels[i], opacity, 0);
        lv_obj_set_style_text_font(wheel_icon_labels[i],
                                   i == selected ? &lv_font_montserrat_16 :
                                   &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_font(wheel_titles[i], watch_ui_get_cn_font(), 0);

    }

    lv_obj_move_foreground(wheel_cards[selected]);
}

static void wheel_snap_to(uint8_t index)
{
    const float step = WHEEL_TAU / WHEEL_ITEM_COUNT;
    const float desired_rotation = -((float)index * step);
    wheel_target_rotation = wheel_rotation + wheel_wrap_delta(desired_rotation - wheel_rotation);
    wheel_velocity = 0.0f;
    wheel_settling = true;
    wheel_motion_active = true;
}

/* -------------------------- 运动与触摸控制 -------------------------- */

static void wheel_motion_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    if(watch_ui_get_view() != WATCH_VIEW_CAROUSEL || wheel_dragging || !wheel_motion_active) return;

    if(fabsf(wheel_velocity) > 0.002f) {
        wheel_rotation += wheel_velocity;
        wheel_velocity *= 0.90f;
        wheel_render();
        return;
    }

    wheel_velocity = 0.0f;
    if(!wheel_settling) wheel_snap_to(wheel_nearest_center());
    const float delta = wheel_wrap_delta(wheel_target_rotation - wheel_rotation);
    if(fabsf(delta) < 0.003f) {
        wheel_rotation = wheel_target_rotation;
        wheel_settling = false;
        wheel_motion_active = false;
        wheel_render();
        return;
    }
    else {
        wheel_rotation += delta * 0.22f;
    }
    wheel_render();
}

static void wheel_card_click_cb(lv_event_t * event)
{
    /*
     * 横滑返回会在 RELEASED 冒泡到根对象时切换页面，随后卡片仍可能收到
     * CLICKED。先检查当前页面和本次手势锁，禁止旧卡片再次打开详情页。
     */
    if(watch_ui_get_view() != WATCH_VIEW_CAROUSEL) return;
    if(wheel_click_blocked || wheel_dragged_since_press) return;

    const uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if(index == wheel_selected) watch_ui_show_detail(wheel_items[index].app);
    else wheel_snap_to(index);
}

static void wheel_touch_cb(lv_event_t * event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_PRESSED) {
        wheel_dragging = true;
        wheel_dragged_since_press = false;
        wheel_click_blocked = false;
        wheel_horizontal_return_ready = false;
        wheel_drag_axis = WHEEL_AXIS_UNDECIDED;
        lv_indev_t * indev = lv_indev_active();
        if(indev != NULL) {
            lv_indev_get_point(indev, &wheel_drag_start);
            wheel_drag_latest = wheel_drag_start;
        }
        wheel_velocity = 0.0f;
        wheel_settling = false;
        wheel_motion_active = false;
    }
    else if(code == LV_EVENT_PRESSING) {
        lv_indev_t * indev = lv_indev_active();
        if(indev == NULL) return;

        lv_point_t point;
        lv_indev_get_point(indev, &point);
        wheel_drag_latest = point;
        const int16_t total_dx = point.x - wheel_drag_start.x;
        const int16_t total_dy = point.y - wheel_drag_start.y;
        const int16_t abs_dx = total_dx < 0 ? -total_dx : total_dx;
        const int16_t abs_dy = total_dy < 0 ? -total_dy : total_dy;
        const int16_t axis_threshold = watch_ui_get_swipe_threshold() / 3;

        if(wheel_drag_axis == WHEEL_AXIS_UNDECIDED) {
            if(abs_dx < axis_threshold && abs_dy < axis_threshold) return;
            wheel_drag_axis = abs_dy > abs_dx ? WHEEL_AXIS_VERTICAL : WHEEL_AXIS_HORIZONTAL;
            wheel_dragged_since_press = true;
            wheel_click_blocked = true;
        }
        if(wheel_drag_axis == WHEEL_AXIS_HORIZONTAL &&
           abs_dx >= watch_ui_get_swipe_threshold() && abs_dx > abs_dy) {
            wheel_horizontal_return_ready = true;
        }
        if(wheel_drag_axis != WHEEL_AXIS_VERTICAL) return;

        lv_point_t vector;
        lv_indev_get_vect(indev, &vector);
        if(vector.y != 0) {
            wheel_velocity = (float)vector.y * 0.018f;
            wheel_rotation += wheel_velocity;
            wheel_render();
        }
    }
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_indev_t * indev = lv_indev_active();
        if(indev != NULL) lv_indev_get_point(indev, &wheel_drag_latest);
        const int16_t total_dx = wheel_drag_latest.x - wheel_drag_start.x;
        const int16_t total_dy = wheel_drag_latest.y - wheel_drag_start.y;
        const int16_t abs_dx = total_dx < 0 ? -total_dx : total_dx;
        const int16_t abs_dy = total_dy < 0 ? -total_dy : total_dy;
        const int16_t axis_threshold = watch_ui_get_swipe_threshold() / 3;

        /* 快速滑动可能没有产生 PRESSING，释放时必须再次完成方向判定。 */
        if(wheel_drag_axis == WHEEL_AXIS_UNDECIDED &&
           (abs_dx >= axis_threshold || abs_dy >= axis_threshold)) {
            wheel_drag_axis = abs_dy > abs_dx ? WHEEL_AXIS_VERTICAL : WHEEL_AXIS_HORIZONTAL;
            wheel_dragged_since_press = true;
            wheel_click_blocked = true;
        }
        if(wheel_drag_axis == WHEEL_AXIS_HORIZONTAL &&
           abs_dx >= watch_ui_get_swipe_threshold() && abs_dx > abs_dy) {
            wheel_horizontal_return_ready = true;
        }

        wheel_dragging = false;
        if(wheel_drag_axis == WHEEL_AXIS_HORIZONTAL && wheel_horizontal_return_ready) {
            watch_ui_show_home();
            return;
        }
        if(wheel_drag_axis == WHEEL_AXIS_HORIZONTAL) {
            wheel_snap_to(wheel_nearest_center());
            return;
        }
        if(fabsf(wheel_velocity) <= 0.002f) {
            wheel_snap_to(wheel_nearest_center());
        }
        else {
            wheel_motion_active = true;
        }
    }
}

static void wheel_rotary_cb(lv_event_t * event)
{
    const int32_t diff = lv_event_get_rotary_diff(event);
    if(diff == 0) return;
    wheel_dragging = false;
    wheel_velocity = (float)diff * 0.055f;
    wheel_rotation += (float)diff * 0.12f;
    wheel_settling = false;
    wheel_motion_active = true;
    wheel_render();
}

void watch_carousel_deactivate(void)
{
    wheel_dragging = false;
    wheel_drag_axis = WHEEL_AXIS_UNDECIDED;
    wheel_velocity = 0.0f;
    wheel_settling = false;
    wheel_motion_active = false;
}

/* ---------------------------- 页面布局 ---------------------------- */

void watch_carousel_build(lv_obj_t * root)
{
    lv_obj_set_style_bg_color(
        root, lv_color_hex(watch_ui_theme_pick(0x07131D, 0xF3E5D3)), 0);
    lv_obj_set_style_bg_grad_color(
        root, lv_color_hex(watch_ui_theme_pick(0x162B35, 0xDDBDA7)), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
    lv_obj_add_event_cb(root, wheel_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(root, wheel_touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(root, wheel_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(root, wheel_touch_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(root, wheel_rotary_cb, LV_EVENT_ROTARY, NULL);
    lv_group_focus_obj(root);

    for(uint8_t i = 0; i < WHEEL_ITEM_COUNT; i++) {
        wheel_cards[i] = watch_ui_make_card(root, 210, 70,
                                            lv_color_hex(WATCH_COLOR_SURFACE));
        lv_obj_set_style_radius(wheel_cards[i], 23, 0);
        lv_obj_add_flag(wheel_cards[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(wheel_cards[i], wheel_card_click_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        wheel_icon_labels[i] = watch_ui_make_label(
            wheel_cards[i], wheel_items[i].icon, &lv_font_montserrat_16,
            lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_LEFT_MID, 14, 0);
        wheel_titles[i] = watch_ui_make_label(
            wheel_cards[i], wheel_items[i].title, watch_ui_get_cn_font(),
            lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_LEFT_MID, 54, 0);
    }

    if(wheel_motion_timer == NULL) wheel_motion_timer = lv_timer_create(wheel_motion_cb, 16, NULL);
    wheel_snap_to(wheel_selected);
    wheel_render();
}
