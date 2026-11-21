/**
 * \file
 * \brief pico9918-core - V9938 sprites
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Tables and registers are written through the host ports as an MSX would write them,
 * then single scanlines are rendered and checked pixel by pixel. Expected values come from
 * the V9938 Technical Data Book's sprite chapter, and from openMSX where the book is silent.
 * A sprite at Y shows its first row on display line Y + 1.
 */

#include "impl/pico9918_priv.h"
#include "pico9918_config.h"
#include "pico9918_frame.h"

#include <stdio.h>
#include <string.h>

#define G1 0x00
#define G2 0x02
#define G3 0x04
#define G4 0x06
#define G5 0x08
#define G6 0x0A
#define G7 0x0E

#define ATTR2  0x1F600u
#define COLOR2 0x1F400u
#define PATT2  0x1F800u
#define ATTR1  0x11B00u
#define PATT1  0x11800u

#define EC 0x80
#define CC 0x40
#define IC 0x20

static int failures;
static const char* scope = "";
static uint8_t lineStatus;

static void check(const char* what, unsigned want, unsigned got)
{
  if (want == got) return;
  ++failures;
  printf("  FAIL %s %s: want %02x got %02x\n", scope, what, want, got);
}

static void checkAt(const char* what, unsigned y, unsigned x, unsigned want, unsigned got)
{
  char buf[80];
  snprintf(buf, sizeof(buf), "%s y%u x%u", what, y, x);
  check(buf, want, got);
}

static void reg(uint8_t r, uint8_t v)
{
  pico9918_write_port(PICO9918_INST 1, v);
  pico9918_write_port(PICO9918_INST 1, (uint8_t)(0x80 | r));
}

static uint8_t status(uint8_t s)
{
  reg(15, s);
  const uint8_t v = pico9918_read_port(PICO9918_INST 1);
  reg(15, 0);
  return v;
}

static void vpoke(uint32_t a, uint8_t d)
{
  reg(14, (uint8_t)(a >> 14));
  pico9918_write_port(PICO9918_INST 1, (uint8_t)a);
  pico9918_write_port(PICO9918_INST 1, (uint8_t)(0x40 | ((a >> 8) & 0x3F)));
  pico9918_write_port(PICO9918_INST 0, d);
}

static const uint8_t* line(unsigned y)
{
  lineStatus = pico9918_scan_line(PICO9918_INST (uint16_t)y);
  return pico9918_line_source(PICO9918_INST_ONLY);
}

static void setup(const char* name, uint8_t r0, uint8_t r1)
{
  scope = name;
  memset(tms9918->vram.bytes, 0, PICO9918_VRAM_BYTES);
  reg(0, r0);
  reg(1, (uint8_t)(0x40 | r1));
  reg(2, 0x1F);
  reg(7, 0);
  reg(8, 0);
  reg(23, 0);
  status(0);
  status(5);
  if (r0 & 0x0C)
  {
    reg(5, 0xEF);
    reg(11, 3);
    reg(6, 0x3F);
  }
  else
  {
    reg(5, 0x36);
    reg(11, 2);
    reg(6, 0x23);
  }
}

static void spr2(unsigned n, uint8_t y, uint8_t x, uint8_t pat, uint8_t color)
{
  vpoke(ATTR2 + 4 * n, y);
  vpoke(ATTR2 + 4 * n + 1, x);
  vpoke(ATTR2 + 4 * n + 2, pat);
  for (unsigned r = 0; r < 16; ++r) vpoke(COLOR2 + 16 * n + r, color);
}

static void end2(unsigned n)
{
  vpoke(ATTR2 + 4 * n, 216);
}

static void patt2(unsigned idx, unsigned row, uint8_t bits)
{
  vpoke(PATT2 + 8 * idx + row, bits);
}

static void spr1(unsigned n, uint8_t y, uint8_t x, uint8_t pat, uint8_t color)
{
  vpoke(ATTR1 + 4 * n, y);
  vpoke(ATTR1 + 4 * n + 1, x);
  vpoke(ATTR1 + 4 * n + 2, pat);
  vpoke(ATTR1 + 4 * n + 3, color);
}

