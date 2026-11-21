/**
 * \file
 * \brief pico9918-core - the V9938 command engine
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Every command driven through the host ports as an MSX would drive it, then checked
 * against VRAM. Expected values come from the V9938 Technical Data Book's page layout
 * and command descriptions, and from openMSX where the book is silent.
 */

#include "impl/pico9918_priv.h"
#include "pico9918_config.h"

#include <stdio.h>
#include <string.h>

#define MAJ 0x01
#define EQ  0x02
#define DIX 0x04
#define DIY 0x08
#define MXS 0x10
#define MXD 0x20

#define HMMC 0xF0
#define YMMM 0xE0
#define HMMM 0xD0
#define HMMV 0xC0
#define LMMC 0xB0
#define LMCM 0xA0
#define LMMM 0x90
#define LMMV 0x80
#define LINE 0x70
#define SRCH 0x60
#define PSET 0x50
#define POINT 0x40

#define IMP  0x0
#define AND  0x1
#define OR   0x2
#define EOR  0x3
#define NOT  0x4
#define TIMP 0x8
#define TAND 0x9
#define TOR  0xA
#define TEOR 0xB
#define TNOT 0xC

#define TR 0x80
#define BD 0x10
#define CE 0x01

static int failures;
static const char* scope = "";

static void check(const char* what, unsigned want, unsigned got)
{
  if (want == got) return;
  ++failures;
  printf("  FAIL %s %s: want %02x got %02x\n", scope, what, want, got);
}

#define V(A) (tms9918->vram.bytes[A])

static void reg(uint8_t r, uint8_t v)
{
  pico9918_write_port(PICO9918_INST 1, v);
  pico9918_write_port(PICO9918_INST 1, (uint8_t)(0x80 | r));
}

static bool step(uint32_t budget)
{
  tms9918->v9938.cmd.produced = tms9918->v9938.cmd.spent + PICO9918_V9938_FRAME_TICKS;
  return pico9918_cmd_step(PICO9918_INST budget);
}

static uint8_t status(uint8_t s)
{
  reg(15, s);
  const uint8_t v = pico9918_read_port(PICO9918_INST 1);
  reg(15, 0);
  return v;
}

static uint8_t s2(void)
{
  return status(2) & (TR | BD | CE);
}

static void setup(unsigned sx, unsigned sy, unsigned dx, unsigned dy, unsigned nx, unsigned ny, uint8_t clr,
                  uint8_t arg, uint8_t cmd)
{
  const uint8_t b[15] = {(uint8_t)sx, (uint8_t)(sx >> 8), (uint8_t)sy, (uint8_t)(sy >> 8), (uint8_t)dx,
                         (uint8_t)(dx >> 8), (uint8_t)dy, (uint8_t)(dy >> 8), (uint8_t)nx, (uint8_t)(nx >> 8),
                         (uint8_t)ny, (uint8_t)(ny >> 8), clr, arg, cmd};

  reg(17, 32);
  for (unsigned i = 0; i < sizeof(b); ++i) pico9918_write_port(PICO9918_INST 3, b[i]);
}

static void run(void)
{
  unsigned guard = 0;
  while (step(7))
  {
    if (++guard > 1000000)
    {
      check("runaway", 0, 1);
      return;
    }
  }
  check("CE after run", 0, s2() & CE);
}

static void cmd(unsigned sx, unsigned sy, unsigned dx, unsigned dy, unsigned nx, unsigned ny, uint8_t clr,
                uint8_t arg, uint8_t c)
{
  setup(sx, sy, dx, dy, nx, ny, clr, arg, c);
  run();
}

static unsigned regPair(uint8_t r)
{
  return TMS_REGISTER(tms9918, r) | (TMS_REGISTER(tms9918, r + 1) << 8);
}

typedef struct
{
  const char* name;
  uint8_t r0;
  uint8_t bpp;
  uint16_t width;
  bool planar;
} vmode_t;

