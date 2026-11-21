/*
 * pico9918-core - V9938 base: sprite mode 2
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Sprites are on the 256-pixel grid in every mode. What one sprite pixel writes:
 *
 *   mode    line byte
 *   G3, G4  the colour code
 *   G5      (c >> 2) << 4 | (c & 3), one hi-res pixel each
 *   G6      the colour code in both nibbles
 *   G7      the fixed sprite colour, GRB332
 */

#include "impl/pico9918_priv.h"

#if PICO9918_V9938_BASE

#define V9938_R8_TP     0x20
#define V9938_SPR_END   216
#define V9938_SPR_LINE  8
#define V9938_SPR_EC    0x80
#define V9938_SPR_CC    0x40
#define V9938_SPR_IC    0x20

static const uint8_t g7SpriteColor[16] = {
  0x00, 0x01, 0x0C, 0x0D, 0x60, 0x61, 0x6C, 0x6D, 0x9D, 0x03, 0x1C, 0x1F, 0xE0, 0xE3, 0xFC, 0xFF,
};

static inline uint8_t sprRead(const uint8_t* vram, uint32_t a, uint32_t planar)
{
  return vram[((a << (planar << 4)) | (a >> planar)) & 0x1FFFF];
}

static inline uint32_t magnify(uint32_t v)
{
  v = (v | (v << 8)) & 0x00FF00FF;
  v = (v | (v << 4)) & 0x0F0F0F0F;
  v = (v | (v << 2)) & 0x33333333;
  v = (v | (v << 1)) & 0x55555555;
  return v * 3;
}

static inline bool sprCollides(uint8_t attr, bool tp)
{
  return !(attr & (V9938_SPR_CC | V9938_SPR_IC)) && (tp || (attr & 0x0F));
}