static void patt1(unsigned idx, unsigned row, uint8_t bits)
{
  vpoke(PATT1 + 8 * idx + row, bits);
}

static void span(const char* what, const uint8_t* l, unsigned y, unsigned x0, unsigned x1, unsigned want)
{
  for (unsigned x = x0; x <= x1; ++x) checkAt(what, y, x, want, l[x]);
}

static void perLineColour(void)
{
  setup("G4 colour table", G4, 0);
  spr2(0, 9, 20, 0, 0);
  for (unsigned r = 0; r < 8; ++r)
  {
    vpoke(COLOR2 + r, (uint8_t)(1 + r));
    patt2(0, r, 0xF0);
  }
  end2(1);

  check("above", 0, line(9)[20]);
  for (unsigned r = 0; r < 8; ++r)
  {
    const uint8_t* l = line(10 + r);
    checkAt("left", 10 + r, 19, 0, l[19]);
    span("row", l, 10 + r, 20, 23, 1 + r);
    checkAt("right", 10 + r, 24, 0, l[24]);
  }
  check("below", 0, line(18)[20]);
  check("terminator number", 1, lineStatus);
}

static void ninthSprite(void)
{
  setup("G4 nine", G4, 0);
  patt2(0, 0, 0x80);
  for (unsigned n = 0; n < 10; ++n) spr2(n, 49, (uint8_t)(10 * n), 0, (uint8_t)(n + 1));
  end2(10);

  const uint8_t* l = line(50);
  for (unsigned n = 0; n < 8; ++n) checkAt("shown", 50, 10 * n, n + 1, l[10 * n]);
  checkAt("ninth hidden", 50, 80, 0, l[80]);
  checkAt("tenth hidden", 50, 90, 0, l[90]);
  check("9th flag and number", PICO9918_SR0_5S | 8, lineStatus);

  pico9918_frame_update_interrupts(PICO9918_INST lineStatus);
  check("S#0", PICO9918_SR0_5S | 8, status(0) & 0x7F);
  check("S#0 after read", 31, status(0) & 0x7F);

  end2(0);
  line(50);
  check("Y=216 ends the list", 0, lineStatus);
  check("Y=216 hides the rest", 0, line(50)[0]);
}

static void earlyClock(void)
{
  setup("G4 EC", G4, 0);
  spr2(0, 9, 34, 0, EC | 5);
  vpoke(COLOR2 + 1, 6);
  patt2(0, 0, 0xFF);
  patt2(0, 1, 0xFF);
  spr2(1, 19, 26, 0, EC | 7);
  end2(2);

  const uint8_t* l = line(10);
  checkAt("before", 10, 1, 0, l[1]);
  span("shifted", l, 10, 2, 9, 5);
  checkAt("after", 10, 10, 0, l[10]);
  checkAt("not at X", 10, 34, 0, l[34]);

  l = line(11);
  span("row 1 has no EC", l, 11, 34, 41, 6);
  checkAt("row 1 not shifted", 11, 2, 0, l[2]);

  l = line(20);
  span("clipped left", l, 20, 0, 1, 7);
  checkAt("clipped end", 20, 2, 0, l[2]);
}

static void orColour(void)
{
  setup("G4 CC", G4, 0);
  patt2(0, 0, 0xF0);
  patt2(1, 0, 0xFF);
  spr2(0, 29, 100, 0, 8);
  spr2(1, 29, 102, 1, CC | 4);
  spr2(2, 29, 96, 1, CC | 2);
  end2(3);

  const uint8_t* l = line(30);
  checkAt("left of all", 30, 95, 0, l[95]);
  span("2 alone", l, 30, 96, 99, 2);
  span("8|2", l, 30, 100, 101, 10);
  span("8|4|2", l, 30, 102, 103, 14);
  span("4 alone", l, 30, 104, 109, 4);
  checkAt("right of all", 30, 110, 0, l[110]);
  check("CC never collides", 3, lineStatus);

  setup("G4 CC leading", G4, 0);
  patt2(1, 0, 0xFF);
  spr2(0, 29, 50, 1, CC | 3);
  spr2(1, 29, 70, 1, 5);
  spr2(2, 29, 74, 1, CC | 2);
  end2(3);
  l = line(30);
  span("leading CC hidden", l, 30, 50, 57, 0);
  span("5 alone", l, 30, 70, 73, 5);
  span("5|2", l, 30, 74, 77, 7);
  span("2 alone", l, 30, 78, 81, 2);
}

