/*
 * pico9918-core - V9938 base: the command engine
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Behaviour follows the Yamaha V9938 Technical Data Book, and openMSX where the book
 * is silent. Timing is not emulated: a command runs as fast as its caller steps it.
 */

#include "impl/pico9918_priv.h"

#if PICO9918_V9938_BASE

#define CMD_NO_ADDR 0xFFFFFFFFu

#define ARG_MAJ 0x01
#define ARG_EQ  0x02
#define ARG_DIX 0x04
#define ARG_DIY 0x08
#define ARG_MXS 0x10
#define ARG_MXD 0x20

#define REG_SX  32
#define REG_SY  34
#define REG_DY  38
#define REG_NY  42
#define REG_CLR 44

enum
{
  OP_POINT = 0x4,
  OP_PSET,
  OP_SRCH,
  OP_LINE,
  OP_LMMV,
  OP_LMMM,
  OP_LMCM,
  OP_LMMC,
  OP_HMMV,
  OP_HMMM,
  OP_YMMM,
  OP_HMMC,
};

#define IS_TRANSFER(OP) ((0x8C00u >> (OP)) & 1)

/* R#0's M5 M4 M3 for G4, G5, G6 and G7, with R#1's M1 and M2 clear */
static inline uint8_t bitmapMode(PICO9918_INST_ONLY_ARG)
{
  if (TMS_REGISTER(tms9918, TMS_REG_1) & 0x18) return 0;
  const uint8_t m = (TMS_REGISTER(tms9918, TMS_REG_0) >> 1) & 7;
  return ((0xB8 >> m) & 1) ? m : 0;
}

static inline uint32_t addrOf(const pico9918_v9938_cmd_t* c, uint32_t x, uint32_t y, bool ext)
{
  const uint32_t b = (x & c->xmask) >> c->pshift;
  if (!ext) return ((b & c->planar) << 16) | (b >> c->planar) | ((y & (1023u >> c->planar)) << 7);
#if PICO9918_EXP_RAM
  return 0x20000u | (b >> c->planar) | ((y & 511u) << 7);
#else
  return CMD_NO_ADDR;
#endif
}

static inline uint8_t get(PICO9918_INST_ARG uint32_t a)
{
  return (a == CMD_NO_ADDR) ? 0xFF : tms9918->vram.bytes[a];
}

static inline void put(PICO9918_INST_ARG uint32_t a, uint8_t value)
{
  if (a != CMD_NO_ADDR) tms9918->vram.bytes[a] = value;
}

static inline uint32_t pixelShift(const pico9918_v9938_cmd_t* c, uint32_t x)
{
  return (~x & ((1u << c->pshift) - 1)) << (3 - c->pshift);
}

static inline uint8_t point(PICO9918_INST_ARG const pico9918_v9938_cmd_t* c, uint32_t x, uint32_t y, bool ext)
{
  const uint32_t a = addrOf(c, x, y, ext);
  if (a == CMD_NO_ADDR) return 0xFF;
  return (uint8_t)((tms9918->vram.bytes[a] >> pixelShift(c, x)) & c->cmask);
}

static inline void pset(PICO9918_INST_ARG const pico9918_v9938_cmd_t* c, uint32_t x, uint32_t y, bool ext, uint8_t sc)
{
  const uint32_t a = addrOf(c, x, y, ext);
  if (a == CMD_NO_ADDR || ((c->logop & 8) && !sc)) return;

  const uint32_t sh = pixelShift(c, x);
  const uint8_t old = tms9918->vram.bytes[a];
  const uint8_t dc  = (uint8_t)(old >> sh);
  uint8_t r;
  switch (c->logop & 7)
  {
  case 0: r = sc; break;
  case 1: r = sc & dc; break;
  case 2: r = sc | dc; break;
  case 3: r = sc ^ dc; break;
  case 4: r = (uint8_t)~sc; break;
  default: return;
  }
  tms9918->vram.bytes[a] = (uint8_t)((old & ~(c->cmask << sh)) | ((r & c->cmask) << sh));
}

