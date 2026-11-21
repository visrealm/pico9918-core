/**
 * \file
 * \brief pico9918-core - V9938 base: display modes
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * The bitmap modes, G4 to G7, onto the 256-byte line every other mode renders to:
 *
 *   mode  pixels      line byte                    LUT class
 *   G4    256 x 4bpp  one pixel, a palette index   DOUBLED
 *   G5    512 x 2bpp  two pixels, a nibble each    PAIRED
 *   G6    512 x 4bpp  two pixels, a nibble each    PAIRED
 *   G7    256 x 8bpp  one pixel, GRB332            DIRECT
 */

#include "impl/pico9918_priv.h"

#if PICO9918_V9938_BASE

pico9918_mode_t pico9918_v9938_mode(PICO9918_INST_ONLY_ARG)
{
  switch (PICO9918_V9938_MODE_BITS(tms9918))
  {
  case 0x01: return TMS_MODE_TEXT;
  case 0x02: return TMS_MODE_MULTICOLOR;
  case 0x04:
  case 0x08: return TMS_MODE_GRAPHICS_II;
  case 0x09: return TMS_MODE_TEXT80;
  case 0x0C: return TMS_MODE_V9938_G4;
  case 0x10: return TMS_MODE_V9938_G5;
  case 0x14: return TMS_MODE_V9938_G6;
  case 0x1C: return TMS_MODE_V9938_G7;
  default: return TMS_MODE_GRAPHICS_I;
  }
}

int pico9918_v9938_lines(PICO9918_INST_ONLY_ARG)
{
  return (TMS_REGISTER(tms9918, PICO9918_V9938_REG_MODE3) & PICO9918_V9938_R9_LN) ? 212 : 192;
}

static inline uint32_t lineAddr(PICO9918_INST_ARG uint32_t y, uint32_t shift)
{
  const uint32_t vy   = (y + TMS_REGISTER(tms9918, PICO9918_V9938_REG_VSCROLL)) & 0xFF;
  const uint32_t mask = ((uint32_t)TMS_REGISTER(tms9918, TMS_REG_NAME_TABLE) << 3) | 7;
  const uint32_t odd =
    (TMS_REGISTER(tms9918, PICO9918_V9938_REG_MODE3) & PICO9918_V9938_R9_EO) ? tms9918->v9938.lineField : 1;
  const uint32_t page = (odd & !tms9918->v9938.blinkOn) << 8;
  return (mask & (((shift == 8) ? 0 : 0x200) | page | vy)) << shift;
}

static void __time_critical_func(lineG4)(PICO9918_INST_ARG uint32_t y, uint8_t* pixels, uint8_t bg)
{
  const uint8_t* src = tms9918->vram.bytes + lineAddr(PICO9918_INST y, 7);
  for (int i = 0; i < 128; ++i)
  {
    const uint8_t b = src[i];
    const uint8_t l = b >> 4, r = b & 0x0F;
    pixels[2 * i]     = l ? l : bg;
    pixels[2 * i + 1] = r ? r : bg;
  }
}

static void __time_critical_func(lineG5)(PICO9918_INST_ARG uint32_t y, uint8_t* pixels, uint8_t bg)
{
  uint8_t pair[16];
  for (int n = 0; n < 16; ++n)
  {
    const uint8_t l = n >> 2, r = n & 3;
    pair[n] = (uint8_t)(((l ? l : (bg >> 2) & 3) << 4) | (r ? r : bg & 3));
  }

  const uint8_t* src = tms9918->vram.bytes + lineAddr(PICO9918_INST y, 7);
  for (int i = 0; i < 128; ++i)
  {
    const uint8_t b   = src[i];
    pixels[2 * i]     = pair[b >> 4];
    pixels[2 * i + 1] = pair[b & 0x0F];
  }
}

/* G6 and G7 are planar: the line's even bytes are in the first 64KB, its odd bytes in the second */
static void __time_critical_func(lineG6)(PICO9918_INST_ARG uint32_t y, uint8_t* pixels, uint8_t bg)
{
  const uint32_t lin  = lineAddr(PICO9918_INST y, 8);
  const uint8_t* even = tms9918->vram.bytes + (lin >> 1);
  const uint8_t* odd  = even + 0x10000;
  const uint8_t bgHi  = (uint8_t)(bg << 4);

  for (int i = 0; i < 128; ++i)
  {
    uint8_t b = even[i];
    if (!(b & 0xF0)) b |= bgHi;
    if (!(b & 0x0F)) b |= bg;
    pixels[2 * i] = b;
    b             = odd[i];
    if (!(b & 0xF0)) b |= bgHi;
    if (!(b & 0x0F)) b |= bg;
    pixels[2 * i + 1] = b;
  }
}

static inline uint16_t rgb12FromGrb332(uint8_t c)
{
  const uint16_t g = PICO9918_RGB333_CH(c >> 5), r = PICO9918_RGB333_CH((c >> 2) & 7);
  const uint16_t b = (uint16_t)((c & 3) * 5);
  return (uint16_t)(r | (((g << 4) | b) << 8));
}

