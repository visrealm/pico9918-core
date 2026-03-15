/*
 * Troy's TMS9918 Emulator - Graphics II Mode Scanline Renderer
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

/* Function:  vrEmuTms9918GraphicsIIScanLine
 * ----------------------------------------
 * generate a Graphics II mode scanline (pure tile renderer — overlays handled by pipeline stages)
 */
static __attribute__((noinline)) uint8_t __time_critical_func(vrEmuTms9918GraphicsIIScanLine)(VR_EMU_INST_ARG uint16_t y, uint8_t pixels[TMS9918_PIXELS_X])
{
  const uint8_t tileY = y >> 3;   /* which name table row (0 - 23) */
  const uint8_t pattRow = y & 0x07;  /* which pattern row (0 - 7) */

  /* address in name table at the start of this row */
  const uint16_t rowNamesAddr = tmsNameTableAddr(tms9918) + tileY * GRAPHICS_NUM_COLS;

  /* the datasheet says the lower bits of the color and pattern tables must
     be all 1's for graphics II mode. however, the lowest 2 bits of the
     pattern address are used to determine if pages 2 & 3 come from page 0
     or not. Similarly, the lowest 6 bits of the color table register are
     used as an and mask with the nametable  index */
  const uint8_t nameMask = ((TMS_REGISTER(tms9918, TMS_REG_COLOR_TABLE) & 0x7f) << 3) | 0x07;

  const uint16_t pageThird = ((tileY & 0x18) >> 3)
    & (TMS_REGISTER(tms9918, TMS_REG_PATTERN_TABLE) & 0x03); /* which page? 0-2 */
  const uint16_t pageOffset = pageThird << 11; /* offset (0, 0x800 or 0x1000) */

  const uint8_t* patternTable = tms9918->vram.bytes + tmsPatternTableAddr(tms9918) + pageOffset + pattRow;
  const uint8_t* colorTable = tms9918->vram.bytes + tmsColorTableAddr(tms9918) + (pageOffset
    & ((TMS_REGISTER(tms9918, TMS_REG_COLOR_TABLE) & 0x60) << 6)) + pattRow;

  uint8_t palette = (TMS_REGISTER(tms9918, 0x18) & 0x03) << 4;

  VR_TMS_FILL32_WAIT();

  uint32_t* quadPixels = (uint32_t*)__builtin_assume_aligned(pixels, 4);

  /* iterate over each tile in this row */
  for (uint8_t tileX = 0; tileX < GRAPHICS_NUM_COLS; ++tileX)
  {
    uint8_t pattIdx = tms9918->vram.bytes[rowNamesAddr + tileX] & nameMask;

    const size_t pattRowOffset = pattIdx * PATTERN_BYTES;
    const uint8_t patt = patternTable[pattRowOffset];
    const uint8_t colorByte = colorTable[pattRowOffset];

    // apply F18A palette. TODO put behind unlocked
    uint8_t bgColor = colorByte & 0x0f;
    uint8_t fgColor = colorByte >> 4;

    if (bgColor) {bgColor |= palette;} else {bgColor = tmsMainBgColor(tms9918);}
    if (fgColor) {fgColor |= palette;} else {fgColor = tmsMainBgColor(tms9918);}

    const uint32_t bgPalette = repeatedPalette[bgColor];
    const uint32_t fgPalette = repeatedPalette[fgColor];

    const uint32_t leftMask = maskExpandNibbleToWordRev[patt >> 4];
    const uint32_t rightMask = maskExpandNibbleToWordRev[patt & 0x0f];

    quadPixels[0] = (fgPalette & leftMask) | (bgPalette & ~leftMask);
    quadPixels[1] = (fgPalette & rightMask) | (bgPalette & ~rightMask);
    quadPixels += 2;
  }
  return 0;
}