static const vmode_t MODES[4] = {
  {"G4", 0x06, 4, 256, false},
  {"G5", 0x08, 2, 512, false},
  {"G6", 0x0A, 4, 512, true},
  {"G7", 0x0E, 8, 256, true},
};

static void setMode(const vmode_t* m)
{
  reg(0, m->r0);
  reg(1, 0x40);
}

static void clearVram(void)
{
  memset(tms9918->vram.bytes, 0, PICO9918_VRAM_BYTES);
}

static uint32_t physOf(const vmode_t* m, unsigned x, unsigned y)
{
  const uint32_t lin = y * (m->width * m->bpp / 8u) + x * m->bpp / 8u;
  return m->planar ? (((lin & 1) << 16) | (lin >> 1)) : lin;
}

static unsigned shiftOf(const vmode_t* m, unsigned x)
{
  const unsigned ppb = 8u / m->bpp;
  return (ppb - 1 - x % ppb) * m->bpp;
}

static uint8_t pixGet(const vmode_t* m, unsigned x, unsigned y)
{
  return (uint8_t)((V(physOf(m, x, y)) >> shiftOf(m, x)) & ((1u << m->bpp) - 1));
}

static void pixSet(const vmode_t* m, unsigned x, unsigned y, uint8_t c)
{
  const uint8_t mask = (uint8_t)(((1u << m->bpp) - 1) << shiftOf(m, x));
  const uint32_t a   = physOf(m, x, y);
  V(a)               = (uint8_t)((V(a) & ~mask) | ((c << shiftOf(m, x)) & mask));
}

static uint8_t byteToPix(const vmode_t* m, uint8_t b, unsigned x)
{
  return (uint8_t)((b >> shiftOf(m, x)) & ((1u << m->bpp) - 1));
}

