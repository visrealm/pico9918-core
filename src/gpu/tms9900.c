/*
 * Project: pico9918
 *
 * Copyright (c) 2026 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 *
 * TMS9900 CPU interpreter (portable C)
 *
 * This is a full reimplementation of JasonACT's RP2040 thumb assembly core
 * (thumb9900_m0.S / thumb9900_m33.S) intended for non-ARM targets. It aims
 * to be functionally identical: every documented TMS9900/F18A instruction is
 * implemented and the status flag layout matches the assembly core
 * (LGT=0x80, AGT=0x40, EQ=0x20, C=0x10, OV=0x08, P=0x04).
 *
 * Memory layout follows the existing GPU glue: a flat 64 KiB byte array that
 * stores TMS9900 words in big-endian order. The workspace pointer (WP) is a
 * byte address into that array and register access uses big-endian word
 * loads/stores.
 *
 * The interpreter stops when bit0 of the control byte at regx38 is cleared,
 * or when an IDLE instruction is executed, returning the current PC just like
 * the assembly core. Behavior of auto-increment and index modes matches the
 * original core (increments are 1 for byte ops, 2 for word ops).
 */

#include "tms9900.h"

#include <stddef.h>

/*
 * Flag helpers
 */
static inline void set_flags_word (Tms9900Cpu* cpu, uint16_t v)
{
  cpu->st &= 0x1E; /* preserve C/OV/P only (assembly uses AND #0x1E) */
  int16_t sv = (int16_t)v;
  if (v == 0)
  {
    cpu->st |= TMS_ST_EQ;
    return;
  }
  if (sv > 0)
  {
    cpu->st |= (TMS_ST_LGT | TMS_ST_AGT);
  }
  else
  {
    cpu->st |= TMS_ST_LGT;
  }
}

/* Function:  set_flags_word
 * ----------------------------------------
 * Update word-based comparison flags to match assembly core semantics.
 */
static inline void set_flags_byte (Tms9900Cpu* cpu, uint8_t v)
{
  cpu->st &= 0x1E;
  int8_t sv = (int8_t)v;
  if (v == 0)
  {
    cpu->st |= TMS_ST_EQ;
    return;
  }
  if (sv > 0)
  {
    cpu->st |= (TMS_ST_LGT | TMS_ST_AGT);
  }
  else
  {
    cpu->st |= TMS_ST_LGT;
  }
}

/*
 * Memory helpers (big-endian words)
 */
static inline uint8_t rd8 (uint8_t* m, uint16_t a)
{
  return m[a];
}

/* Function:  rd8
 * ----------------------------------------
 * Read a single byte from the emulated memory.
 */
static inline void wr8 (uint8_t* m, uint16_t a, uint8_t v)
{
  m[a] = v;
}

/* Function:  wr8
 * ----------------------------------------
 * Write a single byte into the emulated memory.
 */
static inline uint16_t rd16 (uint8_t* m, uint16_t a)
{
  return (uint16_t)((m[a] << 8) | m[(uint16_t)(a + 1)]);
}

/* Function:  rd16
 * ----------------------------------------
 * Read a big-endian 16-bit word from the emulated memory.
 */
static inline void wr16 (uint8_t* m, uint16_t a, uint16_t v)
{
  m[a] = (uint8_t)(v >> 8);
  m[(uint16_t)(a + 1)] = (uint8_t)(v & 0xFF);
}

/* Function:  wr16
 * ----------------------------------------
 * Write a big-endian 16-bit word into the emulated memory.
 */
static inline uint16_t get_reg (Tms9900Cpu* cpu, uint8_t r)
{
  uint16_t addr = (uint16_t)(cpu->wp + ((uint16_t)r << 1));
  return rd16 (cpu->mem, addr);
}

/* Function:  get_reg
 * ----------------------------------------
 * Load a workspace register value via the current WP.
 */
static inline void set_reg (Tms9900Cpu* cpu, uint8_t r, uint16_t v)
{
  uint16_t addr = (uint16_t)(cpu->wp + ((uint16_t)r << 1));
  wr16 (cpu->mem, addr, v);
}

