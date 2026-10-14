/*
 * Kronograf simulator: the watch's LVGL UI, rendered in an SDL2 window.
 *
 *   kronograf_sim                       interactive window (mouse = finger), ×3 scale
 *   kronograf_sim --shot out.png        headless render of one frame, then exit
 *   kronograf_sim --scale 2             changes the window scale
 *   kronograf_sim --record dir          plays the scripted demo, one PNG per frame (README GIF)
 */
#include "lvgl.h"
#include "../src/ui/ui.h"
#include "../src/ui/faces.h"
#include "../src/ui/nav.h"
#include "../src/ui/notif.h"
#include "../src/ui/model.h"
#include "../src/hal/hal.h"
#include "../src/ui/theme.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"

static lv_color_t s_fb[KG_W * KG_H];      /* full screen image */
static lv_color_t s_buf[KG_W * 40];       /* partial buffer, as on the watch */
static int s_mouse_x, s_mouse_y, s_mouse_down;
static int s_scale = 3;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px)
{
    int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++) {
        memcpy(&s_fb[y * KG_W + a->x1], px, w * sizeof(lv_color_t));
        px += w;
    }
    lv_disp_flush_ready(drv);
}

static void pointer_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    data->point.x = s_mouse_x / s_scale;
    data->point.y = s_mouse_y / s_scale;
    data->state = s_mouse_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Scripted demo for the README GIF: pointer strokes on a timeline (ms, 240 px space). The
 * pointer is down between t0 and t1, moving linearly from (x0, y0) to (x1, y1). */
typedef struct {
    uint32_t t0, t1;
    int x0, y0, x1, y1;
} stroke_t;
static const stroke_t DEMO[] = {
    {3000, 3220, 205, 120, 35, 120},     /* aviator → time circuits */
    {5200, 6400, 120, 120, 120, 120},    /* long press on PRESENT TIME: discharge */
    {8600, 8820, 205, 120, 35, 120},     /* → night */
    {10400, 10620, 120, 205, 120, 35},   /* swipe up: launcher */
    {12000, 12080, 65, 195, 65, 195},    /* tap Weather */
    {18600, 18680, 175, 215, 175, 215},  /* OK on the notification card */
};
static const uint32_t DEMO_END_MS = 20500;
#define RECORD_FPS 20

static void demo_pointer(uint32_t t)
{
    s_mouse_down = 0;
    for (size_t i = 0; i < sizeof DEMO / sizeof DEMO[0]; i++) {
        const stroke_t *k = &DEMO[i];
        if (t < k->t0 || t > k->t1) continue;
        int span = (int)(k->t1 - k->t0), p = (int)(t - k->t0);
        s_mouse_x = (k->x0 + (k->x1 - k->x0) * p / span) * s_scale;
        s_mouse_y = (k->y0 + (k->y1 - k->y0) * p / span) * s_scale;
        s_mouse_down = 1;
    }
}

static int write_png(const char *path)
{
    static uint8_t rgb[KG_W * KG_H * 3];
    for (int i = 0; i < KG_W * KG_H; i++) {
        uint16_t c = s_fb[i].full;
        uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
        rgb[i * 3 + 0] = (uint8_t)((r * 527 + 23) >> 6);
        rgb[i * 3 + 1] = (uint8_t)((g * 259 + 33) >> 6);
        rgb[i * 3 + 2] = (uint8_t)((b * 527 + 23) >> 6);
    }
    return stbi_write_png(path, KG_W, KG_H, 3, rgb, KG_W * 3) ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *shot = NULL, *record = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) record = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) s_scale = atoi(argv[++i]);
    }

    SDL_Init(shot || record ? SDL_INIT_TIMER : SDL_INIT_VIDEO | SDL_INIT_TIMER);
    lv_init();

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, s_buf, NULL, KG_W * 40);
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = KG_W;
    disp_drv.ver_res = KG_H;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = pointer_cb;
    lv_indev_drv_register(&indev_drv);

    const char *screen = getenv("KG_SCREEN");
    int diag = screen && !strcmp(screen, "diag");
    if (diag) {
        lv_scr_load(ui_diag_create());
    } else {
        const char *face = getenv("KG_FACE");
        if (face) hal_setting_set("face", atoi(face));
        ui_faces_start();
        ui_nav_init();
        if (getenv("KG_DEMO")) {
            kg_alarm_t a = {true, 6, 45}, b = {true, 7, 30};
            model_alarm_set(0, a);
            model_alarm_set(2, b);
            model_timer_set(12 * 60000);
            model_timer_start_pause();
            model_chrono_start_stop();
        }
        const char *app = getenv("KG_APP");
        if (app && !strcmp(app, "launcher")) ui_nav_open_launcher();
        else if (app && !strcmp(app, "notifs")) ui_notif_open_list();
        else if (app) ui_nav_open_app(app);
        if (getenv("KG_ALERT")) ui_nav_test_alert(!strcmp(getenv("KG_ALERT"), "alarm"));
    }

    if (record) {
        uint32_t start = SDL_GetTicks(), next = 0;
        int frame = 0;
        char path[512];
        for (uint32_t t = 0; t < DEMO_END_MS; t = SDL_GetTicks() - start) {
            demo_pointer(t);
            lv_timer_handler();
            if (t >= next) {
                lv_refr_now(NULL);
                snprintf(path, sizeof path, "%s/frame%04d.png", record, frame++);
                if (write_png(path)) return 1;
                next += 1000 / RECORD_FPS;
            }
            SDL_Delay(1);
        }
        printf("recorded %d frames in %s\n", frame, record);
        return 0;
    }

    if (shot) {
        /* Let the entrance animations (segment flicker) run before the capture. */
        uint32_t until = SDL_GetTicks() + (getenv("KG_SETTLE_MS") ? atoi(getenv("KG_SETTLE_MS")) : 400);
        uint32_t t0 = SDL_GetTicks();
        int laps = 0;
        while (SDL_GetTicks() < until) {
            lv_timer_handler();
            /* Demo: two stopwatch laps while waiting. */
            if (getenv("KG_DEMO") && laps < 2 && SDL_GetTicks() - t0 > 300u + 500u * laps) {
                model_chrono_lap();
                laps++;
            }
            SDL_Delay(5);
        }
        if (getenv("KG_EGG")) {
            face_circuits_discharge();
            until = SDL_GetTicks() + atoi(getenv("KG_EGG"));
            while (SDL_GetTicks() < until) {
                lv_timer_handler();
                SDL_Delay(2);
            }
        }
        lv_refr_now(NULL);
        int rc = write_png(shot);
        printf("%s %s\n", rc ? "FAILED" : "screenshot", shot);
        return rc;
    }

    SDL_Window *win = SDL_CreateWindow("Kronograf — simulator", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, KG_W * s_scale, KG_H * s_scale, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, KG_W, KG_H);

    uint32_t last_update = 0;
    for (int running = 1; running;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_MOUSEMOTION) { s_mouse_x = e.motion.x; s_mouse_y = e.motion.y; }
            else if (e.type == SDL_MOUSEBUTTONDOWN) s_mouse_down = 1;
            else if (e.type == SDL_MOUSEBUTTONUP) s_mouse_down = 0;
        }
        if (diag && SDL_GetTicks() - last_update >= 1000) {
            last_update = SDL_GetTicks();
            ui_diag_update();
        }
        lv_timer_handler();
        SDL_UpdateTexture(tex, NULL, s_fb, KG_W * sizeof(lv_color_t));
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    SDL_Quit();
    return 0;
}
