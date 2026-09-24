/*
 *  Copyright (C) 2021 Steward Fu
 *  Copyright (C) 2001 Peponas Mathieu
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>

#include "emu.h"
#include "conf.h"
#include "font.h"
#include "video.h"
#include "gnutil.h"
#include "screen.h"

SDL_Surface *buffer = NULL;
SDL_Surface *screen = NULL;
SDL_Surface *fontbuf = NULL;
SDL_Rect visible_area = {0};

static SDL_Rect drt = {0, 8, 320, 224};
static SDL_Rect left_border = {16, 16, 8, 224};
static SDL_Rect right_border = {16 + 312, 16, 8, 224};

void screen_flip(void)
{
#ifdef LIBRETRO
  return;
#else
  SDL_FillRect(buffer, &left_border, 0);
  SDL_FillRect(buffer, &right_border, 0);
  SDL_BlitSurface(buffer, &visible_area, screen, &drt);
  SDL_Flip(screen);
#endif
}

void sdl_init(void)
{
  SDL_Init(SDL_INIT_VIDEO);
  atexit(SDL_Quit);

  visible_area.x = 16;
  visible_area.y = 16;
  visible_area.w = 320;
  visible_area.h = 224;

  screen = SDL_SetVideoMode(320, 240, 16, SDL_SWSURFACE | SDL_DOUBLEBUF);
  SDL_ShowCursor(SDL_DISABLE);

  buffer = SDL_CreateRGBSurface(SDL_SWSURFACE, 352, 256, 16, 0xf800, 0x7e0, 0x1f, 0);
  SDL_FillRect(buffer, NULL, SDL_MapRGB(buffer->format, 0xe5, 0xe5, 0xe5));

  fontbuf = SDL_CreateRGBSurfaceFrom(font_image.pixel_data, font_image.width, font_image.height, 24, font_image.width * 3, 0xff0000, 0xff00, 0xff, 0);
  //SDL_SetColorKey(fontbuf, SDL_SRCCOLORKEY, SDL_MapRGB(fontbuf->format, 0xff, 0, 0xff));
  fontbuf = SDL_DisplayFormat(fontbuf);
}

#ifdef LIBRETRO
int screen_init_libretro(void)
{
  visible_area.x = 16;
  visible_area.y = 16;
  visible_area.w = 320;
  visible_area.h = 224;

  if(buffer != NULL) {
    return 1;
  }

  buffer = SDL_CreateRGBSurface(
    SDL_SWSURFACE,
    352,
    256,
    16,
    0xf800,
    0x7e0,
    0x1f,
    0
  );

  if(buffer == NULL) {
    printf("[GnGeo] failed to create video buffer: %s\n", SDL_GetError());
    return 0;
  }

  SDL_FillRect(buffer, NULL, 0);

  return 1;
}

void screen_deinit_libretro(void)
{
  if(buffer != NULL) {
    SDL_FreeSurface(buffer);
    buffer = NULL;
  }
}

void screen_copy_libretro(void *pixels, int pitch)
{
  int y;

  uint8_t *src =
    (uint8_t *)buffer->pixels +
    visible_area.y * buffer->pitch +
    visible_area.x * sizeof(uint16_t);

  uint8_t *dst =
    (uint8_t *)pixels +
    8 * pitch;

  for(y = 0; y < visible_area.h; y++) {
    memcpy(
      dst + y * pitch,
      src + y * buffer->pitch,
      visible_area.w * sizeof(uint16_t)
    );
  }
}
#endif

