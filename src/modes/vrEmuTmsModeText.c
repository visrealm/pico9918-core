/*
 * Troy's TMS9918 Emulator - Text and Text80 Mode Scanline Renderers
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/vrEmuTms9918
 *
 * Included directly into vrEmuTms9918.c — not a separate compilation unit.
 * All static helpers from vrEmuTms9918.c are available here.
 */

/* Function:  vrEmuTms9918TextScanLine
 * ----------------------------------------
 * generate a Text mode scanline (pure tile renderer — overlays handled by pipeline stages)
 */
static uint8_t __time_critical_func(vrEmuTms9918TextScanLine)(VR_EMU_INST_ONLY_ARG)
{
  const uint16_t y = tms9918->scanCtx.y;
  uint8_t* pixels = tms9918->scanCtx.pixels;

  const uint8_t tileY = y >> 3;   /* which name table row (0 - 23) */
  const uint8_t pattRow = y & 0x07;  /* which pattern row (0 - 7) */

  /* address in name table at the start of this row */
  uint8_t* rowNamesTable = tms9918->vram.bytes + tmsNameTableAddr(tms9918) + tileY * TEXT_NUM_COLS;
  const uint8_t* patternTable = tms9918->vram.bytes + tmsPatternTableAddr(tms9918) + pattRow;

  const uint32_t bgFgColor[2] = {
    tmsMainBgColor(tms9918),
    tmsMainFgColor(tms9918)
  };

  uint8_t* pixPtr = pixels;

  pixPtr += TEXT_PADDING_PX;

  VR_TMS_FILL32_WAIT();

  for (uint8_t tileX = 0; tileX < TEXT_NUM_COLS; ++tileX)
  {
    const uint8_t pattByte = patternTable[*rowNamesTable++ * PATTERN_BYTES];

    for (uint8_t pattBit = 7; pattBit > 1; --pattBit)
    {
      *pixPtr++ = bgFgColor[(pattByte >> pattBit) & 0x01];
    }
  }
  return 0;
}

static const uint8_t __aligned(4) maskText80Fg[] = { 0x00, 0x0f, 0xf0, 0xff };
static const uint8_t __aligned(4) maskText80Dual[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };

#define TEXT80_COLOR_WORD(n) ((uint32_t)((n) * 0x111111u))
#define TEXT80_MASK_WORD(bits) ( \
  ((uint32_t)((bits) & 0x20 ? 0x0000F0u : 0x0)) | \
  ((uint32_t)((bits) & 0x10 ? 0x00000Fu : 0x0)) | \
  ((uint32_t)((bits) & 0x08 ? 0x00F000u : 0x0)) | \
  ((uint32_t)((bits) & 0x04 ? 0x000F00u : 0x0)) | \
  ((uint32_t)((bits) & 0x02 ? 0xF00000u : 0x0)) | \
  ((uint32_t)((bits) & 0x01 ? 0x0F0000u : 0x0)))

static const uint32_t text80ColorWord[16] = {
  TEXT80_COLOR_WORD(0x0), TEXT80_COLOR_WORD(0x1), TEXT80_COLOR_WORD(0x2), TEXT80_COLOR_WORD(0x3),
  TEXT80_COLOR_WORD(0x4), TEXT80_COLOR_WORD(0x5), TEXT80_COLOR_WORD(0x6), TEXT80_COLOR_WORD(0x7),
  TEXT80_COLOR_WORD(0x8), TEXT80_COLOR_WORD(0x9), TEXT80_COLOR_WORD(0xa), TEXT80_COLOR_WORD(0xb),
  TEXT80_COLOR_WORD(0xc), TEXT80_COLOR_WORD(0xd), TEXT80_COLOR_WORD(0xe), TEXT80_COLOR_WORD(0xf)
};

