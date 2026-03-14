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
typedef uint8_t (*vrEmuTmsScanLineFn)(VR_EMU_INST_ARG uint16_t y, uint8_t pixels[TMS9918_PIXELS_X]);

/*
 * Display mode operations vtable.
 * One entry per vrEmuTms9918Mode enum value.
 */
typedef struct VrEmuTmsDisplayModeOps
{
  vrEmuTmsScanLineFn scanlineFn; /* NULL = blank (mode not implemented) */
  const char*        name;
} VrEmuTmsDisplayModeOps;

/* Forward-declared per-mode ops structs (defined in vrEmuTms9918.c) */
extern const VrEmuTmsDisplayModeOps vrTmsModeOpsGraphicsI;
extern const VrEmuTmsDisplayModeOps vrTmsModeOpsGraphicsII;
extern const VrEmuTmsDisplayModeOps vrTmsModeOpsText;
extern const VrEmuTmsDisplayModeOps vrTmsModeOpsMulticolor;
extern const VrEmuTmsDisplayModeOps vrTmsModeOpsText80;

#if VR_EMU_TMS9918_MODE == VR_EMU_TMS9918_MODE_V9938
extern const VrEmuTmsDisplayModeOps vrTmsModeOpsV9938G3;
extern const VrEmuTmsDisplayModeOps vrTmsModeOpsV9938G4;
extern const VrEmuTmsDisplayModeOps vrTmsModeOpsV9938G5;
extern const VrEmuTmsDisplayModeOps vrTmsModeOpsV9938G6;
extern const VrEmuTmsDisplayModeOps vrTmsModeOpsV9938G7;
#endif

/*
 * Global active mode ops pointer — updated once per scanline when mode changes.
 * Declared extern; defined in vrEmuTms9918.c.
 */
extern const VrEmuTmsDisplayModeOps* vrTmsActiveModeOps;
