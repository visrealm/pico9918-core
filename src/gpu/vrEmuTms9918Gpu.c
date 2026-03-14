/*
 * Troy's TMS9918 Emulator - GPU Implementation
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/vrEmuTms9918
 *
 * Purpose: TMS9900 GPU glue code (adapted from pico9918/src/gpu/gpu.c)
 *
 * Credits: JasonACT (AtariAge)
 *
 */

#include "vrEmuTms9918Gpu.h"

#include <string.h> /* memcpy */

/* -------------------------------------------------------------------------
 * Platform-specific includes
 * ---------------------------------------------------------------------- */
#ifdef PICO_BUILD
  #include "pico/stdlib.h"
  #include "hardware/structs/mpu.h"
  #include <hardware/flash.h>
  #include "pico.h" /* PICO_RP2040 */

  /* run9900() implemented in platform/thumb9900_{m0,m33}.S */
  extern uint16_t run9900(uint8_t* memory, uint16_t pc, uint16_t wp, uint8_t* regx38);
#else
  #include "tms9900.h"
  #include "impl/vrEmuTmsPlatform.h" /* time_us_32 */

  static uint16_t run9900(uint8_t* mem, uint16_t pc, uint16_t wp, uint8_t* r38)
  {
    Tms9900Cpu cpu;
    tms9900_init(&cpu, mem, r38, pc, wp);
    return run9900_c(&cpu);
  }
#endif

/* -------------------------------------------------------------------------
 * Config key used to request a flash save.
 * Must match CONF_SAVE_TO_FLASH in pico9918/src/config.h (currently 255).
 * ---------------------------------------------------------------------- */
#define CONF_SAVE_TO_FLASH 255

/* -------------------------------------------------------------------------
 * Callbacks (registered by the host application)
 * ---------------------------------------------------------------------- */
static void (*vrTmsGpuFlashCb)(void)           = NULL;
static void (*vrTmsGpuConfigSaveCb)(uint8_t*)  = NULL;

void vrEmuTmsGpuSetFlashCallback(void (*cb)(void))
{
  vrTmsGpuFlashCb = cb;
}

void vrEmuTmsGpuSetConfigSaveCallback(void (*cb)(uint8_t* config))
{
  vrTmsGpuConfigSaveCb = cb;
}

/* -------------------------------------------------------------------------
 * Pre-load data copied into GPU RAM at init time
 * ---------------------------------------------------------------------- */
