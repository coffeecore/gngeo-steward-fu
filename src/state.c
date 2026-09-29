#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "SDL.h"
#include "SDL_endian.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include "memory.h"
#include "state.h"
#include "screen.h"
#include "sound.h"
#include "roms.h"
#include "emu.h"
#include "gnutil.h"
#include "menu.h"

#ifdef ARM
static int m68k_flag = 0x3;
static int z80_flag = 0xC;
#else
static int m68k_flag = 0x2;
static int z80_flag = 0x8;
#endif

static int endian_flag = 0x0;

SDL_Surface *state_img_tmp;

static const char *get_state_dir(void)
{
  const char *state_dir = getenv("GNGEO_STATE_DIR");

  if(state_dir && state_dir[0] != '\0') {
    return state_dir;
  }

  return get_gngeo_dir();
}

static int get_state_path(char *path, size_t size, const char *game, int slot)
{
  const char *dir = get_state_dir();
  size_t dir_len = strlen(dir);
  const char *separator = (dir_len > 0 && dir[dir_len - 1] == '/') ? "" : "/";

  int len = snprintf(
      path,
      size,
      "%s%s%s.%03d",
      dir,
      separator,
      game,
      slot
  );

  return len >= 0 && (size_t)len < size;
}

int get_state_path_template(char *path, size_t size, const char *game)
{
  const char *dir = get_state_dir();
  size_t dir_len = strlen(dir);
  const char *separator = (dir_len > 0 && dir[dir_len - 1] == '/') ? "" : "/";

  int len = snprintf(
      path,
      size,
      "%s%s%s.%%03i",
      dir,
      separator,
      game
  );

  return len >= 0 && (size_t)len < size;
}

void cpu_68k_mkstate(gzFile gzf, int mode);
void cpu_z80_mkstate(gzFile gzf, int mode);
void ym2610_mkstate(gzFile gzf, int mode);
Uint32 how_many_slot(char *game)
{
  char st_name[1024];
  FILE *f;
  Uint32 slot = 0;

  while(get_state_path(st_name, sizeof(st_name), game, slot)) {
    if((f = fopen(st_name, "rb"))) {
      fclose(f);
      slot++;
    }
    else {
      return slot;
    }
  }

  return slot;
}

static gzFile open_state(char *game, int slot, int mode)
{
  char st_name[1024];
  char string[20];
  char *m = (mode == STWRITE ? "wb" : "rb");
  gzFile gzf;
  int  flags;
  Uint32 rate;

  if(!get_state_path(st_name, sizeof(st_name), game, slot)) {
    printf("Save state path is too long\n");
    return NULL;
  }

  if((gzf = gzopen(st_name, m)) == NULL) {
    printf("%s not found\n", st_name);
    return NULL;
  }

  if(mode == STREAD) {

    memset(string, 0, 20);
    gzread(gzf, string, 6);

   if(strcmp(string, "GNGST4") != 0 &&
      strcmp(string, "GNGST3") != 0 &&
      strcmp(string, "GNGST2") != 0) {
      printf("%s is not a valid gngeo st file\n", st_name);
      gzclose(gzf);
      return NULL;
    }

    if(strcmp(string, "GNGST2") == 0) {
      state_version = ST_VER2;
    }
    if(strcmp(string, "GNGST3") == 0) {
      state_version = ST_VER3;
    }

    if(strcmp(string, "GNGST4") == 0) {
      state_version = ST_VER4;
    }


    gzread(gzf, &flags, sizeof(int));

    if(flags != (m68k_flag | z80_flag | endian_flag)) {
      printf("This save state comme from a different endian architecture.\n"
             "This is not currently supported :(\n");
      gzclose(gzf);
      return NULL;
    }
  }
  else {
    int flags = m68k_flag | z80_flag | endian_flag;

    state_version = ST_VER4;

    gzwrite(gzf, "GNGST4", 6);
    gzwrite(gzf, &flags, sizeof(int));
  }
  return gzf;
}

static Uint8 *state_mem_buffer;
static size_t state_mem_size;
static size_t state_mem_offset;
static int state_mem_active;
static int state_mem_error;

static void state_mem_begin(void *data, size_t size)
{
  state_mem_buffer = data;
  state_mem_size = size;
  state_mem_offset = 0;
  state_mem_active = 1;
  state_mem_error = 0;
}

static void state_mem_end(void)
{
  state_mem_buffer = NULL;
  state_mem_size = 0;
  state_mem_offset = 0;
  state_mem_active = 0;
}

