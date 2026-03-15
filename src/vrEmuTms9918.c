/*
 * Troy's TMS9918 Emulator - Core interface
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/VrEmuTms9918
 *
 */



#include "impl/vrEmuTms9918Priv.h"

#include <stdlib.h>
#include <string.h>

#define R0_DOUBLE_ROWS 0x08


#ifdef PICO_BUILD
unsigned int vrTmsDma8  = 4;
unsigned int vrTmsDma32 = 3;
#else
/* desktop stubs for VR_TMS_FILL32 */
void*    _vrTmsFill32Src   = NULL;
unsigned _vrTmsFill32Count = 0;
#endif

VR_TMS_SECTION_SCRATCH_X(buffer) static uint32_t vrTmsBg;


#if VR_EMU_TMS9918_SINGLE_INSTANCE

static VrEmuTms9918 __aligned(256) tms9918Inst;

VrEmuTms9918* tms9918 = &tms9918Inst;

// for a single scanline, we only support a single mode... so let's cache it.
vrEmuTms9918Mode tmsCachedMode = TMS_MODE_GRAPHICS_I;

/* Function:  vrEmuTms9918Init
 * --------------------
 * initialize the TMS9918 library in single-instance mode
 */
VR_EMU_TMS9918_DLLEXPORT
void __time_critical_func(vrEmuTms9918Init)()
{
#ifdef PICO_BUILD
  {
    dma_channel_config cfg = dma_channel_get_default_config(vrTmsDma8);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    dma_channel_set_config(vrTmsDma8, &cfg, false);
  }
#endif

  vrEmuTms9918Reset(tms9918);
}

#else

#include <stdlib.h>

 /* Function:  vrEmuTms9918New
  * ----------------------------------------
  * create a new TMS9918
  */
VR_EMU_TMS9918_DLLEXPORT VrEmuTms9918* vrEmuTms9918New()
{
  VrEmuTms9918* tms9918 = (VrEmuTms9918*)malloc(sizeof(VrEmuTms9918));
  if (tms9918 != NULL)
  {
    vrEmuTms9918Reset(tms9918);
  }

  return tms9918;
}

#endif



vrEmuTms9918Mode r1Modes [] = { TMS_MODE_GRAPHICS_I, TMS_MODE_MULTICOLOR, TMS_MODE_TEXT, TMS_MODE_GRAPHICS_I };

static inline vrEmuTms9918Mode tmsMode(VrEmuTms9918* tms9918)
{
  if (TMS_REGISTER(tms9918, TMS_REG_0) & TMS_R0_MODE_GRAPHICS_II)
    return TMS_MODE_GRAPHICS_II;
  else if (TMS_REGISTER(tms9918, TMS_REG_0) & TMS_R0_MODE_TEXT_80)
    return TMS_MODE_TEXT80;
  else 
    return r1Modes [(TMS_REGISTER(tms9918, TMS_REG_1) & (TMS_R1_MODE_MULTICOLOR | TMS_R1_MODE_TEXT)) >> 3];
}

/* Function:  tmsSpriteSize
 * ----------------------------------------
 * sprite size (8 or 16)
 */
static inline uint8_t tmsSpriteSize(VrEmuTms9918* tms9918)
{
  return TMS_REGISTER(tms9918, TMS_REG_1) & TMS_R1_SPRITE_16 ? 16 : 8;
}

/* Function:  tmsSpriteMagnification
 * ----------------------------------------
 * sprite size (0 = 1x, 1 = 2x)
 */
static inline bool tmsSpriteMag(VrEmuTms9918* tms9918)
{
  return TMS_REGISTER(tms9918, TMS_REG_1) & TMS_R1_SPRITE_MAG2;
}

/* Function:  tmsNameTableAddr
 * ----------------------------------------
 * name table base address
 */
static inline uint16_t tmsNameTableAddr(VrEmuTms9918* tms9918)
{
  return (TMS_REGISTER(tms9918, TMS_REG_NAME_TABLE) & 0x0f) << 10;
}

/* Function:  tmsNameTable2Addr
 * ----------------------------------------
 * name table base address
 */
static inline uint16_t tmsNameTable2Addr(VrEmuTms9918* tms9918)
{
  return (TMS_REGISTER(tms9918, 10) & 0x0f) << 10;
}

/* Function:  tmsColorTableAddr
 * ----------------------------------------
 * color table base address
 */
static inline uint16_t tmsColorTableAddr(VrEmuTms9918* tms9918)
{
  const uint8_t mask = (tmsCachedMode == TMS_MODE_GRAPHICS_II) ? 0x80 : 0xff;

  return (TMS_REGISTER(tms9918, TMS_REG_COLOR_TABLE) & mask) << 6;
}

/* Function:  tmsColorTable2Addr
 * ----------------------------------------
 * color table base address
 */
static inline uint16_t tmsColorTable2Addr(VrEmuTms9918* tms9918)
{
  const uint8_t mask = (tmsCachedMode == TMS_MODE_GRAPHICS_II) ? 0x80 : 0xff;

  return (TMS_REGISTER(tms9918, 11) & mask) << 6;
}

/* Function:  tmsPatternTableAddr
 * ----------------------------------------
 * pattern table base address
 */
static inline uint16_t tmsPatternTableAddr(VrEmuTms9918* tms9918)
{
  const uint8_t mask = (tmsCachedMode == TMS_MODE_GRAPHICS_II) ? 0x04 : 0x07;

  return (TMS_REGISTER(tms9918, TMS_REG_PATTERN_TABLE) & mask) << 11;
}

/* Function:  tmsSpriteAttrTableAddr
 * ----------------------------------------
 * sprite attribute table base address
 */
static inline uint16_t tmsSpriteAttrTableAddr(VrEmuTms9918* tms9918)
{
  return (TMS_REGISTER(tms9918, TMS_REG_SPRITE_ATTR_TABLE) & 0x7f) << 7;
}

/* Function:  tmsSpritePatternTableAddr
 * ----------------------------------------
 * sprite pattern table base address
 */
static inline uint16_t tmsSpritePatternTableAddr(VrEmuTms9918* tms9918)
{
  return (TMS_REGISTER(tms9918, TMS_REG_SPRITE_PATT_TABLE) & 0x07) << 11;
}

/* Function:  tmsBgColor
 * ----------------------------------------
 * background color
 */
static inline vrEmuTms9918Color tmsMainBgColor(VrEmuTms9918* tms9918)
{
  return TMS_REGISTER(tms9918, TMS_REG_FG_BG_COLOR) & 0x0f;
}

/* Function:  tmsFgColor
 * ----------------------------------------
 * foreground color
 */
static inline vrEmuTms9918Color tmsMainFgColor(VrEmuTms9918* tms9918)
{
  const vrEmuTms9918Color c = (vrEmuTms9918Color)(TMS_REGISTER(tms9918, TMS_REG_FG_BG_COLOR) >> 4);
  return c == TMS_TRANSPARENT ? tmsMainBgColor(tms9918) : c;
}

/* Function:  tmsFgColor
 * ----------------------------------------
 * foreground color
 */
static inline vrEmuTms9918Color tmsFgColor(VrEmuTms9918* tms9918, uint8_t colorByte)
{
  const vrEmuTms9918Color c = (vrEmuTms9918Color)(colorByte >> 4);
  return c == TMS_TRANSPARENT ? tmsMainBgColor(tms9918) : c;
}

/* Function:  tmsBgColor
 * ----------------------------------------
 * background color
 */
