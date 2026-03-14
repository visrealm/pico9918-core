/*
 * Troy's TMS9918 Emulator - Platform Abstraction
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/vrEmuTms9918
 *
 */

#pragma once

/*
 * Section placement macros
 * On Pico these place data into fast SRAM banks.
 * On desktop they are no-ops.
 */
#ifdef PICO_BUILD
  #define VR_TMS_SECTION_SCRATCH_X(name) __attribute__((section(".scratch_x." #name)))
  #define VR_TMS_SECTION_SCRATCH_Y(name) __attribute__((section(".scratch_y." #name)))
#else
  #define VR_TMS_SECTION_SCRATCH_X(name)
  #define VR_TMS_SECTION_SCRATCH_Y(name)
#endif


/*
 * DMA abstraction macros
 * On Pico these map to hardware DMA calls; on desktop they collapse to memset.
 *
 * Two DMA channels are used:
 *   vrTmsDma8  – 8-bit channel for tmsMemset (byte fills)
 *   vrTmsDma32 – 32-bit channel for scanline bg-color fill
 *
 * The 32-bit fill pattern:
 *   Before the first scanline: VR_TMS_FILL32_INIT configures the channel
 *     (read addr = vrTmsBg, count = TMS9918_PIXELS_X/4, no increment on read)
 *   Each scanline: VR_TMS_FILL32_START triggers the fill (write addr = pixels)
 *   Throughout rendering: VR_TMS_FILL32_WAIT waits for completion
 *   On desktop both START and WAIT execute synchronously so WAIT is always a no-op.
 */
#ifdef PICO_BUILD
  #include "hardware/dma.h"

  /* 8-bit DMA channel – used by tmsMemset */
  extern unsigned int vrTmsDma8;
  /* 32-bit DMA channel – used for scanline bg fill */
  extern unsigned int vrTmsDma32;

  #define VR_TMS_MEMSET8(ptr, val, n) \
    do { \
      uint8_t _v = (uint8_t)(val); \
      dma_channel_set_read_addr(vrTmsDma8, &_v, false); \
      dma_channel_set_trans_count(vrTmsDma8, (n), false); \
      dma_channel_set_write_addr(vrTmsDma8, (ptr), true); \
      dma_channel_wait_for_finish_blocking(vrTmsDma8); \
    } while (0)

  /* FILL32: configure once per library init */
  #define VR_TMS_FILL32_INIT(bgPtr, count) \
    do { \
      dma_channel_config _cfg = dma_channel_get_default_config(vrTmsDma32); \
      channel_config_set_read_increment(&_cfg, false); \
      channel_config_set_write_increment(&_cfg, true); \
      channel_config_set_transfer_data_size(&_cfg, DMA_SIZE_32); \
      dma_channel_set_config(vrTmsDma32, &_cfg, false); \
      dma_channel_set_read_addr(vrTmsDma32, (bgPtr), false); \
      dma_channel_set_trans_count(vrTmsDma32, (count), false); \
    } while (0)

  /* FILL32: trigger fill (starts asynchronously) */
  #define VR_TMS_FILL32_START(dstPtr)  dma_channel_set_write_addr(vrTmsDma32, (dstPtr), true)

  /* FILL32: wait for completion */
  #define VR_TMS_FILL32_WAIT()         dma_channel_wait_for_finish_blocking(vrTmsDma32)

#else /* !PICO_BUILD */
  #include <string.h>

  #define VR_TMS_MEMSET8(ptr, val, n)  memset((ptr), (val), (n))

  /* On desktop the bg pointer/count are captured at init time for use in START */
  extern void*    _vrTmsFill32Src;
  extern unsigned _vrTmsFill32Count;

  #define VR_TMS_FILL32_INIT(bgPtr, count) \
    do { _vrTmsFill32Src = (bgPtr); _vrTmsFill32Count = (count); } while (0)

  #define VR_TMS_FILL32_START(dstPtr) \
    do { \
      uint32_t _v = *(uint32_t*)_vrTmsFill32Src; \
      uint32_t* _d = (uint32_t*)(dstPtr); \
      for (unsigned _i = 0; _i < _vrTmsFill32Count; ++_i) _d[_i] = _v; \
    } while (0)

  #define VR_TMS_FILL32_WAIT()  /* no-op: START is synchronous */

#endif /* PICO_BUILD */



/*
 * Timer abstraction
 * time_us_32() returns a 32-bit microsecond counter.
 */
#ifndef PICO_BUILD
  #ifdef _WIN32
    #include <windows.h>
    static inline uint32_t time_us_32(void)
    {
      LARGE_INTEGER freq, cnt;
      QueryPerformanceFrequency(&freq);
      QueryPerformanceCounter(&cnt);
      return (uint32_t)((cnt.QuadPart * 1000000ULL) / freq.QuadPart);
    }
  #else
    #include <time.h>
    static inline uint32_t time_us_32(void)
    {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (uint32_t)(ts.tv_sec * 1000000UL + ts.tv_nsec / 1000UL);
    }
  #endif
#endif /* !PICO_BUILD */


/*
 * __time_critical_func / __not_in_flash_func
 * Already defined in pico/stdlib.h on Pico; no-op on desktop.
 */
#ifndef PICO_BUILD
  #ifndef __time_critical_func
    #define __time_critical_func(fn) fn
  #endif
  #ifndef __not_in_flash_func
    #define __not_in_flash_func(fn) fn
  #endif
  #ifndef __force_inline
    #define __force_inline inline
  #endif
#endif /* !PICO_BUILD */


/*
 * __builtin_bswap16
 * GCC/Clang provide this intrinsic; MSVC does not.
 */
#if defined(_MSC_VER) && !defined(__clang__)
  #include <stdlib.h>
  #define __builtin_bswap16(x) _byteswap_ushort(x)
#endif
