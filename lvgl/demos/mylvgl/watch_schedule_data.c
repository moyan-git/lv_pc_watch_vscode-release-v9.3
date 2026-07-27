/**
 * @file watch_schedule_data.c
 * @brief 电子课表共享数据模块。
 *
 * 主要职责：
 * - 保存18周、每周7天、每天最多5节课程；
 * - 根据学期起始日期计算任意周的实际日期；
 * - 为课表展示页和课表设置页提供同一份数据；
 * - 在PC模拟器中保存到 watch_schedule.dat，重启后仍可恢复。
 */
#include "watch_schedule_data.h"

#include <stdio.h>
#include <string.h>

#define SCHEDULE_FILE_NAME "watch_schedule.dat"
#define SCHEDULE_FILE_MAGIC 0x57434831UL
#define SCHEDULE_FILE_VERSION 1U

typedef struct {
    uint32_t magic;
    uint16_t version;
    watch_schedule_date_t start_date;
    watch_schedule_course_t courses[WATCH_SCHEDULE_WEEK_COUNT]
                                           [WATCH_SCHEDULE_DAY_COUNT]
                                           [WATCH_SCHEDULE_MAX_COURSES];
} schedule_store_t;

static schedule_store_t schedule_store;
static bool schedule_initialized;

static bool is_leap_year(uint16_t year)
{
    return ((year % 4U) == 0U && (year % 100U) != 0U) || (year % 400U) == 0U;
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t month_days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if(month < 1U || month > 12U) return 0U;
    if(month == 2U && is_leap_year(year)) return 29U;
    return month_days[month - 1U];
}

static bool is_valid_date(watch_schedule_date_t date)
{
    if(date.year < 2020U || date.year > 2099U || date.month < 1U || date.month > 12U) {
        return false;
    }
    return date.day >= 1U && date.day <= days_in_month(date.year, date.month);
}

static watch_schedule_date_t add_days(watch_schedule_date_t date, uint16_t days)
{
    while(days > 0U) {
        const uint8_t month_days = days_in_month(date.year, date.month);
        const uint16_t remaining = (uint16_t)(month_days - date.day);
        if(days <= remaining) {
            date.day = (uint8_t)(date.day + days);
            break;
        }

        days = (uint16_t)(days - remaining - 1U);
        date.day = 1U;
        date.month++;
        if(date.month > 12U) {
            date.month = 1U;
            date.year++;
        }
    }
    return date;
}

static void copy_text(char * destination, uint32_t size, const char * source)
{
    if(destination == NULL || size == 0U) return;
    if(source == NULL) source = "";
    snprintf(destination, size, "%s", source);
}

static void set_default_course(uint8_t week, uint8_t weekday, uint8_t slot,
                               const char * name, const char * time,
                               const char * location, uint32_t color)
{
    watch_schedule_course_t * course =
        &schedule_store.courses[week - 1U][weekday][slot];
    course->active = true;
    copy_text(course->name, sizeof(course->name), name);
    copy_text(course->time, sizeof(course->time), time);
    copy_text(course->location, sizeof(course->location), location);
    course->color = color;
}