static inline vrEmuTms9918Color tmsBgColor(VrEmuTms9918* tms9918, uint8_t colorByte)
{
  const vrEmuTms9918Color c = (vrEmuTms9918Color)(colorByte & 0x0f);
  return c == TMS_TRANSPARENT ? tmsMainBgColor(tms9918) : c;
}


/* Function:  tmsMemset
 * ----------------------------------------
 * memset using rp2040 hardware dma (or plain memset on desktop)
 */
static void tmsMemset(uint8_t* ptr, uint8_t val8, int count, bool wait)
{
  (void)wait;
  VR_TMS_MEMSET8(ptr, val8, count);
}

// default palette 0xARGB
static const uint16_t defaultPalette[] = {
  //-- Palette 0, Deafult TMS9918A palette
  0x0000, 0xF000, 0xF2C3, 0xF5D6, 0xF54F, 0xF76F, 0xFD54, 0xF4EF, 0xFF54, 0xFF76, 0xFDC3, 0xFED6, 0xF2B2, 0xFC5C, 0xFCCC, 0xFFFF,
  //-- Palette 1, ECM1 (0 index is always 000) version of palette 0
  0x0000, 0xF2C3, 0xF000, 0xF54F, 0xF000, 0xFD54, 0xF000, 0xF4EF, 0xF000, 0xFCCC, 0xF000, 0xFDC3, 0xF000, 0xFC5C, 0xF000, 0xFFFF,
  //-- Palette 2, CGA colors
  0x0000, 0xF00A, 0xF0A0, 0xF0AA, 0xFA00, 0xFA0A, 0xFA50, 0xFAAA, 0xF555, 0xF55F, 0xF5F5, 0xF5FF, 0xFF55, 0xFF5F, 0xFFF5, 0xFFFF,
  //-- Palette 3, ECM1 (0 index is always 000) version of palette 2
  0x0000, 0xF555, 0xF000, 0xF00A, 0xF000, 0xF0A0, 0xF000, 0xF0AA, 0xF000, 0xFA00, 0xF000, 0xFA0A, 0xF000, 0xFA50, 0xF000, 0xFFFF
};

static void __attribute__ ((noinline)) vdpRegisterReset(VrEmuTms9918* tms9918)
{
  tms9918->isUnlocked = false;
  tms9918->restart = 0;
  tms9918->unlockCount = 0;
  tms9918->lockedMask = 0x07;
  memset(&TMS_REGISTER(tms9918, 0), 0, TMS_REGISTERS);
  TMS_REGISTER(tms9918, 0x01) = 0x40;
  TMS_REGISTER(tms9918, 0x03) = 0x10;
  TMS_REGISTER(tms9918, 0x04) = 0x01;
  TMS_REGISTER(tms9918, 0x05) = 0x0A;
  TMS_REGISTER(tms9918, 0x06) = 0x02;
  TMS_REGISTER(tms9918, 0x07) = 0xF2;
  TMS_REGISTER(tms9918, 0x1e) = MAX_SPRITES - 1; // scanline sprites
  TMS_REGISTER(tms9918, 0x30) = 1; // vram address increment register
  TMS_REGISTER(tms9918, 0x33) = MAX_SPRITES; // Sprites to process
  TMS_REGISTER(tms9918, 0x36) = 0x40;
}


/* Function:  vrEmuTms9918Reset
 * ----------------------------------------
 * reset the new TMS9918
 */
VR_EMU_TMS9918_DLLEXPORT void __time_critical_func(vrEmuTms9918Reset)(VR_EMU_INST_ONLY_ARG)
{
  tms9918->regWriteStage0Value = 0;
  tms9918->currentAddress = 0;
  tms9918->gpuAddress = 0xFFFF; // "Odd" don't start value
  tms9918->regWriteStage = 0;
  memset(&TMS_STATUS(tms9918, 0), 0, TMS_STATUS_REGISTERS);
  TMS_STATUS(tms9918, 0) = 0x1f;
  TMS_STATUS(tms9918, 1) = 0xE8;  // ID = F18A (0xE0) set 0x08 for anyone who cares it's not a real one
  TMS_STATUS(tms9918, 14) = 0x1A; // Version
  tms9918->readAheadBuffer = 0;

  vdpRegisterReset(tms9918);
  TMS_REGISTER(tms9918, 0x01) = 0x00; // turn display off
  TMS_REGISTER(tms9918, 0x07) = 0x00;
  tmsCachedMode = TMS_MODE_GRAPHICS_I;

  /* Initialize runtime base VDP from config (default: TMS9918A) */
  tms9918->vdpBase = (tms9918->config[CONF_VDP_BASE] == VR_EMU_TMS9918_BASE_V9938)
                     ? VR_EMU_TMS9918_BASE_V9938 : VR_EMU_TMS9918_BASE_TMS9918;

  // set up default palettes (arm is little-endian, tms9900 is big-endian)
  for (int i = 0; i < sizeof(defaultPalette) / sizeof(uint16_t); ++i)
  {
    tms9918->vram.map.pram[i] = __builtin_bswap16(defaultPalette[i]);
  }

  /* ram intentionally left in unknown state */
}


/* Function:  vrEmuTms9918Destroy
 * ----------------------------------------
 * destroy a TMS9918
 *
 * tms9918: tms9918 object to destroy / clean up
 */
VR_EMU_TMS9918_DLLEXPORT void __time_critical_func(vrEmuTms9918Destroy)(VR_EMU_INST_ONLY_ARG)
{
#if !VR_EMU_TMS9918_SINGLE_INSTANCE
  free(tms9918);
  tms9918 = NULL;
#endif
}

/* Function:  vrEmuTms9918WriteAddr
 * ----------------------------------------
 * write an address (mode = 1) to the tms9918
 *
 * data: the data (DB0 -> DB7) to send
 */
VR_EMU_TMS9918_DLLEXPORT void __time_critical_func(vrEmuTms9918WriteAddr)(VR_EMU_INST_ARG uint8_t data)
{
  vrEmuTms9918WriteAddrImpl(VR_EMU_INST data);
}

/* Function:  vrEmuTms9918ReadStatus
 * ----------------------------------------
 * read from the status register
 */
VR_EMU_TMS9918_DLLEXPORT uint8_t __time_critical_func(vrEmuTms9918ReadStatus)(VR_EMU_INST_ONLY_ARG)
{
  return vrEmuTms9918ReadStatusImpl(VR_EMU_INST_ONLY);
}

/* Function:  vrEmuTms9918PeekStatus
 * ----------------------------------------
 * read from the status register without resetting it
 */
VR_EMU_TMS9918_DLLEXPORT uint8_t __time_critical_func(vrEmuTms9918PeekStatus)(VR_EMU_INST_ONLY_ARG)
{
  return vrEmuTms9918PeekStatusImpl(VR_EMU_INST_ONLY);
}

/* Function:  vrEmuTms9918WriteData
 * ----------------------------------------
 * write data (mode = 0) to the tms9918
 *
 * data: the data (DB0 -> DB7) to send
 */
VR_EMU_TMS9918_DLLEXPORT void __time_critical_func(vrEmuTms9918WriteData)(VR_EMU_INST_ARG uint8_t data)
{
  return vrEmuTms9918WriteDataImpl(VR_EMU_INST data);
}


/* Function:  vrEmuTms9918ReadData
 * ----------------------------------------
 * read data (mode = 0) from the tms9918
 */
VR_EMU_TMS9918_DLLEXPORT uint8_t __time_critical_func(vrEmuTms9918ReadData)(VR_EMU_INST_ONLY_ARG)
{
  return vrEmuTms9918ReadDataImpl(VR_EMU_INST_ONLY);
}