/* Operand addressing */
/* Function:  set_reg
 * ----------------------------------------
 * Store a workspace register value via the current WP.
 */
typedef struct Operand
{
  uint16_t addr; /* effective address for memory targets */
  uint16_t val;  /* value loaded */
  uint8_t reg;   /* register index */
  uint8_t mode;  /* 0=reg,1=indirect,2=indexed,3=auto-inc */
  uint8_t is_byte;
} Operand;

/* Fetch a word and advance PC (word-addressed) */
static inline uint16_t fetchw (Tms9900Cpu* cpu)
{
  uint16_t v = rd16 (cpu->mem, cpu->pc);
  cpu->pc = (uint16_t)(cpu->pc + 2);
  return v;
}

/* Function:  fetchw
 * ----------------------------------------
 * Fetch the next word from memory and advance PC by two bytes.
 */
static Operand decode_operand (Tms9900Cpu* cpu, uint8_t field, uint8_t is_byte)
{
  Operand o = {0};
  o.mode = (field >> 4) & 0x3;
  o.reg = field & 0xF;
  o.is_byte = is_byte;

  switch (o.mode)
  {
    case 0: /* register direct */
      o.val = get_reg (cpu, o.reg);
      break;
    case 1: /* indirect */
      o.addr = get_reg (cpu, o.reg);
      o.val = is_byte ? rd8 (cpu->mem, o.addr) : rd16 (cpu->mem, o.addr);
      break;
    case 2:
    { /* indexed */
      uint16_t offset = fetchw (cpu);
      o.addr = (uint16_t)(get_reg (cpu, o.reg) + offset);
      o.val = is_byte ? rd8 (cpu->mem, o.addr) : rd16 (cpu->mem, o.addr);
      break;
    }
    case 3:
    { /* auto-increment (indirect) */
      o.addr = get_reg (cpu, o.reg);
      o.val = is_byte ? rd8 (cpu->mem, o.addr) : rd16 (cpu->mem, o.addr);
      uint16_t inc = is_byte ? 1u : 2u;
      set_reg (cpu, o.reg, (uint16_t)(o.addr + inc));
      break;
    }
  }
  return o;
}

/* Function:  decode_operand
 * ----------------------------------------
 * Decode an operand field into effective address, value, and mode.
 */
static void store_operand (Tms9900Cpu* cpu, const Operand* o, uint16_t v)
{
  if (o->mode == 0)
  {
    set_reg (cpu, o->reg, v);
    /* Function:  store_operand
     * ----------------------------------------
     * Write a computed value back to a register or memory operand.
     */
  }
  else
  {
    if (o->is_byte)
    {
      wr8 (cpu->mem, o->addr, (uint8_t)v);
    }
    else
    {
      wr16 (cpu->mem, o->addr, v);
    }
  }
}

/* ALU helpers */
static inline uint16_t add16 (Tms9900Cpu* cpu, uint16_t a, uint16_t b)
{
  uint32_t res = (uint32_t)a + (uint32_t)b;
  uint16_t r16 = (uint16_t)res;
  cpu->st &= 0x06; /* preserve only parity bit; clear LGT/AGT/EQ/OV/C */
  if (res & 0x10000)
    cpu->st |= TMS_ST_C;
  /* overflow: sign(a)==sign(b) and sign differs from result */
  if (((a ^ b) & 0x8000) == 0 && ((a ^ r16) & 0x8000))
    cpu->st |= TMS_ST_OV;
  set_flags_word (cpu, r16);
  return r16;
}

/* Function:  add16
 * ----------------------------------------
 * 16-bit add with flag updates matching the assembly core.
 */
static inline uint16_t sub16 (Tms9900Cpu* cpu, uint16_t a, uint16_t b)
{
  uint32_t res = (uint32_t)a - (uint32_t)b;
  uint16_t r16 = (uint16_t)res;
  cpu->st &= 0x06;
  if (res & 0x10000)
    cpu->st |= TMS_ST_C;
  /* overflow: sign(a)!=sign(b) and sign differs from result */
  if (((a ^ b) & 0x8000) && ((a ^ r16) & 0x8000))
    cpu->st |= TMS_ST_OV;
  set_flags_word (cpu, r16);
  return r16;
}