int mkstate_data(gzFile gzf, void *data, int size, int mode)
{
  if(state_mem_active) {
    if(state_mem_buffer != NULL) {
      if(state_mem_offset > state_mem_size ||
         (size_t)size > state_mem_size - state_mem_offset) {
        state_mem_error = 1;
        return 0;
      }

      if(mode == STREAD) {
        memcpy(data, state_mem_buffer + state_mem_offset, size);
      }
      else {
        memcpy(state_mem_buffer + state_mem_offset, data, size);
      }
    }

    state_mem_offset += size;

    return size;
  }

  if(mode == STREAD) {
    return gzread(gzf, data, size);
  }

  return gzwrite(gzf, data, size);
}


SDL_Surface *load_state_img(char *game, int slot)
{
  gzFile gzf;

  if((gzf = open_state(game, slot, STREAD)) == NULL) {
    return NULL;
  }

  gzread(gzf, state_img_tmp->pixels, 304 * 224 * 2);


  gzclose(gzf);
  return state_img_tmp;
}

static void neogeo_mkstate(gzFile gzf, int mode)
{
  GAME_ROMS r;

  memcpy(&r, &memory.rom, sizeof(GAME_ROMS));
  mkstate_data(gzf, &memory, sizeof(memory), mode);

  /* ROM information is needed for Z80 bankswitching. */
  if(mode == STREAD) {
    memcpy(&memory.rom, &r, sizeof(GAME_ROMS));
  }

  mkstate_data(gzf, &bankaddress, sizeof(Uint32), mode);
  mkstate_data(gzf, &sram_lock, sizeof(Uint8), mode);
  cpu_68k_mkstate(gzf, mode);
#ifndef ENABLE_940T
  mkstate_data(gzf, z80_bank, sizeof(Uint16) * 4, mode);
  cpu_z80_mkstate(gzf, mode);
  ym2610_mkstate(gzf, mode);
#else
  /* TODO */
#endif
}

size_t state_serialize_size(void)
{
  size_t size;

  state_mem_begin(NULL, 0);

  neogeo_mkstate(NULL, STWRITE);

  size = state_mem_offset;

  state_mem_end();

  return size;
}

int state_unserialize(const void *data, size_t size)
{
  Uint8 *ng_lo;
  Uint8 *fix_game_usage;
  Uint8 *bksw_unscramble;
  int *bksw_offset;
  unsigned char spr_cache[sizeof(memory.vid.spr_cache)];
  size_t required;
  int success;

  required = state_serialize_size();

  if(data == NULL || size < required) {
    return GN_FALSE;
  }

  /*
   * These pointers and the sprite cache belong to the current process.
   * They must not be restored from a save state created by another
   * process instance.
   */
  ng_lo = memory.ng_lo;
  fix_game_usage = memory.fix_game_usage;
  bksw_unscramble = memory.bksw_unscramble;
  bksw_offset = memory.bksw_offset;

  memcpy(spr_cache, &memory.vid.spr_cache, sizeof(spr_cache));

  state_version = ST_VER3;

  state_mem_begin((void *)data, size);
  neogeo_mkstate(NULL, STREAD);
  success = !state_mem_error;
  state_mem_end();

  memory.ng_lo = ng_lo;
  memory.fix_game_usage = fix_game_usage;
  memory.bksw_unscramble = bksw_unscramble;
  memory.bksw_offset = bksw_offset;
  memcpy(&memory.vid.spr_cache, spr_cache, sizeof(spr_cache));

  if(!success) {
    return GN_FALSE;
  }

  cpu_68k_bankswitch(bankaddress);

  if(memory.current_vector == 0) {
    memcpy(memory.rom.cpu_m68k.p, memory.rom.bios_m68k.p, 0x80);
  }
  else {
    memcpy(memory.rom.cpu_m68k.p, memory.game_vector, 0x80);
  }

  if(memory.vid.currentpal) {
    current_pal = memory.vid.pal_neo[1];
    current_pc_pal = (Uint32 *)memory.vid.pal_host[1];
  }
  else {
    current_pal = memory.vid.pal_neo[0];
    current_pc_pal = (Uint32 *)memory.vid.pal_host[0];
  }

  if(memory.vid.currentfix) {
    current_fix = memory.rom.game_sfix.p;
    fix_usage = memory.fix_game_usage;
  }
  else {
    current_fix = memory.rom.bios_sfix.p;
    fix_usage = memory.fix_board_usage;
  }

  return GN_TRUE;
}