/* Function:  vrEmuTms9918ReadDataNoInc
 * ----------------------------------------
 * read data (mode = 0) from the tms9918
 */
VR_EMU_TMS9918_DLLEXPORT uint8_t __time_critical_func(vrEmuTms9918ReadDataNoInc)(VR_EMU_INST_ONLY_ARG)
{
  return vrEmuTms9918ReadDataNoIncImpl(VR_EMU_INST_ONLY);
}

/* Function:  vrEmuTms9918InterruptStatus
 * --------------------
 * return true if both INT status and INT control set
 */
VR_EMU_TMS9918_DLLEXPORT bool __time_critical_func(vrEmuTms9918InterruptStatus)(VR_EMU_INST_ONLY_ARG)
{
  return vrEmuTms9918InterruptStatusImpl(VR_EMU_INST_ONLY);
}

/* Function:  vrEmuTms9918InterruptSet
 * --------------------
 * return true if both INT status and INT control set
 */
VR_EMU_TMS9918_DLLEXPORT void __time_critical_func(vrEmuTms9918InterruptSet)(VR_EMU_INST_ONLY_ARG)
{
  vrEmuTms9918InterruptSet(VR_EMU_INST_ONLY);
}

/* Function:  vrEmuTms9918SetStatus
 * --------------------
 * set status flag
 */
VR_EMU_TMS9918_DLLEXPORT
void __time_critical_func(vrEmuTms9918SetStatus)(VR_EMU_INST_ARG uint8_t status)
{
  vrEmuTms9918SetStatusImpl(VR_EMU_INST status);
}

VR_TMS_SECTION_SCRATCH_X(lookup) static uint32_t __aligned(4) rowSpriteBits[TMS9918_PIXELS_X / 32];             /* collision mask */
VR_TMS_SECTION_SCRATCH_X(lookup) static uint32_t __aligned(4) rowTransparentSpriteBits[TMS9918_PIXELS_X / 32];  /* transparent sprite pixels */
VR_TMS_SECTION_SCRATCH_X(lookup) static uint32_t __aligned(4) rowBits[TMS9918_PIXELS_X / 32];                   /* pixel mask */

/* Function:  tmsTestCollisionMask
 * ----------------------------------------
 * Test and update the sprite collision mask.
 */
static inline uint32_t tmsTestCollisionMask(VR_EMU_INST_ARG const uint32_t xPos, const uint32_t spritePixels, const uint32_t spriteWidth)
{
  uint32_t rowSpriteBitsWord = xPos >> 5;
  uint32_t rowSpriteBitsWordBit = xPos & 0x1f;

  uint32_t validPixels = (~rowSpriteBits[rowSpriteBitsWord]) & (spritePixels >> rowSpriteBitsWordBit);
  rowSpriteBits[rowSpriteBitsWord] |= validPixels;
  validPixels <<= rowSpriteBitsWordBit;

  rowSpriteBitsWordBit = 32 - rowSpriteBitsWordBit;
  if (rowSpriteBitsWordBit < spriteWidth)
  {
    uint32_t right = (~rowSpriteBits[++rowSpriteBitsWord]) & (spritePixels << rowSpriteBitsWordBit);
    rowSpriteBits[rowSpriteBitsWord] |= right;
    validPixels |= (right >> rowSpriteBitsWordBit);
  }

  return validPixels;
}


/* Function:  tmsSetTransparentSpriteMask
 * ----------------------------------------
 * set the transparent sprite mask.
 */
static inline void tmsSetTransparentSpriteMask(VR_EMU_INST_ARG const uint32_t xPos, const uint32_t spritePixels, const uint32_t spriteWidth)
{
  uint32_t rowSpriteBitsWord = xPos >> 5;
  uint32_t rowSpriteBitsWordBit = xPos & 0x1f;
  
  rowTransparentSpriteBits[rowSpriteBitsWord] |= spritePixels >> rowSpriteBitsWordBit;

  rowSpriteBitsWordBit = 32 - rowSpriteBitsWordBit;
  if (rowSpriteBitsWordBit < spriteWidth)
  {
    rowTransparentSpriteBits[rowSpriteBitsWord + 1] |= spritePixels << rowSpriteBitsWordBit;
  }
}


/* Function:  tmsTestRowBitsMask
 * ----------------------------------------
 * Test and update the row pixels bit mask.
 */
static inline uint32_t tmsTestRowBitsMask(VR_EMU_INST_ARG const uint32_t xPos, const uint32_t tilePixels, const uint32_t tileWidth, const bool update, const bool test, const bool testColl)
{
  uint32_t rowBitsWord = xPos >> 5;
  uint32_t rowBitsWordBit = xPos & 0x1f;

  uint32_t validPixels = tilePixels >> rowBitsWordBit;
  if (testColl) validPixels &= ~rowSpriteBits[rowBitsWord];
  if (test) validPixels &= ~rowBits[rowBitsWord];
  if (update) rowBits[rowBitsWord] |= validPixels;
  if (test || testColl) validPixels <<= rowBitsWordBit;
  
  rowBitsWordBit = 32 - rowBitsWordBit;
  if (rowBitsWordBit < tileWidth)
  {
    ++rowBitsWord;
    uint32_t right = (tilePixels << rowBitsWordBit);

    if (testColl) right &= ~rowSpriteBits[rowBitsWord];
    if (test) right &= ~rowBits[rowBitsWord];

    if (update) rowBits[rowBitsWord] |= right;
    if (test || testColl) validPixels |= (right >> rowBitsWordBit);
  }

  return (test || testColl) ? validPixels : tilePixels;
}


/* lookup for combining ecm nibbles, returning 4 pixels */
static uint32_t __aligned(8) ecmLookup[16 * 16 * 16];

static uint8_t ecmByte(bool h, bool m, bool l)
{
  return (h << 2) | (m << 1) | l;
}

/* lookup from bit planes: 333322221111 to merged palette values for four pixels
 * NOTE: The left-most pixel is stored in the least significant byte of the result
 *       because it's more efficient to offload them that way
 */
static void ecmLookupInit()
{
  for (uint16_t i = 0; i < 16 * 16 * 16; ++i)
  {
    ecmLookup[i] = (ecmByte(i & 0x800, i & 0x080, i & 0x008)) |
                   (ecmByte(i & 0x400, i & 0x040, i & 0x004) << 8) |
                   (ecmByte(i & 0x200, i & 0x020, i & 0x002) << 16) |
                   (ecmByte(i & 0x100, i & 0x010, i & 0x001) << 24);
  }
}

/* random note about how palettes are applied:
 * PR Address bit: 0 1 2 3 4 5
 * --------------------------------------
 * original mode: ps0 ps1 cs0 cs1 cs2 cs3
 * 1-bit (ECM1) : ps0 cs0 cs1 cs2 cs3 px0
 * 2-bit (ECM2) : cs0 cs1 cs2 cs3 px1 px0
 * 3-bit (ECM3) : cs0 cs1 cs2 px2 px1 px0
*/


/*
 * to generate the doubled pixels required when the sprite MAG flag is set,
 * use a lookup table. generate the doubledBits lookup table when we need it
 * using doubledBitsNibble.
 */
static uint8_t  __aligned(4) doubledBitsNibble[16] = {
  0x00, 0x03, 0x0c, 0x0f,
  0x30, 0x33, 0x3c, 0x3f,
  0xc0, 0xc3, 0xcc, 0xcf,
  0xf0, 0xf3, 0xfc, 0xff
};

