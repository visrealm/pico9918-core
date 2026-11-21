/*
 * pico9918-core - V9938 base, private interface
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 *
 * Only a PICO9918_V9938_BASE build has any of this. The V9938 answers while the base is
 * the V9938 and the F18A is locked; an unlock hands every port back to the F18A.
 */

#pragma once

#if PICO9918_V9938_BASE

#include <stdbool.h>
#include <stdint.h>

#define PICO9918_V9938_REG_MODE2       8
#define PICO9918_V9938_REG_MODE3       9
#define PICO9918_V9938_REG_COLOR_HI    10
#define PICO9918_V9938_REG_SPR_ATTR_HI 11
#define PICO9918_V9938_REG_BLINK_COLOR 12
#define PICO9918_V9938_REG_BLINK_TIME  13
#define PICO9918_V9938_REG_VRAM_PAGE   14
#define PICO9918_V9938_REG_PAL_INDEX   16
#define PICO9918_V9938_REG_INDIRECT    17
#define PICO9918_V9938_REG_ADJUST      18
#define PICO9918_V9938_REG_BURST       20
#define PICO9918_V9938_REG_VSCROLL     23
#define PICO9918_V9938_REG_ARG         45
#define PICO9918_V9938_REG_CMD         46
#define PICO9918_V9938_R45_MXC         0x40
#define PICO9918_V9938_R17_AII         0x80
#define PICO9918_V9938_R8_TP           0x20
#define PICO9918_V9938_R8_SPD          0x02
#define PICO9918_V9938_R9_LN           0x80
#define PICO9918_V9938_R9_EO           0x04

/* S#2 */
#define PICO9918_V9938_S2_TR 0x80
#define PICO9918_V9938_S2_VR 0x40
#define PICO9918_V9938_S2_HR 0x20
#define PICO9918_V9938_S2_BD 0x10
#define PICO9918_V9938_S2_EO 0x02
#define PICO9918_V9938_S2_CE 0x01

typedef struct
{
  uint8_t status; /* S#2's TR and BD bits */

  /* TRAP: S#2's CE, its own byte so a read-modify-write of status on the other core cannot undo it */
  uint8_t ce;
  uint8_t color;    /* S#7 */
  uint16_t borderX; /* S#8, S#9 */
  uint8_t op;
  uint8_t logop;
  uint8_t arg;
  uint8_t pending;
  uint8_t pshift;
  uint8_t planar;
  uint8_t cmask;
  int8_t xstep, systep, dystep;
  uint16_t xmask;
  uint16_t sx, dx, nx, ny;
  uint16_t asx, adx, anx, lines;
  int32_t sy, dy;

  /* TRAP: only the rendering core writes produced, only the stepping core spent and busy */
  uint32_t produced;
  uint32_t spent;
  uint32_t busy;
} pico9918_v9938_cmd_t;

#define PICO9918_V9938_FRAME_TICKS (1u << 20)

typedef struct
{
  uint8_t palLatch;
  uint8_t palStage;
  uint8_t hrToggle;
  uint8_t blinkOn;
  uint16_t collisionX; /* S#3, S#4 */
  uint16_t collisionY; /* S#5, S#6 */
  uint8_t blinkCount;
  uint8_t blinkReload;
  uint8_t field; /* S#2 EO */
  uint8_t lineField;
  uint8_t wide;
  uint8_t raw[7];
  uint32_t lineCredit;
  pico9918_v9938_cmd_t cmd;
} pico9918_v9938_t;

#endif