int state_serialize(void *data, size_t size)
{
  size_t required;

  if(data == NULL) {
    return GN_FALSE;
  }

  required = state_serialize_size();

  if(size < required) {
    return GN_FALSE;
  }

  state_mem_begin(data, size);

  neogeo_mkstate(NULL, STWRITE);

  int success = !state_mem_error;

  state_mem_end();

  return success ? GN_TRUE : GN_FALSE;
}

int save_state(char *game, int slot)
{
  gzFile gzf;

  if((gzf = open_state(game, slot, STWRITE)) == NULL) {
    return GN_FALSE;
  }

  gzwrite(gzf, state_img->pixels, 304 * 224 * 2);

  neogeo_mkstate(gzf, STWRITE);

  gzclose(gzf);
  return GN_TRUE;
}
int load_state(char *game, int slot)
{
  gzFile gzf;
  /* Save pointers */
Uint8 *ng_lo = memory.ng_lo;
Uint8 *fix_game_usage = memory.fix_game_usage;
Uint8 *bksw_unscramble = memory.bksw_unscramble;
int *bksw_offset = memory.bksw_offset;
//	GAME_ROMS r;
//	memcpy(&r,&memory.rom,sizeof(GAME_ROMS));

  if((gzf = open_state(game, slot, STREAD)) == NULL) {
    return GN_FALSE;
  }

  if(state_version == ST_VER2) {
    gn_popup_error("Warning!", "You're trying to load an older gngeo save state\nIt may work or not, nobody knows! ;)");
  }


  gzread(gzf, state_img_tmp->pixels, 304 * 224 * 2);

  neogeo_mkstate(gzf, STREAD);

  /* Restore pointers */
memory.ng_lo = ng_lo;
memory.fix_game_usage = fix_game_usage;
memory.bksw_unscramble = bksw_unscramble;
memory.bksw_offset = bksw_offset;
//	memcpy(&memory.rom,&r,sizeof(GAME_ROMS));

cpu_68k_bankswitch(bankaddress);

if(memory.current_vector == 0) {
  memcpy(memory.rom.cpu_m68k.p, memory.rom.bios_m68k.p, 0x80);
}
else {
  memcpy(memory.rom.cpu_m68k.p, memory.game_vector, 0x80);
}

if(memory.vid.currentpal) {
  current_pal = memory.vid.pal_neo[1];
  current_pc_pal = (Uint32 *)memory.vid.pal_host[1];
}
else {
  current_pal = memory.vid.pal_neo[0];
  current_pc_pal = (Uint32 *)memory.vid.pal_host[0];
}

if(memory.vid.currentfix) {
  current_fix = memory.rom.game_sfix.p;
  fix_usage = memory.fix_game_usage;
}
else {
  current_fix = memory.rom.bios_sfix.p;
  fix_usage = memory.fix_board_usage;
}

gzclose(gzf);

return GN_TRUE;
}


#if 0
/* neogeo state register */
static Uint8 st_current_pal, st_current_fix;

static void neogeo_pre_save_state(void)
{

  //st_current_pal=(current_pal==memory.pal1?0:1);
  //st_current_fix=(current_fix==memory.rom.bios_sfix.p?0:1);
  //printf("%d %d\n",st_current_pal,st_current_fix);

}

static void neogeo_post_load_state(void)
{
  int i;
  //printf("%d %d\n",st_current_pal,st_current_fix);
  //current_pal=(st_current_pal==0?memory.pal1:memory.pal2);
  //current_pc_pal=(Uint32 *)(st_current_pal==0?memory.pal_pc1:memory.pal_pc2);
  current_fix = (st_current_fix == 0 ? memory.rom.bios_sfix.p : memory.rom.game_sfix.p);
  update_all_pal();

}

void clear_state_reg(void)
{
  int i;
  ST_REG *t, *s;
  for(i = 0; i < ST_MODULE_END; i++) {
    t = st_mod[i].reglist;
    while(t) {
      s = t;
      t = t->next;
      free(s);
    }
    st_mod[i].reglist = NULL;
  }
}
#endif
void neogeo_init_save_state(void)
{
  int i;

  if(!state_img) {
    state_img = SDL_CreateRGBSurface(SDL_SWSURFACE, 304, 224, 16, 0xF800, 0x7E0, 0x1F, 0);
  }
  if(!state_img_tmp) {
    state_img_tmp = SDL_CreateRGBSurface(SDL_SWSURFACE, 304, 224, 16, 0xF800, 0x7E0, 0x1F, 0);
  }


}





