/*
 * Troy's TMS9918 Emulator - Graphics I Mode Scanline Renderer
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/vrEmuTms9918
 *
 * Includes ECM tile helpers, F18A tile layers, bitmap layer, and the
 * Graphics I scanline entry point.
 *
 * Included directly into vrEmuTms9918.c — not a separate compilation unit.
 * All static helpers from vrEmuTms9918.c are available here.
 */

/* Function:  renderEcmShiftedTile
 * ----------------------------------------
 * render the first shiny new ECM (enhanced color mode) graphics I tile in a scrolled scanline
 * this guy sets the stage for the remaining tiles (offset-wise). if the tile isn't scrolled
 * we end up just using renderEcmAlignedTile() instead
 *
 * quadPixels either incremented by 1 or 0, depending where it lands (how shifted it is)
 *
 * INLINE: so will be different versions generated, depending on hard-coded (or known at compile-time) arguments
 */
static inline uint32_t* renderEcmStartTile(
  uint32_t *quadPixels,
  const uint32_t tilePixels[2],
  const uint32_t pattMask,
  const uint32_t startPattBit,
  const uint32_t shift)
{
  const uint32_t rightMask = maskExpandNibbleToWordRev[pattMask & 0xf];

  // first tile will either take one or two nibbles depending on the shift
  if (startPattBit < 4)
  {
    const uint32_t leftMask = maskExpandNibbleToWordRev[pattMask >> 4];
    const uint32_t reverseShift = 32 - shift;
    const uint32_t mask = (leftMask >> shift) | (rightMask << reverseShift);
    const uint32_t shifted = mask & ((tilePixels[0] >> shift) | (tilePixels[1] << reverseShift));
    *quadPixels++ = (*quadPixels & ~mask) | shifted;
  }

  const uint32_t mask = (rightMask >> shift);
  const uint32_t shifted = mask & (tilePixels[1] >> shift);
  *quadPixels = (*quadPixels & ~mask) | shifted;

  if (startPattBit == 4) ++quadPixels;

  return quadPixels;
}

/* Function:  renderEcmShiftedTile
 * ----------------------------------------
 * render a shiny new ECM (enhanced color mode) graphics I tile which is NOT aligned to a word boundary
 *
 * quadPixels always incremented by 2
 *
 * INLINE: so will be different versions generated, depending on hard-coded (or known at compile-time) arguments
 */
static inline uint32_t* renderEcmShiftedTile(
  uint32_t *quadPixels,
  const uint32_t tilePixels[2],
  const uint32_t pattMask,
  const uint32_t shift,
  const uint32_t reverseShift)
{
  const uint32_t rightMask = maskExpandNibbleToWordRev[pattMask & 0xf];
  const uint32_t leftMask = maskExpandNibbleToWordRev[pattMask >> 4];

  {
    const uint32_t mask = leftMask << reverseShift;
    const uint32_t shifted = mask & (tilePixels[0] << reverseShift);
    *quadPixels++ = (*quadPixels & ~mask) | shifted;
  }

  {
    uint32_t shifted = (tilePixels[0] >> shift) | (tilePixels[1] << reverseShift);
    const uint32_t mask = ~((leftMask >> shift) | (rightMask << reverseShift));
    if (mask) shifted = (*quadPixels & mask) | (~mask & shifted);
    *quadPixels++ = shifted;
  }

  {
    const uint32_t mask = (rightMask >> shift);
    const uint32_t shifted = mask & (tilePixels[1] >> shift);
    *quadPixels = (*quadPixels & ~mask) | shifted;
  }

  return quadPixels;
}

/* Function:  renderEcmAlignedTile
 * ----------------------------------------
 * render a shiny new ECM (enhanced color mode) graphics I tile which is aligned to a word boundary
 *
 * quadPixels always incremented by 2
 *
 * INLINE: so will be different versions generated, depending on hard-coded (or known at compile-time) arguments
 */
