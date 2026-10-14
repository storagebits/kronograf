/*
 * Video: an MJPEG camera (ESP32-CAM, IP camera, 3D printer…) or a JPEG snapshot URL,
 * full screen. Plays while the app is on screen; tap to show the overlay (state, frame
 * rate, source size, "Change URL"). Wi-Fi is held by the app's lease.
 */
#include "nav.h"
#include "kit.h"
#include "theme.h"
#include "fonts/kg_fonts.h"
#include "../hal/hal.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_img, *s_overlay, *s_status, *s_empty, *s_kb_panel, *s_ta;
static lv_timer_t *s_timer;
static lv_img_dsc_t s_dsc;
static uint32_t s_seq, s_ticks;
static bool s_overlay_on;
static uint32_t s_overlay_until; /* tick count: the overlay shows for the first seconds */

static void overlay(bool on)
{
    s_overlay_on = on;
    if (on) lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    if (s_ticks++ % 25 == 0) hal_net_hold(); /* renew the Wi-Fi lease about once a second */
    if (s_ticks < 10 * 60 * 25) lv_disp_trig_activity(NULL); /* watching: the screen stays on, 10 min at most */
    uint32_t seq;
    const uint16_t *f = hal_video_frame(&seq);
    if (f && seq != s_seq) {
        s_seq = seq;
        s_dsc.data = (const uint8_t *)f;
        lv_img_cache_invalidate_src(&s_dsc);
        lv_img_set_src(s_img, &s_dsc);
        lv_obj_clear_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ticks % 8) return; /* status: ~4 Hz */
    kg_video_status_t st;
    hal_video_status(&st);
    char b[80];
    switch (st.state) {
    case KG_VIDEO_PLAYING:
        snprintf(b, sizeof b, "LIVE  %.1f fps · %ux%u · %lu kbit/s", (double)st.fps, st.src_w, st.src_h, (unsigned long)st.kbps);
        break;
    case KG_VIDEO_CONNECTING: snprintf(b, sizeof b, "%s", st.error[0] ? st.error : "Connecting…"); break;
    case KG_VIDEO_ERROR: snprintf(b, sizeof b, "%s", st.error); break;
    default: snprintf(b, sizeof b, "Stopped"); break;
    }
    lv_label_set_text(s_status, b);
    lv_obj_set_style_text_color(s_status, st.state == KG_VIDEO_PLAYING ? KG_C_OK : st.state == KG_VIDEO_ERROR ? KG_C_ALERT : KG_C_WARN, 0);
    if (st.state != KG_VIDEO_PLAYING && !s_seq && hal_video_url()[0]) overlay(true); /* nothing to see yet: say why */
    else if (s_overlay_until && s_ticks >= s_overlay_until) {
        s_overlay_until = 0;
        overlay(false);
    }
}

static void start(void)
{
    s_seq = 0;
    s_overlay_until = s_ticks + 3 * 25; /* status visible for 3 s, then the picture alone */
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    const char *url = hal_video_url();
    if (url[0]) {
        lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
        hal_net_hold();
        hal_video_start(url);
    } else {
        lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    }
}

static void tap_cb(lv_event_t *e)
{
    (void)e;
    s_overlay_until = 0;
    if (hal_video_url()[0]) overlay(!s_overlay_on);
}

static void kb_cb(lv_event_t *e)
{
    lv_obj_add_flag(s_kb_panel, LV_OBJ_FLAG_HIDDEN);
    if (lv_event_get_code(e) != LV_EVENT_READY) return;
    const char *url = lv_textarea_get_text(s_ta);
    hal_video_set_url(!strcmp(url, "http://") ? "" : url);
    hal_video_stop();
    overlay(false);
    start();
}

