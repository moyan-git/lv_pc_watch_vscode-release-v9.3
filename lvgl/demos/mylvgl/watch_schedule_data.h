/**
 * @file watch_schedule_data.h
 * 电子课表的数据模型、日期换算与本地持久化接口。
 */
#ifndef WATCH_SCHEDULE_DATA_H
#define WATCH_SCHEDULE_DATA_H

#include "watch_ui.h"

#include <stdbool.h>
#include <stdint.h>

#define WATCH_SCHEDULE_WEEK_COUNT       18U
#define WATCH_SCHEDULE_DAY_COUNT         7U
#define WATCH_SCHEDULE_MAX_COURSES       5U
#define WATCH_COURSE_NAME_SIZE          40U
#define WATCH_COURSE_TIME_SIZE          24U
#define WATCH_COURSE_LOCATION_SIZE      40U

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
} watch_schedule_date_t;

typedef struct {
    bool active;
    char name[WATCH_COURSE_NAME_SIZE];
    char time[WATCH_COURSE_TIME_SIZE];
    char location[WATCH_COURSE_LOCATION_SIZE];
    uint32_t color;
} watch_schedule_course_t;

void watch_schedule_data_init(void);
watch_schedule_date_t watch_schedule_data_get_start_date(void);
bool watch_schedule_data_set_start_date(watch_schedule_date_t date);
watch_schedule_date_t watch_schedule_data_get_date(uint8_t week, uint8_t weekday);
void watch_schedule_data_format_date(watch_schedule_date_t date, char * buffer,
                                     uint32_t buffer_size);

uint8_t watch_schedule_data_course_count(uint8_t week, uint8_t weekday);
const watch_schedule_course_t * watch_schedule_data_get_course(
    uint8_t week, uint8_t weekday, uint8_t slot);
int8_t watch_schedule_data_add_course(uint8_t week, uint8_t weekday,
                                      const watch_schedule_course_t * course);
bool watch_schedule_data_update_course(uint8_t week, uint8_t weekday, uint8_t slot,
                                       const watch_schedule_course_t * course);
bool watch_schedule_data_remove_course(uint8_t week, uint8_t weekday, uint8_t slot);
bool watch_schedule_data_save(void);

#endif /* WATCH_SCHEDULE_DATA_H */
