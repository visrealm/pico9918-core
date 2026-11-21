/**
 * \file
 * \brief pico9918-core - V9938 base, private interface
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

/* V9938 register numbers and the bits the base reads */
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

/** \brief the command in flight, and the time the rendered lines have given it */
typedef struct
{
  uint8_t status; /**< S#2's TR and BD bits */

  /** TRAP: S#2's CE, its own byte so a read-modify-write of status on the other core cannot undo it */
  uint8_t ce;
  uint8_t color;    /**< S#7 */
  uint16_t borderX; /**< S#8, S#9 */
  uint8_t op;       /**< R#46's command code, 0 when idle; core 0 steps while it is set */
  uint8_t logop;    /**< R#46's logical op */
  uint8_t arg;      /**< R#45 */
  uint8_t pending;  /**< a host transfer byte that arrived before its command */
  uint8_t pshift;   /**< pixels per byte as a shift: 0 in G7, 2 in G5, else 1 */
  uint8_t planar;   /**< 1 in G6 and G7 */
  uint8_t cmask;    /**< a pixel's colour bits */
  int8_t xstep;     /**< the step along X, signed by DIX */
  int8_t systep;    /**< the source's step down each row, signed by DIY */
  int8_t dystep;    /**< the destination's step down each row, signed by DIY */
  uint16_t xmask;   /**< X wraps here: 255 or 511 */
  uint16_t sx;      /**< R#32-R#33 */
  uint16_t dx;      /**< R#36-R#37 */
  uint16_t nx;      /**< R#40-R#41, clipped to the line */
  uint16_t ny;      /**< R#42-R#43, counting down as rows complete */
  uint16_t asx;     /**< the source X along this row; LINE's error term */
  uint16_t adx;     /**< the destination X along this row */
  uint16_t anx;     /**< dots or bytes left in this row; LINE's dot count */
  uint16_t lines;   /**< rows left, after clipping */
  int32_t sy;       /**< the source row */
  int32_t dy;       /**< the destination row */

  /* TRAP: only the rendering core writes produced, only the stepping core spent and busy */
  uint32_t produced; /**< frame ticks the rendered lines have given the engine */
  uint32_t spent;    /**< frame ticks it has used or let lapse while idle */
  uint32_t busy;     /**< frame ticks it has used, for the diagnostics' CMD% */
} pico9918_v9938_cmd_t;

#define PICO9918_V9938_FRAME_TICKS (1u << 20)

/** \brief the V9938 state the F18A has no place for */
typedef struct
{
  uint8_t palLatch;         /**< the first byte of a palette write */
  uint8_t palStage;         /**< 1 between a palette write's two bytes */
  uint8_t hrToggle;         /**< S#2 HR, flipped per read */
  uint8_t blinkOn;          /**< R#13's blink is on its on phase */
  uint16_t collisionX;      /**< S#3, S#4 */
  uint16_t collisionY;      /**< S#5, S#6 */
  uint8_t blinkCount;       /**< frames to the next blink phase */
  uint8_t blinkReload;      /**< R#13 changed; the next frame restarts the blink */
  uint8_t field;            /**< S#2 EO */
  uint8_t lineField;        /**< the field this frame's lines render */
  uint8_t wide;             /**< R#0-R#6 decode at V9938 widths */
  uint8_t raw[7];           /**< R#0-R#6 as written, while narrow */
  uint32_t lineCredit;      /**< frame ticks each rendered line gives the command engine */
  pico9918_v9938_cmd_t cmd; /**< the command engine */
} pico9918_v9938_t;

#endif