/* lookup for doubling pixel patterns in mag mode */
VR_TMS_SECTION_SCRATCH_X(lookup) static uint16_t __aligned(4) doubledBits[256];
static void doubledBitsInit()
{
  for (int i = 0; i < 256; ++i)
  {
    doubledBits[i] = (doubledBitsNibble[(i & 0xf0) >> 4] << 8) | doubledBitsNibble[i & 0x0f];
  }
}

/* reversed bits in a byte */
VR_TMS_SECTION_SCRATCH_X(lookup) static uint8_t __aligned(4) reversedBits[256];

static uint8_t reverseBits(uint8_t byte) {
    byte = (byte & 0xf0) >> 4 | (byte & 0x0f) << 4;
    byte = (byte & 0xcc) >> 2 | (byte & 0x33) << 2;
    return (byte & 0xaa) >> 1 | (byte & 0x55) << 1;
}

static void reversedBitsInit()
{
  for (int i = 0; i < 256; ++i)
  {
    reversedBits[i] = reverseBits(i);
  }
}

/* a lookup to apply a 6-bit palette to 4 bytes of a uint32_t */
VR_TMS_SECTION_SCRATCH_X(lookup) static uint32_t __aligned(4) repeatedPalette[64];
static void repeatedPaletteInit()
{
  for (int i = 0; i < 64; ++i)
  {
    repeatedPalette[i] = (i << 24) | (i << 16) | (i << 8) | i;
  }
}

/* a lookup from a 4-bit mask to a word of 8-bit masks (reversed byte order) */
VR_TMS_SECTION_SCRATCH_X(lookup) static uint32_t __aligned(4) maskExpandNibbleToWordRev[16] =
{
  0x00000000, 0xff000000, 0x00ff0000, 0xffff0000,
  0x0000ff00, 0xff00ff00, 0x00ffff00, 0xffffff00,
  0x000000ff, 0xff0000ff, 0x00ff00ff, 0xffff00ff,
  0x0000ffff, 0xff00ffff, 0x00ffffff, 0xffffffff
};



bool lookupsReady = false;
void initLookups()
{
  if (lookupsReady) return;

  ecmLookupInit();
  doubledBitsInit();
  reversedBitsInit();
  repeatedPaletteInit();
  lookupsReady = true;
}

static inline void loadSpriteData(uint32_t *spriteBits, uint32_t pattOffset, uint32_t *pattMask, const uint32_t ecm, const uint32_t ecmOffset, const bool flipX, const bool sprite16)
{
  int i = 0;
  uint32_t patt;
  do  // do-while since behavior for ecm=0 and ecm==1 is the same
  {
    patt = tms9918->vram.bytes[pattOffset];
    if (flipX) patt = reversedBits[patt];
    spriteBits[i] = patt << ((flipX && sprite16) ? 16 : 24);
    if (sprite16)
    {
      patt = tms9918->vram.bytes[pattOffset + PATTERN_BYTES * 2];
      if (flipX) patt = reversedBits[patt];
      spriteBits[i] |= patt << (flipX ? 24 : 16);
    }
    *pattMask |= spriteBits[i];
    pattOffset += ecmOffset;
  } while (++i < ecm);
}


/* Function:  tmsApplyYScroll
 * ----------------------------------------
 * Apply vertical scroll register to a raw scanline Y coordinate.
 * Returns the scrolled Y; sets *swapPage when the scroll wraps past maxY
 * and the corresponding page-swap bit is set.
 */
static inline uint16_t tmsApplyYScroll(VR_EMU_INST_ARG uint16_t y, uint8_t scrollReg, uint8_t pageSwapMask, bool* swapPage)
{
  uint8_t scroll = TMS_REGISTER(tms9918, scrollReg);
  if (scroll)
  {
    int virtY = y + scroll;
    int maxY = (TMS_REGISTER(tms9918, 0x31) & 0x40) ? (8 * 30) : (8 * 24);
    if (virtY >= maxY)
    {
      virtY -= maxY;
      if (swapPage) *swapPage = (bool)(TMS_REGISTER(tms9918, 0x1d) & pageSwapMask);
    }
    return (uint16_t)virtY;
  }
  return y;
}

/* Function:  vrEmuTms9918OutputSprites
 * ----------------------------------------
 * Output Sprites to a scanline
 */