static void create_defaults(void)
{
    memset(&schedule_store, 0, sizeof(schedule_store));
    schedule_store.magic = SCHEDULE_FILE_MAGIC;
    schedule_store.version = SCHEDULE_FILE_VERSION;
    schedule_store.start_date = (watch_schedule_date_t){ 2026U, 6U, 29U };

    set_default_course(4, 0, 0, "高等数学", "08:00 - 09:40", "教学楼 A201", WATCH_COLOR_CYAN);
    set_default_course(4, 0, 1, "大学英语", "10:10 - 11:50", "教学楼 B305", WATCH_COLOR_PURPLE);
    set_default_course(4, 0, 2, "电路基础", "14:00 - 15:40", "实验楼 210", WATCH_COLOR_ORANGE);
    set_default_course(4, 1, 0, "数据结构", "08:00 - 09:40", "教学楼 C102", WATCH_COLOR_BLUE);
    set_default_course(4, 1, 1, "大学物理", "10:10 - 11:50", "教学楼 A105", WATCH_COLOR_CYAN);
    set_default_course(4, 1, 2, "数字电子技术", "14:00 - 15:40", "实验楼 306", WATCH_COLOR_PURPLE);
    set_default_course(4, 1, 3, "体育运动", "16:00 - 17:40", "体育馆", WATCH_COLOR_GREEN);
    set_default_course(4, 2, 0, "传感器原理", "10:10 - 11:50", "教学楼 B201", WATCH_COLOR_ORANGE);
    set_default_course(4, 2, 1, "嵌入式实验", "14:00 - 15:40", "实验楼 402", WATCH_COLOR_GREEN);
    set_default_course(4, 3, 0, "嵌入式系统", "08:00 - 09:40", "实验楼 401", WATCH_COLOR_CYAN);
    set_default_course(4, 3, 1, "数据结构", "10:10 - 11:50", "教学楼 C102", WATCH_COLOR_PURPLE);
    set_default_course(4, 3, 2, "物联网设计", "13:30 - 15:00", "创新中心", WATCH_COLOR_ORANGE);
    set_default_course(4, 3, 3, "体育运动", "15:10 - 16:40", "体育馆", WATCH_COLOR_GREEN);
    set_default_course(4, 3, 4, "大学英语", "18:30 - 20:00", "教学楼 B305", WATCH_COLOR_BLUE);
    set_default_course(4, 4, 0, "操作系统", "08:00 - 09:40", "教学楼 C203", WATCH_COLOR_RED);
    set_default_course(4, 4, 1, "计算机网络", "10:10 - 11:50", "教学楼 C301", WATCH_COLOR_BLUE);
    set_default_course(4, 4, 2, "创新实践", "14:00 - 15:40", "创新中心", WATCH_COLOR_GREEN);
    set_default_course(4, 5, 0, "项目实践", "09:00 - 10:40", "实验楼 405", WATCH_COLOR_ORANGE);
    set_default_course(4, 5, 1, "创新训练", "14:30 - 16:10", "创新中心", WATCH_COLOR_GREEN);
    set_default_course(4, 6, 0, "自主学习", "10:00 - 11:30", "图书馆", WATCH_COLOR_CYAN);
}

void watch_schedule_data_init(void)
{
    if(schedule_initialized) return;
    schedule_initialized = true;

    FILE * file = fopen(SCHEDULE_FILE_NAME, "rb");
    if(file != NULL) {
        const size_t read_count = fread(&schedule_store, sizeof(schedule_store), 1U, file);
        fclose(file);
        if(read_count == 1U && schedule_store.magic == SCHEDULE_FILE_MAGIC &&
           schedule_store.version == SCHEDULE_FILE_VERSION &&
           is_valid_date(schedule_store.start_date)) {
            return;
        }
    }

    create_defaults();
    watch_schedule_data_save();
}

watch_schedule_date_t watch_schedule_data_get_start_date(void)
{
    watch_schedule_data_init();
    return schedule_store.start_date;
}

bool watch_schedule_data_set_start_date(watch_schedule_date_t date)
{
    watch_schedule_data_init();
    if(!is_valid_date(date)) return false;
    schedule_store.start_date = date;
    return watch_schedule_data_save();
}

watch_schedule_date_t watch_schedule_data_get_date(uint8_t week, uint8_t weekday)
{
    watch_schedule_data_init();
    if(week < 1U) week = 1U;
    if(week > WATCH_SCHEDULE_WEEK_COUNT) week = WATCH_SCHEDULE_WEEK_COUNT;
    if(weekday >= WATCH_SCHEDULE_DAY_COUNT) weekday = WATCH_SCHEDULE_DAY_COUNT - 1U;
    const uint16_t offset = (uint16_t)(((uint16_t)week - 1U) * 7U + weekday);
    return add_days(schedule_store.start_date, offset);
}

