#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "conf.h"
#include "drv.h"
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

static const char *find_driver_alias(const char *name)
{
    static const struct {
        const char *archive;
        const char *driver;
    } aliases[] = {
        {"twsoc96", "tws96"},
        {"moshougi", "mosyougi"},
        {"shocktro", "shocktra"},
        {"socbrawl", "socbrawla"},
        {"maglord", "maglordh"},
        {"kof99", "kof99a"},
        {"wh2j", "wh2jh"},
    };

    for (size_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++) {
        if (strcmp(name, aliases[i].archive) == 0) {
            return aliases[i].driver;
        }
    }

    return NULL;
}

/*
 * Known single-ROM variations of supported drivers. Only the host dumper
 * applies these overrides, and only after normal loading fails. This keeps
 * the original drivers and the emulator's built-in ROM definitions intact.
 * Every CRC/size pairing was compared against the supplied ZIP manifest.
 */
static int try_romset_variant(char *rom_dir, char *rom_name)
{
    static const struct {
        const char *archive;
        const char *driver;
        const char *romfile;
        uint32_t crc;
    } variants[] = {
        {"bstars",   "bstars",   "002-p1.bin", 0xc100b5f5},
        {"fightfev", "fightfeva", "060-s1.bin", 0xd62a72e9},
        {"gowcaizr", "gowcaizr", "094-c8.bin", 0xd80dd241},
        {"gururin",  "gururin",  "067-m1.bin", 0x9e3c6328},
        {"janshin",  "janshin",  "048-p1.bin", 0xfa818cbb},
        {"legendos", "legendos", "029-m1.bin", 0x6f2843f0},
        {"mahretsu", "mahretsu", "004-s1.bin", 0x2bd05a06},
        {"neocup98","neocup98","244-c1.bin", 0xc7a62b23},
        {"pnyaa",    "pnyaa",    "267-p1.bin", 0xbf34e71c},
        {"pulstar",  "pulstar",  "089-v1.bin", 0x6f726ecb},
        {"vliner",   "vliner",   "vl-p1.bin",  0x052f93ed},
    };

    for (size_t v = 0; v < sizeof(variants) / sizeof(variants[0]); v++) {
        if (strcmp(rom_name, variants[v].archive) != 0) {
            continue;
        }

        ROM_DEF *drv = res_load_drv(variants[v].driver);
        if (drv == NULL) {
            return GN_FALSE;
        }

        for (uint32_t i = 0; i < drv->nb_romfile; i++) {
            if (strcmp(drv->rom[i].filename, variants[v].romfile) == 0) {
                uint32_t original_crc = drv->rom[i].crc;
                drv->rom[i].crc = variants[v].crc;

                int result = dr_load_roms_with_driver(
                    &memory.rom, rom_dir, rom_name, variants[v].driver);

                /* Restore the global table even when this variant fails. */
                drv->rom[i].crc = original_crc;

                if (result == GN_TRUE) {
                    printf("ROMset variant: %s (driver %s)\n",
                           rom_name, variants[v].driver);
                }
                return result;
            }
        }
        return GN_FALSE;
    }

    return GN_FALSE;
}

/*
 * Additional ROM layouts found in the supplied 2026-10-30 MAME ZIP manifest.
 * These are host-GNO-dumper variants only. ROM CRCs select the actual entry
 * within the source ZIP, even when the filename extension differs.
 *
 * A ROM_DEF is copied before editing and restored on success or failure.
 * This avoids changing definitions used by other ZIP conversions.
 * Do not infer arbitrary modifications from filename alone: every override
 * is restricted to one known ZIP name, expected driver CRC and byte length.
 */
#define KEEP_FIELD UINT32_MAX

typedef struct {
    const char *archive;
    const char *file;
    uint32_t old_crc;
    uint32_t old_size;
    uint32_t new_crc;
    uint32_t new_size;
    uint32_t new_src;
    int remove;
} HOST_ROM_CHANGE;