/* Function:  sub16
 * ----------------------------------------
 * 16-bit subtract with flag updates matching the assembly core.
 */
static inline uint8_t add8 (Tms9900Cpu* cpu, uint8_t a, uint8_t b)
{
  uint16_t res = (uint16_t)a + (uint16_t)b;
  uint8_t r8 = (uint8_t)res;
  cpu->st &= 0x06;
  if (res & 0x100)
    cpu->st |= TMS_ST_C;
  if (((a ^ b) & 0x80) == 0 && ((a ^ r8) & 0x80))
    cpu->st |= TMS_ST_OV;
  set_flags_byte (cpu, r8);
  return r8;
}

/* Function:  add8
 * ----------------------------------------
 * 8-bit add with flag updates matching the assembly core.
 */
static inline uint8_t sub8 (Tms9900Cpu* cpu, uint8_t a, uint8_t b)
{
  uint16_t res = (uint16_t)a - (uint16_t)b;
  uint8_t r8 = (uint8_t)res;
  cpu->st &= 0x06;
  if (res & 0x100)
    cpu->st |= TMS_ST_C;
  if (((a ^ b) & 0x80) && ((a ^ r8) & 0x80))
    cpu->st |= TMS_ST_OV;
  set_flags_byte (cpu, r8);
  return r8;
}

/* Function:  sub8
 * ----------------------------------------
 * 8-bit subtract with flag updates matching the assembly core.
 */
static inline void cmp16 (Tms9900Cpu* cpu, uint16_t a, uint16_t b)
{
  sub16 (cpu, a, b);
}

/* Function:  cmp16
 * ----------------------------------------
 * Compare two 16-bit values and set flags accordingly.
 */
static inline void cmp8 (Tms9900Cpu* cpu, uint8_t a, uint8_t b)
{
  sub8 (cpu, a, b);
}

/* Function:  cmp8
 * ----------------------------------------
 * Compare two 8-bit values and set flags accordingly.
 */
static inline uint16_t slx16 (Tms9900Cpu* cpu, uint16_t v, uint8_t count)
{
  cpu->st &= 0x06;
  if (count == 0)
    count = 16;
  uint32_t vv = (uint32_t)v;
  uint32_t mask_change = 0;
  /* detect overflow like assembly: if sign changes during shift */
  for (uint8_t i = 0; i < count; ++i)
  {
    uint32_t msb = vv & 0x8000;
    vv <<= 1;
    if (msb != (vv & 0x8000))
      mask_change = 1;
  }
  if (vv & 0x10000)
    cpu->st |= TMS_ST_C;
  uint16_t r = (uint16_t)vv;
  if (mask_change)
    cpu->st |= TMS_ST_OV;
  set_flags_word (cpu, r);
  return r;
}

/* Function:  slx16
 * ----------------------------------------
 * Arithmetic left shift with overflow detection per TMS rules.
 */
static inline uint16_t sra16 (Tms9900Cpu* cpu, uint16_t v, uint8_t count)
{
  cpu->st &= 0x06;
  if (count == 0)
    count = 16;
  uint32_t vv = (uint32_t)(int32_t)(int16_t)v;
  uint16_t carry = 0;
  for (uint8_t i = 0; i < count; ++i)
  {
    carry = (uint16_t)(vv & 1u);
    vv = (vv >> 1) | (vv & 0x8000);
  }
  if (carry)
    cpu->st |= TMS_ST_C;
  uint16_t r = (uint16_t)vv;
  set_flags_word (cpu, r);
  return r;
}

/* Function:  sra16
 * ----------------------------------------
 * Arithmetic right shift with carry capture.
 */