static inline void finish(PICO9918_INST_ARG pico9918_v9938_cmd_t* c)
{
  if (c->op)
  {
    const uint16_t ny = c->ny & 0x3FF;
    TMS_REGISTER(tms9918, REG_SY)     = (uint8_t)c->sy;
    TMS_REGISTER(tms9918, REG_SY + 1) = (uint8_t)((c->sy >> 8) & 3);
    TMS_REGISTER(tms9918, REG_DY)     = (uint8_t)c->dy;
    TMS_REGISTER(tms9918, REG_DY + 1) = (uint8_t)((c->dy >> 8) & 3);
    TMS_REGISTER(tms9918, REG_NY)     = (uint8_t)ny;
    TMS_REGISTER(tms9918, REG_NY + 1) = (uint8_t)(ny >> 8);
  }
  TMS_REGISTER(tms9918, PICO9918_V9938_REG_CMD) &= 0x0F;
  c->op = 0;
  c->status &= (uint8_t)~PICO9918_V9938_S2_CE;
}

static inline bool nextUnit(pico9918_v9938_cmd_t* c)
{
  c->asx = (uint16_t)(c->asx + c->xstep);
  c->adx = (uint16_t)(c->adx + c->xstep);
  if (--c->anx) return true;

  c->sy += c->systep;
  c->dy += c->dystep;
  --c->ny;
  c->asx = c->sx;
  c->adx = c->dx;
  c->anx = c->nx;
  return --c->lines != 0;
}

static inline bool lineUnit(PICO9918_INST_ARG pico9918_v9938_cmd_t* c)
{
  const uint16_t ppl = (uint16_t)(c->xmask + 1);

  pset(PICO9918_INST c, c->adx, (uint32_t)c->dy, c->arg & ARG_MXD, c->color & c->cmask);
  if (!(c->arg & ARG_MAJ))
  {
    c->adx = (uint16_t)(c->adx + c->xstep);
    if (c->anx++ == c->nx || (c->adx & ppl)) return false;
    if (c->asx < c->ny)
    {
      c->asx = (uint16_t)(c->asx + c->nx);
      c->dy += c->dystep;
      if (c->dy < 0) return false;
    }
    c->asx = (uint16_t)((c->asx - c->ny) & 1023);
    return true;
  }

  c->dy += c->dystep;
  if (c->dy < 0) return false;
  if (c->asx < c->ny)
  {
    c->asx = (uint16_t)(c->asx + c->nx);
    c->adx = (uint16_t)(c->adx + c->xstep);
  }
  c->asx = (uint16_t)((c->asx - c->ny) & 1023);
  return !(c->anx++ == c->nx || (c->adx & ppl));
}

static bool PICO9918_IN_FLASH_FUNC(unit)(PICO9918_INST_ARG pico9918_v9938_cmd_t* c)
{
  const bool mxs = c->arg & ARG_MXS, mxd = c->arg & ARG_MXD;

  switch (c->op)
  {
  case OP_SRCH:
  {
    const bool eq = (point(PICO9918_INST c, c->asx, (uint32_t)c->sy, mxd) == (c->color & c->cmask));
    if (eq == ((c->arg & ARG_EQ) != 0))
    {
      c->status |= PICO9918_V9938_S2_BD;
      return false;
    }
    c->asx      = (uint16_t)(c->asx + c->xstep);
    c->borderX  = c->asx;
    return !(c->asx & (c->xmask + 1));
  }
  case OP_LINE: return lineUnit(PICO9918_INST c);
  case OP_LMMV: pset(PICO9918_INST c, c->adx, (uint32_t)c->dy, mxd, c->color & c->cmask); break;
  case OP_LMMM:
    pset(PICO9918_INST c, c->adx, (uint32_t)c->dy, mxd, point(PICO9918_INST c, c->asx, (uint32_t)c->sy, mxs));
    break;
  case OP_HMMV: put(PICO9918_INST addrOf(c, c->adx, (uint32_t)c->dy, mxd), c->color); break;
  case OP_HMMM:
    put(PICO9918_INST addrOf(c, c->adx, (uint32_t)c->dy, mxd), get(PICO9918_INST addrOf(c, c->asx, (uint32_t)c->sy, mxs)));
    break;
  case OP_YMMM:
    put(PICO9918_INST addrOf(c, c->adx, (uint32_t)c->dy, mxd), get(PICO9918_INST addrOf(c, c->adx, (uint32_t)c->sy, mxd)));
    break;

  /* TRAP: the host bus IRQ can STOP or replace the command mid-step, and a transfer is the IRQ's to run */
  default: return IS_TRANSFER(c->op);
  }
  return nextUnit(c);
}