static const HOST_ROM_CHANGE host_rom_changes[] = {
    /* The audio V ROMs are combined into one 1 MiB archive entry. */
    {"androdun", "049-m1.bin", 0x1a009f8c, 0x20000, 0xedd2acf4, KEEP_FIELD, KEEP_FIELD, 0},
    {"androdun", "049-v1.bin", 0x577c85b3, 0x80000, 0xce43cb89, 0x100000, KEEP_FIELD, 0},
    {"androdun", "049-v2.bin", 0xe14551c4, 0x80000, 0, 0, 0, 1},

    /* First program entry is a complete 1 MiB image, not two 512 KiB parts. */
    {"fatfury2", "047-p1.bin", 0xecfdbb69, 0x80000, 0xecfdbb69, 0x100000, KEEP_FIELD, 0},
    {"fatfury2", "047-p2.bin", 0x2a9beac5, 0x80000, 0, 0, 0, 1},

    {"flipshot", "247-p1.bin", 0xd2e7a7e3, 0x80000, 0x95779094, 0x100000, KEEP_FIELD, 0},

    {"irrmaze", "236-p1.bin", 0x6d536c6e, 0x200000, 0x4c2ff660, KEEP_FIELD, KEEP_FIELD, 0},
    {"irrmaze", "236-v2.bin", 0x1e843567, 0x100000, 0x72e3add7, KEEP_FIELD, KEEP_FIELD, 0},

    /* Two graphics ROMs use 1 MiB data rather than 512 KiB. */
    {"miexchng", "231-c3.bin", 0x14524eb5, 0x80000, 0x4f6f7a63, 0x100000, KEEP_FIELD, 0},
    {"miexchng", "231-c4.bin", 0x1694f171, 0x80000, 0x2e35e71b, 0x100000, KEEP_FIELD, 0},

    {"neomrdo", "207-p1.bin", 0x39efdb82, 0x80000, 0x334ea51e, 0x100000, KEEP_FIELD, 0},
    {"neomrdo", "207-m1.bin", 0x81eade02, 0x20000, 0xb5b74a95, KEEP_FIELD, KEEP_FIELD, 0},

    /* V1 contains V1 + V2; C1/C2 contain the first two sprite pairs. */
    {"pgoal", "219-v1.bin", 0x2cc1bd05, 0x200000, 0xd0ae33d9, KEEP_FIELD, KEEP_FIELD, 0},
    {"pgoal", "219-v2.bin", 0x06ac1d3f, 0x200000, 0xd0ae33d9, KEEP_FIELD, 0x200000, 0},
    {"pgoal", "219-c1.bin", 0x2dc69faf, 0x200000, 0x67fec4dc, KEEP_FIELD, KEEP_FIELD, 0},
    {"pgoal", "219-c2.bin", 0x5db81811, 0x200000, 0x86ed01f2, KEEP_FIELD, KEEP_FIELD, 0},
    /* Sprite source offsets in ROM_DEF are doubled (interleaved bytes). */
    {"pgoal", "219-c3.bin", 0x9dbfece5, 0x200000, 0x67fec4dc, KEEP_FIELD, 0x400000, 0},
    {"pgoal", "219-c4.bin", 0xc9f4324c, 0x200000, 0x86ed01f2, KEEP_FIELD, 0x400000, 0},

    /* Remaining common ROMs are expected from parent puzzledp.zip. */
    {"puzzldpr", "235-s1.bin", 0x5a68d91e, 0x10000, 0x3b13a22f, 0x20000, KEEP_FIELD, 0},

    /* One 4 MiB V1 image replaces two 2 MiB images. */
    {"sdodgeb", "208-v1.bin", 0x8b53e945, 0x200000, 0xe7899a24, KEEP_FIELD, KEEP_FIELD, 0},
    {"sdodgeb", "208-v2.bin", 0xaf37ebf8, 0x200000, 0xe7899a24, KEEP_FIELD, 0x200000, 0},

    {"strhoop", "079-v2.bin", 0xb19884f8, 0x80000, 0x720774eb, 0x100000, KEEP_FIELD, 0},
    {"sengoku3", "261-m1.bin", 0x36ed9cdd, 0x20000, 0x7d501c39, 0x80000, KEEP_FIELD, 0},
    {"diggerma", "dig-s1.bin", 0x75a88c1f, 0x10000, 0x9b3168f0, 0x20000, KEEP_FIELD, 0},
    {"diggerma", "dig-m1.bin", 0x833cdf1b, 0x10000, 0xe777a234, 0x20000, KEEP_FIELD, 0},
};