static inline uint32_t* renderEcmAlignedTile(
  uint32_t *quadPixels,
  const uint32_t tilePixels[2],
  const uint32_t pattMask)
{
  if (pattMask == 0xff)
  {
    *quadPixels++ = tilePixels[0];
    *quadPixels++ = tilePixels[1];
  }
  else
  {
    // not shifted, but transparent - need to mask the two nibbles
    const uint32_t rightMask = maskExpandNibbleToWordRev[pattMask & 0xf];
    const uint32_t leftMask = maskExpandNibbleToWordRev[pattMask >> 4];

    *quadPixels++ = (*quadPixels & ~leftMask) | (leftMask & tilePixels[0]);
    *quadPixels++ = (*quadPixels & ~rightMask) | (rightMask & tilePixels[1]);
  }

  return quadPixels;
}

/* Function:  quadPixelIncrement
 * ----------------------------------------
 * compute the amount to increment our quad pixel pointer by.
 * generally, 2 words (8 pixel bytes), but in the case of the first tile
 * in a scrolled row, will either be 1 or even... 0 depending on how
 * many pixels we're scrolled by
 */
static inline uint32_t quadPixelIncrement(uint32_t startPattBit)
{
  if (!startPattBit) return 2;
  return startPattBit <= 4;
}


/* Function:  renderEcm0Tile
 * ----------------------------------------
 * render an ECM0 (enhanced color mode) graphics I tile. basically the same as original, but can scroll
 *
 * INLINE: so will be different versions generated, depending on hard-coded (or known at compile-time) arguments
 */
static inline uint32_t* renderEcm0Tile(
  uint32_t *quadPixels,
  const uint32_t xPos,
  const uint8_t pattIdx,
  const uint8_t patternTable[],
  const uint32_t colorTableAddr,
  const uint32_t startPattBit,
  const uint32_t pal,
  const uint32_t pattRow,
  const uint32_t shift,
  const bool isTile2)
{
  /* was this pattern empty? we remember the last empty pattern.
     OR is the pixel mask full here? in either case, let's bail */
  if ((!isTile2 && !tmsTestRowBitsMask(xPos, 0xff << 24, 8, false, true, false)))
  {
    return quadPixels + quadPixelIncrement(startPattBit);
  }

  /* grab the attributes for this tile */
  uint32_t colorTableOffset = pattIdx >> 3;
  const uint32_t colorByte = tms9918->vram.bytes[colorTableAddr + colorTableOffset];

  uint32_t pattOffset = pattIdx * PATTERN_BYTES + pattRow;
  uint32_t leftIndex = 0, rightIndex = 0;

  const uint32_t patt = patternTable[pattOffset];

  const uint32_t bgColor = colorByte & 0x0f;
  const uint32_t fgColor = colorByte >> 4;

  const uint32_t bgPalette = repeatedPalette[pal | bgColor];
  const uint32_t fgPalette = repeatedPalette[pal | fgColor];

  const uint32_t rightMask = maskExpandNibbleToWordRev[patt & 0xf];
  const uint32_t leftMask = maskExpandNibbleToWordRev[patt >> 4];

  const uint32_t tilePixels[2] = {(fgPalette & leftMask) | (bgPalette & ~leftMask),
                                  (fgPalette & rightMask) | (bgPalette & ~rightMask)};

  /* have we any pixels to draw? */
  const uint32_t offset = (24 + startPattBit);
  uint32_t pattMask = 0xff;
  if (!bgColor) pattMask &= patt;
  if (!fgColor) pattMask ^= patt;

  pattMask <<= offset;
  pattMask = tmsTestRowBitsMask(xPos, pattMask, 8, true, !isTile2, true);

    /* anything to draw?*/
  if (!pattMask)
  {
    return quadPixels + quadPixelIncrement(startPattBit);
  }

  pattMask >>= offset;

  if (startPattBit)
  {
    /* first tile gets different treatment because we discard the pixels shifted off the left */
    quadPixels = renderEcmStartTile(quadPixels, tilePixels, pattMask, startPattBit, shift << 3);
  }
  else
  {
    /* a regual shifted tile... we need to write three nibbles for these */
    switch (shift)
    {
      case 0:
        quadPixels = renderEcmAlignedTile(quadPixels, tilePixels, pattMask);
        break;
      case 1:
        quadPixels = renderEcmShiftedTile(quadPixels, tilePixels, pattMask, 8, 24);
        break;
      case 2:
        quadPixels = renderEcmShiftedTile(quadPixels, tilePixels, pattMask, 16, 16);
        break;
      default:
        quadPixels = renderEcmShiftedTile(quadPixels, tilePixels, pattMask, 24, 8);
        break;
    }
  }

  return quadPixels;
}