static bool __time_critical_func(transferUnit)(PICO9918_INST_ARG pico9918_v9938_cmd_t* c)
{
  switch (c->op)
  {
  case OP_LMCM: c->color = point(PICO9918_INST c, c->asx, (uint32_t)c->sy, c->arg & ARG_MXS); break;
  case OP_LMMC: pset(PICO9918_INST c, c->adx, (uint32_t)c->dy, c->arg & ARG_MXD, c->color & c->cmask); break;
  default: put(PICO9918_INST addrOf(c, c->adx, (uint32_t)c->dy, c->arg & ARG_MXD), c->color); break;
  }
  return nextUnit(c);
}

static inline uint16_t clipX(const pico9918_v9938_cmd_t* c, uint16_t a, uint16_t b, uint16_t n, uint8_t s)
{
  const uint16_t ppl = (uint16_t)((c->xmask + 1) >> s);

  a >>= s;
  b >>= s;
  n >>= s;
  if (a >= ppl || b >= ppl) return 1;
  if (!n) n = ppl;

  const uint16_t lim = (c->arg & ARG_DIX) ? (uint16_t)((a < b ? a : b) + 1) : (uint16_t)(ppl - (a > b ? a : b));
  return n < lim ? n : lim;
}

static inline uint16_t clipY(const pico9918_v9938_cmd_t* c, int32_t a, int32_t b, uint16_t n)
{
  if (!n) n = 1024;
  if (!(c->arg & ARG_DIY)) return n;

  const uint16_t lim = (uint16_t)((a < b ? a : b) + 1);
  return n < lim ? n : lim;
}

static PICO9918_NOINLINE void __time_critical_func(transferEvent)(PICO9918_INST_ARG pico9918_v9938_cmd_t* c)
{
  c->pending = 1;
  if (!IS_TRANSFER(c->op)) return;

  c->pending = 0;
  if (!bitmapMode(PICO9918_INST_ONLY) || !transferUnit(PICO9918_INST c)) finish(PICO9918_INST c);
}

