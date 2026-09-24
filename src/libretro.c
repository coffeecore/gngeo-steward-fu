#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <libretro.h>

#include "conf.h"
#include "memory.h"
#include "roms.h"
#include "gnutil.h"
#include "emu.h"
#include "screen.h"

#define VIDEO_WIDTH  320
#define VIDEO_HEIGHT 240

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static bool game_loaded = false;
static bool first_run = true;

static uint16_t framebuffer[VIDEO_WIDTH * VIDEO_HEIGHT];

void libretro_run_68k_frame(void);

void libretro_run_z80_frame(void);

void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
    audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb;
}

void retro_init(void)
{
    cf_init();
    cf_cache_conf();

    memory.intern_p1 = 0xff;
    memory.intern_p2 = 0xff;
    memory.intern_coin = 0x07;
    memory.intern_start = 0x8f;
}

void retro_deinit(void)
{
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));

    info->library_name = "GnGeo";
    info->library_version = "0.1";
    info->valid_extensions = "gno|zip";

    /*
     * GnGeo needs the real ROM path:
     * - .gno is opened directly
     * - .zip must remain an archive
     */
    info->need_fullpath = true;
    info->block_extract = true;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof(*info));

    info->geometry.base_width = 320;
    info->geometry.base_height = 240;
    info->geometry.max_width = 320;
    info->geometry.max_height = 240;
    info->geometry.aspect_ratio = 4.0f / 3.0f;

    info->timing.fps = 60.0;
    info->timing.sample_rate = 22050.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port;
    (void)device;
}

void retro_reset(void)
{
}

void retro_run(void)
{
    static unsigned test_frames = 0;

    if(!game_loaded) {
        return;
    }

    if(input_poll_cb != NULL) {
        input_poll_cb();
    }

    if(test_frames == 0) {
        printf("[GnGeo] starting CPU execution\n");
    }

    libretro_run_z80_frame();
    libretro_run_68k_frame();

    if(test_frames == 0) {
        printf("[GnGeo] first CPU frame completed\n");
    }

    test_frames++;

    memset(framebuffer, 0, sizeof(framebuffer));

    screen_copy_libretro(
        framebuffer,
        VIDEO_WIDTH * sizeof(uint16_t)
    );

    video_cb(
        framebuffer,
        VIDEO_WIDTH,
        VIDEO_HEIGHT,
        VIDEO_WIDTH * sizeof(uint16_t)
    );
}

size_t retro_serialize_size(void)
{
    return 0;
}

bool retro_serialize(void *data, size_t size)
{
    (void)data;
    (void)size;

    return false;
}

bool retro_unserialize(const void *data, size_t size)
{
    (void)data;
    (void)size;

    return false;
}

void retro_cheat_reset(void)
{
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    (void)index;
    (void)enabled;
    (void)code;
}

bool retro_load_game(const struct retro_game_info *game)
{
    if(game == NULL || game->path == NULL || game->path[0] == '\0') {
        printf("[GnGeo] retro_load_game: missing game path\n");
        return false;
    }

    printf("[GnGeo] retro_load_game: %s\n", game->path);

    const char *system_dir = NULL;

    if(environ_cb == NULL ||
    !environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) ||
    system_dir == NULL ||
    system_dir[0] == '\0') {
        printf("[GnGeo] missing system directory\n");
        return false;
    }

    printf("[GnGeo] system directory: %s\n", system_dir);

    CONF_ITEM *biospath = cf_get_item_by_name("biospath");

    if(biospath == NULL) {
        printf("[GnGeo] biospath configuration unavailable\n");
        return false;
    }

    snprintf(CF_STR(biospath), CF_MAXSTRLEN, "%s", system_dir);

    printf("[GnGeo] loading GNO: %s\n", game->path);

    if(dr_open_gno((char *)game->path) == GN_FALSE) {
        printf("[GnGeo] failed to load GNO\n");
        return false;
    }

    printf(
        "[GnGeo] GNO loaded: %s\n",
        memory.rom.info.name != NULL ? memory.rom.info.name : "(unknown)"
    );

    printf("[GnGeo] initializing Neo Geo machine\n");

    init_neo();

    fix_usage = memory.fix_board_usage;
    current_pal = memory.vid.pal_neo[0];
    current_fix = memory.rom.bios_sfix.p;
    current_pc_pal = (Uint32 *)memory.vid.pal_host[0];

    memory.vid.currentpal = 0;
    memory.vid.currentfix = 0;

    if(!screen_init_libretro()) {
        return false;
    }

    printf("[GnGeo] Neo Geo machine initialized\n");

    enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_RGB565;

    if(!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixel_format)) {
        printf("[GnGeo] RGB565 pixel format unsupported\n");
        return false;
    }

    for(unsigned y = 0; y < VIDEO_HEIGHT; y++) {
        for(unsigned x = 0; x < VIDEO_WIDTH; x++) {
            uint16_t color;

            if(x < VIDEO_WIDTH / 3) {
                color = 0xF800;
            }
            else if(x < (VIDEO_WIDTH * 2) / 3) {
                color = 0x07E0;
            }
            else {
                color = 0x001F;
            }

            framebuffer[y * VIDEO_WIDTH + x] = color;
        }
    }

    game_loaded = true;

    return true;
}

bool retro_load_game_special(
    unsigned game_type,
    const struct retro_game_info *info,
    size_t num_info)
{
    (void)game_type;
    (void)info;
    (void)num_info;

    return false;
}

void retro_unload_game(void)
{
    printf("[GnGeo] retro_unload_game\n");

    screen_deinit_libretro();

    game_loaded = false;
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
    (void)id;

    return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    (void)id;

    return 0;
}
