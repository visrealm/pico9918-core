/*
 * pico9918-core - V9938 base: the command engine
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918-core
 */

#include "impl/pico9918_priv.h"

#if PICO9918_V9938_BASE

void pico9918_v9938_cmd_write(PICO9918_INST_ARG uint8_t reg, uint8_t value)
{
  (void)tms9918;
  (void)reg;
  (void)value;
}

void pico9918_v9938_cmd_status_read(PICO9918_INST_ARG uint8_t reg)
{
  (void)tms9918;
  (void)reg;
}

PICO9918_DLLEXPORT
bool pico9918_cmd_step(PICO9918_INST_ARG uint32_t budget)
{
  (void)tms9918;
  (void)budget;
  return false;
}

#endif
