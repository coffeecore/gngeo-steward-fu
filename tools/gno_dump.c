#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "conf.h"
#include "emu.h"
#include "gnutil.h"
#include "memory.h"
#include "roms.h"

neo_mem memory;
char *original_rom_name = NULL;

void gn_init_pbar(char *name, int size)
{
    printf("%s (%d bytes)\n", name, size);
}

void gn_update_pbar(int pos)
{
    (void)pos;
}

void gn_terminate_pbar(void)
{
}

void gn_popup_error(char *name, char *fmt, ...)
{
    va_list args;

    fprintf(stderr, "%s: ", name);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}

static int split_rom_path(
    const char *input,
    char *rom_dir,
    size_t rom_dir_size,
    char *rom_name,
    size_t rom_name_size)
{
    const char *slash;
    const char *filename;
    size_t dir_len;
    size_t name_len;

    slash = strrchr(input, '/');

    if (slash != NULL) {
        dir_len = (size_t)(slash - input);

        if (dir_len == 0) {
            dir_len = 1;
        }

        if (dir_len >= rom_dir_size) {
            return 0;
        }

        memcpy(rom_dir, input, dir_len);
        rom_dir[dir_len] = '\0';

        filename = slash + 1;
    } else {
        strcpy(rom_dir, ".");
        filename = input;
    }

    name_len = strlen(filename);

    if (name_len <= 4 || strcasecmp(filename + name_len - 4, ".zip") != 0) {
        return 0;
    }

    name_len -= 4;

    if (name_len == 0 || name_len >= rom_name_size) {
        return 0;
    }

    memcpy(rom_name, filename, name_len);
    rom_name[name_len] = '\0';

    return 1;
}

int main(int argc, char **argv)
{
    char rom_dir[1024];
    char rom_name[256];
    char output[1280];
    char bios[1280];
    CONF_ITEM *rompath;

    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: %s game.zip [output.gno]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (!split_rom_path(
            argv[1],
            rom_dir,
            sizeof(rom_dir),
            rom_name,
            sizeof(rom_name))) {
        fprintf(stderr, "ERROR: expected a .zip ROM: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    if (access(argv[1], R_OK) != 0) {
        fprintf(stderr, "ERROR: ROM not readable: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    snprintf(bios, sizeof(bios), "%s/neogeo.zip", rom_dir);

    if (access(bios, R_OK) != 0) {
        fprintf(stderr, "ERROR: neogeo.zip not found: %s\n", bios);
        return EXIT_FAILURE;
    }

    memset(&memory, 0, sizeof(memory));

    cf_init();

    rompath = cf_get_item_by_name("rompath");

    if (rompath == NULL) {
        fprintf(stderr, "ERROR: rompath configuration is missing\n");
        return EXIT_FAILURE;
    }

    if (strlen(rom_dir) >= CF_MAXSTRLEN) {
        fprintf(stderr, "ERROR: ROM directory path is too long\n");
        return EXIT_FAILURE;
    }

    strcpy(CF_STR(rompath), rom_dir);

    conf.system = SYS_ARCADE;
    conf.country = CTY_EUROPE;

    printf("ROM:  %s\n", rom_name);
    printf("Path: %s\n", rom_dir);

    if (dr_load_roms(&memory.rom, rom_dir, rom_name) != GN_TRUE) {
        fprintf(stderr, "ERROR: %s\n", gnerror);
        return EXIT_FAILURE;
    }

    memcpy(
        memory.game_vector,
        memory.rom.cpu_m68k.p,
        sizeof(memory.game_vector)
    );

    if (argc == 3) {
        snprintf(output, sizeof(output), "%s", argv[2]);
    } else {
        snprintf(output, sizeof(output), "%s/%s.gno", rom_dir, rom_name);
    }

    printf("Writing: %s\n", output);

    if (dr_save_gno(&memory.rom, output) != GN_TRUE) {
        fprintf(stderr, "ERROR: unable to write %s\n", output);
        return EXIT_FAILURE;
    }

    printf("Created: %s\n", output);

    return EXIT_SUCCESS;
}