static void __time_critical_func(start)(PICO9918_INST_ARG pico9918_v9938_cmd_t* c, uint8_t value)
{
  const uint8_t op   = value >> 4;
  const uint8_t mode = bitmapMode(PICO9918_INST_ONLY);

  if (op < OP_POINT || !mode)
  {
    finish(PICO9918_INST c);
    return;
  }

  switch (mode)
  {
  case 3: c->pshift = 1; c->planar = 0; c->xmask = 255; break;
  case 4: c->pshift = 2; c->planar = 0; c->xmask = 511; break;
  case 5: c->pshift = 1; c->planar = 1; c->xmask = 511; break;
  default: c->pshift = 0; c->planar = 1; c->xmask = 255; break;
  }
  c->cmask = (uint8_t)(0xFF >> (8 - (8 >> c->pshift)));

  const uint8_t* const r = &TMS_REGISTER(tms9918, REG_SX);
  c->sx    = (uint16_t)(r[0] | ((r[1] & 1) << 8));
  c->sy    = r[2] | ((r[3] & 3) << 8);
  c->dx    = (uint16_t)(r[4] | ((r[5] & 1) << 8));
  c->dy    = r[6] | ((r[7] & 3) << 8);
  c->nx    = (uint16_t)(r[8] | ((r[9] & 3) << 8));
  c->ny    = (uint16_t)(r[10] | ((r[11] & 3) << 8));
  c->arg   = r[13];
  c->logop = value & 0x0F;
  c->op    = op;
  c->status |= PICO9918_V9938_S2_CE;

  const int8_t ystep = (c->arg & ARG_DIY) ? -1 : 1;
  c->xstep  = (c->arg & ARG_DIX) ? -1 : 1;
  c->systep = 0;
  c->dystep = 0;
  c->asx    = c->sx;
  c->adx    = c->dx;

  switch (op)
  {
  case OP_POINT:
    c->color = point(PICO9918_INST c, c->sx, (uint32_t)c->sy, c->arg & ARG_MXS);
    finish(PICO9918_INST c);
    return;
  case OP_PSET:
    pset(PICO9918_INST c, c->dx, (uint32_t)c->dy, c->arg & ARG_MXD, c->color & c->cmask);
    finish(PICO9918_INST c);
    return;
  case OP_SRCH: c->borderX = c->sx; return;
  case OP_LINE:
    c->asx    = (uint16_t)((((uint32_t)c->nx - 1) >> 1) & 1023);
    c->anx    = 0;
    c->dystep = ystep;
    return;
  default: break;
  }

  const bool src = op == OP_LMMM || op == OP_LMCM || op == OP_HMMM || op == OP_YMMM;
  const bool two = op == OP_LMMM || op == OP_HMMM;
  const uint8_t s = (op >= OP_HMMV) ? c->pshift : 0;
  const uint16_t xa = (op == OP_LMCM) ? c->sx : c->dx;
  const int32_t ya  = (op == OP_LMCM) ? c->sy : c->dy;

  c->xstep  = (int8_t)(c->xstep * (1 << s));
  c->nx     = clipX(c, xa, two ? c->sx : xa, (op == OP_YMMM) ? 512 : c->nx, s);
  c->lines  = clipY(c, ya, src ? c->sy : ya, c->ny);
  c->anx    = c->nx;
  c->systep = src ? ystep : 0;
  c->dystep = (op != OP_LMCM) ? ystep : 0;

  if (IS_TRANSFER(op))
  {
    c->status |= PICO9918_V9938_S2_TR;
    if (op == OP_LMCM || c->pending) transferEvent(PICO9918_INST c);
  }
}

void __time_critical_func(pico9918_v9938_cmd_write)(PICO9918_INST_ARG uint8_t reg, uint8_t value)
{
  pico9918_v9938_cmd_t* const c = &tms9918->v9938.cmd;

  if (reg == REG_CLR)
  {
    c->color = value;
    if (!c->op) c->status &= (uint8_t)~PICO9918_V9938_S2_TR;
    transferEvent(PICO9918_INST c);
  }
  else if (reg == PICO9918_V9938_REG_CMD)
  {
    start(PICO9918_INST c, value);
  }
}

void __time_critical_func(pico9918_v9938_cmd_status_read)(PICO9918_INST_ARG uint8_t reg)
{
  pico9918_v9938_cmd_t* const c = &tms9918->v9938.cmd;

  if (reg == 9)
  {
    c->status &= (uint8_t)~PICO9918_V9938_S2_BD;
    return;
  }
  if (!c->op) c->status &= (uint8_t)~PICO9918_V9938_S2_TR;
  transferEvent(PICO9918_INST c);
}

/* TRAP: in flash, so only the core and thread that write flash may call this - core 0's GPU loop on the board */
PICO9918_DLLEXPORT
bool PICO9918_IN_FLASH_FUNC(pico9918_cmd_step)(PICO9918_INST_ARG uint32_t budget)
{
  pico9918_v9938_cmd_t* const c = &tms9918->v9938.cmd;

  if (!c->op) return false;
  if (IS_TRANSFER(c->op)) return true;
  if (!bitmapMode(PICO9918_INST_ONLY))
  {
    finish(PICO9918_INST c);
    return false;
  }
  while (budget--)
  {
    if (!unit(PICO9918_INST c))
    {
      finish(PICO9918_INST c);
      return false;
    }
  }
  return true;
}

#endif
