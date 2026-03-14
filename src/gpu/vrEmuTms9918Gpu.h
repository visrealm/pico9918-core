/*
 * Troy's TMS9918 Emulator - GPU Interface
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/vrEmuTms9918
 *
 * Purpose: Library-public interface to the TMS9900 GPU (F18A compatibility layer)
 *
 */

#pragma once

#include "impl/vrEmuTms9918Priv.h"

/*
 * Initialize the TMS9900 GPU.
 * Must be called after vrEmuTms9918Init() / vrEmuTms9918Reset().
 */
void vrEmuTmsGpuInit(VR_EMU_INST_ONLY_ARG);

/*
 * GPU main loop — call from a dedicated core/thread.
 * Runs indefinitely; processes GPU programs, flash requests, and config saves.
 */
void vrEmuTmsGpuLoop(VR_EMU_INST_ONLY_ARG);

/*
 * Trigger the GPU to (re)start execution at the current gpuAddress.
 * Safe to call from the main/IRQ context.
 */
static inline void vrEmuTmsGpuTrigger(VR_EMU_INST_ONLY_ARG)
{
  tms9918->restart = 1;
}

/*
 * Return the GPU's CPU time in microseconds.
 * If the GPU is still running (hasn't reported back), returns totalTime.
 */
uint32_t vrEmuTmsGpuTime(uint32_t totalTime);

/*
 * Reset the internal GPU time accumulator to 0.
 */
void vrEmuTmsGpuResetTime(void);

/*
 * Register a callback that will be invoked when the GPU wants to flash a sector.
 * Pass NULL to disable.
 */
void vrEmuTmsGpuSetFlashCallback(void (*cb)(void));

/*
 * Register a callback that will be invoked when the GPU wants to save config.
 * The callback receives the config array pointer.
 * Pass NULL to disable.
 */
void vrEmuTmsGpuSetConfigSaveCallback(void (*cb)(uint8_t* config));
