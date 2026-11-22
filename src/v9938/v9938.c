/**
 * \file
 * \brief pico9918-core - V9938 base: the host ports, registers and status
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Behaviour follows the Yamaha V9938 Technical Data Book, and openMSX where the book
 * is silent.
 */

#include "impl/pico9918_priv.h"

#if PICO9918_V9938_BASE

#include <string.h>

/** \brief the address of expansion RAM on a build without it */
#define V9938_NO_ADDR 0xFFFFFFFFu

/** \brief the bits of R#0-R#23 a V9938 implements */
static const uint8_t regValueMask[24] = {
  0x7E, 0x7F, 0x7F, 0xFF, 0x3F, 0xFF, 0x3F, 0xFF, /* R#0-R#7 */
  0xFB, 0xBF, 0x07, 0x03, 0xFF, 0xFF, 0x07, 0x0F, /* R#8-R#15 */
  0x0F, 0xBF, 0xFF, 0xFF, 0x3F, 0x3F, 0x3F, 0xFF, /* R#16-R#23 */
};

/** TRAP: the TI-99/4A console writes R#2=F0, R#4=F9, R#5=86 and R#6=F8 at power-up; a TMS9918A
   ignores the high bits, a V9938 puts every table above 0x1C000 */
static const uint8_t tmsValueMask[7] = {0xFF, 0xFF, 0x0F, 0xFF, 0x07, 0x7F, 0x07};

/** \brief the data book's power-on palette, 0x0GRB at three bits a channel */
static const uint16_t v9938Palette[16] = {
  0x000, 0x000, 0x611, 0x733, 0x117, 0x327, 0x151, 0x627,
  0x171, 0x373, 0x661, 0x664, 0x411, 0x265, 0x555, 0x777,
};

/** \brief a 3-bit channel widened to 4 */
#define C3TO4(V) ((uint16_t)(((V) << 1) | ((V) >> 2)))

/** \brief a V9938 palette entry as PRAM holds it: 0x0RGB, big-endian, four bits a channel */
static inline uint16_t pramFromGrb(uint16_t grb)
{
  const uint16_t g = C3TO4((grb >> 8) & 7), r = C3TO4((grb >> 4) & 7), b = C3TO4(grb & 7);
  return (uint16_t)(r | (((g << 4) | b) << 8));
}

/** \brief the address of the next host access: R#14 above the 14-bit address, and expansion RAM under R#45 MXC */
uint32_t __time_critical_func(pico9918_v9938_cpu_addr)(PICO9918_INST_ONLY_ARG)
{
  uint32_t a = ((uint32_t)(TMS_REGISTER(tms9918, PICO9918_V9938_REG_VRAM_PAGE) & 7) << 14) |
               (tms9918->currentAddress & 0x3FFF);

  /* G6 and G7 interleave: even bytes in the first 64KB, odd in the second */
  if ((TMS_REGISTER(tms9918, TMS_REG_0) & 0x0A) == 0x0A) a = ((a << 16) | (a >> 1)) & 0x1FFFF;

  if (TMS_REGISTER(tms9918, PICO9918_V9938_REG_ARG) & PICO9918_V9938_R45_MXC)
  {
#if PICO9918_EXP_RAM
    a = 0x20000 | (a & 0xFFFF);
#else
    a = V9938_NO_ADDR;
#endif
  }
  return a;
}

/** \brief step the host address */
static inline void advance(PICO9918_INST_ONLY_ARG)
{
  tms9918->currentAddress = (tms9918->currentAddress + 1) & 0x3FFF;

  /* TRAP: the carry into R#14 is only in the bitmap and 80-column modes, which address 128KB */
  if (tms9918->currentAddress == 0 && (TMS_REGISTER(tms9918, TMS_REG_0) & 0x0C))
    TMS_REGISTER(tms9918, PICO9918_V9938_REG_VRAM_PAGE) =
      (uint8_t)((TMS_REGISTER(tms9918, PICO9918_V9938_REG_VRAM_PAGE) + 1) & 7);
}

/** \brief a port 0 write: VRAM at the host address, and the read-ahead buffer too */
void __time_critical_func(pico9918_v9938_write_data)(PICO9918_INST_ARG uint8_t data)
{
  tms9918->regWriteStage   = 0;
  tms9918->readAheadBuffer = data;

  const uint32_t a = pico9918_v9938_cpu_addr(PICO9918_INST_ONLY);
  if (a != V9938_NO_ADDR) tms9918->vram.bytes[a] = data;
  advance(PICO9918_INST_ONLY);
}

/** \brief load the read-ahead buffer from the host address and step it */
uint8_t __time_critical_func(pico9918_v9938_fetch)(PICO9918_INST_ONLY_ARG)
{
  tms9918->regWriteStage = 0;

  const uint32_t a         = pico9918_v9938_cpu_addr(PICO9918_INST_ONLY);
  tms9918->readAheadBuffer = (a != V9938_NO_ADDR) ? tms9918->vram.bytes[a] : 0xFF;
  advance(PICO9918_INST_ONLY);
  return tms9918->readAheadBuffer;
}