static inline uint8_t __time_critical_func(renderSprites)(VR_EMU_INST_ARG uint16_t y, const bool spriteMag, uint8_t pixels[TMS9918_PIXELS_X])
{
  const uint8_t spriteSize = tmsSpriteSize(tms9918);
  const bool sprite16 = spriteSize == 16;
  const uint8_t spriteIdxMask = sprite16 ? 0xfc : 0xff;
  const uint8_t spriteSizePx = spriteSize << spriteMag;
  const uint16_t spriteAttrTableAddr = tmsSpriteAttrTableAddr(tms9918);
  const uint16_t spritePatternAddr = tmsSpritePatternTableAddr(tms9918);
  const bool row30Mode = tms9918->isUnlocked && (TMS_REGISTER(tms9918, 0x31) & 0x40);
  const uint32_t maxY = row30Mode ? 0xf0 : 0xe0;
  uint32_t spritesShown = 0;
  uint8_t tempStatus = 0x1f;
  uint32_t transparentCount = 0;

  // ecm settings  
  const uint32_t ecm = tms9918->isUnlocked ? (TMS_REGISTER(tms9918, 0x31) & 0x03) : 0;
  const uint32_t ecmColorOffset = (ecm == 3) ? 2 : ecm;
  const uint32_t ecmColorMask = (ecm == 3) ? 0x0e : 0x0f;
  const uint32_t ecmOffset = 0x800 >> ((TMS_REGISTER(tms9918, 0x1d) & 0xc0) >> 6);

  uint8_t pal = 0;
  if (tms9918->isUnlocked) pal = TMS_REGISTER(tms9918, 0x18) & 0x30;
  if (ecm == 1)
  {
    pal &= 0x20;
  }
  else if (ecm)
  {
    pal = 0;
  }

  uint32_t maxSprites = TMS_REGISTER(tms9918, 0x33);
  if (maxSprites > MAX_SPRITES) maxSprites = MAX_SPRITES;

  const int32_t realY = (TMS_REGISTER(tms9918, 0x31) & 0x08) ? 0 : 1;

  uint8_t* spriteAttr = tms9918->vram.bytes + spriteAttrTableAddr;
  for (uint32_t spriteIdx = 0; spriteIdx < maxSprites; ++spriteIdx)
  {
    /* Kidd-proofing: not strictly correct, however some F18A games (lookin' at you, Kidd) 
       sometimes have all sprites enabled with all zeros and that hurts us :( */
    if (*(uint32_t*)spriteAttr == 0 && tms9918->isUnlocked)
    {
      spriteAttr += SPRITE_ATTR_BYTES;
      continue;
    }

    int32_t yPos = spriteAttr[SPRITE_ATTR_Y];

    /* stop processing when yPos == LAST_SPRITE_YPOS */
    if (yPos == LAST_SPRITE_YPOS && !row30Mode)
    {
      break;
    }

    /* first row is YPOS -1 (0xff). 2nd row is YPOS 0 */
    yPos += realY;

    /* check if sprite position is in the -31 to 0 range and move back to top */
    if (yPos > maxY)
      yPos -= 256;

    int32_t pattRow = y - yPos;
    if (pattRow < 0)
    {
      spriteAttr += SPRITE_ATTR_BYTES;
      continue;
    }

    pattRow >>= spriteMag;  // this needs to be a shift because -1 / 2 becomes 0. Bad.

    uint8_t thisSpriteSize = spriteSize;
    bool thisSprite16 = sprite16;
    uint8_t thisSpriteIdxMask = spriteIdxMask;
    uint8_t thisSpriteSizePx = spriteSizePx;
    uint8_t spriteAttrColor = spriteAttr[SPRITE_ATTR_COLOR];
    bool opaq = false;
    if (!tms9918->isUnlocked) spriteAttrColor &= 0x8f;

    if (spriteAttrColor & 0x10)
    {
      if (sprite16) 
      {
        // PICO9918-specific. If all sprites are 16px anyway, this bit is used to have opaque sprites
        opaq = true;
      }
      else
      {
        thisSpriteSize = 16;
        thisSprite16 = true;
        thisSpriteIdxMask = 0xfc;
        thisSpriteSizePx = thisSpriteSize << spriteMag;
      }
    }

    /* check if sprite is visible on this line */
    if (pattRow >= thisSpriteSize)
    {
      spriteAttr += SPRITE_ATTR_BYTES;
      continue;
    }

    /* have we exceeded the scanline sprite limit? */
    if (++spritesShown > MAX_SCANLINE_SPRITES)
    {
      if (((tempStatus & STATUS_5S) == 0) && 
          (!tms9918->isUnlocked || (TMS_REGISTER(tms9918, 0x32) & 0x08) == 0 || spritesShown > TMS_REGISTER(tms9918, 0x1e)))
      {
        tempStatus &= 0xe0;
        tempStatus |= STATUS_5S | spriteIdx;
      }

      if (spritesShown > TMS_REGISTER(tms9918, 0x1e))
        break;
    }

    const int32_t earlyClockOffset = (spriteAttrColor & 0x80) ? -32 : 0;
    int32_t xPos = (int32_t)(spriteAttr[SPRITE_ATTR_X]) + earlyClockOffset;
    if ((xPos > TMS9918_PIXELS_X) || (-xPos > thisSpriteSizePx))
    {
      spriteAttr += SPRITE_ATTR_BYTES;
      continue;
    }

    if (spriteAttrColor & 0x20) pattRow = thisSpriteSize - pattRow - 1; // flip Y?

    /* sprite is visible on this line */
    uint8_t spriteColor = (spriteAttrColor & ecmColorMask) << ecmColorOffset;
    const uint8_t pattIdx = spriteAttr[SPRITE_ATTR_NAME] & thisSpriteIdxMask;
    uint16_t pattOffset = spritePatternAddr + pattIdx * PATTERN_BYTES + (uint16_t)pattRow;


    /* create a 32-bit mask of this sprite's pixels
     * left-aligned, so the first pixel in the sprite is the
     * MSB of spriteBits
     */
    uint32_t pattMask = 0;
    uint32_t spriteBits[3] = {0}; // a 32-bit value for each ecm bit plane (also pushed far left)
    const bool flipX = spriteAttrColor & 0x40;

    if (flipX)
    {
      if (thisSprite16)
        loadSpriteData(spriteBits, pattOffset, &pattMask, ecm, ecmOffset, true, true);
      else
        loadSpriteData(spriteBits, pattOffset, &pattMask, ecm, ecmOffset, true, false);
    }
    else
    {
      if (thisSprite16)
      {
        loadSpriteData(spriteBits, pattOffset, &pattMask, ecm, ecmOffset, false, true);
      }
      else
      {
        loadSpriteData(spriteBits, pattOffset, &pattMask, ecm, ecmOffset, false, false);
      }
    }

    if (opaq) pattMask = 0xffff0000;

    /* bail early if no bits to draw */
    if (!pattMask)
    {
      spriteAttr += SPRITE_ATTR_BYTES;
      continue;
    }

    if (spriteMag)
    {
      pattMask = (doubledBits[pattMask >> 24] << 16) | doubledBits[(pattMask >> 16) & 0xff];
    }

    /* perform clipping operations */
    if (xPos < 0)
    {
      int32_t absX = -xPos;
      uint32_t offset = absX >> spriteMag;
      switch (ecm)
      {
        case 3:
          spriteBits[2] <<= offset;
          // fallthrough
        case 2:
          spriteBits[1] <<= offset;
          // fallthrough
        default:
          spriteBits[0] <<= offset;
      }
      pattMask <<= absX;
      
      /* bail early if no bits to draw */
      if (!pattMask)
      {
        spriteAttr += SPRITE_ATTR_BYTES;
        continue;
      }

      thisSpriteSizePx += xPos;
      xPos = 0;
    }
    int overflowPx = (xPos + thisSpriteSizePx) - TMS9918_PIXELS_X;
    if (overflowPx > 0)
    {
      thisSpriteSizePx -= overflowPx;
    }

    /* test and update the collision mask */
    uint32_t validPixels = tmsTestCollisionMask(VR_EMU_INST xPos, pattMask, thisSpriteSizePx);

    /* if the result is different, we collided */
    if (validPixels != pattMask)
    {
      tempStatus |= STATUS_COL;
    }

    // Render valid pixels to the scanline
    if (ecm || (spriteColor != TMS_TRANSPARENT))
    {
      tms9918->scanlineHasSprites = true;
      spriteColor |= pal;
      if (ecm)
      {

      /* Note: Again, I've made the choice to branch early for some of the sprite options
              to improve performance for each case (reduce branches in loops) */
        uint32_t quadPal = repeatedPalette[spriteColor];

        bool singlePix = spriteMag && thisSprite16;

         // 16px magnified is separate because it's harder. we only have 32 bits to play with, so to word align it, we need to go to 64 bits
        if (singlePix)
        {
          register uint32_t sb0 = spriteBits[0];
          register uint32_t sb1 = spriteBits[1];
          register uint32_t sb2 = spriteBits[2];

          while (validPixels)
          {
            /* output the sprite pixels 8 at a time (4x magnified pixels) */
            uint32_t chunkMask = validPixels >> 24;
            if (chunkMask)
            {
              uint32_t ecmIndex = 0;
              switch (ecm)
              {
                case 3:
                  ecmIndex = (sb2 >> 28) << 8;
                  // fallthrough
                case 2:
                  ecmIndex |= (sb1 >> 28) << 4;
                  // fallthrough
                default:
                  ecmIndex |= sb0 >> 28;
              }

              uint32_t color = ecmLookup[ecmIndex] | quadPal;

              uint8_t *p = pixels + xPos;
              if (chunkMask & 0x80) p[0] = color;
              if (chunkMask & 0x40) p[1] = color;
              color >>= 8;
              if (chunkMask & 0x20) p[2] = color;
              if (chunkMask & 0x10) p[3] = color;
              color >>= 8;
              if (chunkMask & 0x8) p[4] = color;
              if (chunkMask & 0x4) p[5] = color;
              color >>= 8;
              if (chunkMask & 0x2) p[6] = color;
              if (chunkMask & 0x1) p[7] = color;
            }
            sb2 <<= 4;
            sb1 <<= 4;
            sb0 <<= 4;
            validPixels <<= 8;
            xPos += 8;
          }
        }
        else  // regular ecm sprite (8 or 16px, non-magnified) or 8px magnified
        {
          if (spriteMag)
          {
            spriteBits[2] = doubledBits[spriteBits[2] >> 24] << 16;
            spriteBits[1] = doubledBits[spriteBits[1] >> 24] << 16;
            spriteBits[0] = doubledBits[spriteBits[0] >> 24] << 16;
          }

          // get him to be word aligned so we can smash out 4 pixels at a time
          uint32_t quadOffset = xPos >> 2;
          const uint32_t pixOffset = xPos & 0x3;
          validPixels >>= pixOffset;
          spriteBits[2] >>= pixOffset;
          spriteBits[1] >>= pixOffset;
          spriteBits[0] >>= pixOffset;

          uint32_t *quadPixels = (uint32_t*)pixels;

          while (validPixels)
          {
            /* output the sprite 4 pixels at a time */
            uint32_t chunkMask = validPixels >> 28;
            if (chunkMask)
            {
              uint32_t ecmIndex = 0;
              switch (ecm)
              {
                case 3:
                  ecmIndex |= (spriteBits[2] >> 28) << 8;
                  // fallthrough
                case 2:
                  ecmIndex |= (spriteBits[1] >> 28) << 4;
                  // fallthrough
                default:
                  ecmIndex |= spriteBits[0] >> 28;
              }

              uint32_t color = ecmLookup[ecmIndex] | quadPal;

              uint32_t maskQuad = maskExpandNibbleToWordRev[chunkMask];

              quadPixels[quadOffset] = (quadPixels[quadOffset] & ~maskQuad) | (color & maskQuad);
            }
            spriteBits[2] <<= 4;
            spriteBits[1] <<= 4;
            spriteBits[0] <<= 4;
            ++quadOffset;
            validPixels <<= 4;
          }
        }
      }
      else  // non-ecm single-color sprite
      {
        if (tmsCachedMode == TMS_MODE_TEXT80) spriteColor |= spriteColor << 4;

        while (validPixels)
        {
          if ((int32_t)validPixels < 0)
          {
            pixels[xPos] = spriteColor;
          }
          validPixels <<= 1;
          ++xPos;
        }          
      }
    }
    else
    {
      // keep track of the transparent sprites, because we want to remove them from the rowSpriteBits mask later
      if (!transparentCount)
      {
        for (int i = 0; i < 8; ++i) // clear rowTransparentSpriteBits only when we need it
        {
          rowTransparentSpriteBits[i] = 0;
        }
      }
      tmsSetTransparentSpriteMask(xPos, validPixels, thisSpriteSizePx);
      ++transparentCount;
    }

    spriteAttr += SPRITE_ATTR_BYTES;
  }

  // remove the transparent sprite pixels if there are any
  if (transparentCount)
  {
    for (int i = 0; i < 8; ++i)
    {
      rowSpriteBits[i] ^= rowTransparentSpriteBits[i];
    }
  }


  return tempStatus;
}