static void collision(void)
{
  setup("G4 collision", G4, 0);
  patt2(1, 0, 0xFF);
  spr2(0, 99, 50, 1, 1);
  spr2(1, 99, 53, 1, 2);
  end2(2);

  const uint8_t* l = line(100);
  span("front", l, 100, 50, 57, 1);
  span("behind", l, 100, 58, 60, 2);
  check("flag", PICO9918_SR0_COLLISION | 2, lineStatus);

  pico9918_frame_update_interrupts(PICO9918_INST lineStatus);
  spr2(0, 109, 150, 1, 1);
  spr2(1, 109, 151, 1, 2);
  line(110);
  check("latched flag stops the coordinates", 2, lineStatus);

  check("S#3", 53 + 12, status(3));
  check("S#4", 0xFE, status(4));
  check("S#5", 100 + 7, status(5));
  check("S#6", 0xFC, status(6));
  check("S#3 after S#5", 0, status(3));
  check("S#5 after S#5", 0, status(5));
  check("S#0", PICO9918_SR0_COLLISION, status(0) & PICO9918_SR0_COLLISION);
  check("S#0 cleared", 0, status(0) & PICO9918_SR0_COLLISION);

  line(110);
  check("S#3 second", 151 + 12, status(3));
  check("S#5 second", 110 + 7, status(5));

  setup("G4 collision EC", G4, 0);
  patt2(1, 0, 0xFF);
  spr2(0, 99, 0, 1, 1);
  spr2(1, 99, 30, 1, EC | 2);
  end2(2);
  line(100);
  check("flag", PICO9918_SR0_COLLISION | 2, lineStatus);
  check("S#3 on screen", 0 + 12, status(3));

  setup("G4 collision off screen", G4, 0);
  patt2(1, 0, 0xFF);
  patt2(2, 0, 0xC0);
  spr2(0, 99, 24, 1, EC | 1);
  spr2(1, 99, 26, 2, EC | 2);
  end2(2);
  line(100);
  check("left border never collides", 2, lineStatus);

  setup("G4 IC", G4, 0);
  patt2(1, 0, 0xFF);
  spr2(0, 99, 50, 1, 1);
  spr2(1, 99, 53, 1, IC | 2);
  end2(2);
  l = line(100);
  check("no flag", 2, lineStatus);
  span("still drawn", l, 100, 58, 60, 2);

  setup("G4 colour 0", G4, 0);
  for (unsigned x = 0; x < 128; ++x) vpoke(100 * 128 + x, 0x77);
  patt2(1, 0, 0xFF);
  spr2(0, 99, 50, 1, 0);
  spr2(1, 99, 53, 1, 2);
  end2(2);
  l = line(100);
  check("TP=0 colour 0 never collides", 2, lineStatus);
  span("TP=0 colour 0 transparent", l, 100, 50, 52, 7);
  span("TP=0 the other drawn", l, 100, 53, 60, 2);
  reg(8, 0x20);
  l = line(100);
  check("TP=1 colour 0 collides", PICO9918_SR0_COLLISION | 2, lineStatus);
  span("TP=1 colour 0 drawn", l, 100, 50, 57, 0);
  span("TP=1 behind", l, 100, 58, 60, 2);
}

