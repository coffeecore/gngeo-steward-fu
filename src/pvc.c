#include "pvc.h"
#include "memory.h"

#include <string.h>

/* 8 KiB of PVC cartridge RAM (0x2FE000-0x2FFFFF). */
static uint16_t cart_ram[0x1000];

void pvc_reset(void)
{
    memset(cart_ram, 0, sizeof(cart_ram));
}

void *pvc_state_data(void)
{
    return cart_ram;
}

uint16_t pvc_read_word(uint32_t addr)
{
    return cart_ram[(addr - PVC_BASE) >> 1];
}

uint8_t pvc_read_byte(uint32_t addr)
{
    uint16_t data = pvc_read_word(addr & ~1u);
    return (addr & 1) ? (uint8_t)data : (uint8_t)(data >> 8);
}

static void unpack_color(void)
{
    uint16_t pen = cart_ram[0xff0];
    uint8_t b = ((pen & 0x000f) << 1) | ((pen & 0x1000) >> 12);
    uint8_t g = ((pen & 0x00f0) >> 3) | ((pen & 0x2000) >> 13);
    uint8_t r = ((pen & 0x0f00) >> 7) | ((pen & 0x4000) >> 14);
    uint8_t s = (pen & 0x8000) >> 15;

    cart_ram[0xff1] = (g << 8) | b;
    cart_ram[0xff2] = (s << 8) | r;
}

static void pack_color(void)
{
    uint16_t gb = cart_ram[0xff4];
    uint16_t sr = cart_ram[0xff5];

    cart_ram[0xff6] = ((gb & 0x001e) >> 1) |
                      ((gb & 0x1e00) >> 5) |
                      ((sr & 0x001e) << 7) |
                      ((gb & 0x0001) << 12) |
                      ((gb & 0x0100) << 5) |
                      ((sr & 0x0001) << 14) |
                      ((sr & 0x0100) << 7);
}

static void switch_bank(void)
{
    uint32_t bank = 0x100000u +
                    ((cart_ram[0xff8] >> 8) | ((uint32_t)cart_ram[0xff9] << 8));

    cart_ram[0xff8] = (cart_ram[0xff8] & 0xfe00) | 0x00a0;
    cart_ram[0xff9] &= 0x7fff;

    /* Do not map outside the 68k cartridge ROM. */
    if (memory.rom.cpu_m68k.size >= 0x100000u &&
        bank <= memory.rom.cpu_m68k.size - 0x100000u) {
        cpu_68k_bankswitch(bank);
    }
}

void pvc_write_word(uint32_t addr, uint16_t data)
{
    uint32_t offset = (addr - PVC_BASE) >> 1;
    cart_ram[offset] = data;

    if (offset == 0xff0) {
        unpack_color();
    } else if (offset == 0xff4 || offset == 0xff5) {
        pack_color();
    }

    if (offset >= 0xff8) {
        switch_bank();
    }
}

void pvc_write_byte(uint32_t addr, uint8_t data)
{
    uint32_t word_addr = addr & ~1u;
    uint16_t value = pvc_read_word(word_addr);

    if (addr & 1) {
        value = (value & 0xff00u) | data;
    } else {
        value = (value & 0x00ffu) | ((uint16_t)data << 8);
    }

    pvc_write_word(word_addr, value);
}
