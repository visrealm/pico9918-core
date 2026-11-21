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

#define PICO9918_V9938_REG_PAL_INDEX  16
#define PICO9918_V9938_REG_INDIRECT   17
#define PICO9918_V9938_REG_VSCROLL    23
#define PICO9918_V9938_REG_ARG        45
#define PICO9918_V9938_REG_CMD        46
#define PICO9918_V9938_R45_MXC        0x40
#define PICO9918_V9938_R17_AII        0x80

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
  uint8_t status;   /* S#2's TR, BD and CE bits */
  uint8_t color;    /* S#7 */
  uint16_t borderX; /* S#8, S#9 */
  uint8_t op;       /* the command running, CMD's high nibble, 0 when idle */
  uint8_t logop;
  uint16_t sx, sy, dx, dy, nx, ny;
  uint16_t ax, ay;  /* where the command has got to */
  uint16_t asx, adx, anx;
  int32_t err;
} pico9918_v9938_cmd_t;

typedef struct
{
  uint8_t palLatch;
  uint8_t palStage;
  uint8_t hrToggle;
  uint8_t pad;
  uint16_t collisionX; /* S#3, S#4 */
  uint16_t collisionY; /* S#5, S#6 */
  pico9918_v9938_cmd_t cmd;
} pico9918_v9938_t;

#endif