static void sizeAndMag(void)
{
  setup("G4 16 mag", G4, TMS_R1_SPRITE_16 | TMS_R1_SPRITE_MAG2);
  patt2(4, 0, 0x80);
  patt2(6, 0, 0x01);
  patt2(4, 1, 0x40);
  spr2(0, 19, 30, 5, 3);
  vpoke(COLOR2 + 1, 9);
  end2(1);

  for (unsigned y = 20; y <= 21; ++y)
  {
    const uint8_t* l = line(y);
    span("left dot", l, y, 30, 31, 3);
    span("gap", l, y, 32, 59, 0);
    span("right dot", l, y, 60, 61, 3);
    checkAt("past", y, 62, 0, l[62]);
  }
  for (unsigned y = 22; y <= 23; ++y)
  {
    const uint8_t* l = line(y);
    checkAt("row 1 off", y, 31, 0, l[31]);
    span("row 1 colour", l, y, 32, 33, 9);
    checkAt("row 1 past", y, 34, 0, l[34]);
  }
  patt2(4, 15, 0x80);
  checkAt("row 30", 50, 30, 3, line(50)[30]);
  checkAt("row 31", 51, 31, 3, line(51)[31]);
  checkAt("row 32 is past the sprite", 52, 30, 0, line(52)[30]);

  setup("G4 8 mag", G4, TMS_R1_SPRITE_MAG2);
  patt2(0, 0, 0xA0);
  spr2(0, 19, 30, 0, 3);
  end2(1);
  const uint8_t* l = line(21);
  span("dot 0", l, 21, 30, 31, 3);
  span("dot 1 clear", l, 21, 32, 33, 0);
  span("dot 2", l, 21, 34, 35, 3);
  checkAt("end", 21, 36, 0, l[36]);
}

static void scroll(void)
{
  setup("G4 R#23", G4, 0);
  patt2(0, 0, 0x80);
  patt2(0, 1, 0x80);
  spr2(0, 210, 40, 0, 4);
  end2(1);
  reg(23, 200);
  checkAt("above", 10, 40, 0, line(10)[40]);
  checkAt("row 0", 11, 40, 4, line(11)[40]);
  checkAt("row 1", 12, 40, 4, line(12)[40]);
  checkAt("row 2 empty", 13, 40, 0, line(13)[40]);

  reg(23, 0);
  spr2(0, 255, 40, 0, 4);
  checkAt("Y=255 at the top", 0, 40, 4, line(0)[40]);

  reg(8, PICO9918_V9938_R8_SPD);
  checkAt("SPD", 0, 40, 0, line(0)[40]);
  check("SPD status", 0, lineStatus);
}

static void hiRes(void)
{
  setup("G5", G5, 0);
  patt2(0, 0, 0xC0);
  spr2(0, 9, 4, 0, 14);
  spr2(1, 9, 8, 0, 1);
  end2(2);
  const uint8_t* l = line(10);
  check("x3", 0x00, l[3]);
  check("c14 x4", 0x32, l[4]);
  check("c14 x5", 0x32, l[5]);
  check("c1", 0x01, l[8]);
  check("x10", 0x00, l[10]);

  setup("G6", G6, 0);
  patt2(0, 0, 0x80);
  spr2(0, 9, 7, 0, 0x0B);
  end2(1);
  check("planar attribute", 9, tms9918->vram.bytes[((ATTR2 << 16) | (ATTR2 >> 1)) & 0x1FFFF]);
  check("planar X", 7, tms9918->vram.bytes[(((ATTR2 + 1) << 16) | ((ATTR2 + 1) >> 1)) & 0x1FFFF]);
  check("planar colour", 0x0B, tms9918->vram.bytes[(((COLOR2 + 1) << 16) | ((COLOR2 + 1) >> 1)) & 0x1FFFF]);
  l = line(10);
  check("x6", 0x00, l[6]);
  check("both nibbles", 0xBB, l[7]);
  check("x8", 0x00, l[8]);

  setup("G7", G7, TMS_R1_SPRITE_16);
  patt2(0, 0, 0x80);
  patt2(0, 1, 0x80);
  patt2(2, 0, 0x80);
  spr2(0, 9, 7, 0, 8);
  vpoke(COLOR2 + 1, 3);
  spr2(1, 9, 9, 0, 0);
  end2(2);
  l = line(10);
  check("colour 8", 0x9D, l[7]);
  check("right half", 0x9D, l[15]);
  check("colour 0 TP=0", 0x00, l[9]);
  check("colour 3", 0x0D, line(11)[7]);
  vpoke(10 * 256 + 9, 0x55);
  check("G7 colour 0 TP=0 shows the bitmap", 0x55, line(10)[9]);
  reg(8, 0x20);
  check("G7 colour 0 TP=1", 0x00, line(10)[9]);
}