static const uint32_t text80MaskWord[64] = {
  TEXT80_MASK_WORD(0x00), TEXT80_MASK_WORD(0x01), TEXT80_MASK_WORD(0x02), TEXT80_MASK_WORD(0x03),
  TEXT80_MASK_WORD(0x04), TEXT80_MASK_WORD(0x05), TEXT80_MASK_WORD(0x06), TEXT80_MASK_WORD(0x07),
  TEXT80_MASK_WORD(0x08), TEXT80_MASK_WORD(0x09), TEXT80_MASK_WORD(0x0a), TEXT80_MASK_WORD(0x0b),
  TEXT80_MASK_WORD(0x0c), TEXT80_MASK_WORD(0x0d), TEXT80_MASK_WORD(0x0e), TEXT80_MASK_WORD(0x0f),
  TEXT80_MASK_WORD(0x10), TEXT80_MASK_WORD(0x11), TEXT80_MASK_WORD(0x12), TEXT80_MASK_WORD(0x13),
  TEXT80_MASK_WORD(0x14), TEXT80_MASK_WORD(0x15), TEXT80_MASK_WORD(0x16), TEXT80_MASK_WORD(0x17),
  TEXT80_MASK_WORD(0x18), TEXT80_MASK_WORD(0x19), TEXT80_MASK_WORD(0x1a), TEXT80_MASK_WORD(0x1b),
  TEXT80_MASK_WORD(0x1c), TEXT80_MASK_WORD(0x1d), TEXT80_MASK_WORD(0x1e), TEXT80_MASK_WORD(0x1f),
  TEXT80_MASK_WORD(0x20), TEXT80_MASK_WORD(0x21), TEXT80_MASK_WORD(0x22), TEXT80_MASK_WORD(0x23),
  TEXT80_MASK_WORD(0x24), TEXT80_MASK_WORD(0x25), TEXT80_MASK_WORD(0x26), TEXT80_MASK_WORD(0x27),
  TEXT80_MASK_WORD(0x28), TEXT80_MASK_WORD(0x29), TEXT80_MASK_WORD(0x2a), TEXT80_MASK_WORD(0x2b),
  TEXT80_MASK_WORD(0x2c), TEXT80_MASK_WORD(0x2d), TEXT80_MASK_WORD(0x2e), TEXT80_MASK_WORD(0x2f),
  TEXT80_MASK_WORD(0x30), TEXT80_MASK_WORD(0x31), TEXT80_MASK_WORD(0x32), TEXT80_MASK_WORD(0x33),
  TEXT80_MASK_WORD(0x34), TEXT80_MASK_WORD(0x35), TEXT80_MASK_WORD(0x36), TEXT80_MASK_WORD(0x37),
  TEXT80_MASK_WORD(0x38), TEXT80_MASK_WORD(0x39), TEXT80_MASK_WORD(0x3a), TEXT80_MASK_WORD(0x3b),
  TEXT80_MASK_WORD(0x3c), TEXT80_MASK_WORD(0x3d), TEXT80_MASK_WORD(0x3e), TEXT80_MASK_WORD(0x3f)
};


static void renderText80Layer(
  uint8_t y, const uint8_t tileY,
  uint8_t *__restrict rowNamesTable,
  const uint8_t *__restrict patternTable,
  uint8_t *__restrict colorTable,
  const bool opaq,
  uint8_t *__restrict pixels)
{
  const uint8_t bgc = tmsMainBgColor(tms9918);
  const uint32_t* colorTable32 = (const uint32_t*)__builtin_assume_aligned(colorTable, 4);
  uint32_t* pix32 = (uint32_t*)__builtin_assume_aligned(pixels, 4);

  if (opaq)
  {
    uint8_t lastColor = 0;
    uint32_t bgColorMask = text80ColorWord[bgc];
    uint32_t fgColorMask = text80ColorWord[bgc];

    for (uint8_t tileX = 0; tileX < TEXT80_NUM_COLS; tileX += 4)
    {
      uint32_t colorWord = *colorTable32++;

      uint32_t words[4];
      for (int i = 0; i < 4; ++i)
      {
        const uint8_t color = (uint8_t)colorWord;
        colorWord >>= 8;

        if (color != lastColor)
        {
          uint8_t bgColor = color & 0xf;
          bgColorMask = text80ColorWord[bgColor ? bgColor : bgc];
          uint8_t fgColor = color >> 4;
          fgColorMask = text80ColorWord[fgColor ? fgColor : bgc];
          lastColor = color;
        }

        const uint32_t mask = text80MaskWord[patternTable[*rowNamesTable++ * PATTERN_BYTES] >> 2];
        words[i] = (fgColorMask & mask) | (bgColorMask & ~mask);
      }

      *pix32++ = words[0] | (words[1] << 24);
      *pix32++ = (words[1] >> 8) | (words[2] << 16);
      *pix32++ = (words[2] >> 16) | (words[3] << 8);
    }
  }
  else
  {
    for (uint8_t tileX = 0; tileX < TEXT80_NUM_COLS; tileX += 4)
    {
      uint32_t colorWord = *colorTable32++;
      uint32_t masks[4];
      uint32_t vals[4];

      for (int i = 0; i < 4; ++i)
      {
        const uint32_t mask = text80MaskWord[patternTable[*rowNamesTable++ * PATTERN_BYTES] >> 2];

        const uint8_t colorByte = (uint8_t)colorWord;
        colorWord >>= 8;

        const uint32_t fgWord = text80ColorWord[colorByte >> 4];
        const uint32_t bgWord = text80ColorWord[colorByte & 0xf];

        const uint32_t fgMask = fgWord ? mask : 0;
        const uint32_t bgMask = bgWord ? ~mask : 0;

        masks[i] = fgMask | bgMask;
        vals[i] = (fgWord & fgMask) | (bgWord & bgMask);
      }

      const uint32_t mask0 = masks[0] | (masks[1] << 24);
      const uint32_t mask1 = (masks[1] >> 8) | (masks[2] << 16);
      const uint32_t mask2 = (masks[2] >> 16) | (masks[3] << 8);

      const uint32_t val0 = vals[0] | (vals[1] << 24);
      const uint32_t val1 = (vals[1] >> 8) | (vals[2] << 16);
      const uint32_t val2 = (vals[2] >> 16) | (vals[3] << 8);

      *pix32++ = (*pix32 & ~mask0) | (val0 & mask0);
      *pix32++ = (*pix32 & ~mask1) | (val1 & mask1);
      *pix32++ = (*pix32 & ~mask2) | (val2 & mask2);
    }
  }
}


