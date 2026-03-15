/*
 * Troy's TMS9918 Emulator - Display Mode Operations Table
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/vrEmuTms9918
 *
 */

#pragma once

#include "../impl/vrEmuTms9918Priv.h"

/*
 * Per-mode scanline function signature.
 * Returns a status byte (sprite overflow/collision flags) or 0.
 */
typedef uint8_t (*vrEmuTmsScanLineFn)(VR_EMU_INST_ONLY_ARG);

/*
 * Display mode operations vtable — five named pipeline stages.
 *
 * Stage call order (fixed; NULL = skip):
 *   stage0 -> stage1 -> stage2 -> stage3 -> stage4
 *
 * Semantics:
 *   stage0  F18A bitmap layer (lowest priority, runs first)
 *   stage1  sprites below tiles (F18A)
 *   stage2  primary tile renderer — never NULL for a valid mode
 *   stage3  F18A tile-2 layer
 *   stage4  sprites above tiles (base) / bitmap-above (F18A some modes)
 */
typedef struct VrEmuTmsDisplayModeOps
{
  vrEmuTmsScanLineFn stage0Fn;  /* F18A: bitmap layer (NULL in base table) */
  vrEmuTmsScanLineFn stage1Fn;  /* sprites below tiles (NULL in base table) */
  vrEmuTmsScanLineFn stage2Fn;  /* primary tile renderer — never NULL       */
  vrEmuTmsScanLineFn stage3Fn;  /* F18A tile-2 layer (NULL in base table)   */
  vrEmuTmsScanLineFn stage4Fn;  /* sprites above tiles / NULL for Text modes */
  const char*        name;
} VrEmuTmsDisplayModeOps;

/*
 * Three parallel vtables, each indexed by vrEmuTms9918Mode:
 *   baseOpsTable_tms9918  — locked / standard TMS9918A behaviour
 *   baseOpsTable_v9938    — V9938 base (initially same as TMS9918A; V9938
 *                           renderers slot in mode-by-mode)
 *   f18aOpsTable          — F18A-unlocked extensions on top of any base
 *
 * Declared extern; defined in vrEmuTms9918.c.
 */
extern const VrEmuTmsDisplayModeOps baseOpsTable_tms9918[TMS_MODE_COUNT];
extern const VrEmuTmsDisplayModeOps baseOpsTable_v9938[TMS_MODE_COUNT];
extern const VrEmuTmsDisplayModeOps f18aOpsTable[TMS_MODE_COUNT];

#if VR_EMU_TMS9918_MODE == VR_EMU_TMS9918_MODE_V9938
/* V9938 per-mode ops — one entry per mode per table, defined in vrEmuTmsV9938.c */
extern const VrEmuTmsDisplayModeOps vrTmsV9938BaseG3;
extern const VrEmuTmsDisplayModeOps vrTmsV9938BaseG4;
extern const VrEmuTmsDisplayModeOps vrTmsV9938BaseG5;
extern const VrEmuTmsDisplayModeOps vrTmsV9938BaseG6;
extern const VrEmuTmsDisplayModeOps vrTmsV9938BaseG7;

extern const VrEmuTmsDisplayModeOps vrTmsV9938V9938G3;
extern const VrEmuTmsDisplayModeOps vrTmsV9938V9938G4;
extern const VrEmuTmsDisplayModeOps vrTmsV9938V9938G5;
extern const VrEmuTmsDisplayModeOps vrTmsV9938V9938G6;
extern const VrEmuTmsDisplayModeOps vrTmsV9938V9938G7;

extern const VrEmuTmsDisplayModeOps vrTmsV9938F18AG3;
extern const VrEmuTmsDisplayModeOps vrTmsV9938F18AG4;
extern const VrEmuTmsDisplayModeOps vrTmsV9938F18AG5;
extern const VrEmuTmsDisplayModeOps vrTmsV9938F18AG6;
extern const VrEmuTmsDisplayModeOps vrTmsV9938F18AG7;
#endif

/*
 * Global active mode ops pointer — swapped once when mode / unlock / base changes.
 * Declared extern; defined in vrEmuTms9918.c.
 */
extern const VrEmuTmsDisplayModeOps* vrTmsActiveModeOps;
