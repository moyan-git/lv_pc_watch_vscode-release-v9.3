/**
 * @file watch_schedule.c
 * @brief 可交互电子课表模块。
 *
 * 主要职责：
 * - 展示18周、每周7天以及每天最多五节课程；
 * - 在页面顶部提供可点击的日期选择器；
 * - 支持课程列表上下滚动；
 * - 支持左右滑动切换日期，并避免误触发页面返回。
 */
#include "watch_schedule.h"
#include "watch_schedule_data.h"

#include <stdint.h>

static const char * weekday_names[WATCH_SCHEDULE_DAY_COUNT] = {
    "一", "二", "三", "四", "五", "六", "日"
};

static uint8_t selected_day = 3;
static uint8_t selected_week = 4;
static lv_point_t swipe_start;
static bool swipe_tracking;
static bool horizontal_swipe_detected;
static bool day_transitioning;
static lv_obj_t * schedule_root;
static lv_obj_t * day_cards[WATCH_SCHEDULE_DAY_COUNT];
static lv_obj_t * weekday_labels[WATCH_SCHEDULE_DAY_COUNT];
static lv_obj_t * date_labels[WATCH_SCHEDULE_DAY_COUNT];
static lv_obj_t * course_list;
static lv_obj_t * week_dropdown;

/* ---------------------------- 课程卡片 ---------------------------- */

static void add_course_card(lv_obj_t * parent, int16_t y,
                            const watch_schedule_course_t * course)
{
    lv_obj_t * item = watch_ui_make_card(parent, 204, 62,
                                         lv_color_hex(WATCH_COLOR_SURFACE));
    lv_obj_set_pos(item, 18, y);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_border_color(item, lv_color_hex(WATCH_COLOR_BORDER), 0);

    lv_obj_t * marker = watch_ui_make_card(item, 6, 36, lv_color_hex(course->color));
    lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_pos(marker, 12, 13);

    lv_obj_t * name_label = watch_ui_make_label(
        item, course->name, watch_ui_get_cn_font(), lv_color_hex(WATCH_COLOR_TEXT),
        LV_ALIGN_LEFT_MID, 28, -11);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_size(name_label, 82, 20);
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_LEFT, 0);

    char compact_time[WATCH_COURSE_TIME_SIZE];
    uint8_t compact_index = 0U;
    for(uint8_t i = 0U; course->time[i] != '\0' &&
                        compact_index + 1U < sizeof(compact_time); i++) {
        if(course->time[i] != ' ') compact_time[compact_index++] = course->time[i];
    }
    compact_time[compact_index] = '\0';
    lv_obj_t * time_label = watch_ui_make_label(
        item, compact_time, &lv_font_montserrat_12, lv_color_hex(WATCH_COLOR_TEXT),
        LV_ALIGN_RIGHT_MID, -10, -11);
    lv_obj_set_width(time_label, 82);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(time_label, LV_LABEL_LONG_DOT);

    lv_obj_t * location_label = watch_ui_make_label(
        item, course->location, watch_ui_get_cn_font(), lv_color_hex(WATCH_COLOR_MUTED),
        LV_ALIGN_LEFT_MID, 28, 14);
    lv_label_set_long_mode(location_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_size(location_label, 82, 20);
    lv_obj_set_style_text_align(location_label, LV_TEXT_ALIGN_LEFT, 0);
}

static lv_obj_t * create_course_list(lv_obj_t * root, uint8_t day_index, int16_t start_x)
{
    lv_obj_t * list = lv_obj_create(root);
    lv_obj_set_size(list, WATCH_VIEWPORT_WIDTH, 181);
    lv_obj_set_pos(list, start_x, 99);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list, lv_color_hex(WATCH_COLOR_CYAN), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);
    lv_obj_add_flag(list, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);

    uint8_t visible_index = 0U;
    for(uint8_t slot = 0U; slot < WATCH_SCHEDULE_MAX_COURSES; slot++) {
        const watch_schedule_course_t * course =
            watch_schedule_data_get_course(selected_week, day_index, slot);
        if(course == NULL) continue;
        add_course_card(list, (int16_t)(visible_index * 68), course);
        visible_index++;
    }

    if(visible_index == 0U) {
        watch_ui_make_label(list, "今天暂无课程", watch_ui_get_cn_font(),
                            lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_TOP_MID, 0, 38);
    }
    return list;
}

/* -------------------------- 日期高亮与动画 -------------------------- */

static void update_date_text(void)
{
    for(uint8_t i = 0U; i < WATCH_SCHEDULE_DAY_COUNT; i++) {
        if(date_labels[i] == NULL) continue;
        const watch_schedule_date_t date = watch_schedule_data_get_date(selected_week, i);
        lv_label_set_text_fmt(date_labels[i], "%02u", (unsigned)date.day);
    }
}