static uint8_t preload[] = {
  0x02, 0x0F, 0x47, 0xFE, 0x10, 0x0D, 0x40, 0x36, 0x40, 0x5A, 0x40, 0x94, 0x40, 0xB4, 0x40, 0xFA,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x0C, 0xA0, 0x41, 0x1C, 0x03, 0x40, 0x04, 0xC1, 0xD0, 0x60, 0x3F, 0x00, 0x09, 0x71, 0xC0, 0x21,
  0x40, 0x06, 0x06, 0x90, 0x10, 0xF7, 0xC0, 0x20, 0x3F, 0x02, 0xC0, 0x60, 0x3F, 0x04, 0xC0, 0xA0,
  0x3F, 0x06, 0xD0, 0xE0, 0x3F, 0x01, 0x13, 0x05, 0xD0, 0x10, 0xDC, 0x40, 0x06, 0x02, 0x16, 0xFD,
  0x10, 0x03, 0xDC, 0x70, 0x06, 0x02, 0x16, 0xFD, 0x04, 0x5B, 0x0D, 0x0B, 0x06, 0xA0, 0x40, 0xB4,
  0x0F, 0x0B, 0xC1, 0xC7, 0x13, 0x16, 0x04, 0xC0, 0xD0, 0x20, 0x60, 0x04, 0x0A, 0x30, 0xC0, 0xC0,
  0x04, 0xC1, 0x02, 0x02, 0x04, 0x00, 0xCC, 0x01, 0x06, 0x02, 0x16, 0xFD, 0x04, 0xC0, 0xD0, 0x20,
  0x41, 0x51, 0x06, 0xC0, 0x0A, 0x30, 0xA0, 0x03, 0x0C, 0xA0, 0x41, 0xAE, 0xD8, 0x20, 0x41, 0x51,
  0xB0, 0x00, 0x04, 0x5B, 0xD8, 0x20, 0x41, 0x1A, 0x3F, 0x00, 0x02, 0x00, 0x41, 0xD6, 0xC8, 0x00,
  0x3F, 0x02, 0x02, 0x00, 0x40, 0x06, 0xC8, 0x00, 0x3F, 0x04, 0x02, 0x00, 0x40, 0x10, 0xC8, 0x00,
  0x3F, 0x06, 0x04, 0x5B, 0x04, 0xC7, 0xD0, 0x20, 0x3F, 0x01, 0x13, 0x13, 0xC0, 0x20, 0x41, 0x18,
  0x06, 0x00, 0x0C, 0xA0, 0x41, 0x52, 0x02, 0x04, 0x00, 0x05, 0x02, 0x05, 0x3F, 0x02, 0x02, 0x06,
  0x41, 0x42, 0x8D, 0xB5, 0x16, 0x03, 0x06, 0x04, 0x16, 0xFC, 0x10, 0x09, 0x06, 0x00, 0x16, 0xF1,
  0x10, 0x09, 0xC0, 0x20, 0x3F, 0x02, 0x0C, 0xA0, 0x41, 0x52, 0x80, 0x40, 0x14, 0x03, 0x0C, 0xA0,
  0x41, 0x9A, 0x05, 0x47, 0xD8, 0x07, 0xB0, 0x00, 0x04, 0x5B, 0x0D, 0x0B, 0x06, 0xA0, 0x40, 0xB4,
  0x0F, 0x0B, 0xC1, 0xC7, 0x13, 0x04, 0xC0, 0x20, 0x3F, 0x0C, 0x0C, 0xA0, 0x41, 0xAE, 0x04, 0x5B,
  0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x41, 0x10,
  0x02, 0x01, 0x41, 0x15, 0x02, 0x02, 0x0B, 0x00, 0x03, 0xA0, 0x32, 0x02, 0x32, 0x30, 0x32, 0x30,
  0x32, 0x30, 0x36, 0x00, 0x02, 0x02, 0x00, 0x06, 0x36, 0x31, 0x06, 0x02, 0x16, 0xFD, 0x03, 0xC0,
  0x0C, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x88, 0x00, 0x41, 0x18, 0x1A, 0x03, 0xC0, 0x60, 0x41, 0x18, 0x0C, 0x00, 0x0D, 0x00,
  0x0A, 0x40, 0x02, 0x01, 0x0B, 0x00, 0xA0, 0x20, 0x41, 0x16, 0x17, 0x01, 0x05, 0x81, 0xA0, 0x60,
  0x41, 0x14, 0x02, 0x03, 0x41, 0x42, 0x02, 0x02, 0x00, 0x10, 0x03, 0xA0, 0x32, 0x01, 0x06, 0xC1,
  0x32, 0x01, 0x32, 0x00, 0x06, 0xC0, 0x32, 0x00, 0x36, 0x00, 0x36, 0x33, 0x06, 0x02, 0x16, 0xFD,
  0x03, 0xC0, 0x0F, 0x00, 0xC0, 0x60, 0x41, 0x18, 0x0C, 0x00, 0x02, 0x00, 0x3F, 0x00, 0x02, 0x01,
  0x41, 0x42, 0x02, 0x02, 0x00, 0x08, 0xCC, 0x31, 0x06, 0x02, 0x16, 0xFD, 0x0C, 0x00, 0x02, 0x01,
  0x41, 0x4C, 0xD0, 0xA0, 0x41, 0x50, 0x06, 0xC2, 0xD0, 0xA0, 0x41, 0x4F, 0x02, 0x03, 0x0B, 0x00,
  0x03, 0xA0, 0x32, 0x03, 0x32, 0x31, 0x32, 0x31, 0x32, 0x31, 0x36, 0x01, 0x36, 0x30, 0x06, 0x02,
  0x16, 0xFD, 0x03, 0xC0, 0x0C, 0x00, 0x03, 0x40
};

/* -------------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------- */
static int didFault = 0;

/* -------------------------------------------------------------------------
 * Hard-fault handler (triggered by MPU for GPU DMA requests)
 * ---------------------------------------------------------------------- */
#ifdef PICO_BUILD
void isr_hardfault(void)
{
  didFault = 1;
  TMS_REGISTER(tms9918, 0x38) = 0; /* Stop the GPU */
  mpu_hw->ctrl = 0;                /* Turn off memory protection - all models */
}
#endif /* PICO_BUILD */

/* -------------------------------------------------------------------------
 * Run a GPU DMA job
 * ---------------------------------------------------------------------- */
static void triggerGpuDma(VR_EMU_INST_ONLY_ARG)
{
  uint32_t srcVramAddr = __builtin_bswap16(*(uint16_t*)(tms9918->vram.bytes + 0x8000));
  uint32_t dstVramAddr = __builtin_bswap16(*(uint16_t*)(tms9918->vram.bytes + 0x8002));
  uint32_t width  = tms9918->vram.bytes[0x8004];
  uint32_t height = tms9918->vram.bytes[0x8005];
  uint32_t stride = tms9918->vram.bytes[0x8006];
  uint32_t params = tms9918->vram.bytes[0x8007];

  int32_t dstInc = (params & 0x02) ? -1 : 1;
  int32_t srcInc = (params & 0x01) ? 0 : dstInc;

  uint8_t* srcPtr = tms9918->vram.bytes + srcVramAddr;
  uint8_t* dstPtr = tms9918->vram.bytes + dstVramAddr;
  for (uint32_t y = 0; y < height; ++y)
  {
    for (uint32_t x = 0; x < width; ++x, srcPtr += srcInc, dstPtr += dstInc)
      *dstPtr = *srcPtr;
    srcPtr += (stride - width) * srcInc;
    dstPtr += (stride - width) * dstInc;
  }

  *(uint16_t*)(tms9918->vram.bytes + 0x8008) = 0;
}