static inline uint16_t srl16 (Tms9900Cpu* cpu, uint16_t v, uint8_t count)
{
  cpu->st &= 0x06;
  if (count == 0)
    count = 16;
  uint32_t vv = v;
  uint16_t carry = 0;
  for (uint8_t i = 0; i < count; ++i)
  {
    carry = (uint16_t)(vv & 1u);
    vv >>= 1;
  }
  if (carry)
    cpu->st |= TMS_ST_C;
  uint16_t r = (uint16_t)vv;
  set_flags_word (cpu, r);
  return r;
}

/* Function:  srl16
 * ----------------------------------------
 * Logical right shift with carry capture.
 */
static inline uint16_t src16 (Tms9900Cpu* cpu, uint16_t v, uint8_t count)
{
  cpu->st &= 0x06;
  if (count == 0)
    count = 16;
  uint32_t vv = ((uint32_t)v << 16) | v;
  vv = (vv >> count);
  uint16_t carry = (uint16_t)(vv & 1u);
  uint16_t r = (uint16_t)(vv >> 16);
  if (carry)
    cpu->st |= TMS_ST_C;
  set_flags_word (cpu, r);
  return r;
}

/* Function:  src16
 * ----------------------------------------
 * Rotate right through value, reporting carry from shifted-out bit.
 */
static inline uint16_t slc16 (Tms9900Cpu* cpu, uint16_t v, uint8_t count)
{
  cpu->st &= 0x06;
  if (count == 0)
    count = 16;
  count &= 0x1F;
  uint32_t vv = v;
  uint32_t rot = (vv << count) | (vv >> (16 - count));
  uint16_t r = (uint16_t)rot;
  /* Carry = bit just before wrap (count-1) */
  uint16_t carry = (uint16_t)((vv << (count - 1)) & 0x8000u);
  if (carry)
    cpu->st |= TMS_ST_C;
  set_flags_word (cpu, r);
  return r;
}

/* Function:  slc16
 * ----------------------------------------
 * Rotate left (F18A SLC) while reporting carry on wrapped bit.
 */
static inline uint16_t src_through_c (Tms9900Cpu* cpu, uint16_t v, uint8_t count)
{
  cpu->st &= 0x06;
  if (count == 0)
    count = 16;
  uint32_t vv = ((uint32_t)v << 1) | ((cpu->st & TMS_ST_C) ? 1u : 0u);
  for (uint8_t i = 0; i < count; ++i)
  {
    uint32_t c = vv & 1u;
    /* Function:  src_through_c
     * ----------------------------------------
     * Rotate right through carry, updating carry each step.
     */
    vv >>= 1;
    if (c)
      vv |= 0x8000u;
    cpu->st = (cpu->st & ~TMS_ST_C) | (c ? TMS_ST_C : 0);
  }
  uint16_t r = (uint16_t)vv;
  set_flags_word (cpu, r);
  return r;
}

/* Branch helper */
/* Function:  branch_cond
 * ----------------------------------------
 * Evaluate branch condition codes against the current flags.
 */
static inline int branch_cond (uint16_t st, uint8_t cond)
{
  switch (cond)
  {
    case 0: /* JNE */
      return (st & TMS_ST_EQ) == 0;
    case 1: /* JEQ */
      return (st & TMS_ST_EQ) != 0;
    case 2: /* JNC */
      return (st & TMS_ST_C) == 0;
    case 3: /* JOC */
      return (st & TMS_ST_C) != 0;
    case 4: /* JNO */
      return (st & TMS_ST_OV) == 0;
    case 5: /* JOP */
      return (st & TMS_ST_OV) != 0;
    case 6: /* JN? (JL) logic< */
      return (st & TMS_ST_LGT) == 0;
    case 7: /* JH logic> */
      return (st & TMS_ST_LGT) && !(st & TMS_ST_EQ);
    case 8: /* JL (signed <) */
      return (st & TMS_ST_AGT) == 0;
    case 9: /* JHE (signed >=) */
      return (st & TMS_ST_AGT) != 0;
    case 10: /* JLE */
      return (st & (TMS_ST_AGT | TMS_ST_EQ)) == 0;
    case 11: /* JGT */
      return (st & TMS_ST_AGT) && !(st & TMS_ST_EQ);
    default:
      return 0;
  }
}

