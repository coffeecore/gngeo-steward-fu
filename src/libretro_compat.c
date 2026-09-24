#include <stdio.h>
#include <stdint.h>

#include "emu.h"
#include "memory.h"
#include "state.h"
#include "pd4990a.h"
#include "sound.h"
#include "ym2610_interf.h"
#include "timer.h"
#include "video.h"

int current_line = 0;
int nb_interlace = 256;

/*
 * Standalone GnGeo displays progress bars while loading/decrypting ROMs.
 * A libretro core has no GnGeo frontend UI, so these are intentionally
 * no-ops for now.
 */
void gn_init_pbar(const char *label, int size)
{
    (void)label;
    (void)size;
}

void gn_update_pbar(int pos)
{
    (void)pos;
}

void gn_terminate_pbar(void)
{
}

/*
 * Temporary replacement for the standalone popup.
 * We will later route errors through the libretro logging interface.
 */
void gn_popup_error(const char *message, ...)
{
    if(message != NULL) {
        fprintf(stderr, "[GnGeo] %s\n", message);
    }
}

/*
 * Temporary stubs.
 *
 * IMPORTANT:
 * These must be replaced before retro_load_game() actually calls
 * GnGeo's init_game().
 */
static void libretro_neogeo_reset(void)
{
    sram_lock = 0;
    sound_code = 0;
    pending_command = 0;
    result_code = 0;

    if(memory.rom.cpu_m68k.size > 0x100000) {
        cpu_68k_bankswitch(0x100000);
    }
    else {
        cpu_68k_bankswitch(0);
    }

    cpu_68k_reset();
}

void init_neo(void)
{
    neogeo_init_save_state();

    cpu_68k_init();
    pd4990a_init();

    /*
     * Initialise the emulated Z80/YM2610, but NOT SDL audio.
     * Audio output will later go through the libretro callback.
     */
    cpu_z80_init();
    YM2610_sh_start();

    libretro_neogeo_reset();
}

static uint32_t libretro_tm_cycle = 0;
static int libretro_fc = 0;

void libretro_run_68k_frame(void)
{
    const uint32_t cpu_68k_timeslice = 200000;

    libretro_tm_cycle =
        cpu_68k_run(cpu_68k_timeslice - libretro_tm_cycle);

    /*
     * Equivalent to the non-video part of neo_interrupt().
     */
    pd4990a_addretrace();

    if(!(memory.vid.irq2control & 0x8)) {
        if(libretro_fc >= neogeo_frame_counter_speed) {
            libretro_fc = 0;
            neogeo_frame_counter++;
        }

        libretro_fc++;
    }

    draw_screen();

    memory.watchdog++;

    if(memory.watchdog > 7) {
        printf("[GnGeo] watchdog reset\n");
        cpu_68k_reset();
    }

    cpu_68k_interrupt(1);
}

void libretro_run_z80_frame(void)
{
    const uint32_t cpu_z80_timeslice = 73333;
    const uint32_t cpu_z80_timeslice_interlace =
        cpu_z80_timeslice / nb_interlace;

    for(int i = 0; i < nb_interlace; i++) {
        cpu_z80_run(cpu_z80_timeslice_interlace);
        my_timer();
    }
}

void setup_misc_patch(char *name)
{
    (void)name;
}