/* Function:  renderEcmTile
 * ----------------------------------------
 * render a shiny new ECM (enhanced color mode) graphics I tile
 *
 * INLINE: so will be different versions generated, depending on hard-coded (or known at compile-time) arguments
 */
static inline uint32_t* renderEcmTile(
  uint32_t *quadPixels,
  const uint32_t xPos,
  const uint8_t pattIdx,
  const uint8_t patternTable[],
  const uint32_t colorTableAddr,
  const uint32_t startPattBit,
  const uint32_t ecm,
  const uint32_t ecmOffset,
  const uint32_t ecmColorMask,
  const uint32_t ecmColorOffset,
  const uint32_t pal,
  const bool attrPerPos,
  const uint32_t rowOffset,
  const uint32_t pattRow,
  const uint32_t tileIndex,
  const uint32_t shift,
  uint32_t *lastEmpty,
  const bool isTile2,
  const bool alwaysOnTop)
{
  /* was this pattern empty? we remember the last empty pattern.
     OR is the pixel mask full here? in either case, let's bail */
  if (*lastEmpty == pattIdx ||
      (!isTile2 && !tmsTestRowBitsMask(xPos, 0xff << 24, 8, false, true, false)))
  {
    return quadPixels + quadPixelIncrement(startPattBit);
  }

  /* grab the attributes for this tile */
  uint32_t colorTableOffset = attrPerPos ? tileIndex : pattIdx;
  uint32_t pattOffset = pattIdx * PATTERN_BYTES;

  const uint32_t colorByte = tms9918->vram.bytes[colorTableAddr + colorTableOffset];

  pattOffset += (colorByte & 0x20) ? 7 - pattRow : pattRow;

  uint32_t pattMask = (colorByte & 0x10) ? 0 : 0xff;
  uint32_t leftIndex = 0, rightIndex = 0;

  /* retreive the pixel data for each ecm bitplane, and generate a
     combined mask while we're at it. if the mask has a bit set
     then we have a non-zero pixel at that location */
  uint32_t patt[3] = {0}; // indexes into this are reversed. ecm3 is in index 0


  switch (ecm)
  {
    case 3:
      patt[0] = patternTable[pattOffset + ecmOffset * 2];
    case 2:
      patt[1] = patternTable[pattOffset + ecmOffset];
    default:
      patt[2] = patternTable[pattOffset];
  }

  if (colorByte & 0x40) // flipX
  {
    patt[0] = reversedBits[patt[0]];
    patt[1] = reversedBits[patt[1]];
    patt[2] = reversedBits[patt[2]];
  }

  switch (ecm)
  {
    case 3:
        pattMask |= patt[0];
        leftIndex = (patt[0] >> 4) << 8;
        rightIndex = (patt[0] & 0xf) << 8;
    case 2:
        pattMask |= patt[1];
        leftIndex |= (patt[1] & 0xf0);
        rightIndex |= (patt[1] & 0xf) << 4;
    default:
        pattMask |= patt[2];
        leftIndex |= (patt[2] >> 4);
        rightIndex |= (patt[2] & 0xf);
  }

  /* have we any pixels to draw? */
  if (pattMask)
  {
    const uint32_t priority = alwaysOnTop || (colorByte & 0x80);
    const uint32_t offset = (24 + startPattBit);
    pattMask <<= offset;
    if (!priority)
      pattMask = tmsTestRowBitsMask(xPos, pattMask, 8, true, !isTile2, true);
    else
      pattMask = tmsTestRowBitsMask(xPos, pattMask, 8, true, !isTile2, false);

      /* anything to draw?*/
    if (!pattMask)
    {
      /* we don't set lastEmpty here, because it had pixels.. they were just masked out */
      return quadPixels + quadPixelIncrement(startPattBit);
    }

    pattMask >>= offset;

    const uint32_t palette = repeatedPalette[pal | ((colorByte & ecmColorMask) << ecmColorOffset)];
    const uint32_t tilePixels[2] = {ecmLookup[leftIndex] | palette,
                                    ecmLookup[rightIndex] | palette};

    if (startPattBit)
    {
      /* first tile gets different treatment because we discard the pixels shifted off the left */
      quadPixels = renderEcmStartTile(quadPixels, tilePixels, pattMask, startPattBit, shift << 3);
    }
    else
    {
      /* a regular shifted tile... we need to write three nibbles for these */
      switch (shift)
      {
        case 0:
            /* not shifted, but has transparency. we'll need to mask it */
          quadPixels = renderEcmAlignedTile(quadPixels, tilePixels, pattMask);
          break;
        case 1:
          quadPixels = renderEcmShiftedTile(quadPixels, tilePixels, pattMask, 8, 24);
          break;
        case 2:
          quadPixels = renderEcmShiftedTile(quadPixels, tilePixels, pattMask, 16, 16);
          break;
        case 3:
          quadPixels = renderEcmShiftedTile(quadPixels, tilePixels, pattMask, 24, 8);
          break;
      }
    }
  }
  else
  {
    quadPixels += quadPixelIncrement(startPattBit);
    *lastEmpty = pattIdx;
  }
  return quadPixels;
}