static void url_cb(lv_event_t *e)
{
    (void)e;
    lv_textarea_set_text(s_ta, hal_video_url()[0] ? hal_video_url() : "http://");
    lv_obj_clear_flag(s_kb_panel, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *create(void)
{
    lv_obj_t *scr = kit_screen();
    s_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_dsc.header.w = KG_VIDEO_W;
    s_dsc.header.h = KG_VIDEO_H;
    s_dsc.data_size = KG_VIDEO_W * KG_VIDEO_H * LV_COLOR_SIZE / 8;
    s_img = lv_img_create(scr);
    lv_obj_set_pos(s_img, 0, 0);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, tap_cb, LV_EVENT_SHORT_CLICKED, NULL);

    s_empty = lv_obj_create(scr);
    lv_obj_remove_style_all(s_empty);
    lv_obj_set_size(s_empty, KG_W, KG_H);
    lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_CLICKABLE);
    kit_title(s_empty, "VIDEO");
    lv_obj_t *t = kit_label(s_empty, &kg_font_label_20, KG_C_TEXT, "No camera yet");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_t *h = kit_label(s_empty, &kg_font_text_16, KG_C_TEXT_DIM,
                            "An MJPEG stream or a JPEG\nsnapshot URL (http://), from an\nESP32-CAM, an IP camera, a printer…");
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_t *set = kit_button(s_empty, "Set URL", true, 140, 40, url_cb, NULL);
    lv_obj_align(set, LV_ALIGN_BOTTOM_MID, 0, -24);

    /* Overlay: a status pill at the top, "Change URL" at the bottom. */
    s_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, KG_W, KG_H);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *pill = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 228, 26);
    lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_color(pill, KG_C_BG, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_70, 0);
    lv_obj_set_style_radius(pill, 13, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    s_status = kit_label(pill, &kg_font_text_16, KG_C_OK, "");
    lv_obj_set_width(s_status, 216);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_status);
    lv_obj_t *chg = kit_button(s_overlay, "Change URL", false, 140, 34, url_cb, NULL);
    lv_obj_set_style_bg_opa(chg, LV_OPA_80, 0);
    lv_obj_align(chg, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    s_kb_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(s_kb_panel);
    lv_obj_set_size(s_kb_panel, KG_W, KG_H);
    lv_obj_set_style_bg_color(s_kb_panel, KG_C_BG, 0);
    lv_obj_set_style_bg_opa(s_kb_panel, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_kb_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *hint = kit_label(s_kb_panel, &kg_font_text_16, KG_C_TEXT_DIM, "MJPEG or JPEG URL, http:// only");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 2);
    s_ta = lv_textarea_create(s_kb_panel);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, 255);
    lv_obj_set_size(s_ta, 228, 38);
    lv_obj_align(s_ta, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_style_text_font(s_ta, &kg_font_text_16, 0);
    lv_obj_set_style_bg_color(s_ta, KG_C_SURFACE, 0);
    lv_obj_set_style_text_color(s_ta, KG_C_TEXT, 0);
    lv_obj_set_style_border_color(s_ta, KG_C_ACCENT, 0);
    lv_obj_t *kb = lv_keyboard_create(s_kb_panel);
    lv_obj_set_size(kb, KG_W, 172);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_ta);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(kb, KG_C_BG, 0);
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_gap(kb, 2, 0);
    lv_obj_set_style_bg_color(kb, KG_C_BTN, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, KG_C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_CANCEL, NULL);

    s_timer = lv_timer_create(tick, 40, NULL);
    lv_timer_pause(s_timer);
    return scr;
}

static void on_show(void)
{
    lv_obj_add_flag(s_kb_panel, LV_OBJ_FLAG_HIDDEN);
    s_ticks = 0;
    start();
    overlay(hal_video_url()[0] != 0);
    lv_timer_resume(s_timer);
}

static void on_hide(void)
{
    lv_timer_pause(s_timer);
    hal_video_stop();
}

const kg_app_t kg_app_video = {
    .name = "video",
    .label = "Video",
    .icon = KG_ICON_VIDEO,
    .create = create,
    .on_show = on_show,
    .on_hide = on_hide,
};