void watch_schedule_data_format_date(watch_schedule_date_t date, char * buffer,
                                     uint32_t buffer_size)
{
    if(buffer == NULL || buffer_size == 0U) return;
    snprintf(buffer, buffer_size, "%04u-%02u-%02u",
             (unsigned)date.year, (unsigned)date.month, (unsigned)date.day);
}

uint8_t watch_schedule_data_course_count(uint8_t week, uint8_t weekday)
{
    watch_schedule_data_init();
    if(week < 1U || week > WATCH_SCHEDULE_WEEK_COUNT ||
       weekday >= WATCH_SCHEDULE_DAY_COUNT) return 0U;
    uint8_t count = 0U;
    for(uint8_t slot = 0U; slot < WATCH_SCHEDULE_MAX_COURSES; slot++) {
        if(schedule_store.courses[week - 1U][weekday][slot].active) count++;
    }
    return count;
}

const watch_schedule_course_t * watch_schedule_data_get_course(
    uint8_t week, uint8_t weekday, uint8_t slot)
{
    watch_schedule_data_init();
    if(week < 1U || week > WATCH_SCHEDULE_WEEK_COUNT ||
       weekday >= WATCH_SCHEDULE_DAY_COUNT || slot >= WATCH_SCHEDULE_MAX_COURSES) {
        return NULL;
    }
    const watch_schedule_course_t * course =
        &schedule_store.courses[week - 1U][weekday][slot];
    return course->active ? course : NULL;
}

int8_t watch_schedule_data_add_course(uint8_t week, uint8_t weekday,
                                      const watch_schedule_course_t * course)
{
    watch_schedule_data_init();
    if(course == NULL || week < 1U || week > WATCH_SCHEDULE_WEEK_COUNT ||
       weekday >= WATCH_SCHEDULE_DAY_COUNT) return -1;

    for(uint8_t slot = 0U; slot < WATCH_SCHEDULE_MAX_COURSES; slot++) {
        if(!schedule_store.courses[week - 1U][weekday][slot].active) {
            schedule_store.courses[week - 1U][weekday][slot] = *course;
            schedule_store.courses[week - 1U][weekday][slot].active = true;
            watch_schedule_data_save();
            return (int8_t)slot;
        }
    }
    return -1;
}

bool watch_schedule_data_update_course(uint8_t week, uint8_t weekday, uint8_t slot,
                                       const watch_schedule_course_t * course)
{
    watch_schedule_data_init();
    if(course == NULL || week < 1U || week > WATCH_SCHEDULE_WEEK_COUNT ||
       weekday >= WATCH_SCHEDULE_DAY_COUNT || slot >= WATCH_SCHEDULE_MAX_COURSES) {
        return false;
    }
    schedule_store.courses[week - 1U][weekday][slot] = *course;
    schedule_store.courses[week - 1U][weekday][slot].active = true;
    return watch_schedule_data_save();
}

bool watch_schedule_data_remove_course(uint8_t week, uint8_t weekday, uint8_t slot)
{
    watch_schedule_data_init();
    if(week < 1U || week > WATCH_SCHEDULE_WEEK_COUNT ||
       weekday >= WATCH_SCHEDULE_DAY_COUNT || slot >= WATCH_SCHEDULE_MAX_COURSES) {
        return false;
    }

    for(uint8_t index = slot; index + 1U < WATCH_SCHEDULE_MAX_COURSES; index++) {
        schedule_store.courses[week - 1U][weekday][index] =
            schedule_store.courses[week - 1U][weekday][index + 1U];
    }
    memset(&schedule_store.courses[week - 1U][weekday][WATCH_SCHEDULE_MAX_COURSES - 1U],
           0, sizeof(watch_schedule_course_t));
    return watch_schedule_data_save();
}

bool watch_schedule_data_save(void)
{
    FILE * file = fopen(SCHEDULE_FILE_NAME, "wb");
    if(file == NULL) return false;
    const bool success = fwrite(&schedule_store, sizeof(schedule_store), 1U, file) == 1U;
    fclose(file);
    return success;
}