/* Function:  renderStdTile
 * ----------------------------------------
 * render an old-school graphics I tile layer
 *
 * INLINE: so will be different versions generated, depending on hard-coded (or known at compile-time) arguments
 */
static inline uint8_t* renderStdTile(
  uint8_t *pixels,
  const uint32_t xPos,
  const uint8_t pattIdx,
  const uint8_t patternTable[],
  const uint32_t colorTableAddr,
  const uint32_t pal,
  const uint32_t pattRow,
  uint32_t count)
{
  uint32_t pattOffset = pattIdx * PATTERN_BYTES;

  // non-ecm - either foreground or background
  int8_t pattByte = patternTable[pattOffset + pattRow];
  const uint32_t colorByte = tms9918->vram.bytes[colorTableAddr + (pattIdx >> 3)];

  const uint32_t bgFgColor[] = {
    pal | tmsBgColor(tms9918, colorByte),
    pal | tmsFgColor(tms9918, colorByte)
  };

  while (count--)
  {
    *pixels++ = bgFgColor[pattByte < 0];
    pattByte <<= 1;
  }

  return pixels;
}


/* Function:  vrEmuF18ATileScanLine
 * ----------------------------------------
 * generate an F18A tile layer scanline
 *
 * INLINE: so will be different versions generated, depending on hard-coded (or known at compile-time) arguments (T1 or T2)
 */
