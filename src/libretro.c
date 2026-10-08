#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <libretro.h>

#include "conf.h"
#include "event.h"
#include "memory.h"
#include "roms.h"
#include "gnutil.h"
#include "emu.h"
#include "state.h"
#include "screen.h"
#include "ym2610.h"

#define VIDEO_WIDTH  320
#define VIDEO_HEIGHT 240

#define SAVE_RAM_SIZE 0x10000

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static unsigned auto_frameskip_max = 1;

static bool retro_audio_buff_active = false;
static bool retro_audio_buff_underrun = false;
static unsigned auto_frameskip_counter = 0;

static bool game_loaded = false;

static uint16_t framebuffer[VIDEO_WIDTH * VIDEO_HEIGHT];

extern Uint16 play_buffer[16384];

static unsigned audio_sample_accumulator = 0;
static unsigned audio_sample_rate = 22050;

static const struct retro_variable gngeo_variables[] = {
    {
        "gngeo-system",
        "System; MVS|AES|UniBIOS"
    },
    {
        "gngeo-region",
        "MVS Region; Europe|USA|Japan|Asia"
    },

    {
        "gngeo-a-button",
        "A Button; A|None|B|C|D|A+B|A+C|A+D|B+C|B+D|C+D|A+B+C|A+B+D|A+C+D|B+C+D|A+B+C+D"
    },
    {
        "gngeo-b-button",
        "B Button; B|None|A|C|D|A+B|A+C|A+D|B+C|B+D|C+D|A+B+C|A+B+D|A+C+D|B+C+D|A+B+C+D"
    },
    {
        "gngeo-x-button",
        "X Button; C|None|A|B|D|A+B|A+C|A+D|B+C|B+D|C+D|A+B+C|A+B+D|A+C+D|B+C+D|A+B+C+D"
    },
    {
        "gngeo-y-button",
        "Y Button; D|None|A|B|C|A+B|A+C|A+D|B+C|B+D|C+D|A+B+C|A+B+D|A+C+D|B+C+D|A+B+C+D"
    },
    {
        "gngeo-l-button",
        "L Button; A+B|None|A|B|C|D|A+C|A+D|B+C|B+D|C+D|A+B+C|A+B+D|A+C+D|B+C+D|A+B+C+D"
    },
    {
        "gngeo-r-button",
        "R Button; A+C|None|A|B|C|D|A+B|A+D|B+C|B+D|C+D|A+B+C|A+B+D|A+C+D|B+C+D|A+B+C+D"
    },
    {
        "gngeo-sample-rate",
        "Sample Rate; 22050|44100"
    },
    {
        "gngeo-auto-frameskip",
        "Auto Frame Skip; enabled|disabled"
    },
    {
        "gngeo-auto-frameskip-max",
        "Auto Frame Skip Max; 1|2|3|4"
    },

    { NULL, NULL }
};

static void libretro_audio_buffer_status_cb(
    bool active,
    unsigned occupancy,
    bool underrun_likely)
{
    retro_audio_buff_active = active;
    retro_audio_buff_underrun = underrun_likely;
}

static void libretro_init_frameskip(void)
{
    if(environ_cb == NULL) {
        return;
    }

    if(conf.autoframeskip) {
        struct retro_audio_buffer_status_callback cb = {
            libretro_audio_buffer_status_cb
        };

        if(!environ_cb(
            RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK,
            &cb)) {
            printf("[GnGeo] auto frameskip unavailable: frontend callback unsupported\n");

            conf.autoframeskip = GN_FALSE;
            retro_audio_buff_active = false;
            retro_audio_buff_underrun = false;
        }
        else {
            printf(
                "[GnGeo] auto frameskip enabled, max consecutive skips: %u\n",
                auto_frameskip_max
            );
        }
    }
    else {
        printf("[GnGeo] auto frameskip disabled\n");

        environ_cb(
            RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK,
            NULL
        );

        retro_audio_buff_active = false;
        retro_audio_buff_underrun = false;
    }

    auto_frameskip_counter = 0;
}

static int libretro_should_skip_frame(void)
{
    if(!conf.autoframeskip ||
       !retro_audio_buff_active ||
       !retro_audio_buff_underrun) {
        auto_frameskip_counter = 0;
        return 0;
    }

    if(auto_frameskip_counter >= auto_frameskip_max) {
        auto_frameskip_counter = 0;

        return 0;
    }

    auto_frameskip_counter++;

    return 1;
}

static uint32_t libretro_button_value(const char *value)
{
    static const char *values[] = {
        "None",
        "A",
        "B",
        "C",
        "D",
        "A+B",
        "A+C",
        "A+D",
        "B+C",
        "B+D",
        "C+D",
        "A+B+C",
        "A+B+D",
        "A+C+D",
        "B+C+D",
        "A+B+C+D"
    };

    if(value == NULL) {
        return 0;
    }

    for(uint32_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        if(strcmp(value, values[i]) == 0) {
            return i;
        }
    }

    return 0;
}