static void everyMode(const vmode_t* m)
{
  const unsigned ppb = 8u / m->bpp;
  const uint8_t cm   = (uint8_t)((1u << m->bpp) - 1);
  char what[64];

  scope = m->name;
  clearVram();
  setMode(m);

  cmd(0, 0, 2 * ppb, 7, 3 * ppb, 2, 0xA5, 0, HMMV);
  for (unsigned y = 7; y <= 9; ++y)
    for (unsigned x = 2 * ppb - 1; x <= 5 * ppb; ++x)
    {
      const bool in = y < 9 && x >= 2 * ppb && x < 5 * ppb;
      snprintf(what, sizeof(what), "HMMV %u,%u", x, y);
      check(what, in ? byteToPix(m, 0xA5, x) : 0, pixGet(m, x, y));
    }
  check("HMMV DY", 9, regPair(38));
  check("HMMV NY", 0, regPair(42));
  check("HMMV CMR", 0, TMS_REGISTER(tms9918, 46));

  cmd(0, 0, 3, 20, 5, 3, 0x5B, 0, LMMV);
  for (unsigned y = 20; y <= 23; ++y)
    for (unsigned x = 2; x <= 8; ++x)
    {
      snprintf(what, sizeof(what), "LMMV %u,%u", x, y);
      check(what, (y < 23 && x >= 3 && x <= 7) ? (0x5B & cm) : 0, pixGet(m, x, y));
    }

  for (unsigned y = 50; y < 52; ++y)
    for (unsigned x = 0; x < m->width; ++x) pixSet(m, x, y, (uint8_t)((x * 7 + y) & cm));

  cmd(1, 50, 17, 60, 6, 2, 0, 0, LMMM | IMP);
  for (unsigned y = 60; y < 63; ++y)
    for (unsigned x = 16; x < 24; ++x)
    {
      const bool in = y < 62 && x >= 17 && x < 23;
      snprintf(what, sizeof(what), "LMMM %u,%u", x, y);
      check(what, in ? pixGet(m, x - 16, y - 10) : 0, pixGet(m, x, y));
    }
  check("LMMM SY", 52, regPair(34));
  check("LMMM DY", 62, regPair(38));

  cmd(ppb, 50, 10 * ppb, 70, 3 * ppb, 2, 0, 0, HMMM);
  for (unsigned y = 70; y < 73; ++y)
    for (unsigned x = 10 * ppb - 1; x <= 13 * ppb; ++x)
    {
      const bool in = y < 72 && x >= 10 * ppb && x < 13 * ppb;
      snprintf(what, sizeof(what), "HMMM %u,%u", x, y);
      check(what, in ? pixGet(m, x - 9 * ppb, y - 20) : 0, pixGet(m, x, y));
    }

  cmd(0, 50, 2 * ppb, 80, 1, 2, 0, 0, YMMM);
  for (unsigned y = 80; y < 83; ++y)
    for (unsigned x = 0; x < m->width; ++x)
    {
      const bool in = y < 82 && x >= 2 * ppb;
      snprintf(what, sizeof(what), "YMMM %u,%u", x, y);
      check(what, in ? pixGet(m, x, y - 30) : 0, pixGet(m, x, y));
    }
  check("YMMM SY", 52, regPair(34));
  check("YMMM DY", 82, regPair(38));

  setup(0, 0, 0, 90, 2 * ppb, 2, 0x11, 0, HMMC);
  check("HMMC start", TR | CE, s2());
  reg(44, 0x22);
  reg(44, 0x33);
  check("HMMC mid", TR | CE, s2());
  reg(44, 0x44);
  check("HMMC end", 0, s2() & CE);
  {
    const uint8_t data[4] = {0x11, 0x22, 0x33, 0x44};
    for (unsigned y = 90; y < 92; ++y)
      for (unsigned x = 0; x < 2 * ppb; ++x)
      {
        snprintf(what, sizeof(what), "HMMC %u,%u", x, y);
        check(what, byteToPix(m, data[(y - 90) * 2 + x / ppb], x), pixGet(m, x, y));
      }
  }
  check("HMMC DY", 92, regPair(38));

  setup(0, 0, 5, 95, 2, 2, 1, 0, LMMC);
  check("LMMC start", TR | CE, s2());
  reg(44, 2);
  reg(44, 3);
  reg(44, 4);
  check("LMMC end", 0, s2() & CE);
  check("LMMC 5,95", 1 & cm, pixGet(m, 5, 95));
  check("LMMC 6,95", 2 & cm, pixGet(m, 6, 95));
  check("LMMC 5,96", 3 & cm, pixGet(m, 5, 96));
  check("LMMC 6,96", 4 & cm, pixGet(m, 6, 96));
  check("LMMC 7,95", 0, pixGet(m, 7, 95));

  setup(5, 95, 0, 0, 2, 2, 0, 0, LMCM);
  for (unsigned i = 1; i <= 4; ++i)
  {
    snprintf(what, sizeof(what), "LMCM S#2 %u", i);
    check(what, TR | (i < 4 ? CE : 0), s2());
    snprintf(what, sizeof(what), "LMCM S#7 %u", i);
    check(what, i & cm, status(7));
  }
  check("LMCM TR after", 0, s2());
  check("LMCM SY", 97, regPair(34));

  cmd(0, 0, 10, 100, 7, 0, 0xFF, 0, LINE);
  for (unsigned x = 9; x <= 18; ++x)
  {
    snprintf(what, sizeof(what), "LINE %u", x);
    check(what, (x >= 10 && x <= 17) ? cm : 0, pixGet(m, x, 100));
  }
  check("LINE DY", 100, regPair(38));

  cmd(0, 0, m->width - 1u, 101, 0, 0, 0x03, 0, PSET);
  check("PSET", 3, pixGet(m, m->width - 1u, 101));
  cmd(m->width - 1u, 101, 0, 0, 0, 0, 0, 0, POINT);
  check("POINT", 3, status(7));

  cmd(0, 101, 0, 0, 0, 0, 3, EQ, SRCH);
  check("SRCH BD", BD, s2());
  check("SRCH S#8", (m->width - 1u) & 0xFF, status(8));
  check("SRCH S#9", ((m->width - 1u) >> 8) | 0xFE, status(9));
  check("SRCH BD read", 0, s2());
}