/** \brief set register \p reg, written through port 1 or indirectly through port 3 */
void __time_critical_func(pico9918_v9938_write_reg)(PICO9918_INST_ARG uint8_t reg, uint8_t value)
{
  if (reg == PICO9918_REG_UNLOCK)
  {
    /* the F18A's unlock register, so the F18A unlock still works on the V9938 base */
    pico9918_write_reg_value_impl(PICO9918_INST 0x80 | reg, value);
    return;
  }

  /* any other register write breaks an unlock sequence */
  tms9918->unlockCount = 0;

  pico9918_v9938_t* const v = &tms9918->v9938;

  /* the first write only a V9938 program makes switches R#0-R#6 to full width */
  if (!v->wide && (reg >= 8 || (reg == 0 && (value & 0x0C))))
  {
    v->wide = 1;
    memcpy(&TMS_REGISTER(tms9918, 0), v->raw, sizeof(v->raw));
  }

  /* R#32-R#46 are the command engine's */
  if (reg >= 32)
  {
    if (reg < 47)
    {
      TMS_REGISTER(tms9918, reg) = value;
      pico9918_v9938_cmd_write(PICO9918_INST reg, value);
    }
    return;
  }
  if (reg >= 24) return;

  value &= regValueMask[reg];

  /* still narrow: the full value waits in raw for the switch to wide */
  if (!v->wide && reg < sizeof(v->raw))
  {
    v->raw[reg] = value;
    value &= tmsValueMask[reg];
  }
  if (reg == PICO9918_V9938_REG_PAL_INDEX) v->palStage = 0;

  TMS_REGISTER(tms9918, reg) = value;
  if (reg == PICO9918_V9938_REG_BLINK_TIME) tms9918->v9938.blinkReload = 1;
  if (reg <= TMS_REG_1) pico9918_write_reconcile_int_impl(PICO9918_INST_ONLY);
}

/** \brief a port 1 write: the first byte waits, the second sets a register or the address */
void __time_critical_func(pico9918_v9938_write_ctrl)(PICO9918_INST_ARG uint8_t data)
{
  if (tms9918->regWriteStage == 0)
  {
    tms9918->regWriteStage0Value = data;
    tms9918->regWriteStage       = 1;
    return;
  }
  tms9918->regWriteStage = 0;

  /* 10RRRRRR writes register R, 11xxxxxx is ignored */
  if (data & 0x80)
  {
    if ((data & 0x40) == 0) pico9918_v9938_write_reg(PICO9918_INST data & 0x3F, tms9918->regWriteStage0Value);
    return;
  }

  /* 00 sets a read address and prefetches, 01 a write address */
  tms9918->currentAddress = (uint32_t)((data & 0x3F) << 8) | tms9918->regWriteStage0Value;
  if ((data & 0x40) == 0) pico9918_v9938_fetch(PICO9918_INST_ONLY);
}

/** \brief a port 2 write: two bytes set R#16's palette entry, 0RRR0BBB then 00000GGG */
void __time_critical_func(pico9918_v9938_write_palette)(PICO9918_INST_ARG uint8_t data)
{
  pico9918_v9938_t* const v = &tms9918->v9938;

  if (!v->palStage)
  {
    v->palLatch = data;
    v->palStage = 1;
    return;
  }
  v->palStage = 0;

  const uint8_t index = TMS_REGISTER(tms9918, PICO9918_V9938_REG_PAL_INDEX) & 0x0F;
  const uint16_t grb  = (uint16_t)(((data & 7) << 8) | (v->palLatch & 0x77));

  PICO9918_PRAM(tms9918)[index] = pramFromGrb(grb);

  /* R#16 steps, so a whole palette goes in one run */
  TMS_REGISTER(tms9918, PICO9918_V9938_REG_PAL_INDEX) = (uint8_t)((index + 1) & 0x0F);
  tms9918->palDirty                                   = 1;
}

/** \brief a port 3 write: the register R#17 names, which steps unless AII is set */
void __time_critical_func(pico9918_v9938_write_indirect)(PICO9918_INST_ARG uint8_t data)
{
  const uint8_t r = TMS_REGISTER(tms9918, PICO9918_V9938_REG_INDIRECT);

  pico9918_v9938_write_reg(PICO9918_INST r & 0x3F, data);
  if ((r & PICO9918_V9938_R17_AII) == 0)
    TMS_REGISTER(tms9918, PICO9918_V9938_REG_INDIRECT) = (uint8_t)((r + 1) & 0x3F);
}