static uint32_t libretro_get_button_variable(
    const char *key,
    uint32_t fallback
)
{
    struct retro_variable var = {0};

    if(environ_cb == NULL) {
        return fallback;
    }

    var.key = key;

    if(environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) &&
       var.value != NULL) {
        return libretro_button_value(var.value);
    }

    return fallback;
}

void libretro_run_68k_frame(int draw_frame);

void libretro_run_z80_frame(void);

void libretro_reset_machine(void);

void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;

    environ_cb(
        RETRO_ENVIRONMENT_SET_VARIABLES,
        (void *)gngeo_variables
    );
}

static void libretro_update_frameskip_variables(void)
{
    struct retro_variable var = {0};

    conf.autoframeskip = GN_TRUE;
    auto_frameskip_max = 1;

    if(environ_cb == NULL) {
        return;
    }

    var.key = "gngeo-auto-frameskip";

    if(environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) &&
       var.value != NULL) {
        if(strcmp(var.value, "disabled") == 0) {
            conf.autoframeskip = GN_FALSE;
        }
    }

    var.key = "gngeo-auto-frameskip-max";
    var.value = NULL;

    if(environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) &&
       var.value != NULL) {
        unsigned value = (unsigned)strtoul(var.value, NULL, 10);

        if(value >= 1 && value <= 4) {
            auto_frameskip_max = value;
        }
    }
}

static void libretro_update_variables(void)
{
    struct retro_variable var = {0};

    /*
     * GnGeo defaults.
     */
    conf.system = SYS_ARCADE;
    conf.country = CTY_EUROPE;
    audio_sample_rate = 22050;

    libretro_update_frameskip_variables();

    if(environ_cb == NULL) {
        return;
    }

    var.key = "gngeo-system";

    if(environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) &&
       var.value != NULL) {
        if(strcmp(var.value, "AES") == 0) {
            conf.system = SYS_HOME;
        }
        else if(strcmp(var.value, "UniBIOS") == 0) {
            conf.system = SYS_UNIBIOS;
        }
    }

    var.key = "gngeo-region";
    var.value = NULL;

    if(environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) &&
       var.value != NULL) {
        if(strcmp(var.value, "USA") == 0) {
            conf.country = CTY_USA;
        }
        else if(strcmp(var.value, "Japan") == 0) {
            conf.country = CTY_JAPAN;
        }
        else if(strcmp(var.value, "Asia") == 0) {
            conf.country = CTY_ASIA;
        }
    }

    var.key = "gngeo-sample-rate";
    var.value = NULL;

    if(environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) &&
    var.value != NULL) {
        if(strcmp(var.value, "44100") == 0) {
            audio_sample_rate = 44100;
        }
    }

    conf.sample_rate = audio_sample_rate;

    conf.a_btn = libretro_get_button_variable(
        "gngeo-a-button",
        conf.a_btn
    );

    conf.b_btn = libretro_get_button_variable(
        "gngeo-b-button",
        conf.b_btn
    );

    conf.x_btn = libretro_get_button_variable(
        "gngeo-x-button",
        conf.x_btn
    );

    conf.y_btn = libretro_get_button_variable(
        "gngeo-y-button",
        conf.y_btn
    );

    conf.l_btn = libretro_get_button_variable(
        "gngeo-l-button",
        conf.l_btn
    );

    conf.r_btn = libretro_get_button_variable(
        "gngeo-r-button",
        conf.r_btn
    );
}

static void libretro_check_variable_updates(void)
{
    bool updated = false;
    int old_autoframeskip;
    unsigned old_auto_frameskip_max;

    if(environ_cb == NULL ||
       !environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) ||
       !updated) {
        return;
    }

    old_autoframeskip = conf.autoframeskip;
    old_auto_frameskip_max = auto_frameskip_max;

    libretro_update_frameskip_variables();

    if(old_autoframeskip != conf.autoframeskip) {
        libretro_init_frameskip();
    }
    else if(old_auto_frameskip_max != auto_frameskip_max) {
        auto_frameskip_counter = 0;

        printf(
            "[GnGeo] auto frameskip max consecutive skips: %u\n",
            auto_frameskip_max
        );
    }
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
    info->timing.sample_rate = (double)audio_sample_rate;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port;
    (void)device;
}

void retro_reset(void)
{
    if(!game_loaded) {
        return;
    }

    libretro_reset_machine();
    audio_sample_accumulator = 0;
    auto_frameskip_counter = 0;
    retro_audio_buff_underrun = false;
}

