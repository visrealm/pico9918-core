/*
 * Troy's TMS9918 Emulator - V9938 Display Mode Stubs
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/vrEmuTms9918
 *
 * Placeholder scanline renderers for V9938 graphics modes G3-G7.
 * The pixel buffer is already filled with bg colour by the caller;
 * these stubs just wait for the DMA fill and draw sprites.
 *
 * Each mode has three ops entries — one per vtable:
 *   vrTmsV9938Base*   — baseOpsTable_tms9918 rows (TMS9918A base, V9938 modes)
 *   vrTmsV9938V9938*  — baseOpsTable_v9938 rows   (V9938 base, V9938 modes)
 *   vrTmsV9938F18A*   — f18aOpsTable rows          (F18A unlock, V9938 modes)
 */

#include "vrEmuTmsModeOps.h"
#include "../impl/vrEmuTms9918Priv.h"

/* ------------------------------------------------------------------
 * Shared blank stage2 renderer — background already filled; nothing to do.
 * ------------------------------------------------------------------ */
static uint8_t __time_critical_func(v9938BlankTiles)(VR_EMU_INST_ONLY_ARG)
{
  VR_TMS_FILL32_WAIT();
  return 0;
}

/* ------------------------------------------------------------------
 * Shared stage4 sprites trampoline for V9938 modes.
 * ------------------------------------------------------------------ */
static uint8_t __time_critical_func(v9938SpritesAbove)(VR_EMU_INST_ONLY_ARG)
{
  return vrEmuTms9918OutputSprites(VR_EMU_INST_ONLY);
}

/* ------------------------------------------------------------------
 * Per-mode ops structs — baseOpsTable_tms9918 entries (TMS9918A base)
 * ------------------------------------------------------------------ */
const VrEmuTmsDisplayModeOps vrTmsV9938BaseG3 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "V9938-G3" };
const VrEmuTmsDisplayModeOps vrTmsV9938BaseG4 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "V9938-G4" };
const VrEmuTmsDisplayModeOps vrTmsV9938BaseG5 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "V9938-G5" };
const VrEmuTmsDisplayModeOps vrTmsV9938BaseG6 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "V9938-G6" };
const VrEmuTmsDisplayModeOps vrTmsV9938BaseG7 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "V9938-G7" };

/* ------------------------------------------------------------------
 * Per-mode ops structs — baseOpsTable_v9938 entries (V9938 base)
 * ------------------------------------------------------------------ */
const VrEmuTmsDisplayModeOps vrTmsV9938V9938G3 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "V9938-G3" };
const VrEmuTmsDisplayModeOps vrTmsV9938V9938G4 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "V9938-G4" };
const VrEmuTmsDisplayModeOps vrTmsV9938V9938G5 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "V9938-G5" };
const VrEmuTmsDisplayModeOps vrTmsV9938V9938G6 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "V9938-G6" };
const VrEmuTmsDisplayModeOps vrTmsV9938V9938G7 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "V9938-G7" };

/* ------------------------------------------------------------------
 * Per-mode ops structs — f18aOpsTable entries (F18A unlock)
 * ------------------------------------------------------------------ */
const VrEmuTmsDisplayModeOps vrTmsV9938F18AG3 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "F18A-V9938-G3" };
const VrEmuTmsDisplayModeOps vrTmsV9938F18AG4 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "F18A-V9938-G4" };
const VrEmuTmsDisplayModeOps vrTmsV9938F18AG5 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "F18A-V9938-G5" };
const VrEmuTmsDisplayModeOps vrTmsV9938F18AG6 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "F18A-V9938-G6" };
const VrEmuTmsDisplayModeOps vrTmsV9938F18AG7 = { NULL, NULL, v9938BlankTiles, NULL, v9938SpritesAbove, "F18A-V9938-G7" };
