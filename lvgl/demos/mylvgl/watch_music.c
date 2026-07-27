/**
 * @file watch_music.c
 * @brief 圆形音乐播放器、动态频谱和滑动切歌模块。
 *
 * 页面交互：
 * - 左滑切换到下一首音乐；
 * - 右滑切换到上一首音乐；
 * - 仅通过左上角返回键退出音乐页面。
 */
#include "watch_music.h"

#define MUSIC_TRACK_COUNT 4
#define MUSIC_DISC_SIZE   150
#define MUSIC_BAR_COUNT   7

typedef struct {
    const char * title;
    uint32_t color_start;
    uint32_t color_end;
} music_track_t;

static const music_track_t music_tracks[MUSIC_TRACK_COUNT] = {
    { "NIGHT DRIVE",  0x6D5DF6, 0xB34BDA },
    { "CITY LIGHTS",  0x009FC2, 0x35D39D },
    { "STAR ECHO",    0xE94F7B, 0xF2A44A },
    { "MORNING BEAT", 0x3678F2, 0x8459EF }
};

static lv_obj_t * music_content;
static lv_obj_t * music_disc;
static lv_obj_t * music_title;
static lv_obj_t * music_control;
static lv_obj_t * music_control_label;
static lv_obj_t * music_bars[MUSIC_BAR_COUNT];
static lv_point_t music_swipe_start;
static uint8_t music_track_index;
static int8_t music_pending_direction;
static bool music_swipe_tracking;
static bool music_transitioning;
static bool music_playing;

/* -------------------------- 动态频谱 -------------------------- */

static void music_bar_height_anim_cb(void * object, int32_t value)
{
    lv_obj_t * bar = (lv_obj_t *)object;
    lv_obj_set_height(bar, value);
    lv_obj_set_y(bar, (MUSIC_DISC_SIZE - value) / 2);
}

static void music_start_bar_animation(lv_obj_t * bar, uint8_t index)
{
    static const int16_t min_heights[MUSIC_BAR_COUNT] = { 18, 30, 22, 38, 24, 32, 17 };
    static const int16_t max_heights[MUSIC_BAR_COUNT] = { 58, 78, 68, 92, 72, 82, 55 };
    static const uint16_t durations[MUSIC_BAR_COUNT] = { 430, 560, 370, 610, 470, 530, 400 };

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, bar);
    lv_anim_set_exec_cb(&animation, music_bar_height_anim_cb);
    lv_anim_set_values(&animation, min_heights[index], max_heights[index]);
    lv_anim_set_duration(&animation, durations[index]);
    lv_anim_set_reverse_duration(&animation, durations[index] - 70);
    lv_anim_set_delay(&animation, index * 45);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}

static void music_create_spectrum(lv_obj_t * disc)
{
    static const int16_t initial_heights[MUSIC_BAR_COUNT] = { 18, 30, 22, 38, 24, 32, 17 };
    const int16_t bar_width = 8;
    const int16_t gap = 7;
    const int16_t total_width = MUSIC_BAR_COUNT * bar_width + (MUSIC_BAR_COUNT - 1) * gap;
    const int16_t start_x = (MUSIC_DISC_SIZE - total_width) / 2;

    for(uint8_t i = 0; i < MUSIC_BAR_COUNT; i++) {
        lv_obj_t * bar = lv_obj_create(disc);
        lv_obj_set_size(bar, bar_width, initial_heights[i]);
        lv_obj_set_pos(bar, start_x + i * (bar_width + gap),
                       (MUSIC_DISC_SIZE - initial_heights[i]) / 2);
        lv_obj_set_style_bg_color(bar, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_90, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, bar_width / 2, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(bar, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        music_bars[i] = bar;
        music_start_bar_animation(bar, i);
    }
}

/* -------------------------- 歌曲切换 -------------------------- */

static void music_apply_track(void)
{
    const music_track_t * track = &music_tracks[music_track_index];
    lv_label_set_text(music_title, track->title);
    lv_obj_set_style_bg_color(music_disc, lv_color_hex(track->color_start), 0);
    lv_obj_set_style_bg_grad_color(music_disc, lv_color_hex(track->color_end), 0);
    lv_obj_set_style_shadow_color(music_disc, lv_color_hex(track->color_start), 0);
    lv_obj_set_style_border_color(music_control, lv_color_hex(track->color_start), 0);
}

static void music_play_pause_clicked_cb(lv_event_t * event)
{
    LV_UNUSED(event);
    music_playing = !music_playing;
    lv_label_set_text(music_control_label,
                      music_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    for(uint8_t i = 0; i < MUSIC_BAR_COUNT; i++) {
        lv_anim_t * animation = lv_anim_get(music_bars[i], music_bar_height_anim_cb);
        if(animation != NULL) {
            if(music_playing) lv_anim_resume(animation);
            else lv_anim_pause(animation);
        }
        lv_obj_set_style_bg_opa(music_bars[i],
                                music_playing ? LV_OPA_90 : LV_OPA_50, 0);
    }
}

static void music_content_x_anim_cb(void * object, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)object, value);
}

static void music_slide_in_ready_cb(lv_anim_t * animation)
{
    LV_UNUSED(animation);
    music_transitioning = false;
}

static void music_slide_out_ready_cb(lv_anim_t * animation)
{
    LV_UNUSED(animation);

    int16_t next = (int16_t)music_track_index + music_pending_direction;
    if(next < 0) next = MUSIC_TRACK_COUNT - 1;
    if(next >= MUSIC_TRACK_COUNT) next = 0;
    music_track_index = (uint8_t)next;
    music_apply_track();

    const int16_t start_x = music_pending_direction > 0 ? 240 : -240;
    lv_obj_set_x(music_content, start_x);

    lv_anim_t slide_in;
    lv_anim_init(&slide_in);
    lv_anim_set_var(&slide_in, music_content);
    lv_anim_set_exec_cb(&slide_in, music_content_x_anim_cb);
    lv_anim_set_values(&slide_in, start_x, 0);
    lv_anim_set_duration(&slide_in, 260);
    lv_anim_set_path_cb(&slide_in, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&slide_in, music_slide_in_ready_cb);
    lv_anim_start(&slide_in);
}

static void music_switch_track(int8_t direction)
{
    if(music_transitioning || direction == 0) return;

    music_transitioning = true;
    music_pending_direction = direction;

    lv_anim_t slide_out;
    lv_anim_init(&slide_out);
    lv_anim_set_var(&slide_out, music_content);
    lv_anim_set_exec_cb(&slide_out, music_content_x_anim_cb);
    lv_anim_set_values(&slide_out, 0, direction > 0 ? -240 : 240);
    lv_anim_set_duration(&slide_out, 180);
    lv_anim_set_path_cb(&slide_out, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&slide_out, music_slide_out_ready_cb);
    lv_anim_start(&slide_out);
}

static void music_touch_cb(lv_event_t * event)
{
    lv_indev_t * indev = lv_indev_active();
    if(indev == NULL) return;

    const lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_PRESSED) {
        if(music_transitioning) {
            music_swipe_tracking = false;
            return;
        }
        lv_indev_get_point(indev, &music_swipe_start);
        music_swipe_tracking = true;
        return;
    }

    if((code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) ||
       !music_swipe_tracking) {
        return;
    }
    music_swipe_tracking = false;

    lv_point_t end_point;
    lv_indev_get_point(indev, &end_point);
    const int16_t dx = end_point.x - music_swipe_start.x;
    const int16_t dy = end_point.y - music_swipe_start.y;
    const int16_t abs_dx = dx < 0 ? -dx : dx;
    const int16_t abs_dy = dy < 0 ? -dy : dy;

    if(abs_dx >= watch_ui_get_swipe_threshold() && abs_dx > abs_dy) {
        music_switch_track(dx < 0 ? 1 : -1);
    }
}

