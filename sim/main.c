/*
 * Simulateur Kronograf : même UI LVGL que la montre, rendue dans une fenêtre SDL2.
 *
 *   kronograf_sim                       fenêtre interactive (souris = doigt), échelle ×3
 *   kronograf_sim --shot out.png        rendu headless d'une image puis sortie
 *   kronograf_sim --scale 2             change l'échelle de la fenêtre
 */
#include "lvgl.h"
#include "../src/ui/ui.h"
#include "../src/ui/faces.h"
#include "../src/hal/hal.h"
#include "../src/ui/theme.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"

static lv_color_t s_fb[KG_W * KG_H];      /* image complète de l'écran */
static lv_color_t s_buf[KG_W * 40];       /* tampon partiel, comme sur la montre */
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
    const char *shot = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) s_scale = atoi(argv[++i]);
    }

    SDL_Init(shot ? SDL_INIT_TIMER : SDL_INIT_VIDEO | SDL_INIT_TIMER);
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
    }

    if (shot) {
        /* Laisse tourner les animations d'apparition (scintillement des segments) avant la capture. */
        uint32_t until = SDL_GetTicks() + (getenv("KG_SETTLE_MS") ? atoi(getenv("KG_SETTLE_MS")) : 400);
        while (SDL_GetTicks() < until) {
            lv_timer_handler();
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
        printf("%s %s\n", rc ? "ECHEC" : "capture", shot);
        return rc;
    }

    SDL_Window *win = SDL_CreateWindow("Kronograf — simulateur", SDL_WINDOWPOS_CENTERED,
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