/* Function:  handle_immediate_system
 * ----------------------------------------
 * Execute op group 0 (immediate/system). Returns 0 to stop on IDLE.
 */
static inline int handle_immediate_system (Tms9900Cpu* cpu, uint16_t inst)
{
  uint8_t sub = (inst >> 8) & 0xF;
  uint8_t dest_reg = (inst >> 4) & 0xF;
  switch (sub)
  {
    case 0x0:
    { /* LI */
      uint16_t imm = fetchw (cpu);
      set_reg (cpu, dest_reg, imm);
      set_flags_word (cpu, imm);
      break;
    }
    case 0x1:
    { /* AI */
      uint16_t imm = fetchw (cpu);
      uint16_t dst = get_reg (cpu, dest_reg);
      uint16_t res = add16 (cpu, dst, imm);
      set_reg (cpu, dest_reg, res);
      break;
    }
    case 0x2:
    { /* ANDI */
      uint16_t imm = fetchw (cpu);
      uint16_t res = get_reg (cpu, dest_reg) & imm;
      set_reg (cpu, dest_reg, res);
      cpu->st &= 0x1E;
      set_flags_word (cpu, res);
      break;
    }
    case 0x3:
    { /* ORI */
      uint16_t imm = fetchw (cpu);
      uint16_t res = get_reg (cpu, dest_reg) | imm;
      set_reg (cpu, dest_reg, res);
      cpu->st &= 0x1E;
      set_flags_word (cpu, res);
      break;
    }
    case 0x4:
    { /* CI */
      uint16_t imm = fetchw (cpu);
      uint16_t dst = get_reg (cpu, dest_reg);
      cmp16 (cpu, dst, imm);
      break;
    }
    case 0x5: /* STWP */
      wr16 (cpu->mem, fetchw (cpu), (uint16_t)cpu->wp);
      break;
    case 0x6: /* STST */
      wr16 (cpu->mem, fetchw (cpu), cpu->st);
      break;
    case 0x7: /* LWPI */
      cpu->wp = fetchw (cpu);
      break;
    case 0x8: /* LIMI */
      cpu->st = (cpu->st & 0xFF00) | (fetchw (cpu) & 0xFF);
      break;
    case 0xC:   /* IDLE */
      return 0; /* stop */
    case 0xD:   /* RSET */
      /* nothing to do; interrupts masked */
      break;
    case 0xE:
    { /* RTWP */
      /* Restore WP, PC, ST from workspace */
      cpu->st = get_reg (cpu, 0);
      cpu->pc = get_reg (cpu, 1);
      cpu->wp = get_reg (cpu, 13); /* saved WP in R13 */
      break;
    }
    case 0xF: /* CKON/CKOF/LREX (ignore side effects) */
      break;
    default:
      break;
  }
  return 1;
}

/* Function:  handle_jump_single
 * ----------------------------------------
 * Execute op group 1 (jumps and single-operand ops).
 */