static inline void __time_critical_func(vrEmuF18ATileScanLine)(VR_EMU_INST_ARG const uint8_t y, const bool hpSize, uint16_t rowNamesAddr, uint16_t colorTableAddr, const uint16_t rowOffset, uint8_t tileIndex, uint8_t startPattBit, const bool attrPerPos, uint8_t pal, const bool alwaysOnTop, const bool isTile2, uint8_t pixels[TMS9918_PIXELS_X])
{
  uint32_t xPos = 0;
  uint32_t lastEmpty = -1;

  const uint32_t pattRow = y & 0x07;  /* which pattern row (0 - 7) */
  const uint8_t* patternTable = tms9918->vram.bytes + tmsPatternTableAddr(tms9918);

  // for the entire scanline, we need to shift our 4-pixel words by this much
  uint32_t lastPattId = -1;

  /* iterate over each tile in this row - if' we're scrolling, add one */
  uint32_t numTiles = GRAPHICS_NUM_COLS;

  /* keep in mind when using this... the byte order will be reversed */
  uint32_t *quadPixels = (uint32_t*)pixels;

  if (tms9918->isUnlocked)
  {
    const uint32_t shift = startPattBit & 0x03;
    const uint32_t ecm = (TMS_REGISTER(tms9918, 0x31) & 0x30) >> 4;

    if (ecm)
    {
      const uint32_t ecmColorOffset = (ecm == 3) ? 2 : ecm;
      const uint32_t ecmColorMask = (ecm == 3) ? 0x0e : 0x0f;
      const uint32_t ecmOffset = 0x800 >> ((TMS_REGISTER(tms9918, 0x1d) & 0x0c) >> 2);

      if (ecm == 1)
      {
        pal &= 0x20;
      }
      else
      {
        pal = 0;
      }

      VR_TMS_FILL32_WAIT();

      if (startPattBit)
      {
        const uint8_t pattIdx = tms9918->vram.bytes[rowNamesAddr + tileIndex];
        quadPixels = renderEcmTile(quadPixels, xPos, pattIdx, patternTable, colorTableAddr, startPattBit, ecm, ecmOffset,
                                    ecmColorMask, ecmColorOffset, pal, attrPerPos, rowOffset, pattRow, tileIndex++, shift,
                                    &lastEmpty, isTile2, alwaysOnTop);
        xPos += 8 - startPattBit;
      }

      if (shift)
      {
        while (numTiles--)
        {
          /* next page? */
          if (tileIndex == GRAPHICS_NUM_COLS)
          {
            if (hpSize)
            {
              rowNamesAddr ^= 0x400;
              if (attrPerPos) colorTableAddr ^= 0x400;
            }
            tileIndex = 0;
          }
          const uint8_t pattIdx = tms9918->vram.bytes[rowNamesAddr + tileIndex];
          const uint8_t noStartPattBit = 0;
          quadPixels = renderEcmTile(quadPixels, xPos, pattIdx, patternTable, colorTableAddr, noStartPattBit, ecm, ecmOffset,
                                      ecmColorMask, ecmColorOffset, pal, attrPerPos, rowOffset, pattRow, tileIndex++, shift,
                                      &lastEmpty, isTile2, alwaysOnTop);
          xPos += 8;
        }
      }
      else
      {
        while (numTiles--)
        {
          /* next page? */
          if (tileIndex == GRAPHICS_NUM_COLS)
          {
            if (hpSize)
            {
              rowNamesAddr ^= 0x400;
              if (attrPerPos) colorTableAddr ^= 0x400;
            }
            tileIndex = 0;
          }
          const uint8_t pattIdx = tms9918->vram.bytes[rowNamesAddr + tileIndex];
          const uint8_t noStartPattBit = 0;
          const uint32_t noShift = 0;
          quadPixels = renderEcmTile(quadPixels, xPos, pattIdx, patternTable, colorTableAddr, noStartPattBit, ecm, ecmOffset,
                                      ecmColorMask, ecmColorOffset, pal, attrPerPos, rowOffset, pattRow, tileIndex++, noShift,
                                      &lastEmpty, isTile2, alwaysOnTop);
          xPos += 8;
        }
      }
    }
    else  // ECM0 is a bit different
    {
      if (startPattBit) ++numTiles;
      while (numTiles--)
      {
        /* next page? */
        if (tileIndex == GRAPHICS_NUM_COLS)
        {
          if (hpSize)
          {
            rowNamesAddr ^= 0x400;
            if (attrPerPos) colorTableAddr ^= 0x400;
          }
          tileIndex = 0;
        }
        const uint8_t pattIdx = tms9918->vram.bytes[rowNamesAddr + tileIndex];
        quadPixels = renderEcm0Tile(quadPixels, xPos, pattIdx, patternTable, colorTableAddr, startPattBit, pal, pattRow, shift, isTile2);
        xPos += 8 - startPattBit;
        startPattBit = 0;
        ++tileIndex;
      }
    }
  }
  else
  {
    VR_TMS_FILL32_WAIT();

    while (numTiles--)
    {
      const uint8_t pattIdx = tms9918->vram.bytes[rowNamesAddr + tileIndex];

      pixels = renderStdTile(pixels, xPos, pattIdx, patternTable, colorTableAddr, pal, pattRow, 8);
      ++tileIndex;
      xPos += 8;
    }
  }
}