uint8_t __time_critical_func(pico9918_v9938_sprites)(PICO9918_INST_ARG uint32_t y, uint8_t* pixels)
{
  if (TMS_REGISTER(tms9918, 8) & PICO9918_V9938_R8_SPD) return 0;

  const uint8_t* const vram = tms9918->vram.bytes;
  const uint32_t planar     = (TMS_REGISTER(tms9918, TMS_REG_0) & 0x0A) == 0x0A;
  const bool tp             = TMS_REGISTER(tms9918, 8) & V9938_R8_TP;
  const uint8_t r1          = TMS_REGISTER(tms9918, TMS_REG_1);
  const bool size16         = r1 & TMS_R1_SPRITE_16;
  const uint32_t mag        = r1 & TMS_R1_SPRITE_MAG2;
  const uint32_t height     = (size16 ? 16u : 8u) << mag;
  const uint32_t idxMask    = size16 ? 0xFC : 0xFF;
  const uint32_t wideMask   = size16 ? 0xFFFF0000u : 0xFF000000u;
  const uint32_t base       = ((uint32_t)TMS_REGISTER(tms9918, 11) << 15) |
                        ((uint32_t)TMS_REGISTER(tms9918, TMS_REG_SPRITE_ATTR_TABLE) << 7) | 0x7F;
  const uint32_t hi         = base & ~0x3FFu;
  const uint32_t lo         = base & 0x3FF;
  const uint32_t patterns   = (uint32_t)(TMS_REGISTER(tms9918, TMS_REG_SPRITE_PATT_TABLE) & 0x3F) << 11;
  const uint32_t line       = (y + TMS_REGISTER(tms9918, PICO9918_V9938_REG_VSCROLL) - 1) & 0xFF;

  uint32_t pat[V9938_SPR_LINE];
  int16_t xs[V9938_SPR_LINE];
  uint8_t attr[V9938_SPR_LINE];
  int count      = 0;
  int first      = V9938_SPR_LINE;
  uint8_t status = 0;
  uint32_t n     = 0;

  for (; n < 32; ++n)
  {
    const uint32_t a = hi | (lo & (0x200 | (n << 2)));
    const uint32_t sy = sprRead(vram, a, planar);
    if (sy == V9938_SPR_END) break;

    uint32_t row = (line - sy) & 0xFF;
    if (row >= height) continue;

    if (count == V9938_SPR_LINE)
    {
      status = (uint8_t)(PICO9918_SR0_5S | n);
      break;
    }

    row >>= mag;
    const uint8_t ca  = sprRead(vram, hi | (lo & ((n << 4) | row)), planar);
    const uint32_t pa = patterns | ((sprRead(vram, a + 2, planar) & idxMask) << 3) | row;
    uint32_t p        = ((uint32_t)sprRead(vram, pa, planar) << 24) | ((uint32_t)sprRead(vram, pa + 16, planar) << 16);
    p &= wideMask;
    p = (magnify(p >> 16) & -mag) | (p & (mag - 1));

    pat[count]  = p;
    xs[count]   = (int16_t)(sprRead(vram, a + 1, planar) - ((ca & V9938_SPR_EC) ? 32 : 0));
    attr[count] = ca;
    if (!(ca & V9938_SPR_CC) && first > count) first = count;
    ++count;
  }
  if (!status) status = (uint8_t)(n > 31 ? 31 : n);

  if (count > 1 && !(TMS_STATUS(tms9918, PICO9918_SR_STATUS) & PICO9918_SR0_COLLISION))
  {
    int32_t minX = 256;
    for (int i = 1; i < count; ++i)
    {
      if (!sprCollides(attr[i], tp)) continue;
      for (int j = 0; j < i; ++j)
      {
        if (!sprCollides(attr[j], tp)) continue;
        const int32_t dist = xs[j] - xs[i];
        if (dist <= -32 || dist >= 32) continue;
        uint32_t both = pat[i] & (dist < 0 ? pat[j] << -dist : pat[j] >> dist);
        if (xs[i] < 0) both &= (1u << (32 + xs[i])) - 1;
        if (!both) continue;
        int32_t cx = xs[i];
        for (; !(both & 0x80000000u); both <<= 1) ++cx;
        if (cx < minX) minX = cx;
      }
    }
    if (minX < 256)
    {
      status |= PICO9918_SR0_COLLISION;
      tms9918->v9938.collisionX = (uint16_t)(minX + 12);
      tms9918->v9938.collisionY = (uint16_t)(y + 7);
    }
  }

  if (first >= count) return status;

  const pico9918_mode_t mode = pico9918_cached_mode;
  uint8_t local[16];
  const uint8_t* lut = g7SpriteColor;
  if (mode != TMS_MODE_V9938_G7)
  {
    for (uint32_t c = 0; c < 16; ++c)
      local[c] = (uint8_t)((mode == TMS_MODE_V9938_G5) ? (((c >> 2) << 4) | (c & 3))
                                                       : (mode == TMS_MODE_V9938_G6) ? c * 0x11 : c);
    lut = local;
  }

  uint32_t cover[9] = {0};
  for (int i = first; i < count; ++i)
  {
    const uint8_t c = attr[i] & 0x0F;
    if (!c && !tp) continue;

    int32_t x  = xs[i];
    uint32_t p = pat[i];
    uint32_t m0 = 0, m1 = 0, m2 = 0, m3 = 0;
#if defined(__GNUC__)
#pragma GCC unroll 1
#endif
    for (int j = i + 1; j < count && (attr[j] & V9938_SPR_CC); ++j)
    {
      const int32_t d = xs[j] - x;
      if (d <= -32 || d >= 32) continue;
      const uint32_t q  = (d >= 0) ? pat[j] >> d : pat[j] << -d;
      const uint32_t cj = attr[j];
      m0 |= q & -(cj & 1);
      m1 |= q & -((cj >> 1) & 1);
      m2 |= q & -((cj >> 2) & 1);
      m3 |= q & -((cj >> 3) & 1);
    }

    if (x < 0)
    {
      if (x <= -32) continue;
      p <<= -x;
      m0 <<= -x;
      m1 <<= -x;
      m2 <<= -x;
      m3 <<= -x;
      x = 0;
    }

    if (x >= TMS9918_PIXELS_X) continue;
    const uint32_t w = (uint32_t)x >> 5, s = (uint32_t)x & 31;
    p &= ~((cover[w] << s) | (s ? cover[w + 1] >> (32 - s) : 0));
    if (!p) continue;
    cover[w] |= p >> s;
    if (s) cover[w + 1] |= p << (32 - s);

    const bool cc = (m0 | m1 | m2 | m3) != 0;
    while (p && x < TMS9918_PIXELS_X)
    {
      if (!(p >> 24))
      {
        p <<= 8;
        m0 <<= 8;
        m1 <<= 8;
        m2 <<= 8;
        m3 <<= 8;
        x += 8;
        continue;
      }
      if (p & 0x80000000u)
        pixels[x] = cc ? lut[c | (m0 >> 31) | ((m1 >> 31) << 1) | ((m2 >> 31) << 2) | ((m3 >> 31) << 3)] : lut[c];
      p <<= 1;
      m0 <<= 1;
      m1 <<= 1;
      m2 <<= 1;
      m3 <<= 1;
      ++x;
    }
  }

  return status;
}

#endif