static void update_date_highlight(void)
{
    for(uint8_t i = 0; i < WATCH_SCHEDULE_DAY_COUNT; i++) {
        const bool selected = i == selected_day;
        lv_obj_set_style_bg_color(
            day_cards[i], lv_color_hex(selected ? WATCH_COLOR_CYAN : WATCH_COLOR_SURFACE), 0);
        lv_obj_set_style_text_color(
            weekday_labels[i],
            lv_color_hex(selected ? WATCH_COLOR_BG : WATCH_COLOR_MUTED), 0);
        lv_obj_set_style_text_color(
            date_labels[i],
            lv_color_hex(selected ? WATCH_COLOR_BG : WATCH_COLOR_TEXT), 0);
    }
}

static void refresh_current_course_list(void)
{
    if(schedule_root == NULL) return;
    if(course_list != NULL && lv_obj_is_valid(course_list)) lv_obj_delete(course_list);
    course_list = create_course_list(schedule_root, selected_day, 0);
    watch_ui_set_brightness(watch_ui_get_brightness());
}

static void course_list_x_anim_cb(void * object, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)object, value);
}

static void old_list_anim_completed_cb(lv_anim_t * animation)
{
    lv_obj_t * old_list = lv_anim_get_user_data(animation);
    if(old_list != NULL && lv_obj_is_valid(old_list)) lv_obj_delete(old_list);
}

static void new_list_anim_completed_cb(lv_anim_t * animation)
{
    LV_UNUSED(animation);
    day_transitioning = false;
}

static void animate_course_list(lv_obj_t * list, int16_t from_x, int16_t to_x,
                                lv_anim_completed_cb_t completed_cb, void * user_data)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, list);
    lv_anim_set_exec_cb(&animation, course_list_x_anim_cb);
    lv_anim_set_values(&animation, from_x, to_x);
    lv_anim_set_duration(&animation, 240);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation, completed_cb);
    lv_anim_set_user_data(&animation, user_data);
    lv_anim_start(&animation);
}