/** \brief status register \p reg as the host would read it, without the read's side effects */
uint8_t __time_critical_func(pico9918_v9938_status_peek)(PICO9918_INST_ARG uint8_t reg)
{
  const pico9918_v9938_t* const v = &tms9918->v9938;

  switch (reg)
  {
  case 0: return TMS_STATUS(tms9918, PICO9918_SR_STATUS);
  case 1: return TMS_STATUS(tms9918, PICO9918_SR_IDENT) & PICO9918_SR1_HF;
  case 2:
  {
    /* bits 2 and 3 always read 1 */
    uint8_t s = (uint8_t)(0x0C | v->cmd.status | v->cmd.ce);
    if (PICO9918_SCANLINE(tms9918) >= pico9918_v_pixels_impl(PICO9918_INST_ONLY)) s |= PICO9918_V9938_S2_VR;
    if (v->hrToggle) s |= PICO9918_V9938_S2_HR;
    if (v->field) s |= PICO9918_V9938_S2_EO;
    return s;
  }
  case 3: return (uint8_t)v->collisionX;
  case 4: return (uint8_t)((v->collisionX >> 8) | 0xFE);
  case 5: return (uint8_t)v->collisionY;
  case 6: return (uint8_t)((v->collisionY >> 8) | 0xFC);
  case 7: return v->cmd.color;
  case 8: return (uint8_t)v->cmd.borderX;
  case 9: return (uint8_t)((v->cmd.borderX >> 8) | 0xFE);
  default: return 0xFF;
  }
}

/** \brief the side effects of the host reading status register \p reg */
void __time_critical_func(pico9918_v9938_status_read)(PICO9918_INST_ARG uint8_t reg)
{
  switch (reg)
  {

  /* HR is not tracked against the beam; flipping it per read ends a poll for either edge */
  case 2: tms9918->v9938.hrToggle ^= 1; break;
  case 5:
    tms9918->v9938.collisionX = 0;
    tms9918->v9938.collisionY = 0;
    break;
  case 7:
  case 9: pico9918_v9938_cmd_status_read(PICO9918_INST reg); break;
  default: break;
  }
}

/** \brief once per frame: flip the even/odd field and count down the R#13 page blink */
void pico9918_v9938_frame(PICO9918_INST_ONLY_ARG)
{
  pico9918_v9938_t* const v = &tms9918->v9938;

  v->field ^= 1;
  if (v->blinkReload)
  {
    v->blinkReload = 0;

    /* TRAP: R#13 is read after the flag clears, so a write racing this frame is never lost */
    const uint8_t r13 = TMS_REGISTER(tms9918, PICO9918_V9938_REG_BLINK_TIME);
    v->blinkOn        = (r13 & 0xF0) != 0;
    v->blinkCount     = ((r13 & 0xF0) && (r13 & 0x0F)) ? (uint8_t)((r13 >> 4) * 10) : 0;
  }

  /* R#13's high nibble is the on time, the low the off time, in units of 10 frames */
  else if (v->blinkCount && !--v->blinkCount)
  {
    const uint8_t r13 = TMS_REGISTER(tms9918, PICO9918_V9938_REG_BLINK_TIME);
    v->blinkOn ^= 1;
    v->blinkCount = (uint8_t)((v->blinkOn ? r13 >> 4 : r13 & 0x0F) * 10);
  }
}

/** \brief power-on state: registers clear but R#21 and R#22, and the data book's palette */
void pico9918_v9938_reset(PICO9918_INST_ONLY_ARG)
{
  memset(&tms9918->v9938, 0, sizeof(tms9918->v9938));
  memset(&TMS_REGISTER(tms9918, 0), 0, 24);
  memset(&TMS_REGISTER(tms9918, 32), 0, 15);
  TMS_REGISTER(tms9918, PICO9918_V9938_REG_BURST + 1) = 0x3B;
  TMS_REGISTER(tms9918, PICO9918_V9938_REG_BURST + 2) = 0x05;
  tms9918->currentAddress                             = 0;

  for (int i = 0; i < 16; ++i) PICO9918_PRAM(tms9918)[i] = pramFromGrb(v9938Palette[i]);
  tms9918->palDirty = 1;
}

PICO9918_DLLEXPORT
void pico9918_write_port(PICO9918_INST_ARG uint8_t port, uint8_t data)
{
  pico9918_write_port_impl(PICO9918_INST port, data);
}

PICO9918_DLLEXPORT
uint8_t pico9918_read_port(PICO9918_INST_ARG uint8_t port)
{
  /* ports 2 and 3 are write-only on a V9938 */
  if (port & 2)
  {
    if (PICO9918_V9938_ACTIVE(tms9918))
    {
      tms9918->regWriteStage = 0;
      return 0xFF;
    }
  }
  return (port & 1) ? pico9918_read_status_impl(PICO9918_INST_ONLY)
                    : pico9918_read_data_impl(PICO9918_INST_ONLY);
}

#endif