/* Function:  vrEmuF18ATile1ScanLine
 * ----------------------------------------
 * generate a Graphics I mode scanline for the T1 layer
 */
static void __time_critical_func(vrEmuF18ATile1ScanLine)(VR_EMU_INST_ARG uint16_t y, uint8_t pixels[TMS9918_PIXELS_X])
{
  bool swapYPage = false;

  /* vertical scroll */
  if (TMS_REGISTER(tms9918, 0x1c))
  {
    int virtY = y;
    virtY += TMS_REGISTER(tms9918, 0x1c);

    int maxY = (TMS_REGISTER(tms9918, 0x31) & 0x40) ? (8 * 30) : (8 * 24);

    if (virtY >= maxY)
    {
      virtY -= maxY;
      swapYPage = (bool)(TMS_REGISTER(tms9918, 0x1d) & 0x01);
    }

    y = virtY;
  }

  const uint8_t tileY = y >> 3;   /* which name table row (0 - 23)... or 29 */

  /* address in name table at the start of this row */
  const bool attrPerPos = TMS_REGISTER(tms9918, 0x32) & 0x02;
  const uint16_t rowOffset = tileY * GRAPHICS_NUM_COLS;

  uint16_t rowNamesAddr = tmsNameTableAddr(tms9918) + rowOffset;
  if (swapYPage) rowNamesAddr ^= 0x800;

  uint16_t colorTableAddr = tmsColorTableAddr(tms9918);
  if (attrPerPos)
  {
    colorTableAddr = (colorTableAddr & ~0x400) | (rowNamesAddr & 0x400);
    if (swapYPage) colorTableAddr ^= 0x800;
    colorTableAddr += rowOffset;
  }

  const uint8_t pal = (TMS_REGISTER(tms9918, 0x18) & 0x03) << 4;
  const uint8_t startPattBit = TMS_REGISTER(tms9918, 0x1b) & 0x07;
  const uint8_t tileIndex = (TMS_REGISTER(tms9918, 0x1b) >> 3);
  const bool hpSize = TMS_REGISTER(tms9918, 0x1d) & 0x02;
  const bool isTile2 = false;

  vrEmuF18ATileScanLine(VR_EMU_INST y, hpSize, rowNamesAddr, colorTableAddr, rowOffset, tileIndex, startPattBit, attrPerPos, pal, isTile2, 0, pixels);
}

/* Function:  vrEmuF18ATile2ScanLine
 * ----------------------------------------
 * generate a Graphics I mode scanline for the T2 layer
 */
