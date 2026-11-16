#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>

#include <ppu-types.h>
#include <sys/process.h>
#include <io/pad.h>
#include <rsx/rsx.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>

SYS_PROCESS_PARAM(1001, 0x100000);

#define FB_COUNT 2
#define HOST_ALIGN (1024 * 1024)
#define HOST_IO_SIZE (16 * 1024 * 1024)
#define COMMAND_BUFFER_SIZE 0x80000
#define MAX_BULLETS 24
#define MAX_ENEMIES 10
#define COLOR(r,g,b) (0xFF000000u | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

static gcmContextData *g_context = NULL;
static u32 *g_framebuffer[FB_COUNT] = { NULL, NULL };
static u32 g_framebuffer_offset[FB_COUNT] = { 0, 0 };
static u32 g_width = 0;
static u32 g_height = 0;
static u32 g_pitch = 0;
static u32 g_front = 0;
static u32 g_first_flip = 1;
static int g_running = 1;

typedef struct {
    float x;
    float y;
    float vy;
    int on_ground;
} Player;

typedef struct {
    float x;
    float y;
    int active;
} Bullet;

typedef struct {
    float x;
    float y;
    int active;
} Enemy;

static Player player;
static Bullet bullets[MAX_BULLETS];
static Enemy enemies[MAX_ENEMIES];
static float camera_x = 0.0f;
static int score = 0;
static int lives = 3;
static int game_over = 0;
static int fire_cooldown = 0;
static int spawn_timer = 45;
static unsigned int frame_number = 0;
static int font_scale = 2;

static const unsigned char font5x7[36][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
};

static void system_callback(u64 status, u64 param, void *userdata)
{
    (void)param;
    (void)userdata;
    if (status == SYSUTIL_EXIT_GAME) g_running = 0;
}

static int configure_video(void)
{
    static const u32 modes[] = {
        VIDEO_RESOLUTION_720,
        VIDEO_RESOLUTION_480,
        VIDEO_RESOLUTION_576
    };
    videoResolution resolution;
    videoConfiguration config;
    int found = 0;
    unsigned int i;

    memset(&resolution, 0, sizeof(resolution));
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        if (videoGetResolutionAvailability(VIDEO_PRIMARY, modes[i], VIDEO_ASPECT_AUTO, 0) != 1)
            continue;
        if (videoGetResolution((s32)modes[i], &resolution) != 0)
            continue;
        if (resolution.width == 0 || resolution.height == 0)
            continue;
        config.resolution = (u8)modes[i];
        config.format = VIDEO_BUFFER_FORMAT_XRGB;
        config.aspect = VIDEO_ASPECT_AUTO;
        memset(config.padding, 0, sizeof(config.padding));
        config.pitch = (u32)resolution.width * 4u;
        if (videoConfigure(VIDEO_PRIMARY, &config, NULL, 0) == 0) {
            found = 1;
            break;
        }
    }

    if (!found) {
        printf("[VIDEO] No compatible 720/480/576 mode found.\n");
        return -1;
    }

    g_width = resolution.width;
    g_height = resolution.height;
    g_pitch = g_width * 4u;
    printf("[VIDEO] Selected %ux%u, pitch=%u bytes.\n",
           (unsigned)g_width, (unsigned)g_height, (unsigned)g_pitch);
    return 0;
}

static int initialize_graphics(void)
{
    void *host_io;
    unsigned int i;
    const u32 framebuffer_bytes = g_width * g_height * 4u;

    host_io = memalign(HOST_ALIGN, HOST_IO_SIZE);
    if (!host_io) {
        printf("[RSX] Could not allocate %u-byte aligned IO buffer.\n", HOST_IO_SIZE);
        return -1;
    }
    memset(host_io, 0, HOST_IO_SIZE);

    if (rsxInit(&g_context, COMMAND_BUFFER_SIZE, HOST_IO_SIZE, host_io) != 0 || !g_context) {
        printf("[RSX] rsxInit failed.\n");
        free(host_io);
        return -1;
    }

    if (configure_video() != 0)
        return -1;

    gcmSetFlipMode(GCM_FLIP_VSYNC);

    for (i = 0; i < FB_COUNT; ++i) {
        g_framebuffer[i] = (u32 *)rsxMemalign(64, framebuffer_bytes);
        if (!g_framebuffer[i]) {
            printf("[RSX] Framebuffer %u allocation failed (%u bytes).\n",
                   i, (unsigned)framebuffer_bytes);
            return -1;
        }
        memset(g_framebuffer[i], 0, framebuffer_bytes);
        if (rsxAddressToOffset(g_framebuffer[i], &g_framebuffer_offset[i]) != 0) {
            printf("[RSX] rsxAddressToOffset failed for framebuffer %u.\n", i);
            return -1;
        }
        if (gcmSetDisplayBuffer((u8)i, g_framebuffer_offset[i], g_pitch, g_width, g_height) != 0) {
            printf("[RSX] gcmSetDisplayBuffer failed for framebuffer %u.\n", i);
            return -1;
        }
        printf("[RSX] FB%u address=%p offset=0x%08x size=%u alignment=64.\n",
               i, g_framebuffer[i], (unsigned)g_framebuffer_offset[i],
               (unsigned)framebuffer_bytes);
    }

    gcmResetFlipStatus();
    g_front = 0;
    g_first_flip = 1;
    return 0;
}