static void logicalOps(void)
{
  static const struct
  {
    uint8_t op, clr, want;
  } cases[] = {
    {IMP, 3, 0x3A},  {AND, 3, 0x1A},  {OR, 3, 0x7A},   {EOR, 3, 0x6A},   {NOT, 3, 0xCA},
    {IMP, 0xF3, 0x3A}, {AND, 0, 0x0A}, {NOT, 0, 0xFA},  {TIMP, 0, 0x5A},  {TAND, 0, 0x5A},
    {TOR, 0, 0x5A},  {TEOR, 0, 0x5A}, {TNOT, 0, 0x5A}, {TIMP, 3, 0x3A},  {TAND, 3, 0x1A},
    {TOR, 3, 0x7A},  {TEOR, 3, 0x6A}, {TNOT, 3, 0xCA}, {0x5, 3, 0x5A},   {0xF, 3, 0x5A},
  };
  char what[32];

  scope = "logop";
  clearVram();
  setMode(&MODES[0]);
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
  {
    V(0) = 0x5A;
    cmd(0, 0, 0, 0, 1, 1, cases[i].clr, 0, (uint8_t)(LMMV | cases[i].op));
    snprintf(what, sizeof(what), "LMMV #%u", i);
    check(what, cases[i].want, V(0));

    V(0) = 0x5A;
    cmd(0, 0, 0, 0, 0, 0, cases[i].clr, 0, (uint8_t)(PSET | cases[i].op));
    snprintf(what, sizeof(what), "PSET #%u", i);
    check(what, cases[i].want, V(0));

    V(0) = 0xA5;
    setup(0, 0, 1, 0, 1, 1, cases[i].clr, 0, (uint8_t)(LMMC | cases[i].op));
    snprintf(what, sizeof(what), "LMMC odd #%u", i);
    check(what, (uint8_t)((cases[i].want << 4) | (cases[i].want >> 4)), V(0));
    check("LMMC done", 0, s2() & CE);
  }

  V(0)   = 0x5A;
  V(128) = 0x03;
  cmd(1, 1, 0, 0, 1, 1, 0, 0, LMMM | EOR);
  check("LMMM EOR", 0x6A, V(0));
  V(128) = 0x00;
  cmd(1, 1, 0, 0, 1, 1, 0, 0, LMMM | TIMP);
  check("LMMM TIMP 0", 0x6A, V(0));
  cmd(1, 1, 0, 0, 1, 1, 0, 0, LMMM | IMP);
  check("LMMM IMP 0", 0x0A, V(0));

  setMode(&MODES[3]);
  V(0) = 0xF0;
  cmd(0, 0, 0, 0, 0, 0, 0x3C, 0, PSET | EOR);
  check("G7 PSET EOR", 0xCC, V(0));
  V(0x10000) = 0x0F;
  cmd(0, 0, 1, 0, 0, 0, 0x80, 0, PSET | OR);
  check("G7 PSET OR odd", 0x8F, V(0x10000));
}