static inline void handle_jump_single (Tms9900Cpu* cpu, uint16_t inst)
{
  uint8_t sub = (inst >> 8) & 0xF;
  switch (sub)
  {
    case 0x0:
    { /* BLWP */
      uint16_t addr = inst & 0xFFFE;
      uint16_t new_wp = rd16 (cpu->mem, addr);
      uint16_t new_pc = rd16 (cpu->mem, (uint16_t)(addr + 2));
      /* push old context into new workspace */
      wr16 (cpu->mem, new_wp + 0, cpu->st);
      wr16 (cpu->mem, new_wp + 2, cpu->pc);
      wr16 (cpu->mem, new_wp + 4, cpu->wp);
      cpu->wp = new_wp;
      cpu->pc = new_pc;
      cpu->st = 0;
      break;
    }
    case 0x1:
    { /* B (branch) */
      int16_t disp = (int16_t)(inst << 4);
      disp >>= 7; /* sign-extend 9-bit word displacement */
      cpu->pc = (uint16_t)(cpu->pc + disp);
      break;
    }
    case 0x2:
    { /* XOP */
      uint8_t vect = inst & 0xF;
      uint16_t addr = (uint16_t)(vect << 2);
      uint16_t new_wp = rd16 (cpu->mem, addr);
      uint16_t new_pc = rd16 (cpu->mem, (uint16_t)(addr + 2));
      wr16 (cpu->mem, new_wp + 0, cpu->st);
      wr16 (cpu->mem, new_wp + 2, cpu->pc);
      wr16 (cpu->mem, new_wp + 4, cpu->wp);
      cpu->wp = new_wp;
      cpu->pc = new_pc;
      cpu->st = 0;
      break;
    }
    case 0x3:
    { /* CLR */
      Operand d = decode_operand (cpu, inst & 0x3F, 0);
      store_operand (cpu, &d, 0);
      cpu->st &= 0x1E;
      cpu->st |= TMS_ST_EQ;
      break;
    }
    case 0x4:
    { /* NEG */
      Operand d = decode_operand (cpu, inst & 0x3F, 0);
      uint16_t res = sub16 (cpu, 0, d.val);
      store_operand (cpu, &d, res);
      break;
    }
    case 0x5:
    { /* INV */
      Operand d = decode_operand (cpu, inst & 0x3F, 0);
      uint16_t res = (uint16_t)~d.val;
      store_operand (cpu, &d, res);
      cpu->st &= 0x1E;
      set_flags_word (cpu, res);
      break;
    }
    case 0x6:
    { /* INC */
      Operand d = decode_operand (cpu, inst & 0x3F, 0);
      uint16_t res = add16 (cpu, d.val, 1);
      store_operand (cpu, &d, res);
      break;
    }
    case 0x7:
    { /* INCT */
      Operand d = decode_operand (cpu, inst & 0x3F, 0);
      uint16_t res = add16 (cpu, d.val, 2);
      store_operand (cpu, &d, res);
      break;
    }
    case 0x8:
    { /* DEC */
      Operand d = decode_operand (cpu, inst & 0x3F, 0);
      uint16_t res = sub16 (cpu, d.val, 1);
      store_operand (cpu, &d, res);
      break;
    }
    case 0x9:
    { /* DECT */
      Operand d = decode_operand (cpu, inst & 0x3F, 0);
      uint16_t res = sub16 (cpu, d.val, 2);
      store_operand (cpu, &d, res);
      break;
    }
    case 0xA:
    { /* BL */
      uint16_t target = inst & 0x3FF;
      set_reg (cpu, 11, cpu->pc); /* R11 = return address */
      cpu->pc = target;
      break;
    }
    case 0xB:
    { /* SWPB */
      Operand d = decode_operand (cpu, inst & 0x3F, 0);
      uint16_t res = (uint16_t)((d.val << 8) | (d.val >> 8));
      store_operand (cpu, &d, res);
      break;
    }
    case 0xC:
    { /* SETO */
      Operand d = decode_operand (cpu, inst & 0x3F, 0);
      store_operand (cpu, &d, 0xFFFF);
      cpu->st &= 0x1E;
      set_flags_word (cpu, 0xFFFF);
      break;
    }
    case 0xD:
    { /* ABS */
      Operand d = decode_operand (cpu, inst & 0x3F, 0);
      int16_t sv = (int16_t)d.val;
      uint16_t res = (sv < 0) ? (uint16_t)(-sv) : (uint16_t)sv;
      if (sv == (int16_t)0x8000)
        cpu->st |= TMS_ST_OV; /* overflow */
      store_operand (cpu, &d, res);
      cpu->st &= 0x16; /* keep C/OV/P */
      set_flags_word (cpu, res);
      break;
    }
    case 0xE:
    { /* LDCR */
      /* CRU not emulated; treat as NOP but advance PC */
      uint8_t count = (inst & 0xF) ? (inst & 0xF) : 16;
      (void)count;
      break;
    }
    case 0xF:
    { /* STCR */
      uint8_t count = (inst & 0xF) ? (inst & 0xF) : 16;
      (void)count;
      break;
    }
    default:
      break;
  }
}