static void __time_critical_func(lineG7)(PICO9918_INST_ARG uint32_t y, uint8_t* pixels, uint8_t bg)
{
  pico9918_palette_lut[0] = PICO9918_PIXEL_PAIR(PICO9918_PIXEL_FROM_RGB12(rgb12FromGrb332(bg)));

  const uint32_t lin  = lineAddr(PICO9918_INST y, 8);
  const uint8_t* even = tms9918->vram.bytes + (lin >> 1);
  const uint8_t* odd  = even + 0x10000;

  for (int i = 0; i < 128; ++i)
  {
    pixels[2 * i]     = even[i];
    pixels[2 * i + 1] = odd[i];
  }
}

void __time_critical_func(pico9918_v9938_bitmap_line)(PICO9918_INST_ARG uint32_t y, uint8_t* pixels)
{
  const pico9918_mode_t mode = pico9918_cached_mode;
  const bool transparent     = !(TMS_REGISTER(tms9918, PICO9918_V9938_REG_MODE2) & PICO9918_V9938_R8_TP);
  const uint8_t r7           = TMS_REGISTER(tms9918, TMS_REG_FG_BG_COLOR);

  switch (mode)
  {
  case TMS_MODE_V9938_G4: lineG4(PICO9918_INST y, pixels, transparent ? (r7 & 0x0F) : 0); break;
  case TMS_MODE_V9938_G5: lineG5(PICO9918_INST y, pixels, transparent ? (r7 & 0x0F) : 0); break;
  case TMS_MODE_V9938_G6: lineG6(PICO9918_INST y, pixels, transparent ? (r7 & 0x0F) : 0); break;
  default: lineG7(PICO9918_INST y, pixels, transparent ? r7 : 0); break;
  }
}

void __time_critical_func(pico9918_v9938_text2_blink)(PICO9918_INST_ARG uint32_t y, uint8_t* pixels)
{
  const uint8_t* const r    = &TMS_REGISTER(tms9918, 0);
  const uint8_t* const vram = tms9918->vram.bytes;
  const uint32_t sy         = (uint8_t)(y + r[PICO9918_V9938_REG_VSCROLL]);
  const uint32_t row        = sy >> 3;
  const uint8_t* names      = vram + ((uint32_t)(r[TMS_REG_NAME_TABLE] & 0x7C) << 10) + row * 80;
  const uint8_t* pattern    = vram + ((uint32_t)(r[TMS_REG_PATTERN_TABLE] & 0x3F) << 11) + (sy & 7);

  const uint32_t colors = (r[PICO9918_V9938_REG_COLOR_HI] << 14) | (r[TMS_REG_COLOR_TABLE] << 6) | 0x3Fu;
  const uint8_t r12     = r[PICO9918_V9938_REG_BLINK_COLOR];
  const uint8_t bg      = r12 & 0x0F;
  const uint8_t fg      = (r12 >> 4) ? (r12 >> 4) : bg;

  pixels += TEXT_PADDING_PX;
  for (uint32_t group = 0; group < 10; ++group)
  {
    const uint8_t bits = vram[colors & (~0x1FFu | (row * 10 + group))];
    for (uint32_t i = 0; i < 8; ++i)
    {
      if (!(bits & (0x80 >> i))) continue;
      const uint32_t c   = group * 8 + i;
      const uint8_t patt = pattern[names[c] * 8];
      uint8_t* dest      = pixels + c * 3;
      for (uint32_t b = 0; b < 3; ++b)
      {
        const uint8_t hi = (patt & (0x80 >> (2 * b))) ? fg : bg;
        const uint8_t lo = (patt & (0x40 >> (2 * b))) ? fg : bg;
        dest[b]          = (uint8_t)((hi << 4) | lo);
      }
    }
  }
}

void pico9918_v9938_direct_lut(void)
{
  for (int i = 0; i < 256; ++i)
    pico9918_palette_lut[i] = PICO9918_PIXEL_PAIR(PICO9918_PIXEL_FROM_RGB12(rgb12FromGrb332((uint8_t)i)));
}

uint32_t pico9918_v9938_border(PICO9918_INST_ONLY_ARG)
{
  const uint8_t r7 = TMS_REGISTER(tms9918, TMS_REG_FG_BG_COLOR);

  if (pico9918_cached_mode == TMS_MODE_V9938_G7)
    return PICO9918_PIXEL_PAIR(PICO9918_PIXEL_FROM_RGB12(rgb12FromGrb332(r7)));

  if (pico9918_cached_mode == TMS_MODE_V9938_G5)
    return (uint32_t)PICO9918_PIXEL_FROM_RGB12(PICO9918_PRAM(tms9918)[(r7 >> 2) & 3]) |
           ((uint32_t)PICO9918_PIXEL_FROM_RGB12(PICO9918_PRAM(tms9918)[r7 & 3]) << 16);

  return PICO9918_PIXEL_PAIR(PICO9918_PIXEL_FROM_RGB12(PICO9918_PRAM(tms9918)[r7 & 0x0F]));
}

#endif