static void g4Specifics(void)
{
  scope = "G4";
  clearVram();
  setMode(&MODES[0]);

  cmd(0, 0, 11, 5, 6, 2, 0xAB, 0, HMMV);
  check("HMMV odd DX left", 0, V(5 * 128 + 4));
  check("HMMV odd DX 5", 0xAB, V(5 * 128 + 5));
  check("HMMV odd DX 7", 0xAB, V(5 * 128 + 7));
  check("HMMV odd DX right", 0, V(5 * 128 + 8));
  check("HMMV line 6", 0xAB, V(6 * 128 + 7));

  cmd(0, 0, 20, 10, 4, 3, 0x77, DIX | DIY, HMMV);
  for (unsigned y = 7; y <= 11; ++y)
    for (unsigned b = 8; b <= 11; ++b)
      check("HMMV DIX DIY", (y >= 8 && y <= 10 && (b == 9 || b == 10)) ? 0x77 : 0, V(y * 128 + b));
  check("HMMV DIY DY", 7, regPair(38));

  cmd(0, 0, 0, 200, 0, 1, 0x12, 0, HMMV);
  check("HMMV NX=0 first", 0x12, V(200 * 128));
  check("HMMV NX=0 last", 0x12, V(200 * 128 + 127));
  check("HMMV NX=0 next line", 0, V(201 * 128));

  cmd(0, 0, 250, 202, 20, 1, 0x34, 0, HMMV);
  check("HMMV clip 124", 0, V(202 * 128 + 124));
  check("HMMV clip 125", 0x34, V(202 * 128 + 125));
  check("HMMV clip 127", 0x34, V(202 * 128 + 127));
  check("HMMV clip no wrap", 0, V(203 * 128));

  cmd(0, 0, 0, 0, 2, 0, 0x56, 0, HMMV);
  check("HMMV NY=0 line 0", 0x56, V(0));
  check("HMMV NY=0 line 1023", 0x56, V(1023 * 128));
  check("HMMV NY=0 col 1", 0, V(1023 * 128 + 1));
  check("HMMV NY=0 DY", 0, regPair(38));
  check("HMMV NY=0 NY", 0, regPair(42));

  clearVram();
  cmd(0, 0, 4, 3, 2, 0, 0x9C, DIY, HMMV);
  check("HMMV NY=0 DIY line 0", 0x9C, V(2));
  check("HMMV NY=0 DIY line 3", 0x9C, V(3 * 128 + 2));
  check("HMMV NY=0 DIY 1023", 0, V(1023 * 128 + 2));
  check("HMMV NY=0 DIY NY", 1020, regPair(42));

  clearVram();
  V(0) = 0x12;
  V(1) = 0x34;
  cmd(1, 0, 4, 2, 3, 1, 0, 0, LMMM);
  check("LMMM 2", 0x23, V(2 * 128 + 2));
  check("LMMM 3", 0x40, V(2 * 128 + 3));
  check("LMMM SY", 1, regPair(34));
  check("LMMM DY", 3, regPair(38));
  cmd(3, 0, 10, 2, 3, 1, 0, DIX, LMMM);
  check("LMMM DIX 4", 0x23, V(2 * 128 + 4));
  check("LMMM DIX 5", 0x40, V(2 * 128 + 5));

  clearVram();
  for (unsigned b = 0; b < 128; ++b) V(b) = (uint8_t)(b + 1);
  cmd(0, 0, 4, 10, 99, 1, 0, 0, YMMM);
  check("YMMM 1", 0, V(10 * 128 + 1));
  check("YMMM 2", 3, V(10 * 128 + 2));
  check("YMMM 127", 128, V(10 * 128 + 127));
  check("YMMM SY", 1, regPair(34));
  check("YMMM DY", 11, regPair(38));
  cmd(0, 0, 4, 12, 0, 1, 0, DIX, YMMM);
  check("YMMM DIX 0", 1, V(12 * 128));
  check("YMMM DIX 2", 3, V(12 * 128 + 2));
  check("YMMM DIX 3", 0, V(12 * 128 + 3));

  clearVram();
  V(0)   = 0x12;
  V(1)   = 0x34;
  V(128) = 0x56;
  V(129) = 0x78;
  setup(0, 0, 0, 0, 3, 2, 0, 0, LMCM);
  {
    static const uint8_t want[6] = {1, 2, 3, 5, 6, 7};
    for (unsigned i = 0; i < 6; ++i)
    {
      check("LMCM S#2", TR | (i < 5 ? CE : 0), s2());
      check("LMCM S#7", want[i], status(7));
    }
  }
  check("LMCM TR cleared", 0, s2());
  check("LMCM SY", 2, regPair(34));
  check("LMCM NY", 0, regPair(42));
  check("LMCM CMR", 0, TMS_REGISTER(tms9918, 46));

  clearVram();
  setup(0, 0, 1, 0, 3, 1, 7, 0, LMMC);
  check("LMMC first", 0x07, V(0));
  reg(44, 8);
  reg(44, 9);
  check("LMMC 0", 0x07, V(0));
  check("LMMC 1", 0x89, V(1));
  check("LMMC done", 0, s2() & CE);

  clearVram();
  cmd(0, 0, 0, 0, 4, 2, 0x0F, 0, LINE);
  check("LINE x 0", 0xF0, V(0));
  check("LINE x 1", 0x0F, V(128));
  check("LINE x 2", 0xF0, V(129));
  check("LINE x 3", 0x0F, V(256 + 1));
  check("LINE x 4", 0xF0, V(256 + 2));
  check("LINE x row0", 0, V(1));
  check("LINE x row2", 0, V(256));
  check("LINE x DY", 2, regPair(38));

  clearVram();
  cmd(0, 0, 10, 20, 3, 1, 0x0F, MAJ | DIX | DIY, LINE);
  check("LINE y 20", 0xF0, V(20 * 128 + 5));
  check("LINE y 19", 0xF0, V(19 * 128 + 5));
  check("LINE y 18", 0x0F, V(18 * 128 + 4));
  check("LINE y 17", 0x0F, V(17 * 128 + 4));
  check("LINE y 16", 0, V(16 * 128 + 4) | V(16 * 128 + 5));
  check("LINE y DY", 16, regPair(38));

  clearVram();
  cmd(0, 0, 254, 0, 10, 0, 0x01, 0, LINE);
  check("LINE edge", 0x11, V(127));
  check("LINE edge no wrap", 0, V(0));

  clearVram();
  cmd(0, 0, 4, 1, 5, 0, 0x01, MAJ | DIY, LINE);
  check("LINE top 1", 0x10, V(128 + 2));
  check("LINE top 0", 0x10, V(2));
  check("LINE top 1023", 0, V(1023 * 128 + 2));

  clearVram();
  V(3 * 128 + 3) = 0x05;
  cmd(2, 3, 0, 0, 0, 0, 5, EQ, SRCH);
  check("SRCH eq BD", BD, s2());
  check("SRCH eq S#8", 7, status(8));
  check("SRCH eq S#9", 0xFE, status(9));
  check("SRCH BD cleared", 0, s2());

  cmd(2, 3, 0, 0, 0, 0, 0, 0, SRCH);
  check("SRCH ne BD", BD, s2());
  check("SRCH ne S#8", 7, status(8));
  status(9);

  cmd(2, 3, 0, 0, 0, 0, 9, EQ, SRCH);
  check("SRCH miss BD", 0, s2());
  check("SRCH miss S#8", 0x00, status(8));
  check("SRCH miss S#9", 0xFF, status(9));

  cmd(5, 3, 0, 0, 0, 0, 9, EQ | DIX, SRCH);
  check("SRCH miss left BD", 0, s2());
  check("SRCH miss left S#8", 0xFF, status(8));
  check("SRCH miss left S#9", 0xFF, status(9));

  cmd(9, 3, 0, 0, 0, 0, 5, EQ | DIX, SRCH);
  check("SRCH left BD", BD, s2());
  check("SRCH left S#8", 7, status(8));
  status(9);

  V(0) = 0x5A;
  cmd(1, 0, 0, 0, 0, 0, 0, 0, POINT);
  check("POINT S#7", 0x0A, status(7));
  check("POINT CE", 0, s2() & CE);
}