static void present_frame(void)
{
    if (!g_first_flip) {
        while (gcmGetFlipStatus() != 0)
            usleep(200);
        gcmResetFlipStatus();
    } else {
        gcmResetFlipStatus();
    }

    gcmSetFlip(g_context, (u8)g_front);
    rsxFlushBuffer(g_context);
    gcmSetWaitFlip(g_context);
    g_front ^= 1u;
    g_first_flip = 0;
}

static void fill_rect(u32 *fb, int x, int y, int w, int h, u32 color)
{
    int row;
    int left = x < 0 ? 0 : x;
    int top = y < 0 ? 0 : y;
    int right = x + w > (int)g_width ? (int)g_width : x + w;
    int bottom = y + h > (int)g_height ? (int)g_height : y + h;
    if (right <= left || bottom <= top) return;
    for (row = top; row < bottom; ++row) {
        u32 *dst = fb + (u32)row * g_width + (u32)left;
        int col;
        for (col = left; col < right; ++col) *dst++ = color;
    }
}

static void draw_glyph(u32 *fb, char ch, int x, int y, int scale, u32 color)
{
    int index = -1;
    int row;
    if (ch >= 'A' && ch <= 'Z') index = ch - 'A';
    else if (ch >= '0' && ch <= '9') index = 26 + ch - '0';
    if (index < 0) {
        if (ch == '-') fill_rect(fb, x, y + 3 * scale, 5 * scale, scale, color);
        else if (ch == ':') {
            fill_rect(fb, x + 2 * scale, y + scale, scale, scale, color);
            fill_rect(fb, x + 2 * scale, y + 5 * scale, scale, scale, color);
        }
        return;
    }
    for (row = 0; row < 7; ++row) {
        int col;
        for (col = 0; col < 5; ++col) {
            if (font5x7[index][row] & (1u << (4 - col)))
                fill_rect(fb, x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

static void draw_text(u32 *fb, const char *text, int x, int y, int scale, u32 color)
{
    while (*text) {
        draw_glyph(fb, *text++, x, y, scale, color);
        x += 6 * scale;
    }
}

static void draw_number(u32 *fb, int value, int x, int y, int scale, u32 color)
{
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d", value);
    draw_text(fb, buffer, x, y, scale, color);
}

static void draw_background(u32 *fb)
{
    int y;
    int floor_y = (int)g_height - 78;
    u32 sky_top = COLOR(9, 17, 42);
    u32 sky_bottom = COLOR(34, 54, 78);
    for (y = 0; y < floor_y; ++y) {
        unsigned int t = (unsigned int)y * 255u / (unsigned int)(floor_y ? floor_y : 1);
        unsigned int r = 9u + (25u * t) / 255u;
        unsigned int g = 17u + (37u * t) / 255u;
        unsigned int b = 42u + (36u * t) / 255u;
        u32 color = COLOR(r, g, b);
        u32 *line = fb + (u32)y * g_width;
        unsigned int x;
        (void)sky_top; (void)sky_bottom;
        for (x = 0; x < g_width; ++x) line[x] = color;
    }

    {
        int offset = (int)(camera_x * 0.25f) % 220;
        int x;
        for (x = -offset; x < (int)g_width + 220; x += 220) {
            int base = floor_y - 25 - ((x / 220) % 3) * 18;
            fill_rect(fb, x, base - 80, 72, 80, COLOR(22, 36, 58));
            fill_rect(fb, x + 12, base - 64, 8, 10, COLOR(64, 100, 116));
            fill_rect(fb, x + 34, base - 64, 8, 10, COLOR(64, 100, 116));
            fill_rect(fb, x + 12, base - 40, 8, 10, COLOR(64, 100, 116));
            fill_rect(fb, x + 34, base - 40, 8, 10, COLOR(64, 100, 116));
        }
    }
    fill_rect(fb, 0, floor_y, (int)g_width, 78, COLOR(40, 45, 42));
    fill_rect(fb, 0, floor_y, (int)g_width, 5, COLOR(102, 116, 72));
    {
        int offset = (int)camera_x % 80;
        int x;
        for (x = -offset; x < (int)g_width; x += 80) {
            fill_rect(fb, x, floor_y + 18, 36, 4, COLOR(72, 78, 65));
            fill_rect(fb, x + 14, floor_y + 42, 24, 3, COLOR(72, 78, 65));
        }
    }
}

static void reset_game(void)
{
    unsigned int i;
    player.x = 120.0f;
    player.y = (float)g_height - 78.0f - 42.0f;
    player.vy = 0.0f;
    player.on_ground = 1;
    camera_x = 0.0f;
    score = 0;
    lives = 3;
    game_over = 0;
    fire_cooldown = 0;
    spawn_timer = 45;
    frame_number = 0;
    for (i = 0; i < MAX_BULLETS; ++i) bullets[i].active = 0;
    for (i = 0; i < MAX_ENEMIES; ++i) enemies[i].active = 0;
}

static void spawn_enemy(void)
{
    unsigned int i;
    for (i = 0; i < MAX_ENEMIES; ++i) {
        if (!enemies[i].active) {
            enemies[i].active = 1;
            enemies[i].x = camera_x + (float)g_width + 35.0f;
            enemies[i].y = (float)g_height - 78.0f - 34.0f;
            return;
        }
    }
}

static void fire_bullet(void)
{
    unsigned int i;
    for (i = 0; i < MAX_BULLETS; ++i) {
        if (!bullets[i].active) {
            bullets[i].active = 1;
            bullets[i].x = player.x + 30.0f;
            bullets[i].y = player.y + 18.0f;
            fire_cooldown = 10;
            return;
        }
    }
}

static int overlap(float ax, float ay, float aw, float ah,
                   float bx, float by, float bw, float bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void update_game(const padData *pad)
{
    unsigned int i;
    const float floor_y = (float)g_height - 78.0f;
    if (game_over) {
        if (pad->BTN_START || pad->BTN_CROSS) reset_game();
        return;
    }

    if (pad->BTN_LEFT) player.x -= 3.2f;
    if (pad->BTN_RIGHT) player.x += 3.2f;
    if (player.x < 0.0f) player.x = 0.0f;
    if (pad->BTN_CROSS && player.on_ground) {
        player.vy = -11.5f;
        player.on_ground = 0;
    }
    player.vy += 0.48f;
    player.y += player.vy;
    if (player.y + 42.0f >= floor_y) {
        player.y = floor_y - 42.0f;
        player.vy = 0.0f;
        player.on_ground = 1;
    }

    if (fire_cooldown > 0) --fire_cooldown;
    if (pad->BTN_SQUARE && fire_cooldown == 0) fire_bullet();

    camera_x = player.x > (float)g_width * 0.34f
        ? player.x - (float)g_width * 0.34f : 0.0f;

    if (spawn_timer > 0) --spawn_timer;
    else {
        spawn_enemy();
        spawn_timer = 75 + (int)(frame_number % 45u);
    }

    for (i = 0; i < MAX_BULLETS; ++i) {
        if (!bullets[i].active) continue;
        bullets[i].x += 10.0f;
        if (bullets[i].x > camera_x + (float)g_width + 20.0f)
            bullets[i].active = 0;
    }

    for (i = 0; i < MAX_ENEMIES; ++i) {
        unsigned int j;
        if (!enemies[i].active) continue;
        enemies[i].x -= 2.0f;
        if (enemies[i].x < camera_x - 50.0f) enemies[i].active = 0;
        if (overlap(player.x, player.y, 28.0f, 42.0f,
                    enemies[i].x, enemies[i].y, 30.0f, 34.0f)) {
            enemies[i].active = 0;
            --lives;
            if (lives <= 0) game_over = 1;
        }
        for (j = 0; j < MAX_BULLETS; ++j) {
            if (bullets[j].active &&
                overlap(bullets[j].x, bullets[j].y, 14.0f, 5.0f,
                        enemies[i].x, enemies[i].y, 30.0f, 34.0f)) {
                bullets[j].active = 0;
                enemies[i].active = 0;
                score += 100;
                break;
            }
        }
    }
    ++frame_number;
}

static void draw_player(u32 *fb)
{
    int x = (int)(player.x - camera_x);
    int y = (int)player.y;
    fill_rect(fb, x + 7, y, 15, 14, COLOR(220, 177, 132));
    fill_rect(fb, x + 4, y + 12, 21, 19, COLOR(45, 116, 72));
    fill_rect(fb, x + 1, y + 18, 7, 8, COLOR(190, 190, 190));
    fill_rect(fb, x + 22, y + 17, 13, 5, COLOR(65, 70, 76));
    fill_rect(fb, x + 5, y + 30, 8, 12, COLOR(42, 48, 60));
    fill_rect(fb, x + 17, y + 30, 8, 12, COLOR(42, 48, 60));
    fill_rect(fb, x + 18, y + 5, 3, 3, COLOR(20, 20, 20));
}

static void draw_game(u32 *fb)
{
    unsigned int i;
    char score_text[32];
    int scale = font_scale;
    int hud_y = 14 * scale;
    draw_background(fb);

    for (i = 0; i < MAX_BULLETS; ++i) {
        if (bullets[i].active)
            fill_rect(fb, (int)(bullets[i].x - camera_x), (int)bullets[i].y,
                      14, 5, COLOR(255, 225, 80));
    }
    for (i = 0; i < MAX_ENEMIES; ++i) {
        if (!enemies[i].active) continue;
        {
            int x = (int)(enemies[i].x - camera_x);
            int y = (int)enemies[i].y;
            fill_rect(fb, x + 3, y + 5, 24, 22, COLOR(150, 55, 48));
            fill_rect(fb, x, y + 12, 30, 12, COLOR(185, 70, 48));
            fill_rect(fb, x + 7, y + 10, 5, 5, COLOR(255, 235, 170));
            fill_rect(fb, x + 19, y + 10, 5, 5, COLOR(255, 235, 170));
            fill_rect(fb, x + 4, y + 27, 7, 7, COLOR(45, 45, 48));
            fill_rect(fb, x + 19, y + 27, 7, 7, COLOR(45, 45, 48));
        }
    }

    draw_player(fb);
    fill_rect(fb, 8, 8, (int)g_width - 16, 30 * scale, COLOR(5, 10, 20));
    draw_text(fb, "MINI CONTRA DOS", 16, 14, scale, COLOR(255, 220, 100));
    draw_text(fb, "SCORE", (int)g_width - 210 * scale, 14, scale, COLOR(220, 230, 240));
    snprintf(score_text, sizeof(score_text), "%d", score);
    draw_text(fb, score_text, (int)g_width - 130 * scale, 14, scale, COLOR(255, 220, 100));
    draw_text(fb, "LIVES", 16, hud_y + 10 * scale, scale, COLOR(220, 230, 240));
    draw_number(fb, lives, 16 + 60 * scale, hud_y + 10 * scale, scale, COLOR(255, 100, 90));

    if (frame_number < 500) {
        draw_text(fb, "LEFT RIGHT MOVE", 16, (int)g_height - 28 * scale, scale, COLOR(230, 230, 230));
        draw_text(fb, "CROSS JUMP SQUARE FIRE", 16, (int)g_height - 17 * scale, scale, COLOR(230, 230, 230));
    }
    if (game_over) {
        fill_rect(fb, (int)g_width / 2 - 150, (int)g_height / 2 - 45, 300, 90, COLOR(8, 12, 22));
        draw_text(fb, "GAME OVER", (int)g_width / 2 - 88, (int)g_height / 2 - 25, scale, COLOR(255, 100, 90));
        draw_text(fb, "PRESS START", (int)g_width / 2 - 100, (int)g_height / 2 + 8, scale, COLOR(240, 240, 240));
    }
}

int main(int argc, char **argv)
{
    padInfo pad_info;
    padData pad;
    unsigned int idle_frames = 0;
    (void)argc;
    (void)argv;

    printf("Mini Contra Dos: booting.\n");
    if (ioPadInit(7) != 0)
        printf("[PAD] ioPadInit reported an error; game will still boot.\n");

    if (initialize_graphics() != 0) {
        printf("[FATAL] Graphics initialization failed; refusing to enter render loop.\n");
        return 1;
    }

    font_scale = g_width >= 1000 ? 2 : 1;
    reset_game();
    memset(&pad_info, 0, sizeof(pad_info));
    memset(&pad, 0, sizeof(pad));
    sysUtilRegisterCallback(0, system_callback, NULL);

    printf("[GAME] Main loop started. D-pad move, CROSS jump, SQUARE fire.\n");
    while (g_running) {
        u32 *draw_buffer = g_framebuffer[g_front ^ 1u];
        memset(&pad, 0, sizeof(pad));
        if (ioPadGetInfo(&pad_info) == 0 && pad_info.status[0]) {
            ioPadGetData(0, &pad);
            idle_frames = 0;
        } else {
            ++idle_frames;
        }
        sysUtilCheckCallback();
        update_game(&pad);
        draw_game(draw_buffer);
        present_frame();
        usleep(16667);
    }

    printf("[GAME] Exit requested.\n");
    sysUtilUnregisterCallback(0);
    ioPadEnd();
    if (g_context) {
        gcmSetWaitFlip(g_context);
        rsxFinish(g_context, 1);
    }
    return 0;
}
