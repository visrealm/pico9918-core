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

#define PICO9918_V9938_REG_COLOR_HI   10
#define PICO9918_V9938_REG_BLINK_COLOR 12
#define PICO9918_V9938_REG_BLINK_TIME 13
#define PICO9918_V9938_REG_PAL_INDEX  16
#define PICO9918_V9938_REG_INDIRECT   17
#define PICO9918_V9938_REG_ADJUST     18
#define PICO9918_V9938_REG_VSCROLL    23
#define PICO9918_V9938_REG_ARG        45
#define PICO9918_V9938_REG_CMD        46
#define PICO9918_V9938_R45_MXC        0x40
#define PICO9918_V9938_R17_AII        0x80
#define PICO9918_V9938_R8_SPD         0x02

/* S#2 */
#define PICO9918_V9938_S2_TR 0x80
#define PICO9918_V9938_S2_VR 0x40
#define PICO9918_V9938_S2_HR 0x20
#define PICO9918_V9938_S2_BD 0x10
#define PICO9918_V9938_S2_EO 0x02
#define PICO9918_V9938_S2_CE 0x01

/* the command engine's own state, owned by pico9918_v9938_cmd.c */
typedef struct
{
  uint8_t status;   /* S#2's TR and BD bits */
  uint8_t ce;       /* TRAP: S#2's CE, its own byte so a read-modify-write of status on the other core cannot undo it */
  uint8_t color;    /* S#7, and the CLR every command uses */
  uint16_t borderX; /* S#8, S#9 */
  uint8_t op;       /* the command running, CMD's high nibble, 0 when idle */
  uint8_t logop;
  uint8_t arg;
  uint8_t pending; /* a CLR write or S#7 read no transfer has consumed yet */
  uint8_t pshift;
  uint8_t planar;
  uint8_t cmask;
  int8_t xstep, systep, dystep;
  uint16_t xmask;
  uint16_t sx, dx, nx, ny;
  uint16_t asx, adx, anx, lines;
  int32_t sy, dy;
  uint32_t produced; /* time credit, added each line by the renderer's core */
  uint32_t spent;    /* time credit used, by the core that steps commands */
  uint32_t busy;     /* the part of spent that did work, for the diagnostics' CMD row */
} pico9918_v9938_cmd_t;

/* a frame of command time, in the units pico9918_v9938_cmd.c costs each unit of work in */
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
  uint8_t field;     /* S#2 EO: the field this frame is */
  uint8_t lineField; /* the field the line being rendered belongs to */
  uint8_t wide;      /* the host has used a V9938 register, so R#0-R#6 decode at the V9938's widths */
  uint8_t raw[7];    /* R#0-R#6 as written while they decode at the TMS9918A's widths */
  uint32_t lineCredit;
  pico9918_v9938_cmd_t cmd;
} pico9918_v9938_t;

#endif