static void g5Specifics(void)
{
  scope = "G5";
  clearVram();
  setMode(&MODES[1]);

  V(0) = 0xE4;
  cmd(1, 0, 0, 0, 0, 0, 0, 0, POINT);
  check("POINT 1", 2, status(7));
  cmd(3, 0, 0, 0, 0, 0, 0, 0, POINT);
  check("POINT 3", 0, status(7));
  cmd(0, 0, 3, 0, 0, 0, 2, 0, PSET | OR);
  check("PSET OR", 0xE6, V(0));

  cmd(0, 0, 0, 2, 3, 1, 0x5D, 0, HMMV);
  check("HMMV NX<4 line", 0x5D, V(2 * 128 + 127));
  check("HMMV NX<4 next", 0, V(3 * 128));

  cmd(0, 0, 510, 4, 0, 1, 0x02, 0, LMMV);
  check("LMMV NX=0 clip", 0x0A, V(4 * 128 + 127));
  check("LMMV NX=0 left", 0, V(4 * 128 + 126));

  setup(0, 0, 8, 1, 8, 2, 0x11, 0, HMMC);
  reg(44, 0x22);
  reg(44, 0x33);
  reg(44, 0x44);
  check("HMMC 1", 0x11, V(128 + 2));
  check("HMMC 2", 0x22, V(128 + 3));
  check("HMMC 3", 0x33, V(256 + 2));
  check("HMMC 4", 0x44, V(256 + 3));
  check("HMMC done", 0, s2() & CE);
}