static void __time_critical_func(vrEmuF18ATile2ScanLine)(VR_EMU_INST_ARG uint16_t y, uint8_t pixels[TMS9918_PIXELS_X])
{
  bool swapYPage = false;

  /* vertical scroll */
  if (TMS_REGISTER(tms9918, 0x1a))
  {
    int virtY = y;
    virtY += TMS_REGISTER(tms9918, 0x1a);

    int maxY = (TMS_REGISTER(tms9918, 0x31) & 0x40) ? (8 * 30) : (8 * 24);

    if (virtY >= maxY)
    {
      virtY -= maxY;
      swapYPage = (bool)(TMS_REGISTER(tms9918, 0x1d) & 0x10);
    }

    y = virtY;
  }

  const uint8_t tileY = y >> 3;   /* which name table row (0 - 23)... or 29 */

  /* address in name table at the start of this row */
  const uint16_t rowOffset = tileY * GRAPHICS_NUM_COLS;
  const bool attrPerPos = TMS_REGISTER(tms9918, 0x32) & 0x02;

  uint16_t rowNamesAddr = tmsNameTable2Addr(tms9918) + rowOffset;
  if (swapYPage) rowNamesAddr ^= 0x800;

  uint16_t colorTableAddr = tmsColorTable2Addr(tms9918);
  if (attrPerPos)
  {
    colorTableAddr = (colorTableAddr & ~0x400) | (rowNamesAddr & 0x400);
    if (swapYPage) colorTableAddr ^= 0x800;
    colorTableAddr += rowOffset;
  }

  const uint8_t pal = (TMS_REGISTER(tms9918, 0x18) & 0x0c) << 2;
  const uint8_t startPattBit = TMS_REGISTER(tms9918, 0x19) & 0x07;
  const uint8_t tileIndex = (TMS_REGISTER(tms9918, 0x19) >> 3);
  const bool hpSize = TMS_REGISTER(tms9918, 0x1d) & 0x20;
  const bool tile2Priority = !(TMS_REGISTER(tms9918, 0x32) & 0x01);
  const bool isTile2 = true;

  vrEmuF18ATileScanLine(VR_EMU_INST y, hpSize, rowNamesAddr, colorTableAddr, rowOffset, tileIndex, startPattBit, attrPerPos, pal, tile2Priority, isTile2, pixels);
}

/* Function:  renderBitmapLayer
 * ----------------------------------------
 * generate an F18A bitmap layer scanline
 *
 * INLINE: so will be different versions generated, depending on hard-coded (or known at compile-time) arguments
 */
static inline bool __time_critical_func(renderBitmapLayer)(VR_EMU_INST_ARG uint16_t y, bool opaque, const uint8_t width, const uint16_t addr, const uint8_t bmlCtl, uint8_t pixels[TMS9918_PIXELS_X])
{
  bool writeMask = bmlCtl & 0x40;

  bool returnVal = true;

  if (writeMask && opaque && (width == 64))
  {
    for (int i = 0; i < TMS9918_PIXELS_X / 32; ++i)
      rowBits[i] = -1;
    writeMask = false;
    returnVal = false;
  }

  uint32_t currentMask = 0;
  uint8_t xPos = TMS_REGISTER(tms9918, 0x21);

  if (bmlCtl & 0x10)  // fat 4bpp pixels?
  {
    const uint8_t colorMask = 0xf0;
    const uint8_t colorOffset = 4;
    const uint8_t colorCount = 2;
    const uint8_t colorSize = 4;
    uint32_t maskPixelMask = 0x3 << 30;
    uint32_t maskX = xPos;

    uint8_t pal = (bmlCtl & 0xc) << 2;

    VR_TMS_FILL32_WAIT();

    for (int xOff = 0; xOff < width; ++xOff)
    {
      uint8_t data = tms9918->vram.bytes[addr + xOff];
      for (int sp = 0; sp < colorCount; ++sp)
      {
        uint8_t color = (data & colorMask);
        if (opaque || color)
        {
          uint8_t finalColour = pal | (color >> colorOffset);
          pixels[xPos] = finalColour;
          pixels[xPos + 1] = finalColour;
          currentMask |= maskPixelMask;
        }
        xPos += 2;
        data <<= colorSize;
        maskPixelMask >>= 2;
      }
      if (writeMask && !maskPixelMask && currentMask)
      {
        tmsTestRowBitsMask(maskX, currentMask, 32, true, false, false);
        maskX = xPos;
        maskPixelMask = 0x3 << 30;
        currentMask = 0;
      }
    }
    if (writeMask && currentMask)
    {
      tmsTestRowBitsMask(maskX, currentMask, xPos - maskX, true, false, false);
    }
  }
  else // regular 2bpp pixels
  {
    const uint8_t colorMask = 0xc0;
    const uint8_t colorOffset = 6;
    const uint8_t colorCount = 4;
    const uint8_t colorSize = 2;
    uint32_t maskPixelMask = 0x1 << 31;
    uint32_t maskX = xPos;

    uint8_t pal = (bmlCtl & 0xf) << 2;

    VR_TMS_FILL32_WAIT();

    for (int xOff = 0; xOff < width; ++xOff)
    {
      uint8_t data = tms9918->vram.bytes[addr + xOff];
      for (int sp = 0; sp < colorCount; ++sp)
      {
        uint8_t color = (data & colorMask);
        if (opaque || color)
        {
          pixels[xPos] = pal | (color >> colorOffset);
          currentMask |= maskPixelMask;
        }
        ++xPos;
        data <<= colorSize;
        maskPixelMask >>= 1;
      }

      if (writeMask && !maskPixelMask && currentMask)
      {
        tmsTestRowBitsMask(maskX, currentMask, 32, true, false, false);
        maskX = xPos;
        maskPixelMask = 0x1 << 31;
        currentMask = 0;
      }
    }
    if (writeMask && currentMask)
    {
      tmsTestRowBitsMask(maskX, currentMask, xPos - maskX, true, false, false);
    }
  }
  return returnVal;
}


