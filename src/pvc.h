#pragma once

#include <stdint.h>

#define BKS_PVC 1
#define PVC_BASE 0x2FE000u
#define PVC_END  0x2FFFFFu

#define PVC_RAM_SIZE 0x2000u

void pvc_reset(void);
uint8_t pvc_read_byte(uint32_t addr);
uint16_t pvc_read_word(uint32_t addr);
void pvc_write_byte(uint32_t addr, uint8_t data);
void pvc_write_word(uint32_t addr, uint16_t data);
void *pvc_state_data(void);