uint8_t __time_critical_func(vrEmuTms9918OutputSprites)(VR_EMU_INST_ONLY_ARG)
{
  const bool spriteMag = tmsSpriteMag(tms9918);
  uint16_t y = tms9918->scanCtx.y;
  uint8_t* pixels = tms9918->scanCtx.pixels;

  if (TMS_REGISTER(tms9918, 0) & R0_DOUBLE_ROWS)  // double rows (high-res)? still only have low-res sprites
    y >>= 1;

  if (spriteMag)
  {
    return renderSprites(VR_EMU_INST y, true, pixels);
  }
  else
  {
    return renderSprites(VR_EMU_INST y, false, pixels);
  }
}

/* =========================================================================
 * Display mode scanline renderers
 * =========================================================================
 *
 * Each file is #include'd directly (not a separate compilation unit) so that
 * the static helpers defined above remain accessible to the renderers.
 */

#include "modes/vrEmuTmsModeText.c"
#include "modes/vrEmuTmsModeGraphicsI.c"
#include "modes/vrEmuTmsModeGraphicsII.c"
#include "modes/vrEmuTmsModeMulticolor.c"

/* =========================================================================
 * Display mode pipeline vtable
 * =========================================================================
 *
 * Three parallel vtables, each indexed by vrEmuTms9918Mode:
 *   baseOpsTable_tms9918  — locked / standard TMS9918A behaviour
 *   baseOpsTable_v9938    — V9938 base (initially same; V9938 renderers slot in later)
 *   f18aOpsTable          — F18A-unlocked extensions
 *
 * Stage call order (NULL = skip):
 *   stage0 -> stage1 -> stage2 -> stage3 -> stage4
 */

#include "modes/vrEmuTmsModeOps.h"

/* -------------------------------------------------------------------------
 * Shared stage trampolines — small wrappers so multiple table entries can
 * share the same function pointer.
 * ------------------------------------------------------------------------- */

static uint8_t __time_critical_func(stageSpritesAbove)(VR_EMU_INST_ONLY_ARG)
{
  return vrEmuTms9918OutputSprites(VR_EMU_INST_ONLY);
}

static uint8_t __time_critical_func(stageSpritesBelow)(VR_EMU_INST_ONLY_ARG)
{
  return vrEmuTms9918OutputSprites(VR_EMU_INST_ONLY);
}

/* Graphics-II F18A stage4: sprites then bitmap (preserves current exact order) */
static uint8_t __time_critical_func(stageGfxIIF18ASpritesAndBitmap)(VR_EMU_INST_ONLY_ARG)
{
  uint8_t st = vrEmuTms9918OutputSprites(VR_EMU_INST_ONLY);
  vrEmuTms9918BitmapLayerScanLine(VR_EMU_INST_ONLY);
  return st;
}

/* Scanline-local flag: set by stageGfxIBitmapF18A, read by stageGfxITilesF18A.
 * Safe because both stages run on the same core within the same scanline call. */
static bool gfxIWriteMask = true;

/* Graphics-I F18A stage0: bitmap layer — captures writeMask for stage2 */
static uint8_t __time_critical_func(stageGfxIBitmapF18A)(VR_EMU_INST_ONLY_ARG)
{
  gfxIWriteMask = vrEmuTms9918BitmapLayerScanLine(VR_EMU_INST_ONLY);
  return 0;
}

/* Graphics-I F18A stage2: draws tile2 and tile1 only when writeMask was set */
static uint8_t __time_critical_func(stageGfxITilesF18A)(VR_EMU_INST_ONLY_ARG)
{
  if (gfxIWriteMask)
  {
    if (TMS_REGISTER(tms9918, 0x31) & 0x80) vrEmuF18ATile2ScanLine(VR_EMU_INST_ONLY);
    if (!(TMS_REGISTER(tms9918, 0x32) & 0x10)) vrEmuF18ATile1ScanLine(VR_EMU_INST_ONLY);
  }
  return 0;
}

/* -------------------------------------------------------------------------
 * Base table — locked / standard TMS9918A behaviour
 *
 * { stage0, stage1, stage2, stage3, stage4, name }
 * ------------------------------------------------------------------------- */
const VrEmuTmsDisplayModeOps baseOpsTable_tms9918[TMS_MODE_COUNT] = {
  /* Graphics-I  */ { NULL, NULL, vrEmuTms9918GraphicsIScanLine, NULL, stageSpritesAbove, "Graphics-I"  },
  /* Graphics-II */ { NULL, NULL, vrEmuTms9918GraphicsIIScanLine, NULL, stageSpritesAbove, "Graphics-II" },
  /* Text        */ { NULL, NULL, vrEmuTms9918TextScanLine,       NULL, NULL,              "Text"        },
  /* Multicolor  */ { NULL, NULL, vrEmuTms9918MulticolorScanLine, NULL, stageSpritesAbove, "Multicolor"  },
  /* Text80      */ { NULL, NULL, vrEmuTms9918Text80ScanLine,     NULL, NULL,              "Text80"      },
#if VR_EMU_TMS9918_MODE == VR_EMU_TMS9918_MODE_V9938
  /* V9938-G3..G7 — defined in vrEmuTmsV9938.c */
  vrTmsV9938BaseG3, vrTmsV9938BaseG4, vrTmsV9938BaseG5, vrTmsV9938BaseG6, vrTmsV9938BaseG7,
#endif
};