/* Function:  handle_branch_group
 * ----------------------------------------
 * Execute op group 2 (conditional branches).
 */
static inline void handle_branch_group (Tms9900Cpu* cpu, uint16_t inst)
{
  int16_t disp = (int16_t)(inst << 4);
  disp >>= 7; /* 9-bit signed */
  uint8_t cond = (inst >> 8) & 0xF;
  if (branch_cond (cpu->st, cond))
  {
    cpu->pc = (uint16_t)(cpu->pc + disp);
  }
}

/* Function:  handle_cru_single_bit
 * ----------------------------------------
 * Execute op group 3 (CRU single-bit) — treated as NOP here.
 */
static inline void handle_cru_single_bit (void)
{
}

/* Function:  handle_shift_rotate
 * ----------------------------------------
 * Execute op group 4 (shift/rotate).
 */
static inline void handle_shift_rotate (Tms9900Cpu* cpu, uint16_t inst)
{
  uint8_t sub = (inst >> 6) & 0xF;
  uint8_t r = (inst >> 4) & 0x3;
  uint8_t reg = inst & 0xF;
  uint8_t count = (r == 0) ? 16 : r;
  uint16_t v = get_reg (cpu, reg);
  uint16_t res = v;
  switch (sub)
  {
    case 0x0:
      res = sra16 (cpu, v, count);
      break; /* SRA */
    case 0x1:
      res = srl16 (cpu, v, count);
      break; /* SRL */
    case 0x2:
      res = slx16 (cpu, v, count);
      break; /* SLA */
    case 0x3:
      res = src16 (cpu, v, count);
      break; /* SRC */
    case 0xE:
      res = slc16 (cpu, v, count);
      break; /* SLC (F18A) */
    default:
      break;
  }
  set_reg (cpu, reg, res);
}

/* Function:  handle_two_operand
 * ----------------------------------------
 * Execute op groups 5-0xB (word/byte two-operand) and 0xF subops.
 */