static void libretro_set_key(uint32_t key, int pressed)
{
    if(pressed) {
        memory.intern_p1 &= ~(1 << key);
    }
    else {
        memory.intern_p1 |= (1 << key);
    }
}

static void libretro_set_button(uint32_t value, int pressed)
{
    switch(value) {
    case 0:
        break;
    case 1:
        libretro_set_key(KEY_A, pressed);
        break;
    case 2:
        libretro_set_key(KEY_B, pressed);
        break;
    case 3:
        libretro_set_key(KEY_C, pressed);
        break;
    case 4:
        libretro_set_key(KEY_D, pressed);
        break;
    case 5:
        libretro_set_key(KEY_A, pressed);
        libretro_set_key(KEY_B, pressed);
        break;
    case 6:
        libretro_set_key(KEY_A, pressed);
        libretro_set_key(KEY_C, pressed);
        break;
    case 7:
        libretro_set_key(KEY_A, pressed);
        libretro_set_key(KEY_D, pressed);
        break;
    case 8:
        libretro_set_key(KEY_B, pressed);
        libretro_set_key(KEY_C, pressed);
        break;
    case 9:
        libretro_set_key(KEY_B, pressed);
        libretro_set_key(KEY_D, pressed);
        break;
    case 10:
        libretro_set_key(KEY_C, pressed);
        libretro_set_key(KEY_D, pressed);
        break;
    case 11:
        libretro_set_key(KEY_A, pressed);
        libretro_set_key(KEY_B, pressed);
        libretro_set_key(KEY_C, pressed);
        break;
    case 12:
        libretro_set_key(KEY_A, pressed);
        libretro_set_key(KEY_B, pressed);
        libretro_set_key(KEY_D, pressed);
        break;
    case 13:
        libretro_set_key(KEY_A, pressed);
        libretro_set_key(KEY_C, pressed);
        libretro_set_key(KEY_D, pressed);
        break;
    case 14:
        libretro_set_key(KEY_B, pressed);
        libretro_set_key(KEY_C, pressed);
        libretro_set_key(KEY_D, pressed);
        break;
    case 15:
        libretro_set_key(KEY_A, pressed);
        libretro_set_key(KEY_B, pressed);
        libretro_set_key(KEY_C, pressed);
        libretro_set_key(KEY_D, pressed);
        break;
    }
}

static int libretro_button_pressed(unsigned id)
{
    if(input_state_cb == NULL) {
        return 0;
    }

    return input_state_cb(
        0,
        RETRO_DEVICE_JOYPAD,
        0,
        id
    ) != 0;
}

static void libretro_update_input(void)
{
    if(input_poll_cb != NULL) {
        input_poll_cb();
    }

    libretro_set_key(
        KEY_UP,
        libretro_button_pressed(RETRO_DEVICE_ID_JOYPAD_UP)
    );

    libretro_set_key(
        KEY_DOWN,
        libretro_button_pressed(RETRO_DEVICE_ID_JOYPAD_DOWN)
    );

    libretro_set_key(
        KEY_LEFT,
        libretro_button_pressed(RETRO_DEVICE_ID_JOYPAD_LEFT)
    );

    libretro_set_key(
        KEY_RIGHT,
        libretro_button_pressed(RETRO_DEVICE_ID_JOYPAD_RIGHT)
    );

    libretro_set_button(
        conf.a_btn,
        libretro_button_pressed(RETRO_DEVICE_ID_JOYPAD_A)
    );

    libretro_set_button(
        conf.b_btn,
        libretro_button_pressed(RETRO_DEVICE_ID_JOYPAD_B)
    );

    libretro_set_button(
        conf.x_btn,
        libretro_button_pressed(RETRO_DEVICE_ID_JOYPAD_X)
    );

    libretro_set_button(
        conf.y_btn,
        libretro_button_pressed(RETRO_DEVICE_ID_JOYPAD_Y)
    );

    libretro_set_button(
        conf.l_btn,
        libretro_button_pressed(RETRO_DEVICE_ID_JOYPAD_L)
    );

    libretro_set_button(
        conf.r_btn,
        libretro_button_pressed(RETRO_DEVICE_ID_JOYPAD_R)
    );

    if(libretro_button_pressed(RETRO_DEVICE_ID_JOYPAD_START)) {
        memory.intern_start &= ~(1 << 0);
    }
    else {
        memory.intern_start |= (1 << 0);
    }

    if(libretro_button_pressed(RETRO_DEVICE_ID_JOYPAD_SELECT)) {
        memory.intern_coin &= ~(1 << 0);
    }
    else {
        memory.intern_coin |= (1 << 0);
    }
}

