/*
 * Troy's TMS9918 Emulator - V9938 Display Mode Stubs
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/vrEmuTms9918
 *
 * These are placeholder scanline renderers for V9938 graphics modes G3-G7.
 * They render a blank line (background colour) until proper V9938 rendering
 * is implemented.
 */

#include "vrEmuTmsModeOps.h"
#include "../impl/vrEmuTms9918Priv.h"

/* ------------------------------------------------------------------
 * Shared blank scanline renderer — used by all unimplemented V9938 modes.
 * The buffer is already filled with bg colour by the caller; nothing to do.
 * ------------------------------------------------------------------ */
static uint8_t __time_critical_func(vrEmuTmsV9938BlankScanLine)(VR_EMU_INST_ARG uint16_t y, uint8_t pixels[TMS9918_PIXELS_X])
{
  (void)y;
  (void)pixels;
  VR_TMS_FILL32_WAIT();
  return 0;
}

/* ------------------------------------------------------------------
 * Per-mode ops structs (all point to the shared blank renderer for now)
 * ------------------------------------------------------------------ */
const VrEmuTmsDisplayModeOps vrTmsModeOpsV9938G3 = { vrEmuTmsV9938BlankScanLine, "V9938-G3" };
const VrEmuTmsDisplayModeOps vrTmsModeOpsV9938G4 = { vrEmuTmsV9938BlankScanLine, "V9938-G4" };
const VrEmuTmsDisplayModeOps vrTmsModeOpsV9938G5 = { vrEmuTmsV9938BlankScanLine, "V9938-G5" };
const VrEmuTmsDisplayModeOps vrTmsModeOpsV9938G6 = { vrEmuTmsV9938BlankScanLine, "V9938-G6" };
const VrEmuTmsDisplayModeOps vrTmsModeOpsV9938G7 = { vrEmuTmsV9938BlankScanLine, "V9938-G7" };
