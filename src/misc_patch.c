#include <string.h>
#include <stdint.h>

#include "memory.h"

void setup_misc_patch(char *name)
{
    if(!strcmp(name, "ssideki")) {
        WRITE_WORD_ROM(&memory.rom.cpu_m68k.p[0x2240], 0x4e71);
    }

    if(!strcmp(name, "mslugx")) {
        /* patch out protection checks */
        int i;
        uint8_t *RAM = memory.rom.cpu_m68k.p;

        for(i = 0; i < memory.rom.cpu_m68k.size; i += 2) {
            if((READ_WORD_ROM(&RAM[i + 0]) == 0x0243)
                && (READ_WORD_ROM(&RAM[i + 2]) == 0x0001)
                && (READ_WORD_ROM(&RAM[i + 4]) == 0x6600)) {

                WRITE_WORD_ROM(&RAM[i + 4], 0x4e71);
                WRITE_WORD_ROM(&RAM[i + 6], 0x4e71);
            }
        }

        WRITE_WORD_ROM(&RAM[0x3bdc], 0x4e71);
        WRITE_WORD_ROM(&RAM[0x3bde], 0x4e71);
        WRITE_WORD_ROM(&RAM[0x3be0], 0x4e71);
        WRITE_WORD_ROM(&RAM[0x3c0c], 0x4e71);
        WRITE_WORD_ROM(&RAM[0x3c0e], 0x4e71);
        WRITE_WORD_ROM(&RAM[0x3c10], 0x4e71);

        WRITE_WORD_ROM(&RAM[0x3c36], 0x4e71);
        WRITE_WORD_ROM(&RAM[0x3c38], 0x4e71);
    }
}