static void libretro_update_audio(void)
{
    unsigned frames;

    if(audio_batch_cb == NULL) {
        return;
    }

    /*
    * Keep the fractional part because the sample rate is not necessarily
    * evenly divisible by the video refresh rate.
    */
    audio_sample_accumulator += audio_sample_rate;

    frames = audio_sample_accumulator / 60;
    audio_sample_accumulator %= 60;

    YM2610Update_stream(frames);

    audio_batch_cb(
        (const int16_t *)play_buffer,
        frames
    );
}

void retro_run(void)
{
    int skip_frame;

    if(!game_loaded) {
        return;
    }

    libretro_check_variable_updates();

    libretro_update_input();

    skip_frame = libretro_should_skip_frame();

    libretro_run_z80_frame();
    libretro_run_68k_frame(!skip_frame);

    libretro_update_audio();

    if(skip_frame) {
        video_cb(
            NULL,
            VIDEO_WIDTH,
            VIDEO_HEIGHT,
            VIDEO_WIDTH * sizeof(uint16_t)
        );

        return;
    }

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
    if(!game_loaded) {
        return 0;
    }

    return state_serialize_size();
}

bool retro_serialize(void *data, size_t size)
{
    if(!game_loaded) {
        return false;
    }

    return state_serialize(data, size) == GN_TRUE;
}

bool retro_unserialize(const void *data, size_t size)
{
    if(!game_loaded) {
        return false;
    }

    if(state_unserialize(data, size) != GN_TRUE) {
        return false;
    }

    /*
     * The audio fractional position is frontend-specific rather than
     * part of the emulated Neo Geo state.
     */
    audio_sample_accumulator = 0;
    auto_frameskip_counter = 0;
    retro_audio_buff_underrun = false;

    return true;
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

static int libretro_load_zip(const char *path)
{
    CONF_ITEM *rompath = cf_get_item_by_name("rompath");

    if(rompath == NULL) {
        printf("[GnGeo] rompath configuration unavailable\n");
        return GN_FALSE;
    }

    const char *filename = strrchr(path, '/');
    const char *extension;

    if(filename != NULL) {
        size_t dir_len = filename - path;

        if(dir_len == 0) {
            snprintf(CF_STR(rompath), CF_MAXSTRLEN, "/");
        }
        else {
            if(dir_len >= CF_MAXSTRLEN) {
                return GN_FALSE;
            }

            memcpy(CF_STR(rompath), path, dir_len);
            CF_STR(rompath)[dir_len] = '\0';
        }

        filename++;
    }
    else {
        snprintf(CF_STR(rompath), CF_MAXSTRLEN, ".");
        filename = path;
    }

    extension = strrchr(filename, '.');

    if(extension == NULL || strcmp(extension, ".zip") != 0) {
        return GN_FALSE;
    }

    size_t name_len = extension - filename;

    if(name_len == 0 || name_len >= CF_MAXSTRLEN) {
        return GN_FALSE;
    }

    char game_name[CF_MAXSTRLEN];

    memcpy(game_name, filename, name_len);
    game_name[name_len] = '\0';

    printf(
        "[GnGeo] loading ZIP: %s from %s\n",
        game_name,
        CF_STR(rompath)
    );

    return dr_load_game(game_name);
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

    libretro_update_variables();

    libretro_init_frameskip();

    const char *extension = strrchr(game->path, '.');

    if(extension == NULL) {
        printf("[GnGeo] missing ROM extension\n");
        return false;
    }

    if(strcmp(extension, ".gno") == 0) {
        printf("[GnGeo] loading GNO: %s\n", game->path);

        if(dr_open_gno((char *)game->path) == GN_FALSE) {
            printf("[GnGeo] failed to load GNO\n");
            return false;
        }
    }
    else if(strcmp(extension, ".zip") == 0) {
        if(libretro_load_zip(game->path) == GN_FALSE) {
            printf("[GnGeo] failed to load ZIP\n");
            return false;
        }
    }
    else {
        printf("[GnGeo] unsupported ROM extension: %s\n", extension);
        return false;
    }

    printf(
        "[GnGeo] ROM loaded: %s\n",
        memory.rom.info.name != NULL ? memory.rom.info.name : "(unknown)"
    );

    printf("[GnGeo] initializing Neo Geo machine\n");

    conf.sound = 1;
    conf.sample_rate = audio_sample_rate;

    init_neo();

    setup_misc_patch(conf.game);

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

    audio_sample_accumulator = 0;

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

    if(!game_loaded) {
        return;
    }

    game_loaded = false;

    screen_deinit_libretro();
    dr_free_roms(&memory.rom);

    audio_sample_accumulator = 0;
    auto_frameskip_counter = 0;
    retro_audio_buff_underrun = false;
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
    if(id == RETRO_MEMORY_SAVE_RAM && game_loaded) {
        return memory.sram;
    }

    return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    if(id == RETRO_MEMORY_SAVE_RAM && game_loaded) {
        return SAVE_RAM_SIZE;
    }

    return 0;
}