/* Function:  vrEmuTms9918Text80ScanLine
 * ----------------------------------------
 * generate an 80-column text mode scanline (pure tile renderer — overlays handled by pipeline stages)
 */
static uint8_t __time_critical_func(vrEmuTms9918Text80ScanLine)(VR_EMU_INST_ONLY_ARG)
{
  uint8_t* pixels = tms9918->scanCtx.pixels;
  uint16_t y = tms9918->scanCtx.y1;

  uint8_t tileY = y >> 3;   /* which name table row (0 - 23... or 29) */
  uint8_t pattRow = y & 0x07;  /* which pattern row (0 - 7) */

  uint8_t nameTableMask = tms9918->isUnlocked ? 0x0f : 0x0c;

  /* address in name table at the start of this row */
  uint32_t rowNamesAddr = (tmsNameTableAddr(tms9918) & (nameTableMask << 10)) + tileY * TEXT80_NUM_COLS;

  uint8_t* patternTable = tms9918->vram.bytes + tmsPatternTableAddr(tms9918) + pattRow;

  const vrEmuTms9918Color bgColor = tmsMainBgColor(tms9918);

  pixels += TEXT_PADDING_PX;

  VR_TMS_FILL32_WAIT();

  if (TMS_REGISTER(tms9918, 0x32) & 0x02)  // position-based attributes
  {
    uint16_t colorTableAddr = (tmsColorTableAddr(tms9918)) + tileY * TEXT80_NUM_COLS;

    const bool tilesDisabled = TMS_REGISTER(tms9918, 0x32) & 0x10;
    if (!tilesDisabled) renderText80Layer(y, tileY, tms9918->vram.bytes + rowNamesAddr, patternTable, tms9918->vram.bytes + colorTableAddr, true, pixels);

    if (TMS_REGISTER(tms9918, 0x31) & 0x80)
    {
      uint16_t y2 = tms9918->scanCtx.y2;

      uint8_t tileY = y2 >> 3;   /* which name table row (0 - 23... or 29) */
      uint8_t pattRow = y2 & 0x07;  /* which pattern row (0 - 7) */

      uint8_t* patternTable = tms9918->vram.bytes + tmsPatternTableAddr(tms9918) + pattRow;

      rowNamesAddr = (tmsNameTable2Addr(tms9918) & (nameTableMask << 10)) + tileY * TEXT80_NUM_COLS;
      colorTableAddr = (tmsColorTable2Addr(tms9918) ) + tileY * TEXT80_NUM_COLS;

      renderText80Layer(y2, tileY, tms9918->vram.bytes + rowNamesAddr, patternTable, tms9918->vram.bytes + colorTableAddr, false, pixels);
    }
  }
  else  // just plain old two-tone
  {
    const vrEmuTms9918Color fgColor = tmsMainFgColor(tms9918);

    const uint32_t bgFgColor[4] =
    {
      (bgColor << 4) | bgColor,
      (bgColor << 4) | fgColor,
      (fgColor << 4) | bgColor,
      (fgColor << 4) | fgColor
    };

    uint8_t *rowNamesTable = tms9918->vram.bytes + rowNamesAddr;

    for (uint8_t tileX = 0; tileX < TEXT80_NUM_COLS; ++tileX)
    {
      uint8_t pattByte = patternTable[*rowNamesTable++ * PATTERN_BYTES];
      for (uint8_t pattBit = 6; pattBit > 1; pattBit -= 2)
      {
        *pixels++ = bgFgColor[(pattByte >> pattBit) & 0x03];
      }
    }
  }
  return 0;
}