static void graphics3(void)
{
  setup("G3", G3, 0);
  reg(7, 0x04);
  patt2(0, 0, 0x80);
  for (unsigned n = 0; n < 6; ++n) spr2(n, 59, (uint8_t)(20 * n), 0, (uint8_t)(9 + n));
  vpoke(COLOR2 + 16 * 2 + 0, 1);
  end2(6);
  const uint8_t* l = line(60);
  const uint8_t bg = l[255];
  check("backdrop", 4, bg & 0x0F);
  for (unsigned n = 0; n < 6; ++n) checkAt("all six", 60, 20 * n, n == 2 ? 1 : 9 + n, l[20 * n]);
  checkAt("between", 60, 1, bg, l[1]);
  check("mode 2 status", 6, lineStatus);
}

static void spriteMode1(void)
{
  setup("G1 wide tables", G1, 0);
  reg(7, 0x01);
  patt1(1, 0, 0xC0);
  spr1(0, 29, 60, 1, 6);
  spr1(1, 208, 0, 0, 0);
  const uint8_t* l = line(30);
  const uint8_t bg = l[255];
  span("drawn", l, 30, 60, 61, 6);
  checkAt("past", 30, 62, bg, l[62]);
  checkAt("above", 29, 60, bg, line(29)[60]);

  setup("G1 four", G1, 0);
  patt1(0, 0, 0x80);
  for (unsigned n = 0; n < 6; ++n) spr1(n, 49, (uint8_t)(20 * n + 10), 0, (uint8_t)(n + 2));
  spr1(6, 208, 0, 0, 0);
  l = line(50);
  for (unsigned n = 0; n < 4; ++n) checkAt("shown", 50, 20 * n + 10, n + 2, l[20 * n + 10]);
  checkAt("fifth hidden", 50, 90, l[255], l[90]);
  check("5th flag and number", PICO9918_SR0_5S | 4, lineStatus);

  setup("G2 terminator", G2, 0);
  patt1(0, 0, 0x80);
  spr1(0, 216, 0, 0, 3);
  spr1(1, 49, 30, 0, 5);
  spr1(2, 208, 0, 0, 0);
  checkAt("216 is not the end", 50, 30, 5, line(50)[30]);
  spr1(0, 208, 0, 0, 3);
  l = line(50);
  checkAt("208 is", 50, 30, l[255], l[30]);

  setup("G2 R#23", G2, 0);
  patt1(0, 0, 0x80);
  spr1(0, 100, 30, 0, 5);
  spr1(1, 0xEF, 40, 0, 6);
  spr1(2, 208, 0, 0, 0);
  reg(23, 50);
  l = line(50);
  checkAt("not yet", 50, 30, l[255], l[30]);
  checkAt("scrolled", 51, 30, 5, line(51)[30]);
  reg(23, 0xF0);
  checkAt("scrolled past the wrap", 0, 40, 6, line(0)[40]);

  reg(23, 50);
  reg(8, PICO9918_V9938_R8_SPD);
  l = line(51);
  checkAt("SPD", 51, 30, l[255], l[30]);

  setup("MC", G1, 0x08);
  patt1(1, 0, 0xC0);
  spr1(0, 29, 60, 1, 6);
  spr1(1, 208, 0, 0, 0);
  span("drawn", line(30), 30, 60, 61, 6);
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

  perLineColour();
  ninthSprite();
  earlyClock();
  orColour();
  collision();
  sizeAndMag();
  scroll();
  hiRes();
  graphics3();
  spriteMode1();

  if (failures)
  {
    printf("v9938spr: %d failure(s)\n", failures);
    return 1;
  }
  printf("v9938spr: all passed\n");
  return 0;
}