/* -------------------------------------------------------------------------
 * V9938 base table — identical to TMS9918A for now; V9938-specific renderers
 * slot in mode-by-mode once implemented.
 * ------------------------------------------------------------------------- */
const VrEmuTmsDisplayModeOps baseOpsTable_v9938[TMS_MODE_COUNT] = {
  /* Graphics-I  */ { NULL, NULL, vrEmuTms9918GraphicsIScanLine, NULL, stageSpritesAbove, "Graphics-I"  },
  /* Graphics-II */ { NULL, NULL, vrEmuTms9918GraphicsIIScanLine, NULL, stageSpritesAbove, "Graphics-II" },
  /* Text        */ { NULL, NULL, vrEmuTms9918TextScanLine,       NULL, NULL,              "Text"        },
  /* Multicolor  */ { NULL, NULL, vrEmuTms9918MulticolorScanLine, NULL, stageSpritesAbove, "Multicolor"  },
  /* Text80      */ { NULL, NULL, vrEmuTms9918Text80ScanLine,     NULL, NULL,              "Text80"      },
#if VR_EMU_TMS9918_MODE == VR_EMU_TMS9918_MODE_V9938
  /* V9938-G3..G7 — defined in vrEmuTmsV9938.c */
  vrTmsV9938V9938G3, vrTmsV9938V9938G4, vrTmsV9938V9938G5, vrTmsV9938V9938G6, vrTmsV9938V9938G7,
#endif
};

/* -------------------------------------------------------------------------
 * F18A table — unlocked extensions (preserves current exact per-mode behaviour)
 *
 * Graphics-I F18A pipeline: bitmap(stage0) -> sprites-below(stage1) ->
 *                           tiles+tile2(stage2) -> NULL -> NULL
 * Graphics-II F18A:         NULL -> NULL -> tiles -> NULL ->
 *                           sprites+bitmap (stage4 trampoline)
 * Text/MC/Text80 F18A:      NULL -> NULL -> tiles -> NULL -> sprites
 * ------------------------------------------------------------------------- */
const VrEmuTmsDisplayModeOps f18aOpsTable[TMS_MODE_COUNT] = {
  /* Graphics-I  */ { stageGfxIBitmapF18A, stageSpritesBelow, stageGfxITilesF18A,            NULL, NULL,                          "F18A-Graphics-I"  },
  /* Graphics-II */ { NULL,                NULL,               vrEmuTms9918GraphicsIIScanLine, NULL, stageGfxIIF18ASpritesAndBitmap, "F18A-Graphics-II" },
  /* Text        */ { NULL,                NULL,               vrEmuTms9918TextScanLine,       NULL, stageSpritesAbove,              "F18A-Text"        },
  /* Multicolor  */ { NULL,                NULL,               vrEmuTms9918MulticolorScanLine, NULL, stageSpritesAbove,              "F18A-Multicolor"  },
  /* Text80      */ { NULL,                NULL,               vrEmuTms9918Text80ScanLine,     NULL, stageSpritesAbove,              "F18A-Text80"      },
#if VR_EMU_TMS9918_MODE == VR_EMU_TMS9918_MODE_V9938
  /* V9938-G3..G7 — defined in vrEmuTmsV9938.c */
  vrTmsV9938F18AG3, vrTmsV9938F18AG4, vrTmsV9938F18AG5, vrTmsV9938F18AG6, vrTmsV9938F18AG7,
#endif
};

/* Active mode ops — swapped once when mode / unlock / vdpBase changes */
const VrEmuTmsDisplayModeOps* vrTmsActiveModeOps = &baseOpsTable_tms9918[TMS_MODE_GRAPHICS_I];

/* Function:  updateActiveOps
 * ----------------------------------------
 * Swap the active vtable entry based on current unlock and base state.
 * Called when: mode changes, isUnlocked changes, vdpBase changes.
 */
static void updateActiveOps(VrEmuTms9918* tms9918)
{
  if (tms9918->isUnlocked)
    vrTmsActiveModeOps = &f18aOpsTable[tmsCachedMode];
  else if (tms9918->vdpBase == VR_EMU_TMS9918_BASE_V9938)
    vrTmsActiveModeOps = &baseOpsTable_v9938[tmsCachedMode];
  else
    vrTmsActiveModeOps = &baseOpsTable_tms9918[tmsCachedMode];
}

/* Function:  vrEmuTms9918ScanLine
 * ----------------------------------------
 * generate a scanline
 */
VR_EMU_TMS9918_DLLEXPORT uint8_t __time_critical_func(vrEmuTms9918ScanLine)(VR_EMU_INST_ARG uint16_t y, uint8_t pixels[TMS9918_PIXELS_X])
{
  uint8_t tempStatus = 0;

  if (!lookupsReady)
  {
    initLookups();
    VR_TMS_FILL32_INIT(&vrTmsBg, TMS9918_PIXELS_X / 4);
  }

  vrEmuTms9918Mode currentMode = tmsMode(tms9918);
  if (currentMode != tmsCachedMode)
  {
    tmsCachedMode = currentMode;
    updateActiveOps(tms9918);
    tms9918->palDirty = 1;
  }

  /* clear the buffer with background color */
  vrTmsBg = repeatedPalette[tmsMainBgColor(tms9918)];
  if (tmsCachedMode == TMS_MODE_TEXT80) vrTmsBg |= vrTmsBg << 4;
  VR_TMS_FILL32_START(pixels);

  bool dispActive = (TMS_REGISTER(tms9918, TMS_REG_1) & TMS_R1_DISP_ACTIVE);

  if (dispActive)
  {
    for (int i = 0; i < TMS9918_PIXELS_X / 32; ++i)
    {
      rowSpriteBits[i] = 0;
      rowBits[i] = 0;
    }
    tms9918->scanlineHasSprites = false;

    /* populate scanCtx before pipeline */
    tms9918->scanCtx.pixels     = pixels;
    tms9918->scanCtx.y          = y;
    tms9918->scanCtx.y1         = y;
    tms9918->scanCtx.y2         = y;
    tms9918->scanCtx.swapY1Page = false;
    tms9918->scanCtx.swapY2Page = false;
    if (tms9918->isUnlocked)
    {
      tms9918->scanCtx.y1 = tmsApplyYScroll(VR_EMU_INST y, 0x1c, 0x01, &tms9918->scanCtx.swapY1Page);
      if (TMS_REGISTER(tms9918, 0x31) & 0x80)  /* T2 enabled */
        tms9918->scanCtx.y2 = tmsApplyYScroll(VR_EMU_INST y, 0x1a, 0x10, &tms9918->scanCtx.swapY2Page);
    }

    const VrEmuTmsDisplayModeOps* ops = vrTmsActiveModeOps;
    if (ops->stage0Fn) ops->stage0Fn(VR_EMU_INST_ONLY);
    if (ops->stage1Fn) tempStatus |= ops->stage1Fn(VR_EMU_INST_ONLY);
                       ops->stage2Fn(VR_EMU_INST_ONLY);   /* never NULL */
    if (ops->stage3Fn) ops->stage3Fn(VR_EMU_INST_ONLY);
    if (ops->stage4Fn) tempStatus |= ops->stage4Fn(VR_EMU_INST_ONLY);
  }

  return tempStatus;
}

