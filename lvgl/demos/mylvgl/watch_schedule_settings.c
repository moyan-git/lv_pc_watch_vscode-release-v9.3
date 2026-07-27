/**
 * @file watch_schedule_settings.c
 * @brief 电子课表设置与课程编辑模块。
 *
 * 主要职责：
 * - 设置第一周周一对应的学期起始日期；
 * - 选择第1至18周以及周一至周日；
 * - 新增、修改、删除每天最多五节课程；
 * - 使用屏幕键盘录入课程名称、上课时段和地点；
 * - 保存后立即写入共享课表数据并同步至电子课表页面。
 */
#include "watch_schedule_settings.h"

#include "watch_schedule_data.h"

#include <stdio.h>
#include <string.h>

typedef enum {
    EDIT_FIELD_NAME,
    EDIT_FIELD_TIME,
    EDIT_FIELD_LOCATION,
} edit_field_t;

typedef enum {
    SETTINGS_PAGE_MANAGER,
    SETTINGS_PAGE_COURSE_EDITOR,
    SETTINGS_PAGE_START_DATE,
} schedule_settings_page_t;

static const char * week_options =
    "第1周\n第2周\n第3周\n第4周\n第5周\n第6周\n"
    "第7周\n第8周\n第9周\n第10周\n第11周\n第12周\n"
    "第13周\n第14周\n第15周\n第16周\n第17周\n第18周";
static const char * day_options =
    "周一\n周二\n周三\n周四\n周五\n周六\n周日";
static const char * month_options =
    "1月\n2月\n3月\n4月\n5月\n6月\n7月\n8月\n9月\n10月\n11月\n12月";
static const char * day_number_options =
    "1日\n2日\n3日\n4日\n5日\n6日\n7日\n8日\n9日\n10日\n"
    "11日\n12日\n13日\n14日\n15日\n16日\n17日\n18日\n19日\n20日\n"
    "21日\n22日\n23日\n24日\n25日\n26日\n27日\n28日\n29日\n30日\n31日";

static const uint32_t course_colors[WATCH_SCHEDULE_MAX_COURSES] = {
    0x33D9FF, 0xB58CFF, 0xFFAA4C, 0x53E28C, 0x5E9CFF
};

static lv_obj_t * settings_root;
static lv_obj_t * content_layer;
static lv_obj_t * course_list;
static lv_obj_t * keyboard_overlay;
static lv_obj_t * keyboard_textarea;
static lv_obj_t * keyboard_object;
static lv_obj_t * candidate_matrix;
static lv_obj_t * input_mode_label;
static lv_obj_t * field_value_labels[3];
static uint8_t selected_week = 1U;
static uint8_t selected_day;
static int8_t editing_slot = -1;
static edit_field_t active_field;
static watch_schedule_course_t editing_course;
static schedule_settings_page_t active_page;
static lv_point_t swipe_start;
static bool swipe_tracking;
static bool chinese_input_mode;

static void build_manager_page(void);
static void build_course_editor(void);

static void settings_page_touch_cb(lv_event_t * event)
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

    lv_point_t end;
    lv_indev_get_point(indev, &end);
    const int16_t dx = end.x - swipe_start.x;
    const int16_t dy = end.y - swipe_start.y;
    const int16_t abs_dx = dx < 0 ? -dx : dx;
    const int16_t abs_dy = dy < 0 ? -dy : dy;
    if(abs_dx < watch_ui_get_swipe_threshold() || abs_dx <= abs_dy) return;
    if(active_page == SETTINGS_PAGE_MANAGER) watch_ui_show_detail(WATCH_APP_SETTINGS);
    else build_manager_page();
}

static void copy_text(char * destination, uint32_t size, const char * source)
{
    if(destination == NULL || size == 0U) return;
    snprintf(destination, size, "%s", source == NULL ? "" : source);
}