static void change_day(uint8_t new_day, int8_t direction)
{
    if(new_day >= WATCH_SCHEDULE_DAY_COUNT || new_day == selected_day || day_transitioning ||
       schedule_root == NULL) return;

    day_transitioning = true;
    lv_obj_t * old_list = course_list;
    selected_day = new_day;
    update_date_highlight();

    const int16_t enter_x = direction > 0 ? WATCH_VIEWPORT_WIDTH : -WATCH_VIEWPORT_WIDTH;
    const int16_t exit_x = direction > 0 ? -WATCH_VIEWPORT_WIDTH : WATCH_VIEWPORT_WIDTH;
    course_list = create_course_list(schedule_root, selected_day, enter_x);

    if(old_list != NULL) {
        lv_obj_clear_flag(old_list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
        animate_course_list(old_list, 0, exit_x, old_list_anim_completed_cb, old_list);
    }
    animate_course_list(course_list, enter_x, 0, new_list_anim_completed_cb, NULL);

    /* 动态创建的新课程层需保持在亮度遮罩下方。 */
    watch_ui_set_brightness(watch_ui_get_brightness());
}

/* ---------------------------- 周次选择 ---------------------------- */

static const char * week_options =
    "第1周\n第2周\n第3周\n第4周\n第5周\n第6周\n"
    "第7周\n第8周\n第9周\n第10周\n第11周\n第12周\n"
    "第13周\n第14周\n第15周\n第16周\n第17周\n第18周";

/* 下拉列表创建完成后，统一设置字体、选中态与滚动区域外观。 */
static void week_dropdown_ready_cb(lv_event_t * event)
{
    lv_obj_t * dropdown = lv_event_get_target_obj(event);
    lv_obj_t * list = lv_dropdown_get_list(dropdown);
    if(list == NULL) return;

    lv_obj_set_style_text_font(list, watch_ui_get_cn_font(), LV_PART_MAIN);
    lv_obj_set_style_text_font(list, watch_ui_get_cn_font(), LV_PART_SELECTED);
    lv_obj_set_style_text_color(list, lv_color_hex(WATCH_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_color(list, lv_color_hex(WATCH_COLOR_CYAN), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(list, lv_color_hex(WATCH_COLOR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(list, lv_color_hex(WATCH_COLOR_SURFACE2), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_border_width(list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(list, lv_color_hex(WATCH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(list, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(list, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(list, 8, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
}

/* 直接选择周次：下拉列表会自动收起，无需额外确认按钮。 */
static void week_dropdown_changed_cb(lv_event_t * event)
{
    lv_obj_t * dropdown = lv_event_get_target_obj(event);
    selected_week = (uint8_t)(lv_dropdown_get_selected(dropdown) + 1U);
    update_date_text();
    refresh_current_course_list();
}

/* -------------------------- 日期输入控制 -------------------------- */

static void select_day_cb(lv_event_t * event)
{
    if(horizontal_swipe_detected) return;
    const uint8_t new_day = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if(new_day == selected_day) return;
    change_day(new_day, new_day > selected_day ? 1 : -1);
}

static void schedule_touch_cb(lv_event_t * event)
{
    lv_indev_t * indev = lv_indev_active();
    if(indev == NULL) return;

    const lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &swipe_start);
        swipe_tracking = true;
        horizontal_swipe_detected = false;
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
    if(abs_dx < watch_ui_get_swipe_threshold() || abs_dx <= abs_dy) return;

    horizontal_swipe_detected = true;
    if(dx < 0) {
        change_day((uint8_t)((selected_day + 1U) % WATCH_SCHEDULE_DAY_COUNT), 1);
    }
    else {
        change_day((uint8_t)((selected_day + WATCH_SCHEDULE_DAY_COUNT - 1U) %
                             WATCH_SCHEDULE_DAY_COUNT), -1);
    }
}

/* ---------------------------- 页面布局 ---------------------------- */

void watch_schedule_build(lv_obj_t * root)
{
    schedule_root = root;
    swipe_tracking = false;
    horizontal_swipe_detected = false;
    day_transitioning = false;
    watch_ui_add_detail_header(root, "电子课表");
    lv_obj_add_event_cb(root, schedule_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(root, schedule_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(root, schedule_touch_cb, LV_EVENT_PRESS_LOST, NULL);

    week_dropdown = lv_dropdown_create(root);
    lv_obj_set_size(week_dropdown, 64, 34);
    lv_obj_set_pos(week_dropdown, 164, 14);
    lv_dropdown_set_options_static(week_dropdown, week_options);
    lv_dropdown_set_selected(week_dropdown, selected_week - 1U);
    lv_dropdown_set_dir(week_dropdown, LV_DIR_BOTTOM);
    lv_dropdown_set_symbol(week_dropdown, NULL);
    lv_dropdown_set_selected_highlight(week_dropdown, true);
    lv_obj_set_style_text_font(week_dropdown, watch_ui_get_cn_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(week_dropdown, lv_color_hex(WATCH_COLOR_CYAN), LV_PART_MAIN);
    lv_obj_set_style_text_align(week_dropdown, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(week_dropdown, lv_color_hex(WATCH_COLOR_SURFACE2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(week_dropdown, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(week_dropdown, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(week_dropdown, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(week_dropdown,
                                  lv_color_hex(WATCH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_pad_all(week_dropdown, 0, LV_PART_MAIN);
    lv_obj_add_flag(week_dropdown, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(week_dropdown, week_dropdown_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(week_dropdown, week_dropdown_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    watch_schedule_data_init();

    for(uint8_t i = 0; i < WATCH_SCHEDULE_DAY_COUNT; i++) {
        const bool selected = i == selected_day;
        const watch_schedule_date_t date = watch_schedule_data_get_date(selected_week, i);
        lv_obj_t * day = watch_ui_make_card(
            root, 30, 42, lv_color_hex(selected ? WATCH_COLOR_CYAN : WATCH_COLOR_SURFACE));
        lv_obj_set_pos(day, 5 + (i * 33), 51);
        lv_obj_set_style_radius(day, 10, 0);
        lv_obj_add_flag(day, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(day, select_day_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        day_cards[i] = day;
        weekday_labels[i] = watch_ui_make_label(
            day, weekday_names[i], watch_ui_get_cn_font(),
            lv_color_hex(selected ? WATCH_COLOR_BG : WATCH_COLOR_MUTED),
            LV_ALIGN_TOP_MID, 0, 3);
        date_labels[i] = watch_ui_make_label(
            day, "", &lv_font_montserrat_12,
            lv_color_hex(selected ? WATCH_COLOR_BG : WATCH_COLOR_TEXT),
            LV_ALIGN_BOTTOM_MID, 0, -3);
        lv_label_set_text_fmt(date_labels[i], "%02u", (unsigned)date.day);
    }

    course_list = create_course_list(root, selected_day, 0);
}

void watch_schedule_deactivate(void)
{
    if(week_dropdown != NULL && lv_obj_is_valid(week_dropdown)) {
        lv_dropdown_close(week_dropdown);
    }
    schedule_root = NULL;
    course_list = NULL;
    week_dropdown = NULL;
    swipe_tracking = false;
    horizontal_swipe_detected = false;
    day_transitioning = false;
    for(uint8_t i = 0; i < WATCH_SCHEDULE_DAY_COUNT; i++) {
        day_cards[i] = NULL;
        weekday_labels[i] = NULL;
        date_labels[i] = NULL;
    }
}