/* Function:  vrEmuTms9918RegValue
 * ----------------------------------------
 * return a reigister value
 */
VR_EMU_TMS9918_DLLEXPORT
uint8_t __time_critical_func(vrEmuTms9918RegValue)(VR_EMU_INST_ARG vrEmuTms9918Register reg)
{
  return TMS_REGISTER(tms9918, reg & tms9918->lockedMask); // was 0x07
}

/* Function:  vrEmuTms9918WriteRegValue
 * ----------------------------------------
 * write a reigister value
 */
VR_EMU_TMS9918_DLLEXPORT
void __time_critical_func(vrEmuTms9918WriteRegValue)(VR_EMU_INST_ARG vrEmuTms9918Register reg, uint8_t value)
{
  if ((reg == (0x80 | 0x39)) && ((value & 0xfc) == 0x1c))
  {
    TMS_REGISTER(tms9918, 0x39) = 0x1c; // Allow this one through even when locked
    if (++tms9918->unlockCount == 2)
    {
      tms9918->unlockCount = 0;
      tms9918->isUnlocked = true;
      tms9918->lockedMask = 0x3f;
      TMS_REGISTER(tms9918, 0x1e) = MAX_SPRITES - 1; // Sprites to process
      updateActiveOps(tms9918);
    }
  }
  else
  {
    tms9918->unlockCount = 0;
    
    if ((reg & ~tms9918->lockedMask) != 0x80) return; //ignore higher registers when locked

    int regIndex = reg & tms9918->lockedMask; // was 0x07
    
    // Auto-lock if we're unlocked but register 0 is being written
    // This handles case where system resets without resetting VDP (common on ColecoVision)
    // Legitimate F18A code should unlock after writing R0, so this is safe
    if (false && tms9918->isUnlocked && regIndex == 0)
    {
      // Force re-lock to standard TMS9918 mode
      tms9918->isUnlocked = false;
      tms9918->lockedMask = 0x07;
      tms9918->unlockCount = 0;
      // Don't call vdpRegisterReset as that would reset all registers
      // Just ensure sprite limit is back to normal
      TMS_REGISTER(tms9918, 0x1e) = MAX_SPRITES - 1;
    }
    
    TMS_REGISTER(tms9918, regIndex) = value;

    if (regIndex < 0x0f) return;

    if ((regIndex == 0x37) || ((regIndex == 0x38) && ((value & 1) == 0)))
    {
      tms9918->gpuAddress = ((TMS_REGISTER(tms9918, 0x36) << 8) | TMS_REGISTER(tms9918, 0x37)) & 0xFFFE;
      if (regIndex == 0x37)
      {
        TMS_REGISTER(tms9918, 0x38) = 0;
        tms9918->restart = 1;
      }
    }
    else if ((regIndex == 0x38) && (value & 1))
    {
      tms9918->restart = 1;
    }
    else if ((regIndex == 0x3F)) // firmware update
    {
      // b7      : 0 = idle:   1 = execute
      // b6      : 0 = verify: 1 = write
      // b5 - b0 : address to read firmware data (256 byte boundaries)
      //           reads one UF2 frame (512 bytes)
      if (TMS_REGISTER(tms9918, 0x38) == 0)
      {
        TMS_STATUS(tms9918, 2) = 0x80; // set gpu processing flag
        tms9918->flash = 1;
      }
      else
      {
        TMS_STATUS(tms9918, 2) = 0x14; // error - busy
      }
    }
    else if (regIndex == 0x1e && value == 0)
    {
      TMS_REGISTER(tms9918, 0x1e) = MAX_SPRITES - 1;
    }
    else if ((regIndex == 0x32) && (value & 0x80))
    { // reset all registers?
      vdpRegisterReset(tms9918);
      updateActiveOps(tms9918);  /* isUnlocked just changed to false */

      // reset palette, etc as well?
      if (value & 0x40)
      {
        tms9918->configDirty = true;
      }
    }
    else if (regIndex == 0x0F)
    {
      uint8_t statReg = (value & 0x0f);
      TMS_STATUS(tms9918, 0x0F) = statReg;  // is this right? or should this be the read-ahead value?
      if (value & 0x40) tms9918->startTime = time_us_32();    // reset
      if (value & 0x20) tms9918->currentTime = time_us_32();  // snap      
      else if (value & 0x10) tms9918->startTime += (tms9918->stopTime - tms9918->startTime);
      else tms9918->currentTime = tms9918->stopTime = time_us_32();

      if (statReg > 3 && statReg < 12)
      {
        uint32_t elapsed = tms9918->currentTime - tms9918->startTime;
        div_t micro = div((int)elapsed, 1000);
        div_t milli = div(micro.quot, 1000);

        TMS_STATUS(tms9918, 0x06) = micro.rem & 0x0ff;
        TMS_STATUS(tms9918, 0x07) = micro.rem >> 8;
        TMS_STATUS(tms9918, 0x08) = milli.rem & 0x0ff;
        TMS_STATUS(tms9918, 0x09) = milli.rem >> 8;
        TMS_STATUS(tms9918, 0x0a) = milli.quot & 0x00ff;
        TMS_STATUS(tms9918, 0x0b) = milli.quot >> 8;
      }
    }
    else if (regIndex == 58)  // SR12 holds the value of the option in VR58 (options)
    {
      TMS_STATUS(tms9918, 12) = tms9918->config[TMS_REGISTER(tms9918, 58)];
    }
    else if (regIndex == 59 && TMS_REGISTER(tms9918, 58) >= 8)  // option number in reg 58, value in 59 (options)
    {
      uint8_t configIdx = TMS_REGISTER(tms9918, 58);
      tms9918->config[configIdx] = value;
      TMS_STATUS(tms9918, 12) = value;
      tms9918->configDirty = true;
      /* Update runtime vdpBase when CONF_VDP_BASE is written */
      if (configIdx == CONF_VDP_BASE)
      {
        tms9918->vdpBase = (value == VR_EMU_TMS9918_BASE_V9938)
                           ? VR_EMU_TMS9918_BASE_V9938 : VR_EMU_TMS9918_BASE_TMS9918;
        updateActiveOps(tms9918);
      }
    }
  }
}



/* Function:  vrEmuTms9918VramValue
 * ----------------------------------------
 * return a value from vram
 */
VR_EMU_TMS9918_DLLEXPORT
uint8_t __time_critical_func(vrEmuTms9918VramValue)(VR_EMU_INST_ARG uint16_t addr)
{
  return tms9918->vram.bytes[addr & VRAM_MASK];
}

/* Function:  vrEmuTms9918DisplayEnabled
  * ----------------------------------------
  * check BLANK flag
  */
VR_EMU_TMS9918_DLLEXPORT
bool __time_critical_func(vrEmuTms9918DisplayEnabled)(VR_EMU_INST_ONLY_ARG)
{
  return (TMS_REGISTER(tms9918, TMS_REG_1) & TMS_R1_DISP_ACTIVE);
}

/* Function:  vrEmuTms9918DisplayMode
  * --------------------
  * current display mode
  */
VR_EMU_TMS9918_DLLEXPORT
vrEmuTms9918Mode __time_critical_func(vrEmuTms9918DisplayMode)(VR_EMU_INST_ONLY_ARG)
{
  return tmsCachedMode;
}

/* Function:  vrEmuTms9918DefaultPalette
  * --------------------
  * a default palette value 0x0rgb
  */
VR_EMU_TMS9918_DLLEXPORT
uint16_t vrEmuTms9918DefaultPalette(int index)
{
  return defaultPalette[index & 0x3f];
}