static lv_obj_t * create_layer(void)
{
    if(content_layer != NULL && lv_obj_is_valid(content_layer)) lv_obj_delete(content_layer);
    content_layer = lv_obj_create(settings_root);
    lv_obj_set_size(content_layer, WATCH_VIEWPORT_WIDTH, WATCH_VIEWPORT_HEIGHT);
    lv_obj_set_pos(content_layer, 0, 0);
    lv_obj_set_style_bg_color(content_layer, lv_color_hex(WATCH_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(content_layer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content_layer, 0, 0);
    lv_obj_set_style_pad_all(content_layer, 0, 0);
    lv_obj_clear_flag(content_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(content_layer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(content_layer, settings_page_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(content_layer, settings_page_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(content_layer, settings_page_touch_cb, LV_EVENT_PRESS_LOST, NULL);
    watch_ui_set_brightness(watch_ui_get_brightness());
    return content_layer;
}

static void add_header(lv_obj_t * parent, const char * title, lv_event_cb_t back_cb)
{
    lv_obj_t * back = watch_ui_round_button(parent, 36, lv_color_hex(WATCH_COLOR_SURFACE),
                                            "<", back_cb, 0);
    lv_obj_set_pos(back, 14, 13);
    watch_ui_make_label(parent, title, watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_TOP_MID, 0, 20);
}

static lv_obj_t * create_action_button(lv_obj_t * parent, int16_t x, int16_t y,
                                       int16_t width, const char * text,
                                       uint32_t color, lv_event_cb_t callback)
{
    lv_obj_t * button = watch_ui_make_card(parent, width, 38, lv_color_hex(color));
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    watch_ui_make_label(button, text, watch_ui_get_cn_font(),
                        lv_color_hex(color == WATCH_COLOR_CYAN ? WATCH_COLOR_BG :
                                     WATCH_COLOR_TEXT),
                        LV_ALIGN_CENTER, 0, 0);
    return button;
}

/* ---------------------------- 下拉控件 ---------------------------- */

static void dropdown_ready_cb(lv_event_t * event)
{
    lv_obj_t * list = lv_dropdown_get_list(lv_event_get_target_obj(event));
    if(list == NULL) return;
    lv_obj_set_style_text_font(list, watch_ui_get_cn_font(), LV_PART_MAIN);
    lv_obj_set_style_text_font(list, watch_ui_get_cn_font(), LV_PART_SELECTED);
    lv_obj_set_style_text_color(list, lv_color_hex(WATCH_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_color(list, lv_color_hex(WATCH_COLOR_CYAN), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(list, lv_color_hex(WATCH_COLOR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(list, lv_color_hex(WATCH_COLOR_SURFACE2), LV_PART_SELECTED);
    lv_obj_set_style_border_width(list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(list, lv_color_hex(WATCH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(list, 10, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
}

static lv_obj_t * create_dropdown(lv_obj_t * parent, int16_t x, int16_t y,
                                  int16_t width, const char * options, uint8_t selected)
{
    lv_obj_t * dropdown = lv_dropdown_create(parent);
    lv_obj_set_size(dropdown, width, 36);
    lv_obj_set_pos(dropdown, x, y);
    lv_dropdown_set_options_static(dropdown, options);
    lv_dropdown_set_selected(dropdown, selected);
    lv_dropdown_set_dir(dropdown, LV_DIR_BOTTOM);
    lv_dropdown_set_selected_highlight(dropdown, true);
    lv_obj_set_style_text_font(dropdown, watch_ui_get_cn_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, lv_color_hex(WATCH_COLOR_CYAN), LV_PART_MAIN);
    lv_obj_set_style_bg_color(dropdown, lv_color_hex(WATCH_COLOR_SURFACE2), LV_PART_MAIN);
    lv_obj_set_style_radius(dropdown, 11, LV_PART_MAIN);
    lv_obj_set_style_border_width(dropdown, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dropdown, lv_color_hex(WATCH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_add_flag(dropdown, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(dropdown, dropdown_ready_cb, LV_EVENT_READY, NULL);
    return dropdown;
}

/* ---------------------------- 课程列表 ---------------------------- */

static void edit_course_clicked_cb(lv_event_t * event)
{
    editing_slot = (int8_t)(intptr_t)lv_event_get_user_data(event);
    const watch_schedule_course_t * course =
        watch_schedule_data_get_course(selected_week, selected_day, (uint8_t)editing_slot);
    if(course == NULL) return;
    editing_course = *course;
    build_course_editor();
}

static void add_course_clicked_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    if(watch_schedule_data_course_count(selected_week, selected_day) >=
       WATCH_SCHEDULE_MAX_COURSES) return;
    memset(&editing_course, 0, sizeof(editing_course));
    editing_course.active = true;
    editing_course.color =
        course_colors[watch_schedule_data_course_count(selected_week, selected_day)];
    /* 新增课程名称保持为空，进入输入界面后直接显示光标。 */
    copy_text(editing_course.name, sizeof(editing_course.name), "");
    copy_text(editing_course.time, sizeof(editing_course.time), "");
    copy_text(editing_course.location, sizeof(editing_course.location), "");
    editing_slot = -1;
    build_course_editor();
}

static void rebuild_course_list(void)
{
    if(course_list == NULL || !lv_obj_is_valid(course_list)) return;
    lv_obj_clean(course_list);
    const uint8_t count = watch_schedule_data_course_count(selected_week, selected_day);

    int16_t y = 0;
    for(uint8_t slot = 0U; slot < WATCH_SCHEDULE_MAX_COURSES; slot++) {
        const watch_schedule_course_t * course =
            watch_schedule_data_get_course(selected_week, selected_day, slot);
        if(course == NULL) continue;

        lv_obj_t * card = watch_ui_make_card(course_list, 204, 52,
                                             lv_color_hex(WATCH_COLOR_SURFACE));
        lv_obj_set_pos(card, 0, y);
        lv_obj_set_style_radius(card, 13, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, edit_course_clicked_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)slot);

        lv_obj_t * marker = watch_ui_make_card(card, 5, 30, lv_color_hex(course->color));
        lv_obj_set_pos(marker, 9, 11);
        lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
        watch_ui_make_label(card, course->name, watch_ui_get_cn_font(),
                            lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_TOP_LEFT, 22, 7);

        char detail[80];
        snprintf(detail, sizeof(detail), "%s  %s", course->time, course->location);
        lv_obj_t * detail_label = watch_ui_make_label(
            card, detail, watch_ui_get_cn_font(), lv_color_hex(WATCH_COLOR_MUTED),
            LV_ALIGN_BOTTOM_LEFT, 22, -6);
        lv_obj_set_width(detail_label, 174);
        lv_label_set_long_mode(detail_label, LV_LABEL_LONG_DOT);
        y += 58;
    }

    if(count < WATCH_SCHEDULE_MAX_COURSES) {
        lv_obj_t * add = watch_ui_make_card(course_list, 204, 42,
                                            lv_color_hex(WATCH_COLOR_SURFACE2));
        lv_obj_set_pos(add, 0, y);
        lv_obj_set_style_radius(add, 13, 0);
        lv_obj_set_style_border_width(add, 1, 0);
        lv_obj_set_style_border_color(add, lv_color_hex(WATCH_COLOR_BORDER), 0);
        lv_obj_add_flag(add, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(add, add_course_clicked_cb, LV_EVENT_CLICKED, NULL);
        watch_ui_make_label(add, "+ 添加课程", watch_ui_get_cn_font(),
                            lv_color_hex(WATCH_COLOR_CYAN), LV_ALIGN_CENTER, 0, 0);
    }
    else {
        watch_ui_make_label(course_list, "每天最多五节课", watch_ui_get_cn_font(),
                            lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_BOTTOM_MID, 0, -4);
    }
}

static void manager_week_changed_cb(lv_event_t * event)
{
    selected_week = (uint8_t)(lv_dropdown_get_selected(lv_event_get_target_obj(event)) + 1U);
    rebuild_course_list();
}

static void manager_day_changed_cb(lv_event_t * event)
{
    selected_day = (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(event));
    rebuild_course_list();
}

/* ---------------------------- 日期设置 ---------------------------- */

static void manager_back_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    watch_ui_show_detail(WATCH_APP_SETTINGS);
}

static void editor_back_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    build_manager_page();
}

static void date_save_cb(lv_event_t * event)
{
    lv_obj_t ** selectors = lv_event_get_user_data(event);
    watch_schedule_date_t date = {
        .year = (uint16_t)(2020U + lv_dropdown_get_selected(selectors[0])),
        .month = (uint8_t)(1U + lv_dropdown_get_selected(selectors[1])),
        .day = (uint8_t)(1U + lv_dropdown_get_selected(selectors[2])),
    };
    static const uint8_t month_days[] =
        { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint8_t maximum = month_days[date.month - 1U];
    if(date.month == 2U &&
       (((date.year % 4U) == 0U && (date.year % 100U) != 0U) ||
        (date.year % 400U) == 0U)) maximum = 29U;
    if(date.day > maximum) date.day = maximum;
    watch_schedule_data_set_start_date(date);
    build_manager_page();
}

static void start_date_clicked_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    active_page = SETTINGS_PAGE_START_DATE;
    lv_obj_t * layer = create_layer();
    add_header(layer, "学期起始日期", editor_back_cb);
    watch_ui_make_label(layer, "第一周周一", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_TOP_MID, 0, 61);

    static char year_options[800];
    year_options[0] = '\0';
    for(uint16_t year = 2020U; year <= 2099U; year++) {
        char item[10];
        snprintf(item, sizeof(item), "%u年%s", (unsigned)year,
                 year == 2099U ? "" : "\n");
        strncat(year_options, item, sizeof(year_options) - strlen(year_options) - 1U);
    }

    const watch_schedule_date_t date = watch_schedule_data_get_start_date();
    static lv_obj_t * selectors[3];
    selectors[0] = create_dropdown(layer, 8, 91, 86, year_options,
                                   (uint8_t)(date.year - 2020U));
    selectors[1] = create_dropdown(layer, 97, 91, 66, month_options,
                                   (uint8_t)(date.month - 1U));
    selectors[2] = create_dropdown(layer, 166, 91, 66, day_number_options,
                                   (uint8_t)(date.day - 1U));

    lv_obj_t * save = create_action_button(layer, 18, 157, 204, "保存起始日期",
                                           WATCH_COLOR_CYAN, date_save_cb);
    lv_obj_remove_event_cb(save, date_save_cb);
    lv_obj_add_event_cb(save, date_save_cb, LV_EVENT_CLICKED, selectors);
    watch_ui_make_label(layer, "后续18周日期将自动重新计算", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_TOP_MID, 0, 214);
}

/* ---------------------------- 屏幕键盘 ---------------------------- */

static const char * const time_quick_map[] = {
    "08:00", "10:10", "14:00", " - ", "\n",
    "09:40", "11:50", "15:40", "18:30", ""
};

typedef struct {
    const char * pinyin;
    const char * candidates[4];
} pinyin_entry_t;

/*
 * 面向课程名称和上课地点的轻量拼音词库。
 * 既支持完整词组拼音，也支持逐字输入；后续可继续追加词条。
 */
static const pinyin_entry_t pinyin_dictionary[] = {
    { "gaodengshuxue",   { "高等数学", NULL } },
    { "daxueyingyu",     { "大学英语", NULL } },
    { "daxuewuli",       { "大学物理", NULL } },
    { "shujujiegou",     { "数据结构", NULL } },
    { "dianlujichu",     { "电路基础", NULL } },
    { "shuzidianzijishu",{ "数字电子技术", NULL } },
    { "tiyuyundong",     { "体育运动", NULL } },
    { "chuanganqiyuanli",{ "传感器原理", NULL } },
    { "qianrushishiyan", { "嵌入式实验", NULL } },
    { "qianrushixitong", { "嵌入式系统", NULL } },
    { "wulianwangsheji", { "物联网设计", NULL } },
    { "caozuoxitong",    { "操作系统", NULL } },
    { "jisuanjiwangluo", { "计算机网络", NULL } },
    { "chuangxinshijian",{ "创新实践", NULL } },
    { "xiangmushijian",  { "项目实践", NULL } },
    { "zizhuxuexi",      { "自主学习", NULL } },
    { "jiaoxuelou",      { "教学楼", NULL } },
    { "shiyanlou",       { "实验楼", NULL } },
    { "tushuguan",       { "图书馆", NULL } },
    { "tiyuguan",        { "体育馆", NULL } },
    { "chuangxinzhongxin",{ "创新中心", NULL } },
    { "jiaoshi",         { "教室", NULL } },
    { "gao",             { "高", NULL } },
    { "deng",            { "等", NULL } },
    { "shu",             { "数", "书", NULL } },
    { "xue",             { "学", NULL } },
    { "da",              { "大", NULL } },
    { "ying",            { "英", NULL } },
    { "yu",              { "语", "育", NULL } },
    { "wu",              { "物", NULL } },
    { "li",              { "理", NULL } },
    { "dian",            { "电", NULL } },
    { "lu",              { "路", NULL } },
    { "ji",              { "计", "机", "基", "技" } },
    { "chu",             { "础", NULL } },
    { "zi",              { "字", "自", NULL } },
    { "ti",              { "体", NULL } },
    { "yun",             { "运", NULL } },
    { "dong",            { "动", NULL } },
    { "chuan",           { "传", NULL } },
    { "gan",             { "感", NULL } },
    { "qi",              { "器", NULL } },
    { "yuan",            { "原", NULL } },
    { "qian",            { "嵌", NULL } },
    { "ru",              { "入", NULL } },
    { "shi",             { "实", "时", "式", "室" } },
    { "yan",             { "验", NULL } },
    { "xi",              { "系", "习", NULL } },
    { "tong",            { "统", NULL } },
    { "wang",            { "网", NULL } },
    { "lian",            { "联", "练", NULL } },
    { "she",             { "设", NULL } },
    { "cao",             { "操", NULL } },
    { "zuo",             { "作", NULL } },
    { "suan",            { "算", NULL } },
    { "jie",             { "结", NULL } },
    { "gou",             { "构", NULL } },
    { "chuang",          { "创", NULL } },
    { "xin",             { "新", NULL } },
    { "xiang",           { "项", NULL } },
    { "mu",              { "目", NULL } },
    { "ke",              { "课", NULL } },
    { "cheng",           { "程", NULL } },
    { "jiao",            { "教", NULL } },
    { "lou",             { "楼", NULL } },
    { "tu",              { "图", NULL } },
    { "guan",            { "馆", NULL } },
    { "zhong",           { "中", NULL } },
    { "xin",             { "心", "新", NULL } },
};

static const char * candidate_map[6] = { "直接输入拼音", "" };
static uint8_t candidate_count;

static bool text_starts_with(const char * text, const char * prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool candidate_already_added(const char * text)
{
    for(uint8_t i = 0U; i < candidate_count; i++) {
        if(strcmp(candidate_map[i], text) == 0) return true;
    }
    return false;
}

static void collect_candidates(const char * pinyin, bool exact_only)
{
    const uint32_t dictionary_count =
        sizeof(pinyin_dictionary) / sizeof(pinyin_dictionary[0]);
    for(uint32_t entry = 0U; entry < dictionary_count && candidate_count < 4U; entry++) {
        const bool matches = exact_only ?
            strcmp(pinyin_dictionary[entry].pinyin, pinyin) == 0 :
            text_starts_with(pinyin_dictionary[entry].pinyin, pinyin);
        if(!matches) continue;
        for(uint8_t item = 0U; item < 4U && candidate_count < 4U; item++) {
            const char * candidate = pinyin_dictionary[entry].candidates[item];
            if(candidate == NULL) break;
            if(!candidate_already_added(candidate)) {
                candidate_map[candidate_count++] = candidate;
            }
        }
    }
}

static uint8_t get_active_pinyin(char * buffer, uint8_t buffer_size)
{
    if(buffer == NULL || buffer_size == 0U || keyboard_textarea == NULL) return 0U;
    const char * text = lv_textarea_get_text(keyboard_textarea);
    const uint32_t cursor_position = lv_textarea_get_cursor_pos(keyboard_textarea);

    uint32_t byte_position = 0U;
    uint32_t character_position = 0U;
    while(text[byte_position] != '\0' && character_position < cursor_position) {
        const uint8_t lead = (uint8_t)text[byte_position];
        if((lead & 0x80U) == 0U) byte_position += 1U;
        else if((lead & 0xE0U) == 0xC0U) byte_position += 2U;
        else if((lead & 0xF0U) == 0xE0U) byte_position += 3U;
        else byte_position += 4U;
        character_position++;
    }

    uint32_t start = byte_position;
    while(start > 0U) {
        const char character = text[start - 1U];
        if(character < 'a' || character > 'z') break;
        start--;
    }

    uint32_t length = byte_position - start;
    if(length >= buffer_size) length = buffer_size - 1U;
    memcpy(buffer, &text[start], length);
    buffer[length] = '\0';
    return (uint8_t)length;
}

static void refresh_pinyin_candidates(void)
{
    if(candidate_matrix == NULL || keyboard_textarea == NULL) return;
    char pinyin[32];
    const uint8_t pinyin_length = get_active_pinyin(pinyin, sizeof(pinyin));
    candidate_count = 0U;
    if(pinyin_length > 0U) {
        collect_candidates(pinyin, true);
        if(candidate_count < 4U) collect_candidates(pinyin, false);
    }

    if(candidate_count == 0U) {
        candidate_map[0] = pinyin_length == 0U ? "直接输入拼音" : "暂无候选";
        candidate_count = 1U;
    }
    candidate_map[candidate_count] = "";
    lv_buttonmatrix_set_map(candidate_matrix, candidate_map);
}

static void main_input_changed_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    if(chinese_input_mode) refresh_pinyin_candidates();
}

static uint8_t remove_active_pinyin(void)
{
    char pinyin[32];
    const uint8_t length = get_active_pinyin(pinyin, sizeof(pinyin));
    for(uint8_t i = 0U; i < length; i++) lv_textarea_delete_char(keyboard_textarea);
    return length;
}

static bool commit_first_candidate(void)
{
    if(candidate_matrix == NULL || keyboard_textarea == NULL) {
        return false;
    }
    refresh_pinyin_candidates();
    if(candidate_count == 0U || strcmp(candidate_map[0], "直接输入拼音") == 0 ||
       strcmp(candidate_map[0], "暂无候选") == 0) return false;
    if(remove_active_pinyin() == 0U) return false;
    lv_textarea_add_text(keyboard_textarea, candidate_map[0]);
    return true;
}

static void candidate_clicked_cb(lv_event_t * event)
{
    if(keyboard_textarea == NULL) return;
    const uint32_t button = lv_buttonmatrix_get_selected_button(
        lv_event_get_target_obj(event));
    if(button == LV_BUTTONMATRIX_BUTTON_NONE) return;
    const char * text = lv_buttonmatrix_get_button_text(
        lv_event_get_target_obj(event), button);
    if(text == NULL || strcmp(text, "直接输入拼音") == 0 ||
       strcmp(text, "暂无候选") == 0) return;
    if(remove_active_pinyin() == 0U) return;
    lv_textarea_add_text(keyboard_textarea, text);
}

static void apply_keyboard_input_mode(void)
{
    if(keyboard_object == NULL || active_field == EDIT_FIELD_TIME) return;
    if(input_mode_label != NULL) lv_label_set_text(input_mode_label,
                                                   chinese_input_mode ? "中" : "英");
    lv_keyboard_set_textarea(keyboard_object, keyboard_textarea);
    lv_keyboard_set_mode(keyboard_object, LV_KEYBOARD_MODE_TEXT_LOWER);
    if(chinese_input_mode) {
        lv_obj_clear_flag(candidate_matrix, LV_OBJ_FLAG_HIDDEN);
        refresh_pinyin_candidates();
    }
    else {
        lv_obj_add_flag(candidate_matrix, LV_OBJ_FLAG_HIDDEN);
    }
    lv_textarea_set_cursor_pos(keyboard_textarea, LV_TEXTAREA_CURSOR_LAST);
}

static void input_mode_clicked_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    chinese_input_mode = !chinese_input_mode;
    apply_keyboard_input_mode();
}

static void close_keyboard(bool accept)
{
    if(keyboard_overlay == NULL) return;
    if(accept && keyboard_textarea != NULL) {
        const char * text = lv_textarea_get_text(keyboard_textarea);
        if(active_field == EDIT_FIELD_NAME) {
            copy_text(editing_course.name, sizeof(editing_course.name), text);
        }
        else if(active_field == EDIT_FIELD_TIME) {
            copy_text(editing_course.time, sizeof(editing_course.time), text);
        }
        else {
            copy_text(editing_course.location, sizeof(editing_course.location), text);
        }
        lv_label_set_text(field_value_labels[active_field], text);
    }

    lv_obj_t * overlay = keyboard_overlay;
    keyboard_overlay = NULL;
    keyboard_textarea = NULL;
    keyboard_object = NULL;
    candidate_matrix = NULL;
    input_mode_label = NULL;
    lv_obj_delete_async(overlay);
    watch_ui_set_brightness(watch_ui_get_brightness());
}

static void keyboard_ready_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    if(chinese_input_mode && commit_first_candidate()) return;
    close_keyboard(true);
}

static void keyboard_cancel_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    close_keyboard(false);
}

static void quick_input_cb(lv_event_t * event)
{
    const uint32_t button = lv_buttonmatrix_get_selected_button(
        lv_event_get_target_obj(event));
    if(button == LV_BUTTONMATRIX_BUTTON_NONE || keyboard_textarea == NULL) return;
    const char * text = lv_buttonmatrix_get_button_text(
        lv_event_get_target_obj(event), button);
    if(text != NULL) lv_textarea_add_text(keyboard_textarea, text);
}

static void input_textarea_clicked_cb(lv_event_t * event)
{
    lv_obj_t * textarea = lv_event_get_target_obj(event);
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    /* 重新发送聚焦事件，使点击后光标从可见阶段重新开始闪烁。 */
    lv_obj_send_event(textarea, LV_EVENT_FOCUSED, NULL);
    lv_obj_invalidate(textarea);
}

static void style_input_textarea(lv_obj_t * textarea)
{
    lv_obj_set_style_text_font(textarea, watch_ui_get_cn_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(textarea, lv_color_hex(WATCH_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(textarea, lv_color_hex(WATCH_COLOR_SURFACE2), LV_PART_MAIN);
    lv_obj_set_style_border_color(textarea, lv_color_hex(WATCH_COLOR_CYAN), LV_PART_MAIN);
    lv_obj_set_style_border_width(textarea, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(textarea, 10, LV_PART_MAIN);

    /* 使用3像素主题前景色竖线作为所有输入字段的高对比度闪烁光标。 */
    lv_obj_set_style_bg_opa(textarea, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_border_color(textarea, lv_color_hex(WATCH_COLOR_TEXT), LV_PART_CURSOR);
    lv_obj_set_style_border_width(textarea, 3, LV_PART_CURSOR);
    lv_obj_set_style_border_side(textarea, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_pad_left(textarea, -1, LV_PART_CURSOR);
    /*
     * LVGL 默认主题对聚焦光标另有状态样式，需要用同等状态优先级覆盖，
     * 否则点击输入框后会被默认主题覆盖。
     */
    lv_obj_set_style_bg_opa(textarea, LV_OPA_TRANSP,
                            LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(textarea, lv_color_hex(WATCH_COLOR_TEXT),
                                  LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(textarea, 3,
                                  LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(textarea, LV_BORDER_SIDE_LEFT,
                                 LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(textarea, lv_color_hex(WATCH_COLOR_TEXT),
                                LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(textarea, lv_color_hex(WATCH_COLOR_TEXT),
                                  LV_PART_CURSOR | LV_STATE_EDITED);
    /* 显示0.5秒、隐藏0.5秒，形成一秒一个完整周期。 */
    lv_obj_set_style_anim_duration(textarea, 500, LV_PART_CURSOR);
    lv_textarea_set_cursor_pos(textarea, LV_TEXTAREA_CURSOR_LAST);
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    lv_obj_send_event(textarea, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(textarea, input_textarea_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_invalidate(textarea);
}

static void open_keyboard(edit_field_t field)
{
    if(keyboard_overlay != NULL) return;
    active_field = field;
    keyboard_overlay = lv_obj_create(settings_root);
    lv_obj_set_size(keyboard_overlay, WATCH_VIEWPORT_WIDTH, WATCH_VIEWPORT_HEIGHT);
    lv_obj_set_pos(keyboard_overlay, 0, 0);
    lv_obj_set_style_bg_color(
        keyboard_overlay,
        lv_color_hex(watch_ui_theme_pick(0x090D14, 0xEEF2F5)), 0);
    lv_obj_set_style_bg_opa(keyboard_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(keyboard_overlay, 0, 0);
    lv_obj_set_style_pad_all(keyboard_overlay, 0, 0);
    lv_obj_clear_flag(keyboard_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(keyboard_overlay);

    static const char * titles[] = { "输入课程名称", "输入上课时段", "输入上课地点" };
    watch_ui_make_label(keyboard_overlay, titles[field], watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_TEXT), LV_ALIGN_TOP_MID, 0, 7);

    keyboard_textarea = lv_textarea_create(keyboard_overlay);
    lv_obj_set_size(keyboard_textarea, 216, 36);
    lv_obj_set_pos(keyboard_textarea, 12, 30);
    lv_textarea_set_one_line(keyboard_textarea, true);
    lv_textarea_set_max_length(keyboard_textarea,
                               field == EDIT_FIELD_TIME ? 20U : 32U);
    const char * current = field == EDIT_FIELD_NAME ? editing_course.name :
                           field == EDIT_FIELD_TIME ? editing_course.time :
                           editing_course.location;
    lv_textarea_set_text(keyboard_textarea, current);
    style_input_textarea(keyboard_textarea);
    lv_obj_add_event_cb(keyboard_textarea, main_input_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    if(field == EDIT_FIELD_TIME) {
        chinese_input_mode = false;
        lv_obj_t * quick = lv_buttonmatrix_create(keyboard_overlay);
        lv_obj_set_size(quick, 216, 54);
        lv_obj_set_pos(quick, 12, 71);
        lv_buttonmatrix_set_map(quick, time_quick_map);
        lv_obj_set_style_text_font(quick, watch_ui_get_cn_font(), LV_PART_ITEMS);
        lv_obj_set_style_bg_color(quick, lv_color_hex(WATCH_COLOR_SURFACE), LV_PART_MAIN);
        lv_obj_set_style_bg_color(quick, lv_color_hex(WATCH_COLOR_SURFACE2), LV_PART_ITEMS);
        lv_obj_set_style_text_color(quick, lv_color_hex(WATCH_COLOR_CYAN), LV_PART_ITEMS);
        lv_obj_set_style_radius(quick, 8, LV_PART_MAIN);
        lv_obj_set_style_radius(quick, 6, LV_PART_ITEMS);
        lv_obj_add_event_cb(quick, quick_input_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    else {
        chinese_input_mode = true;
        /* 中英切换按钮嵌入原文本框右侧，不再创建独立拼音框。 */
        lv_obj_set_style_pad_right(keyboard_textarea, 44, LV_PART_MAIN);
        lv_obj_t * mode_button = watch_ui_make_card(
            keyboard_overlay, 34, 28, lv_color_hex(WATCH_COLOR_CYAN));
        lv_obj_set_pos(mode_button, 190, 34);
        lv_obj_set_style_radius(mode_button, 8, 0);
        lv_obj_add_flag(mode_button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(mode_button, input_mode_clicked_cb, LV_EVENT_CLICKED, NULL);
        input_mode_label = watch_ui_make_label(
            mode_button, "中", watch_ui_get_cn_font(), lv_color_hex(WATCH_COLOR_BG),
            LV_ALIGN_CENTER, 0, 0);

        candidate_matrix = lv_buttonmatrix_create(keyboard_overlay);
        lv_obj_set_size(candidate_matrix, 216, 54);
        lv_obj_set_pos(candidate_matrix, 12, 71);
        lv_buttonmatrix_set_map(candidate_matrix, candidate_map);
        lv_obj_set_style_text_font(candidate_matrix, watch_ui_get_cn_font(), LV_PART_ITEMS);
        lv_obj_set_style_bg_color(candidate_matrix,
                                  lv_color_hex(WATCH_COLOR_SURFACE), LV_PART_MAIN);
        lv_obj_set_style_bg_color(candidate_matrix,
                                  lv_color_hex(WATCH_COLOR_SURFACE2), LV_PART_ITEMS);
        lv_obj_set_style_text_color(candidate_matrix,
                                    lv_color_hex(WATCH_COLOR_TEXT), LV_PART_ITEMS);
        lv_obj_set_style_radius(candidate_matrix, 7, LV_PART_MAIN);
        lv_obj_set_style_radius(candidate_matrix, 5, LV_PART_ITEMS);
        lv_obj_add_event_cb(candidate_matrix, candidate_clicked_cb,
                            LV_EVENT_VALUE_CHANGED, NULL);
        refresh_pinyin_candidates();
    }

    keyboard_object = lv_keyboard_create(keyboard_overlay);
    lv_obj_set_size(keyboard_object, WATCH_VIEWPORT_WIDTH, 150);
    lv_obj_align(keyboard_object, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard_object, keyboard_textarea);
    lv_keyboard_set_popovers(keyboard_object, false);
    /* 时间段与名称、地点统一使用完整字母键盘。 */
    lv_keyboard_set_mode(keyboard_object, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_style_text_font(keyboard_object, &lv_font_montserrat_12, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard_object,
                              lv_color_hex(WATCH_COLOR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(keyboard_object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(keyboard_object,
                              lv_color_hex(WATCH_COLOR_SURFACE2), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(keyboard_object, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard_object,
                                lv_color_hex(WATCH_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard_object, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(keyboard_object,
                                  lv_color_hex(WATCH_COLOR_BORDER), LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard_object, 5, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard_object, lv_color_hex(WATCH_COLOR_CYAN),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(keyboard_object, lv_color_hex(WATCH_COLOR_BG),
                                LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(keyboard_object, lv_color_hex(WATCH_COLOR_CYAN),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(keyboard_object, lv_color_hex(WATCH_COLOR_BG),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(keyboard_object, keyboard_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard_object, keyboard_cancel_cb, LV_EVENT_CANCEL, NULL);
    if(field != EDIT_FIELD_TIME) apply_keyboard_input_mode();
}

static void field_clicked_cb(lv_event_t * event)
{
    open_keyboard((edit_field_t)(intptr_t)lv_event_get_user_data(event));
}

/* ---------------------------- 课程编辑 ---------------------------- */

static lv_obj_t * add_editor_field(lv_obj_t * parent, int16_t y, const char * title,
                                   const char * value, edit_field_t field)
{
    lv_obj_t * card = watch_ui_make_card(parent, 212, 39,
                                         lv_color_hex(WATCH_COLOR_SURFACE));
    lv_obj_set_pos(card, 14, y);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, field_clicked_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)field);
    watch_ui_make_label(card, title, watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t * label = watch_ui_make_label(card, value, watch_ui_get_cn_font(),
                                           lv_color_hex(WATCH_COLOR_TEXT),
                                           LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_width(label, 138);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    field_value_labels[field] = label;
    return card;
}

static void course_save_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    if(editing_course.name[0] == '\0') copy_text(editing_course.name,
                                                 sizeof(editing_course.name), "未命名课程");
    if(editing_slot < 0) {
        watch_schedule_data_add_course(selected_week, selected_day, &editing_course);
    }
    else {
        watch_schedule_data_update_course(selected_week, selected_day,
                                          (uint8_t)editing_slot, &editing_course);
    }
    build_manager_page();
}

static void course_delete_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    if(editing_slot >= 0) {
        watch_schedule_data_remove_course(selected_week, selected_day,
                                          (uint8_t)editing_slot);
    }
    build_manager_page();
}

static void build_course_editor(void)
{
    active_page = SETTINGS_PAGE_COURSE_EDITOR;
    lv_obj_t * layer = create_layer();
    add_header(layer, editing_slot < 0 ? "添加课程" : "修改课程", editor_back_cb);
    add_editor_field(layer, 61, "名称", editing_course.name, EDIT_FIELD_NAME);
    add_editor_field(layer, 108, "时段", editing_course.time, EDIT_FIELD_TIME);
    add_editor_field(layer, 155, "地点", editing_course.location, EDIT_FIELD_LOCATION);
    create_action_button(layer, 14, 207, editing_slot < 0 ? 212 : 132, "保存",
                         WATCH_COLOR_CYAN, course_save_cb);
    if(editing_slot >= 0) {
        create_action_button(layer, 154, 207, 72, "删除",
                             WATCH_COLOR_RED, course_delete_cb);
    }
}

/* ---------------------------- 管理首页 ---------------------------- */

static void build_manager_page(void)
{
    active_page = SETTINGS_PAGE_MANAGER;
    lv_obj_t * layer = create_layer();
    add_header(layer, "课表设置", manager_back_cb);

    const watch_schedule_date_t start_date = watch_schedule_data_get_start_date();
    char date_text[24];
    watch_schedule_data_format_date(start_date, date_text, sizeof(date_text));
    lv_obj_t * date_card = watch_ui_make_card(layer, 212, 38,
                                              lv_color_hex(WATCH_COLOR_SURFACE));
    lv_obj_set_pos(date_card, 14, 55);
    lv_obj_set_style_radius(date_card, 12, 0);
    lv_obj_add_flag(date_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(date_card, start_date_clicked_cb, LV_EVENT_CLICKED, NULL);
    watch_ui_make_label(date_card, "学期开始", watch_ui_get_cn_font(),
                        lv_color_hex(WATCH_COLOR_MUTED), LV_ALIGN_LEFT_MID, 10, 0);
    watch_ui_make_label(date_card, date_text, &lv_font_montserrat_14,
                        lv_color_hex(WATCH_COLOR_CYAN), LV_ALIGN_RIGHT_MID, -10, 0);

    lv_obj_t * week = create_dropdown(layer, 14, 99, 100, week_options, selected_week - 1U);
    lv_obj_t * day = create_dropdown(layer, 126, 99, 100, day_options, selected_day);
    lv_obj_add_event_cb(week, manager_week_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(day, manager_day_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    course_list = lv_obj_create(layer);
    lv_obj_set_size(course_list, 212, 139);
    lv_obj_set_pos(course_list, 14, 141);
    lv_obj_set_style_bg_opa(course_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(course_list, 0, 0);
    lv_obj_set_style_pad_all(course_list, 0, 0);
    lv_obj_set_scroll_dir(course_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(course_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(course_list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(course_list, lv_color_hex(WATCH_COLOR_CYAN),
                              LV_PART_SCROLLBAR);
    lv_obj_add_flag(course_list, LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_EVENT_BUBBLE);
    rebuild_course_list();
}

void watch_schedule_settings_build(lv_obj_t * root)
{
    settings_root = root;
    keyboard_overlay = NULL;
    keyboard_textarea = NULL;
    keyboard_object = NULL;
    candidate_matrix = NULL;
    input_mode_label = NULL;
    swipe_tracking = false;
    content_layer = NULL;
    course_list = NULL;
    watch_schedule_data_init();
    build_manager_page();
}

void watch_schedule_settings_deactivate(void)
{
    settings_root = NULL;
    content_layer = NULL;
    course_list = NULL;
    keyboard_overlay = NULL;
    keyboard_textarea = NULL;
    keyboard_object = NULL;
    candidate_matrix = NULL;
    input_mode_label = NULL;
    for(uint8_t i = 0U; i < 3U; i++) field_value_labels[i] = NULL;
}