/* -------------------------- 页面构建 -------------------------- */

void watch_music_build(lv_obj_t * root)
{
    music_track_index = 0;
    music_pending_direction = 0;
    music_swipe_tracking = false;
    music_transitioning = false;
    music_playing = true;

    watch_ui_add_detail_header(root, "音乐");

    music_content = lv_obj_create(root);
    lv_obj_set_size(music_content, 240, 224);
    lv_obj_set_pos(music_content, 0, 56);
    lv_obj_set_style_bg_opa(music_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(music_content, 0, 0);
    lv_obj_set_style_pad_all(music_content, 0, 0);
    lv_obj_clear_flag(music_content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(music_content, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);

    music_disc = watch_ui_make_card(music_content, MUSIC_DISC_SIZE, MUSIC_DISC_SIZE,
                                    lv_color_hex(music_tracks[0].color_start));
    lv_obj_set_pos(music_disc, 45, 2);
    lv_obj_set_style_radius(music_disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_grad_color(music_disc,
                                   lv_color_hex(music_tracks[0].color_end), 0);
    lv_obj_set_style_bg_grad_dir(music_disc, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(music_disc, 22, 0);
    lv_obj_set_style_shadow_spread(music_disc, 1, 0);
    lv_obj_set_style_shadow_color(music_disc,
                                  lv_color_hex(music_tracks[0].color_start), 0);
    lv_obj_set_style_shadow_opa(music_disc, LV_OPA_30, 0);

    lv_obj_t * inner_ring = lv_obj_create(music_disc);
    lv_obj_set_size(inner_ring, 132, 132);
    lv_obj_center(inner_ring);
    lv_obj_set_style_bg_opa(inner_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(inner_ring, 1, 0);
    lv_obj_set_style_border_color(inner_ring, lv_color_white(), 0);
    lv_obj_set_style_border_opa(inner_ring, LV_OPA_30, 0);
    lv_obj_set_style_radius(inner_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(inner_ring, 0, 0);
    lv_obj_clear_flag(inner_ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(inner_ring, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);

    music_create_spectrum(music_disc);

    music_title = watch_ui_make_label(music_content, music_tracks[0].title,
                                      &lv_font_montserrat_18,
                                      lv_color_hex(WATCH_COLOR_TEXT),
                                      LV_ALIGN_BOTTOM_MID, 0, -5);

    music_control = watch_ui_make_card(music_content, 40, 40,
                                       lv_color_hex(WATCH_COLOR_SURFACE2));
    lv_obj_set_pos(music_control, 100, 158);
    lv_obj_set_style_radius(music_control, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(music_control, 1, 0);
    lv_obj_set_style_border_color(music_control,
                                  lv_color_hex(music_tracks[0].color_start), 0);
    lv_obj_add_flag(music_control, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(music_control, music_play_pause_clicked_cb,
                        LV_EVENT_CLICKED, NULL);
    music_control_label = watch_ui_make_label(music_control, LV_SYMBOL_PAUSE,
                                              &lv_font_montserrat_20,
                                              lv_color_hex(WATCH_COLOR_TEXT),
                                              LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(root, music_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(root, music_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(root, music_touch_cb, LV_EVENT_PRESS_LOST, NULL);
}
