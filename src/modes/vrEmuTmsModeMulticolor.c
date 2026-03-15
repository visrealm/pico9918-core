/*
 * Troy's TMS9918 Emulator - Multicolor Mode Scanline Renderer
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

/* Function:  vrEmuTms9918MulticolorScanLine
 * ----------------------------------------
 * generate a Multicolor mode scanline (pure tile renderer — overlays handled by pipeline stages)
 */
static uint8_t __time_critical_func(vrEmuTms9918MulticolorScanLine)(VR_EMU_INST_ONLY_ARG)
{
  const uint16_t y = tms9918->scanCtx.y;
  uint8_t* pixels = tms9918->scanCtx.pixels;

  const uint8_t tileY = y >> 3;
  const uint8_t pattRow = ((y >> 2) & 0x01) + (tileY & 0x03) * 2;

  const uint8_t* nameTable = tms9918->vram.bytes + tmsNameTableAddr(tms9918) + tileY * GRAPHICS_NUM_COLS;
  const uint8_t* patternTable = tms9918->vram.bytes + tmsPatternTableAddr(tms9918) + pattRow;

  uint32_t *quadPixels = (uint32_t *)pixels;

  VR_TMS_FILL32_WAIT();

  for (uint8_t tileX = 0; tileX < GRAPHICS_NUM_COLS; ++tileX)
  {
    const uint8_t colorByte = patternTable[nameTable[tileX] * PATTERN_BYTES];
    const uint32_t fgColor = repeatedPalette[tmsFgColor(tms9918, colorByte)];
    const uint32_t bgColor = repeatedPalette[tmsBgColor(tms9918, colorByte)];

    *quadPixels++ = fgColor;
    *quadPixels++ = bgColor;
  }
  return 0;
}