typedef struct {
    const char *archive;
    uint32_t region;
    uint32_t old_size;
    uint32_t new_size;
} HOST_REGION_CHANGE;

static const HOST_REGION_CHANGE host_region_changes[] = {
    {"miexchng", 9, 0x500000, 0x600000},
    {"puzzldpr", 6, 0x10000, 0x20000},
    {"strhoop", 3, 0x280000, 0x300000},
    {"sengoku3", 1, 0x20000, 0x80000},
    {"diggerma", 6, 0x10000, 0x20000},
    {"diggerma", 1, 0x10000, 0x20000},
};

static int try_romset_layout(char *rom_dir, char *rom_name)
{
    ROM_DEF *driver = res_load_drv(rom_name);
    ROM_DEF saved;
    size_t changes = 0;
    int result = GN_FALSE;
    int valid = 1;

    if (driver == NULL) {
        return GN_FALSE;
    }

    saved = *driver;

    for (size_t n = 0; n < sizeof(host_rom_changes) / sizeof(host_rom_changes[0]); n++) {
        const HOST_ROM_CHANGE *change = &host_rom_changes[n];
        if (strcmp(rom_name, change->archive) != 0) {
            continue;
        }
        changes++;

        uint32_t index;
        for (index = 0; index < driver->nb_romfile; index++) {
            if (strcmp(driver->rom[index].filename, change->file) == 0 &&
                driver->rom[index].crc == change->old_crc &&
                driver->rom[index].size == change->old_size) {
                break;
            }
        }
        if (index == driver->nb_romfile) {
            valid = 0;
            break;
        }

        if (change->remove) {
            memmove(&driver->rom[index], &driver->rom[index + 1],
                    (driver->nb_romfile - index - 1) * sizeof(driver->rom[0]));
            driver->nb_romfile--;
        } else {
            driver->rom[index].crc = change->new_crc;
            if (change->new_size != KEEP_FIELD) {
                driver->rom[index].size = change->new_size;
            }
            if (change->new_src != KEEP_FIELD) {
                driver->rom[index].src = change->new_src;
            }
        }
    }

    for (size_t n = 0; n < sizeof(host_region_changes) / sizeof(host_region_changes[0]); n++) {
        const HOST_REGION_CHANGE *change = &host_region_changes[n];
        if (strcmp(rom_name, change->archive) != 0) {
            continue;
        }
        if (driver->romsize[change->region] != change->old_size) {
            valid = 0;
            break;
        }
        driver->romsize[change->region] = change->new_size;
    }

    if (changes != 0 && valid) {
        result = dr_load_roms_with_driver(&memory.rom, rom_dir, rom_name, rom_name);
        if (result == GN_TRUE) {
            printf("ROMset layout: %s (%zu ROM adjustments)\n", rom_name, changes);
        }
    }

    *driver = saved;
    return result;
}

#undef KEEP_FIELD

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
        const char *driver = find_driver_alias(rom_name);
        int loaded = GN_FALSE;

        if (driver != NULL) {
            loaded = dr_load_roms_with_driver(
                &memory.rom, rom_dir, rom_name, driver);
            if (loaded == GN_TRUE) {
                printf("Driver alias: %s -> %s\n", rom_name, driver);
            }
        }

        if (loaded != GN_TRUE) {
            loaded = try_romset_variant(rom_dir, rom_name);
        }

        if (loaded != GN_TRUE) {
            loaded = try_romset_layout(rom_dir, rom_name);
        }

        if (loaded != GN_TRUE) {
            fprintf(stderr, "ERROR: %s\n", gnerror);
            return EXIT_FAILURE;
        }
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