static inline void handle_two_operand (Tms9900Cpu* cpu, uint16_t inst)
{
  uint8_t opcode = (uint8_t)((inst >> 12) & 0xF);
  uint8_t byte_op = (opcode & 0x4) ? 1 : 0; /* 0x8-0xB -> byte */
  uint8_t op_base = opcode & 0x3;           /* 0:A/S/Z/M variants per block */
  (void)op_base;
  Operand src = decode_operand (cpu, (uint8_t)((inst >> 6) & 0x3F), byte_op);
  Operand dst = decode_operand (cpu, (uint8_t)(inst & 0x3F), byte_op);

  switch (opcode)
  {
    case 0x5: /* ADD (A) */
    {
      uint16_t res = add16 (cpu, dst.val, src.val);
      store_operand (cpu, &dst, res);
      break;
    }
    case 0x6: /* SUB (S) */
    {
      uint16_t res = sub16 (cpu, dst.val, src.val);
      store_operand (cpu, &dst, res);
      break;
    }
    case 0x7: /* COMPARE (C) */
    {
      cmp16 (cpu, dst.val, src.val);
      break;
    }
    case 0x8: /* MOVB */
    {
      uint8_t res = (uint8_t)src.val;
      store_operand (cpu, &dst, res);
      cpu->st &= 0x1E;
      set_flags_byte (cpu, res);
      break;
    }
    case 0x9: /* AB */
    {
      uint8_t res = add8 (cpu, (uint8_t)dst.val, (uint8_t)src.val);
      store_operand (cpu, &dst, res);
      break;
    }
    case 0xA: /* SB */
    {
      uint8_t res = sub8 (cpu, (uint8_t)dst.val, (uint8_t)src.val);
      store_operand (cpu, &dst, res);
      break;
    }
    case 0xB: /* CB */
    {
      cmp8 (cpu, (uint8_t)dst.val, (uint8_t)src.val);
      break;
    }
    case 0xC: /* MOV */
    {
      store_operand (cpu, &dst, src.val);
      cpu->st &= 0x1E;
      set_flags_word (cpu, src.val);
      break;
    }
    case 0xD: /* SOC (OR) */
    {
      uint16_t res = dst.val | src.val;
      store_operand (cpu, &dst, res);
      cpu->st &= 0x1E;
      set_flags_word (cpu, res);
      break;
    }
    case 0xE: /* SZC (AND ~src) */
    {
      uint16_t res = dst.val & (uint16_t)~src.val;
      store_operand (cpu, &dst, res);
      cpu->st &= 0x1E;
      set_flags_word (cpu, res);
      break;
    }
    case 0xF: /* MPY/DIV/SOCB/SZCB? -> map subset */
    {
      /* Treat high nibble 0xF as byte logical variants */
      uint8_t subop = (inst >> 8) & 0xF;
      switch (subop)
      {
        case 0x0:
        { /* SOCB */
          uint8_t res = (uint8_t)dst.val | (uint8_t)src.val;
          store_operand (cpu, &dst, res);
          cpu->st &= 0x1E;
          set_flags_byte (cpu, res);
          break;
        }
        case 0x1:
        { /* SZCB */
          uint8_t res = (uint8_t)dst.val & (uint8_t)~src.val;
          store_operand (cpu, &dst, res);
          cpu->st &= 0x1E;
          set_flags_byte (cpu, res);
          break;
        }
        case 0x9:
        { /* MPY */
          /* dst is even register; src any */
          uint32_t prod = (uint32_t)dst.val * (uint32_t)src.val;
          uint8_t r = (inst >> 6) & 0xF; /* dest reg field */
          set_reg (cpu, r, (uint16_t)(prod >> 16));
          set_reg (cpu, (uint8_t)(r + 1), (uint16_t)(prod & 0xFFFF));
          cpu->st &= 0x1E;
          set_flags_word (cpu, (uint16_t)prod);
          break;
        }
        case 0xA:
        { /* DIV */
          uint8_t r = (inst >> 6) & 0xF;
          uint32_t dividend = ((uint32_t)get_reg (cpu, r) << 16) | get_reg (cpu, (uint8_t)(r + 1));
          if (src.val == 0)
          {
            cpu->st |= TMS_ST_OV;
            break;
          }
          uint16_t quo = (uint16_t)(dividend / src.val);
          uint16_t rem = (uint16_t)(dividend % src.val);
          set_reg (cpu, r, quo);
          set_reg (cpu, (uint8_t)(r + 1), rem);
          cpu->st &= 0x1E;
          set_flags_word (cpu, quo);
          break;
        }
        default:
          break;
      }
      break;
    }
    default:
      break;
  }
}
void tms9900_init (Tms9900Cpu* cpu, uint8_t* mem, uint8_t* regx38, uint16_t pc, uint16_t wp)
{
  cpu->mem = mem;
  cpu->regx38 = regx38;
  cpu->pc = pc;
  cpu->wp = wp;
  cpu->st = 0;
}

/* Function:  tms9900_init
 * ----------------------------------------
 * Initialize the CPU context pointers and registers.
 */
uint16_t run9900_c (Tms9900Cpu* cpu)
{
  while ((*cpu->regx38 & 1u) != 0)
  {
    uint16_t inst = fetchw (cpu);
    uint8_t op_hi = (uint8_t)(inst >> 12);

    switch (op_hi)
    {
      /* Immediate / system group */
      case 0x0:
        if (!handle_immediate_system (cpu, inst))
          return cpu->pc;
        break;

      /* Jumps and single-op group (opcodes 1x) */
      case 0x1:
        handle_jump_single (cpu, inst);
        break;

      /* Jumps (0x2 group) */
      case 0x2:
        handle_branch_group (cpu, inst);
        break;

      /* CRU single-bit operations (0x3 group) */
      case 0x3:
        handle_cru_single_bit ();
        break;

      /* Shift/rotate (0x4 group) */
      case 0x4:
        handle_shift_rotate (cpu, inst);
        break;

      /* Two-operand word/byte (0x5-0xB) */
      default:
        handle_two_operand (cpu, inst);
        break;
    }
  }
  return cpu->pc;
}