/* -------------------------------------------------------------------------
 * Set up MPU guard for a 32-byte page (Pico only)
 * ---------------------------------------------------------------------- */
#ifdef PICO_BUILD
static void guard(void* a)
{
  uintptr_t addr = (uintptr_t)a;
#if PICO_RP2040
  mpu_hw->rbar = (addr & (uint)~0xff) | M0PLUS_MPU_RBAR_VALID_BITS | 0;
  mpu_hw->rasr = 1 | (0x07 << 1) | (0xfe << 8) | 0x10000000;
#else
  mpu_hw->rnr  = 0;
  mpu_hw->rbar = (addr & (uint)~31u) | (2u << M33_MPU_RBAR_AP_LSB) | M33_MPU_RBAR_XN_BITS;
  mpu_hw->rlar = (addr & (uint)~31u) | M33_MPU_RLAR_EN_BITS;
#endif
}
#endif /* PICO_BUILD */

/* -------------------------------------------------------------------------
 * Core GPU execution (non-inlined for stack safety)
 * ---------------------------------------------------------------------- */
static void __attribute__((noinline)) volatileHack(VR_EMU_INST_ONLY_ARG)
{
  tms9918->restart = 0;
  if ((tms9918->gpuAddress & 1) == 0) /* Odd addresses crash the RP2040 */
  {
    uint16_t lastAddress = tms9918->gpuAddress;

restart:
    TMS_REGISTER(tms9918, 0x38) = 1;
    TMS_STATUS(tms9918, 2) |= 0x80; /* Running */

#ifdef PICO_BUILD
#if PICO_RP2040
    mpu_hw->ctrl = M0PLUS_MPU_CTRL_PRIVDEFENA_BITS | M0PLUS_MPU_CTRL_ENABLE_BITS;
#else
    mpu_hw->ctrl = M33_MPU_CTRL_PRIVDEFENA_BITS | M33_MPU_CTRL_ENABLE_BITS;
#endif
#endif /* PICO_BUILD */

    lastAddress = run9900(tms9918->vram.bytes, lastAddress, 0xFFFE, &TMS_REGISTER(tms9918, 0x38));

#ifdef PICO_BUILD
    mpu_hw->ctrl = 0; /* Turn off memory protection - all models */
#endif

    if (TMS_REGISTER(tms9918, 0x38) & 1)
    {
      tms9918->gpuAddress = lastAddress;
      tms9918->restart = 0;
    }
    if (tms9918->vram.bytes[0x8008])
    {
      triggerGpuDma(VR_EMU_INST_ONLY);
    }
    if (didFault)
    {
      didFault = 0;
      goto restart;
    }
  }
  TMS_STATUS(tms9918, 2) &= ~0x80; /* Stopped */
  TMS_REGISTER(tms9918, 0x38) = 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/*
 * Initialize the TMS9900 GPU
 */
void vrEmuTmsGpuInit(VR_EMU_INST_ONLY_ARG)
{
  memcpy(tms9918->vram.map.gram1,        preload, sizeof(preload));
  memcpy(tms9918->vram.map.gram1 + 0x800, preload, sizeof(preload));

  tms9918->gpuAddress = 0x4000;

#ifdef PICO_BUILD
  guard(&(tms9918->vram.bytes[0x8000]));
#endif
}

static bool reportedBack = true;
static uint32_t gpuTimeUs = 0;

/*
 * Return GPU CPU time in microseconds.
 */
uint32_t vrEmuTmsGpuTime(uint32_t totalTime)
{
  if (!reportedBack)
    return totalTime;
  return gpuTimeUs;
}

/*
 * Reset internal GPU time accumulator.
 */
void vrEmuTmsGpuResetTime(void)
{
  gpuTimeUs = 0;
}

/*
 * GPU main loop — runs indefinitely, call from a dedicated core/thread.
 */
void vrEmuTmsGpuLoop(VR_EMU_INST_ONLY_ARG)
{
  while (1)
  {
    if (tms9918->restart)
    {
      reportedBack = false;
      uint32_t gpuStart = time_us_32();
      volatileHack(VR_EMU_INST_ONLY);
      gpuTimeUs += time_us_32() - gpuStart;
    }
    reportedBack = true;

    if (tms9918->flash)
    {
      if (vrTmsGpuFlashCb) vrTmsGpuFlashCb();
    }

    if (tms9918->config[CONF_SAVE_TO_FLASH])
    {
      tms9918->config[CONF_SAVE_TO_FLASH] = 0;
      if (vrTmsGpuConfigSaveCb) vrTmsGpuConfigSaveCb(tms9918->config);
    }
  }
}