static void planar(void)
{
  scope = "G7";
  clearVram();
  setMode(&MODES[3]);

  for (unsigned x = 0; x < 8; ++x) V(((x & 1) << 16) | (x >> 1)) = (uint8_t)(0x10 + x);
  cmd(0, 0, 100, 3, 4, 1, 0, 0, HMMM);
  check("HMMM 100", 0x10, V(434));
  check("HMMM 101", 0x11, V(0x10000 + 434));
  check("HMMM 102", 0x12, V(435));
  check("HMMM 103", 0x13, V(0x10000 + 435));
  check("HMMM 104", 0, V(436));

  cmd(3, 0, 0, 0, 0, 0, 0, 0, POINT);
  check("POINT odd", 0x13, status(7));

  cmd(0, 0, 0, 511, 2, 2, 0x66, 0, HMMV);
  check("HMMV 511", 0x66, V(511 * 128));
  check("HMMV 511 odd", 0x66, V(0x10000 + 511 * 128));
  check("HMMV wraps to 0", 0x66, V(0));
  check("HMMV wrap DY", 513, regPair(38));

  scope = "G6";
  clearVram();
  setMode(&MODES[2]);
  cmd(0, 0, 5, 2, 4, 1, 0x0C, 0, LMMV);
  check("LMMV 5", 0x0C, V(257));
  check("LMMV 6,7", 0xCC, V(0x10000 + 257));
  check("LMMV 8", 0xC0, V(258));

  V(0x10000 + 257) = 0x9E;
  cmd(7, 2, 0, 0, 0, 0, 0, 0, POINT);
  check("POINT 7", 0x0E, status(7));
  cmd(6, 2, 0, 0, 0, 0, 0, 0, POINT);
  check("POINT 6", 0x09, status(7));
}

static void expansion(void)
{
  scope = "exp";
  clearVram();
  setMode(&MODES[0]);

#if PICO9918_EXP_RAM
  V(0x20000) = 0xAB;
  cmd(0, 0, 0, 1, 2, 1, 0, MXS, HMMM);
  check("HMMM MXS", 0xAB, V(128));
  cmd(0, 1, 2, 0, 2, 1, 0, MXD, HMMM);
  check("HMMM MXD", 0xAB, V(0x20000 + 1));
  check("HMMM MXD not main", 0, V(1));

  cmd(0, 0, 0, 600, 2, 1, 0x3C, MXD, HMMV);
  check("MXD wraps 512", 0x3C, V(0x20000 + 88 * 128));
  check("MXD main untouched", 0, V(600 * 128));

  V(0x20000 + 5 * 128 + 4) = 0x07;
  cmd(0, 5, 0, 0, 0, 0, 7, EQ | MXD, SRCH);
  check("SRCH MXD BD", BD, s2());
  check("SRCH MXD S#8", 9, status(8));
  status(9);
  cmd(0, 5, 0, 0, 0, 0, 7, EQ | MXS, SRCH);
  check("SRCH MXS is main", 0, s2());

  cmd(9, 5, 0, 0, 0, 0, 0, MXS, POINT);
  check("POINT MXS", 7, status(7));

  setMode(&MODES[3]);
  cmd(0, 0, 3, 1, 0, 0, 0x5E, MXD, PSET);
  check("G7 PSET MXD", 0x5E, V(0x20081));
  check("G7 PSET MXD main", 0, V(0x10081));
#else
  cmd(0, 0, 0, 0, 0, 0, 0, MXS, POINT);
  check("POINT no exp", 0xFF, status(7));
  cmd(0, 0, 0, 0, 2, 1, 0x3C, MXD, HMMV);
  check("HMMV no exp", 0, V(0));
#endif
}