/* Function:  vrEmuTms9918BitmapLayerScanLine
 * ----------------------------------------
 * generate an F18A bitmap layer scanline
 */
static bool __time_critical_func(vrEmuTms9918BitmapLayerScanLine)(VR_EMU_INST_ARG uint16_t y, uint8_t pixels[TMS9918_PIXELS_X])
{
  /* bml enabled? */
  const uint8_t bmlCtl = TMS_REGISTER(tms9918, 0x1f);
  if (!(bmlCtl & 0x80))
    return true;

  /* bml on this scanline? */
  const uint8_t top = TMS_REGISTER(tms9918, 0x22);
  if (top > y)
    return true;

  y -= top;
  if (y >= TMS_REGISTER(tms9918, 0x24))
    return true;

  const uint8_t width = TMS_REGISTER(tms9918, 0x23) ? (TMS_REGISTER(tms9918, 0x23) >> 2) : 64;
  const uint16_t addr = (TMS_REGISTER(tms9918, 0x20) << 6) + (y * width);

  //if (bmlCtl & 0x20) // transp
  {
    return renderBitmapLayer(VR_EMU_INST y, !(bmlCtl & 0x20), width, addr, bmlCtl, pixels);
  }
//  else
  {
//    renderBitmapLayer(VR_EMU_INST y, false, width, addr, bmlCtl, pixels);
  }
}


/* Function:  vrEmuTms9918GraphicsIScanLine
 * ----------------------------------------
 * generate a Graphics I mode scanline (pure tile renderer — overlays handled by pipeline stages)
 */
static uint8_t __time_critical_func(vrEmuTms9918GraphicsIScanLine)(VR_EMU_INST_ARG uint16_t y, uint8_t pixels[TMS9918_PIXELS_X])
{
  const uint8_t tileY = y >> 3;   /* which name table row (0 - 23)... or 29 */

  /* address in name table at the start of this row */
  const uint16_t rowOffset = tileY * GRAPHICS_NUM_COLS;
  uint16_t rowNamesAddr = tmsNameTableAddr(tms9918) + rowOffset;
  uint16_t colorTableAddr = tmsColorTableAddr(tms9918);

  const bool attrPerPos = false;
  const uint8_t pal = 0;
  const uint8_t startPattBit = 0;
  const uint8_t tileIndex = 0;
  const bool hpSize = 0;
  const bool isTile2 = false;

  VR_TMS_FILL32_WAIT();

  vrEmuF18ATileScanLine(VR_EMU_INST y, hpSize, rowNamesAddr, colorTableAddr, rowOffset, tileIndex, startPattBit, attrPerPos, pal, isTile2, 0, pixels);

  return 0;
}