static void control(void)
{
  scope = "control";
  clearVram();
  setMode(&MODES[0]);

  setup(0, 0, 0, 0, 0, 0, 0x11, 0, HMMV);
  check("HMMV running", CE, s2() & CE);
  step(300);
  reg(46, 0);
  check("STOP CE", 0, s2() & CE);
  check("STOP step", 0, step(1));
  check("STOP wrote", 0x11, V(128 + 127));
  check("STOP stopped", 0, V(3 * 128));
  check("STOP DY", 2, regPair(38));
  check("STOP NY", 1022, regPair(42));

  setup(0, 0, 0, 0, 2, 1, 0x22, 0, 0x10);
  check("invalid", 0, s2() & CE);
  check("invalid wrote", 0x11, V(0));

  setup(0, 0, 0, 0, 2, 2, 0x55, 0, HMMC);
  check("HMMC wait", 0x55, V(0));
  check("HMMC step waits", 1, step(100));
  check("HMMC no extra", 0x11, V(128));
  reg(46, 0);

  clearVram();
  reg(0, 0x00);
  reg(1, 0x50);
  setup(0, 0, 0, 0, 2, 1, 0x77, 0, HMMV);
  check("text CE", 0, s2() & CE);
  step(100);
  check("text no write", 0, V(0));

  setMode(&MODES[0]);
  setup(0, 0, 0, 0, 0, 4, 0x77, 0, HMMV);
  reg(0, 0x00);
  check("mode change aborts", 0, step(100));
  check("mode change CE", 0, s2() & CE);
}

static void tiBoot(void)
{
  static const uint8_t console[8] = {0x00, 0x20, 0xF0, 0x0E, 0xF9, 0x86, 0xF8, 0xF7};

  scope = "ti boot";
  for (uint8_t r = 0; r < 8; ++r) reg(r, console[r]);
  check("R#2 narrow", 0x00, TMS_REGISTER(tms9918, 2));
  check("R#4 narrow", 0x01, TMS_REGISTER(tms9918, 4));
  check("R#5 narrow", 0x06, TMS_REGISTER(tms9918, 5));
  check("R#6 narrow", 0x00, TMS_REGISTER(tms9918, 6));

  reg(15, 0);
  check("R#2 wide", 0x70, TMS_REGISTER(tms9918, 2));
  check("R#4 wide", 0x39, TMS_REGISTER(tms9918, 4));
  check("R#5 wide", 0x86, TMS_REGISTER(tms9918, 5));
  check("R#6 wide", 0x38, TMS_REGISTER(tms9918, 6));
}

int main(void)
{
  pico9918_init();
  pico9918_config(PICO9918_INST_ONLY)[PICO9918_CONF_VDP_BASE] = PICO9918_BASE_V9938;
  pico9918_config_apply_now(PICO9918_INST true);
  if (tms9918->vdpBase != PICO9918_BASE_V9938)
  {
    printf("FAIL: the V9938 base did not apply\n");
    return 1;
  }

  tiBoot();
  for (unsigned i = 0; i < 4; ++i) everyMode(&MODES[i]);
  logicalOps();
  g4Specifics();
  g5Specifics();
  planar();
  expansion();
  control();

  if (failures)
  {
    printf("v9938cmd: %d failure(s)\n", failures);
    return 1;
  }
  printf("v9938cmd: all passed\n");
  return 0;
}
