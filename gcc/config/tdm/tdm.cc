/* Target code for TDM (The Disintegrated Machine).
   Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */

#define IN_TARGET_CODE 1

#include "config.h"
#define INCLUDE_STRING
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "fold-const.h"
#include "stringpool.h"
#include "attribs.h"
#include "df.h"
#include "regs.h"
#include "memmodel.h"
#include "tm_p.h"
#include "insn-config.h"
#include "emit-rtl.h"
#include "recog.h"
#include "insn-attr.h"
#include "diagnostic-core.h"
#include "output.h"
#include "stor-layout.h"
#include "varasm.h"
#include "calls.h"
#include "explow.h"
#include "expr.h"
#include "optabs.h"
#include "builtins.h"

/* This file should be included last.  */
#include "target-def.h"

/* Instruction output.

   An instruction combines a register operand (OPA: $pc, $sp, $r0 or
   $r1) with a second operand (OPB): an immediate, a register other
   than $r1 plus an immediate, or the memory word at such an address.
   The register operand is the destination unless the instruction is
   written with the memory operand first.  Only $r0, $sp and $pc can be
   the base of an address.

   The patterns accept operand combinations that need more than one
   instruction; the functions below expand them.  An operation that
   needs a register uses $r1, saved in the scratch word [$sp-1] or
   swapped with the destination.  Data pointers are byte addresses: an
   access through $r0 rotates it right (halving it, as it's even) and
   back afterwards.  $sp holds a word address, which the compiler sees
   doubled.  */

enum tdm_loc_kind
{
  LOC_R0, LOC_R1, LOC_SP, LOC_PC,	/* Registers.  */
  LOC_MR,				/* $mrN.  */
  LOC_FP,				/* $mr6 as the frame pointer.  */
  LOC_SPMEM, LOC_ABSMEM, LOC_R0MEM,	/* Memory words.  */
  LOC_FPMEM,				/* A word addressed through $mr6.  */
  LOC_SCRATCH,				/* [$sp-1].  */
  LOC_R0SAVE,				/* [$sp], holding $r0.  */
  LOC_IMM, LOC_SYM			/* Constants.  */
};

struct tdm_loc
{
  enum tdm_loc_kind kind;
  /* The $mr number, the word offset of a memory word, or the value of
     an immediate.  */
  HOST_WIDE_INT n;
  /* QImode memory: the octet is the high one of the word, or unknown
     at compile time.  */
  bool high, dynamic;
  /* LOC_R0MEM: $r0 holds a word address rather than a byte pointer.  */
  bool word;
  rtx sym;
};

static bool tdm_counting;
static int tdm_words;

static tdm_loc
tdm_make_loc (enum tdm_loc_kind kind, HOST_WIDE_INT n = 0)
{
  tdm_loc l = {};
  l.kind = kind;
  l.n = n;
  return l;
}

static const tdm_loc tdm_r1 = tdm_make_loc (LOC_R1);

static HOST_WIDE_INT
tdm_sext16 (HOST_WIDE_INT v)
{
  return ((v & 0xffff) ^ 0x8000) - 0x8000;
}

/* Describe operand X.  */

static tdm_loc
tdm_classify (rtx x)
{
  if (SUBREG_P (x))
    {
      if (REG_P (SUBREG_REG (x)))
	x = gen_rtx_REG (GET_MODE (x), subreg_regno (x));
      else
	x = adjust_address_nv (SUBREG_REG (x), GET_MODE (x),
			       SUBREG_BYTE (x));
    }

  switch (GET_CODE (x))
    {
    case REG:
      switch (REGNO (x))
	{
	case R0_REGNUM:
	  return tdm_make_loc (LOC_R0);
	case R1_REGNUM:
	  return tdm_make_loc (LOC_R1);
	case SP_REGNUM:
	  return tdm_make_loc (LOC_SP);
	case PC_REGNUM:
	  return tdm_make_loc (LOC_PC);
	case HARD_FRAME_POINTER_REGNUM:
	  if (frame_pointer_needed)
	    return tdm_make_loc (LOC_FP);
	  /* Fall through.  */
	default:
	  gcc_assert (TDM_MR_P (REGNO (x)));
	  return tdm_make_loc (LOC_MR, REGNO (x) - MR0_REGNUM);
	}

    case MEM:
      {
	rtx addr = XEXP (x, 0);
	HOST_WIDE_INT off = 0;
	tdm_loc l;

	if (GET_CODE (addr) == PLUS && CONST_INT_P (XEXP (addr, 1)))
	  {
	    off = INTVAL (XEXP (addr, 1));
	    addr = XEXP (addr, 0);
	  }
	if (CONST_INT_P (addr))
	  l = tdm_make_loc (LOC_ABSMEM, INTVAL (addr) + off);
	else if (REG_P (addr) && REGNO (addr) == SP_REGNUM)
	  l = tdm_make_loc (LOC_SPMEM, off);
	else if (REG_P (addr) && REGNO (addr) == HARD_FRAME_POINTER_REGNUM
		 && frame_pointer_needed)
	  l = tdm_make_loc (LOC_FPMEM, off);
	else if (REG_P (addr) && REGNO (addr) == R0_REGNUM)
	  {
	    l = tdm_make_loc (LOC_R0MEM, off);
	    l.dynamic = GET_MODE (x) == QImode && MEM_ALIGN (x) < 16;
	  }
	else
	  gcc_unreachable ();
	l.high = l.n & 1;
	l.n >>= 1;
	return l;
      }

    case CONST_INT:
      return tdm_make_loc (LOC_IMM, tdm_sext16 (INTVAL (x)));

    case SYMBOL_REF:
    case LABEL_REF:
    case CONST:
      {
	tdm_loc l = tdm_make_loc (LOC_SYM);
	l.sym = x;
	return l;
      }

    default:
      gcc_unreachable ();
    }
}

static bool
tdm_reg_loc_p (const tdm_loc &l)
{
  return l.kind == LOC_R0 || l.kind == LOC_R1;
}

static bool
tdm_mem_loc_p (const tdm_loc &l)
{
  return (l.kind == LOC_SPMEM || l.kind == LOC_ABSMEM
	  || l.kind == LOC_R0MEM || l.kind == LOC_FPMEM);
}

/* Memory addressed through a byte pointer in $r0, which an access
   rotates right and back.  */

static bool
tdm_wrap_p (const tdm_loc &l)
{
  return l.kind == LOC_R0MEM && !l.word;
}

/* A stack slot beyond the reach of an [$sp+IMM] operand, or one
   addressed through the frame pointer: memory words that need $r0 as
   their base.  */

static bool
tdm_far_p (const tdm_loc &l)
{
  return ((l.kind == LOC_SPMEM && !IN_RANGE (l.n, -32, 31))
	  || l.kind == LOC_FPMEM);
}

static bool
tdm_loc_equal_p (const tdm_loc &a, const tdm_loc &b)
{
  if (a.kind != b.kind)
    return false;
  if (a.kind == LOC_SYM)
    return rtx_equal_p (a.sym, b.sym);
  return a.n == b.n && a.high == b.high;
}

/* The name of the register in location L.  */

static const char *
tdm_reg_text (const tdm_loc &l)
{
  switch (l.kind)
    {
    case LOC_R0: return "$r0";
    case LOC_R1: return "$r1";
    case LOC_SP: return "$sp";
    case LOC_PC: return "$pc";
    default: gcc_unreachable ();
    }
}

/* The second operand (OPB) text for location L.  */

static const char *
tdm_opb_text (const tdm_loc &l)
{
  static char bufs[4][32];
  static int next;
  char *b = bufs[next++ & 3];
  int n = (int) l.n;

  switch (l.kind)
    {
    case LOC_R0: return "$r0";
    case LOC_SP: return "$sp";
    case LOC_SCRATCH: return "[$sp-1]";
    case LOC_R0SAVE: return "[$sp]";
    case LOC_MR: snprintf (b, 32, "$mr%d", n); break;
    case LOC_SPMEM: snprintf (b, 32, n ? "[$sp%+d]" : "[$sp]", n); break;
    case LOC_R0MEM: snprintf (b, 32, n ? "[$r0%+d]" : "[$r0]", n); break;
    case LOC_ABSMEM: snprintf (b, 32, "[%d]", n); break;
    case LOC_IMM: snprintf (b, 32, "%d", n); break;
    default: gcc_unreachable ();
    }
  return b;
}

/* The memory registers $mr3-$mr5 hold constants.  */

static const char *
tdm_const_reg_text (HOST_WIDE_INT v)
{
  switch (v & 0xffff)
    {
    case 0x00ff: return "$mr3";
    case 0xff00: return "$mr4";
    case 0x7fff: return "$mr5";
    default: return NULL;
    }
}

/* Write the assembler expression for label X to BUF.  */

static void
tdm_label_text (rtx x, char *buf, size_t len)
{
  if (LABEL_REF_P (x))
    x = label_ref_label (x);
  snprintf (buf, len, ".L%d", CODE_LABEL_NUMBER (x));
}

/* Write the assembler expression for the value of the symbolic
   constant X to BUF.  Data addresses are byte addresses; code
   addresses (functions and labels) are word addresses.  */

static void
tdm_symbolic_text (rtx x, char *buf, size_t len)
{
  HOST_WIDE_INT off = 0;

  if (GET_CODE (x) == CONST)
    x = XEXP (x, 0);
  if (GET_CODE (x) == PLUS && CONST_INT_P (XEXP (x, 1)))
    {
      off = INTVAL (XEXP (x, 1));
      x = XEXP (x, 0);
    }

  if (LABEL_REF_P (x))
    {
      char lab[32];
      tdm_label_text (x, lab, sizeof lab);
      snprintf (buf, len, "%s%+d", lab, (int) off / 2);
      return;
    }

  gcc_assert (SYMBOL_REF_P (x));
  const char *name = targetm.strip_name_encoding (XSTR (x, 0));
  /* As output_addr_const and assemble_name do: this is how weak
     references get declared weak.  */
  if (!tdm_counting)
    {
      if (SYMBOL_REF_DECL (x))
	assemble_external (SYMBOL_REF_DECL (x));
      if (tree id = maybe_get_identifier (name))
	mark_referenced (id);
    }
  if (SYMBOL_REF_FUNCTION_P (x))
    {
      if (off)
	snprintf (buf, len, "%s%+d", name, (int) off / 2);
      else
	snprintf (buf, len, "%s", name);
    }
  else if (off)
    snprintf (buf, len, "%%bptr(%s) %c %d", name, off < 0 ? '-' : '+',
	      (int) (off < 0 ? -off : off));
  else
    snprintf (buf, len, "%%bptr(%s)", name);
}

/* Emit one instruction or data word.  When only counting (for the
   length attribute), count it.  */

static void ATTRIBUTE_PRINTF_1
tdm_emit (const char *fmt, ...)
{
  va_list ap;

  tdm_words++;
  if (tdm_counting)
    return;
  va_start (ap, fmt);
  putc ('\t', asm_out_file);
  vfprintf (asm_out_file, fmt, ap);
  putc ('\n', asm_out_file);
  va_end (ap);
}

static void
tdm_sorry (const char *msg)
{
  if (!tdm_counting)
    sorry ("%s", msg);
}

/* Emit OP with the word WORD as second operand, from a literal that
   the code jumps over.  */

static void
tdm_emit_literal (const char *op, const char *dest, const char *word)
{
  if (strcmp (op, "mov") == 0 && strcmp (dest, "$pc") == 0)
    {
      tdm_emit ("mov\t$pc, [$pc+1]");
      tdm_emit (".word\t%s", word);
      return;
    }
  tdm_emit ("%s\t%s, [$pc+2]", op, dest);
  tdm_emit ("mov\t$pc, $pc+2");
  tdm_emit (".word\t%s", word);
}

/* Emit "OP D, S", with D a register ($r0, $r1, $sp or $pc).  For $sp
   immediates count words.  */

static void
tdm_emit_to_reg (const char *op, const tdm_loc &d, const tdm_loc &s)
{
  bool mov = strcmp (op, "mov") == 0;
  const char *dn = tdm_reg_text (d);
  char word[128];

  switch (s.kind)
    {
    case LOC_R1:
      /* $r1 can't be OPB.  */
      if (d.kind == LOC_R1 && mov)
	return;
      tdm_emit ("mov\t[$sp-1], $r1");
      tdm_emit ("%s\t%s, [$sp-1]", op, dn);
      return;

    case LOC_R0:
      if (d.kind == LOC_R0 && mov)
	return;
      break;

    case LOC_SP:
    case LOC_FP:
      {
	/* The byte address in $sp (or the frame pointer) is twice its
	   value.  */
	const char *sn = s.kind == LOC_SP ? "$sp" : "$mr6";
	if (mov)
	  {
	    tdm_emit ("mov\t%s, %s", dn, sn);
	    tdm_emit ("add\t%s, %s", dn, sn);
	  }
	else if (strcmp (op, "add") == 0 || strcmp (op, "sub") == 0)
	  {
	    tdm_emit ("%s\t%s, %s", op, dn, sn);
	    tdm_emit ("%s\t%s, %s", op, dn, sn);
	  }
	else
	  tdm_sorry ("logical operation on the stack pointer");
	return;
      }

    case LOC_R0MEM:
      if (s.word)
	break;
      gcc_assert (d.kind != LOC_PC && d.kind != LOC_SP);
      if (d.kind == LOC_R0 && !mov)
	{
	  /* $r0 is both an operand and the base: keep its value in the
	     scratch word.  */
	  tdm_emit ("mov\t[$sp-1], $r0");
	  tdm_emit ("ror\t$r0");
	  tdm_emit ("mov\t$r0, %s", tdm_opb_text (s));
	  tdm_emit ("%s\t$r0, [$sp-1]",
		    strcmp (op, "sub") == 0 ? "isub" : op);
	  return;
	}
      tdm_emit ("ror\t$r0");
      tdm_emit ("%s\t%s, %s", op, dn, tdm_opb_text (s));
      if (d.kind != LOC_R0)
	tdm_emit ("rol\t$r0");
      return;

    case LOC_IMM:
      if (IN_RANGE (s.n, -32, 31))
	break;
      if (const char *creg = tdm_const_reg_text (s.n))
	{
	  tdm_emit ("%s\t%s, %s", op, dn, creg);
	  return;
	}
      snprintf (word, sizeof word, "%d", (int) (s.n & 0xffff));
      tdm_emit_literal (op, dn, word);
      return;

    case LOC_SYM:
      tdm_symbolic_text (s.sym, word, sizeof word);
      tdm_emit_literal (op, dn, word);
      return;

    default:
      break;
    }
  tdm_emit ("%s\t%s, %s", op, dn, tdm_opb_text (s));
}

/* Emit "OP D, S", with D a memory word or $mr register.  */

static void
tdm_emit_to_mem (const char *op, const tdm_loc &d, const tdm_loc &s)
{
  bool wrap = tdm_wrap_p (d);

  if (s.kind == LOC_R1 || (s.kind == LOC_R0 && !wrap))
    {
      if (wrap)
	tdm_emit ("ror\t$r0");
      tdm_emit ("%s\t%s, %s", op, tdm_opb_text (d), tdm_reg_text (s));
      if (wrap)
	tdm_emit ("rol\t$r0");
      return;
    }

  /* Work in $r1.  Swap it with the destination if that's simple...  */
  if (!wrap && s.kind != LOC_SP && !tdm_loc_equal_p (d, s))
    {
      tdm_emit ("swap\t$r1, %s", tdm_opb_text (d));
      tdm_emit_to_reg (op, tdm_r1, s);
      tdm_emit ("swap\t$r1, %s", tdm_opb_text (d));
      return;
    }

  /* ... otherwise load the source into it.  */
  tdm_emit ("mov\t[$sp-1], $r1");
  tdm_emit_to_reg ("mov", tdm_r1, s);
  if (wrap)
    tdm_emit ("ror\t$r0");
  tdm_emit ("%s\t%s, $r1", op, tdm_opb_text (d));
  if (wrap)
    tdm_emit ("rol\t$r0");
  tdm_emit ("mov\t$r1, [$sp-1]");
}

/* Emit D = D OP S, or D = S for "mov".  */

static void
tdm_emit_op (const char *op, const tdm_loc &d, const tdm_loc &s)
{
  if (strcmp (op, "mov") == 0 && tdm_loc_equal_p (d, s))
    return;

  switch (d.kind)
    {
    case LOC_R0:
    case LOC_R1:
    case LOC_PC:
      tdm_emit_to_reg (op, d, s);
      break;

    case LOC_SP:
      /* Only moves from $r0 or $r1, which hold a byte address.  Halve
	 it before it gets to $sp, which interrupts may use.  */
      gcc_assert (strcmp (op, "mov") == 0);
      if (s.kind == LOC_R0)
	{
	  tdm_emit ("ror\t$r0");
	  tdm_emit ("mov\t$sp, $r0");
	  tdm_emit ("rol\t$r0");
	}
      else
	{
	  gcc_assert (s.kind == LOC_R1);
	  tdm_emit ("ror\t$r1");
	  tdm_emit ("mov\t[$sp-1], $r1");
	  tdm_emit ("rol\t$r1");
	  tdm_emit ("mov\t$sp, [$sp-1]");
	}
      break;

    case LOC_FP:
      /* Only moves; halve the value.  $sp is a word address already.  */
      gcc_assert (strcmp (op, "mov") == 0);
      if (s.kind == LOC_SP)
	tdm_emit ("mov\t$mr6, $sp");
      else if (tdm_reg_loc_p (s))
	{
	  tdm_emit ("ror\t%s", tdm_reg_text (s));
	  tdm_emit ("mov\t$mr6, %s", tdm_reg_text (s));
	  tdm_emit ("rol\t%s", tdm_reg_text (s));
	}
      else
	{
	  tdm_emit ("mov\t[$sp-1], $r1");
	  tdm_emit_to_reg ("mov", tdm_r1, s);
	  tdm_emit ("ror\t$r1");
	  tdm_emit ("mov\t$mr6, $r1");
	  tdm_emit ("mov\t$r1, [$sp-1]");
	}
      break;

    default:
      tdm_emit_to_mem (op, d, s);
      break;
    }
}

/* Emit a rotate (OP is "ror" or "rol") of D by one bit.  */

static void
tdm_emit_rotate (const char *op, const tdm_loc &d)
{
  if (tdm_reg_loc_p (d))
    tdm_emit ("%s\t%s", op, tdm_reg_text (d));
  else if (tdm_wrap_p (d))
    {
      tdm_emit ("ror\t$r0");
      tdm_emit ("%s\t%s", op, tdm_opb_text (d));
      tdm_emit ("rol\t$r0");
    }
  else
    tdm_emit ("%s\t%s", op, tdm_opb_text (d));
}

/* Octet access through a byte pointer in $r0, whose half is only
   known at run time.  C data lives in the first 32K words, so after
   "ror $r0" bit 15 holds the half and the rest the word address.  The
   high half path clears bit 15 with $mr5 (0x7fff) and puts $r0 back
   with "rol; add 1", the low half path with "rol".  The jumps skip
   fixed instruction counts.  OFF is the byte offset of the access.  */

/* Load the octet at [$r0 + OFF], zero extended, into register D.  */

static void
tdm_emit_load_octet_dynamic (const tdm_loc &d, HOST_WIDE_INT off)
{
  tdm_loc r0 = tdm_make_loc (LOC_R0);

  gcc_assert (tdm_reg_loc_p (d) || d.kind == LOC_MR);
  if (d.kind != LOC_R1)
    tdm_emit ("mov\t[$sp-1], $r1");
  if (off)
    tdm_emit_to_reg ("add", r0, tdm_make_loc (LOC_IMM, tdm_sext16 (off)));
  tdm_emit ("ror\t$r0");
  tdm_emit ("if_ges\t$r0, 0");
  tdm_emit ("mov\t$pc, $pc+14");
  /* High half.  */
  tdm_emit ("and\t$r0, $mr5");
  tdm_emit ("mov\t$r1, [$r0]");
  for (int i = 0; i < 8; i++)
    tdm_emit ("ror\t$r1");
  tdm_emit ("rol\t$r0");
  tdm_emit ("add\t$r0, 1");
  tdm_emit ("mov\t$pc, $pc+3");
  /* Low half.  */
  tdm_emit ("mov\t$r1, [$r0]");
  tdm_emit ("rol\t$r0");
  tdm_emit ("and\t$r1, $mr3");

  switch (d.kind)
    {
    case LOC_R1:
      break;
    case LOC_R0:
      tdm_emit ("swap\t$r1, [$sp-1]");
      tdm_emit ("mov\t$r0, [$sp-1]");
      return;
    default:
      tdm_emit ("mov\t%s, $r1", tdm_opb_text (d));
      tdm_emit ("mov\t$r1, [$sp-1]");
      break;
    }
  if (off)
    tdm_emit_to_reg ("add", r0, tdm_make_loc (LOC_IMM, tdm_sext16 (-off)));
}

/* Store the low octet of S at [$r0 + OFF]:
   mem ^= (value ^ mem) & mask, in $r1.  */

static void
tdm_emit_store_octet_dynamic (const tdm_loc &s, HOST_WIDE_INT off)
{
  tdm_loc r0 = tdm_make_loc (LOC_R0);

  tdm_emit ("mov\t[$sp-1], $r1");
  tdm_emit_to_reg ("mov", tdm_r1, s);
  if (off)
    tdm_emit_to_reg ("add", r0, tdm_make_loc (LOC_IMM, tdm_sext16 (off)));
  tdm_emit ("ror\t$r0");
  tdm_emit ("if_ges\t$r0, 0");
  tdm_emit ("mov\t$pc, $pc+16");
  /* High half.  */
  tdm_emit ("and\t$r0, $mr5");
  for (int i = 0; i < 8; i++)
    tdm_emit ("rol\t$r1");
  tdm_emit ("xor\t$r1, [$r0]");
  tdm_emit ("and\t$r1, $mr4");
  tdm_emit ("xor\t[$r0], $r1");
  tdm_emit ("rol\t$r0");
  tdm_emit ("add\t$r0, 1");
  tdm_emit ("mov\t$pc, $pc+5");
  /* Low half.  */
  tdm_emit ("xor\t$r1, [$r0]");
  tdm_emit ("and\t$r1, $mr3");
  tdm_emit ("xor\t[$r0], $r1");
  tdm_emit ("rol\t$r0");

  if (off)
    tdm_emit_to_reg ("add", r0, tdm_make_loc (LOC_IMM, tdm_sext16 (-off)));
  tdm_emit ("mov\t$r1, [$sp-1]");
}

/* Emit an 8-bit move.  Octets in memory are the low or the high half
   of a word.  */

static void
tdm_emit_movqi (const tdm_loc &d, const tdm_loc &s)
{
  if (tdm_mem_loc_p (s))
    {
      tdm_loc w = s;

      if (s.dynamic)
	{
	  tdm_emit_load_octet_dynamic (d, s.n * 2 + s.high);
	  return;
	}
      w.high = false;
      tdm_emit_op ("mov", d, w);
      if (s.high)
	for (int i = 0; i < 8; i++)
	  tdm_emit_rotate ("ror", d);
      tdm_emit_op ("and", d, tdm_make_loc (LOC_IMM, 0x00ff));
    }
  else if (tdm_mem_loc_p (d))
    {
      bool wrap = tdm_wrap_p (d);
      tdm_loc w = d;

      if (d.dynamic)
	{
	  tdm_emit_store_octet_dynamic (s, d.n * 2 + d.high);
	  return;
	}
      w.high = false;
      /* mem ^= (value ^ mem) & mask, in $r1.  */
      tdm_emit ("mov\t[$sp-1], $r1");
      tdm_emit_to_reg ("mov", tdm_r1, s);
      if (d.high)
	for (int i = 0; i < 8; i++)
	  tdm_emit ("rol\t$r1");
      if (wrap)
	tdm_emit ("ror\t$r0");
      tdm_emit ("xor\t$r1, %s", tdm_opb_text (w));
      tdm_emit ("and\t$r1, %s", d.high ? "$mr4" : "$mr3");
      tdm_emit ("xor\t%s, $r1", tdm_opb_text (w));
      if (wrap)
	tdm_emit ("rol\t$r0");
      tdm_emit ("mov\t$r1, [$sp-1]");
    }
  else
    tdm_emit_op ("mov", d, s);
}

/* Stack slots beyond [$sp+31], and those addressed through the frame
   pointer, are reached through $r0, loaded with their word address.  $r0 itself is kept in [$sp], the free word at
   the bottom of the frame, and operands in $r0 are taken from there;
   the constraints keep accesses through a byte pointer in $r0 out of
   such instructions.  When LOAD, D is only written, after S has been
   read, so a destination $r0 needs no saving.  Return whether $r0 was
   saved, for tdm_far_end.  */

static bool
tdm_far_begin (tdm_loc *d, tdm_loc *s, bool load)
{
  tdm_loc r0 = tdm_make_loc (LOC_R0);
  tdm_loc *f = tdm_far_p (*d) ? d : s && tdm_far_p (*s) ? s : NULL;
  bool save;
  HOST_WIDE_INT n;

  if (!f)
    return false;
  gcc_assert (!(f == s && tdm_far_p (*d))
	      && d->kind != LOC_R0MEM && !(s && s->kind == LOC_R0MEM));

  save = !(load && d->kind == LOC_R0);
  if (save)
    {
      tdm_emit ("mov\t[$sp], $r0");
      if (d->kind == LOC_R0)
	*d = tdm_make_loc (LOC_R0SAVE);
      if (s && s->kind == LOC_R0)
	*s = tdm_make_loc (LOC_R0SAVE);
    }

  if (f->kind == LOC_FPMEM)
    {
      n = f->n;
      tdm_emit ("mov\t$r0, $mr6");
    }
  else if (f->n > 0)
    {
      n = f->n - 31;
      tdm_emit ("mov\t$r0, $sp+31");
    }
  else
    {
      /* Below $sp: only out of bounds accesses get there.  */
      n = f->n + 32;
      tdm_emit ("mov\t$r0, $sp-32");
    }
  if (!IN_RANGE (n, -32, 31))
    {
      tdm_emit_to_reg ("add", r0, tdm_make_loc (LOC_IMM, tdm_sext16 (n)));
      n = 0;
    }
  f->kind = LOC_R0MEM;
  f->n = n;
  f->word = true;
  return save;
}

static void
tdm_far_end (bool saved)
{
  if (saved)
    tdm_emit ("mov\t$r0, [$sp]");
}

/* Save the frame pointer $mr6 to MEM (SAVE) or restore it from there,
   as the word address it holds.  */

const char *
tdm_output_raw_fp (rtx mem, bool save)
{
  tdm_loc m = tdm_classify (mem);
  tdm_loc r = tdm_make_loc (LOC_MR, HARD_FRAME_POINTER_REGNUM - MR0_REGNUM);
  tdm_loc *d = save ? &m : &r;
  tdm_loc *s = save ? &r : &m;
  bool saved = tdm_far_begin (d, s, true);

  tdm_emit_op ("mov", *d, *s);
  tdm_far_end (saved);
  return "";
}

const char *
tdm_output_move (rtx *operands, machine_mode mode)
{
  tdm_loc d = tdm_classify (operands[0]);
  tdm_loc s = tdm_classify (operands[1]);
  bool saved = tdm_far_begin (&d, &s, true);

  if (mode == QImode)
    tdm_emit_movqi (d, s);
  else
    tdm_emit_op ("mov", d, s);
  tdm_far_end (saved);
  return "";
}

const char *
tdm_output_binop (const char *op, rtx *operands)
{
  tdm_loc d = tdm_classify (operands[0]);
  tdm_loc s = tdm_classify (operands[2]);

  if (d.kind == LOC_SP && CONST_INT_P (operands[2]))
    {
      /* Adjust the stack pointer, in words.  */
      HOST_WIDE_INT words = INTVAL (operands[2]) / 2;
      if (strcmp (op, "sub") == 0)
	words = -words;
      tdm_emit_to_reg ("add", d, tdm_make_loc (LOC_IMM, tdm_sext16 (words)));
    }
  else if (d.kind == LOC_SP)
    {
      /* By an (even) octet count in $r0 or $r1, for alloca.  $r1 can't
	 be OPB: go through the scratch word.  */
      const char *rn = tdm_reg_text (s);
      tdm_emit ("ror\t%s", rn);
      if (s.kind == LOC_R0)
	tdm_emit ("%s\t$sp, $r0", op);
      else
	{
	  tdm_emit ("mov\t[$sp-1], $r1");
	  tdm_emit ("%s\t$sp, [$sp-1]", op);
	}
      tdm_emit ("rol\t%s", rn);
    }
  else
    {
      bool saved = tdm_far_begin (&d, &s, false);
      tdm_emit_op (op, d, s);
      tdm_far_end (saved);
    }
  return "";
}

const char *
tdm_output_unop (enum rtx_code code, rtx *operands)
{
  tdm_loc d = tdm_classify (operands[0]);
  const char *insn = code == NEG ? "isub" : "xor";
  int imm = code == NEG ? 0 : -1;
  bool saved = tdm_far_begin (&d, NULL, false);

  if (tdm_reg_loc_p (d))
    tdm_emit ("%s\t%s, %d", insn, tdm_reg_text (d), imm);
  else
    {
      bool wrap = tdm_wrap_p (d);
      if (wrap)
	tdm_emit ("ror\t$r0");
      tdm_emit ("swap\t$r1, %s", tdm_opb_text (d));
      tdm_emit ("%s\t$r1, %d", insn, imm);
      tdm_emit ("swap\t$r1, %s", tdm_opb_text (d));
      if (wrap)
	tdm_emit ("rol\t$r0");
    }
  tdm_far_end (saved);
  return "";
}

/* Shifts by a constant: rotate, then mask (and fix up the sign).  */

const char *
tdm_output_shift (enum rtx_code code, rtx *operands)
{
  tdm_loc d = tdm_classify (operands[0]);
  int n = INTVAL (operands[2]) & 15;
  bool left = code == ASHIFT;
  HOST_WIDE_INT mask;
  bool saved;

  if (n == 0)
    return "";
  saved = tdm_far_begin (&d, NULL, false);
  if (n <= 8)
    for (int i = 0; i < n; i++)
      tdm_emit_rotate (left ? "rol" : "ror", d);
  else
    for (int i = n; i < 16; i++)
      tdm_emit_rotate (left ? "ror" : "rol", d);
  mask = left ? (0xffff << n) & 0xffff : 0xffff >> n;
  tdm_emit_op ("and", d, tdm_make_loc (LOC_IMM, tdm_sext16 (mask)));
  if (code == ASHIFTRT)
    {
      /* (x ^ m) - m sign extends from the bit m.  */
      tdm_loc m = tdm_make_loc (LOC_IMM, 0x8000 >> n);
      tdm_emit_op ("xor", d, m);
      tdm_emit_op ("sub", d, m);
    }
  tdm_far_end (saved);
  return "";
}

const char *
tdm_output_extend (enum rtx_code code, rtx *operands)
{
  tdm_loc d = tdm_classify (operands[0]);
  tdm_loc s = tdm_classify (operands[1]);
  bool saved = tdm_far_begin (&d, &s, true);

  if (tdm_mem_loc_p (s))
    tdm_emit_movqi (d, s);
  else
    {
      tdm_emit_op ("mov", d, s);
      tdm_emit_op ("and", d, tdm_make_loc (LOC_IMM, 0x00ff));
    }
  if (code == SIGN_EXTEND)
    {
      tdm_loc m = tdm_make_loc (LOC_IMM, 0x80);
      tdm_emit_op ("xor", d, m);
      tdm_emit_op ("sub", d, m);
    }
  tdm_far_end (saved);
  return "";
}

static const char *
tdm_condition_name (enum rtx_code code)
{
  switch (code)
    {
    case EQ: return "eq";
    case NE: return "neq";
    case LTU: return "ltu";
    case GEU: return "geu";
    case LT: return "lts";
    case GE: return "ges";
    case LE: return "les";
    case GT: return "gts";
    default: gcc_unreachable ();
    }
}

/* Branches: a predicate instruction, then a jump.  A far branch skips
   over a long jump with the inverted condition.  */

const char *
tdm_output_cbranch (rtx *operands, rtx_insn *insn)
{
  enum rtx_code code = GET_CODE (operands[0]);
  tdm_loc a = tdm_classify (operands[1]);
  tdm_loc b = tdm_classify (operands[2]);
  bool far = get_attr_length (insn) > 6;
  char lab[32];

  gcc_assert (tdm_reg_loc_p (a));
  if (tdm_wrap_p (b) || tdm_far_p (b))
    {
      /* The second alternative: put B in the scratch word, by way of
	 $r1, which holds A.  */
      tdm_loc r1 = tdm_r1;
      bool saved;

      gcc_assert (a.kind == LOC_R1);
      tdm_emit ("swap\t$r1, [$sp-1]");
      saved = tdm_far_begin (&r1, &b, true);
      tdm_emit_to_reg ("mov", r1, b);
      tdm_far_end (saved);
      tdm_emit ("swap\t$r1, [$sp-1]");
      b = tdm_make_loc (LOC_SCRATCH);
    }
  else if (b.kind == LOC_R1)
    {
      tdm_emit ("mov\t[$sp-1], $r1");
      b = tdm_make_loc (LOC_SCRATCH);
    }
  if (far)
    code = reverse_condition (code);
  tdm_label_text (operands[3], lab, sizeof lab);
  tdm_emit ("if_%s\t%s, %s", tdm_condition_name (code), tdm_reg_text (a),
	    tdm_opb_text (b));
  if (!far)
    tdm_emit ("mov\t$pc, $pc+(%s-.)", lab);
  else
    {
      tdm_emit ("mov\t$pc, $pc+3");
      tdm_emit ("mov\t$pc, [$pc+1]");
      tdm_emit (".word\t%s", lab);
    }
  return "";
}

const char *
tdm_output_jump (rtx label, rtx_insn *insn)
{
  char lab[32];

  tdm_label_text (label, lab, sizeof lab);
  if (get_attr_length (insn) > 2)
    {
      tdm_emit ("mov\t$pc, [$pc+1]");
      tdm_emit (".word\t%s", lab);
    }
  else
    tdm_emit ("mov\t$pc, $pc+(%s-.)", lab);
  return "";
}

const char *
tdm_output_indirect_jump (rtx target)
{
  tdm_emit_to_reg ("mov", tdm_make_loc (LOC_PC), tdm_classify (target));
  return "";
}

/* A call stores the address of the callee in [$sp] and swaps it with
   $pc, leaving the return address there.  */

const char *
tdm_output_call (rtx addr)
{
  tdm_emit_to_reg ("mov", tdm_r1, tdm_classify (addr));
  tdm_emit ("mov\t[$sp], $r1");
  tdm_emit ("swap\t$pc, [$sp]");
  return "";
}

/* The length of INSN, in octets: count what its output function
   emits.  */

int
tdm_insn_length (rtx_insn *insn)
{
  int code = recog_memoized (insn);
  const char *tmpl;
  int words;

  if (code < 0 || !reload_completed)
    return 2;

  extract_constrain_insn_cached (insn);
  tdm_counting = true;
  tdm_words = 0;
  tmpl = get_insn_template (code, insn);
  tdm_counting = false;
  words = tdm_words;
  if (tmpl && *tmpl)
    {
      words++;
      for (const char *p = tmpl; *p; p++)
	if (*p == '\n')
	  words++;
    }
  return words * 2;
}

/* Branch expansion.  The predicate instructions compare a register
   with a second operand; unsigned > and <= are only available with the
   operands swapped.  */

void
tdm_expand_cbranch (rtx *operands)
{
  enum rtx_code code = GET_CODE (operands[0]);
  rtx a = operands[1];
  rtx b = operands[2];

  if (code == GTU || code == LEU)
    {
      if (CONST_INT_P (b) && (UINTVAL (b) & 0xffff) != 0xffff)
	{
	  b = gen_int_mode (INTVAL (b) + 1, HImode);
	  code = code == GTU ? GEU : LTU;
	}
      else
	{
	  std::swap (a, b);
	  code = swap_condition (code);
	}
    }
  if (!register_operand (a, HImode))
    a = force_reg (HImode, a);

  rtx cond = gen_rtx_fmt_ee (code, VOIDmode, a, b);
  emit_jump_insn (gen_rtx_SET (pc_rtx,
			       gen_rtx_IF_THEN_ELSE (VOIDmode, cond,
						     gen_rtx_LABEL_REF
						       (VOIDmode, operands[3]),
						     pc_rtx)));
}

/* Memory not addressed through $r0, which can't be rotated around a
   predicate or a jump: an absolute address, or a stack slot.  Stack
   slots beyond [$sp+31] need $r0 too (see tdm_far_begin), so they only
   qualify while their offset isn't known.  */

bool
tdm_q_memory_p (rtx op)
{
  rtx x;
  HOST_WIDE_INT off = 0;

  if (!MEM_P (op))
    return false;
  x = XEXP (op, 0);
  if (GET_CODE (x) == PLUS && CONST_INT_P (XEXP (x, 1)))
    {
      off = INTVAL (XEXP (x, 1));
      x = XEXP (x, 0);
    }
  if (CONST_INT_P (x))
    return true;
  if (!REG_P (x))
    return false;
  if (REGNO (x) == AP_REGNUM || REGNO (x) == FP_REGNUM
      || VIRTUAL_REGISTER_NUM_P (REGNO (x)))
    return true;
  return (REGNO (x) == SP_REGNUM
	  && (off + GET_MODE_SIZE (GET_MODE (op)) - 1) >> 1 <= 31);
}

/* Whether an instruction can have both A and B as operands: they
   mustn't both be memory that is addressed through $r0, the only base
   register for pointers.  The constraints sort this out in register
   allocation; this keeps the passes before it from combining such
   operands.  Memory can also come inside a SUBREG.  */

bool
tdm_operand_pair_ok_p (rtx a, rtx b)
{
  if (SUBREG_P (a))
    a = SUBREG_REG (a);
  if (SUBREG_P (b))
    b = SUBREG_REG (b);
  return !(MEM_P (a) && MEM_P (b)
	   && !tdm_q_memory_p (a) && !tdm_q_memory_p (b));
}

/* Prepare the operands of a move expander in MODE that stores to
   memory: the value must be in a register.  Not in $r0 if the memory is
   addressed through a pointer, which needs $r0 too: an incoming
   argument stored to an over-aligned stack slot has its address
   computed while it is still in $r0, so copy it out first.  */

void
tdm_expand_store (rtx *operands, machine_mode mode)
{
  if (!MEM_P (operands[0]) || !can_create_pseudo_p ())
    return;
  if (!register_operand (operands[1], mode))
    operands[1] = force_reg (mode, operands[1]);
  else if (REG_P (operands[1]) && REGNO (operands[1]) == R0_REGNUM
	   && !tdm_q_memory_p (operands[0]))
    operands[1] = copy_to_mode_reg (mode, operands[1]);
}

/* Implement TARGET_MD_ASM_ADJUST.  Memory addressed through a pointer
   needs $r0, so an asm can't have two such operands.  An operand that
   may be a register or memory ("g", "rm") is left in memory when it
   is already there, though: in alternatives that allow a register,
   only allow memory that isn't addressed through $r0, so the others
   are loaded into registers.  Those mustn't be $r0 either, which
   would leave no base for the next one's load: "r" becomes "j".

   An output in such memory becomes a register stored there after the
   asm.  Left as memory, an in-out operand ("+g") would keep its
   address in $r0 from the load before the asm to the store after
   it.

   Without optimization, the input and the output of a "+m" operand
   are expanded separately, each with its own address: make the input
   the output's memory, as there is only one base register.  */

static rtx_insn *
tdm_md_asm_adjust (vec<rtx> &outputs, vec<rtx> &inputs, vec<machine_mode> &,
		   vec<const char *> &constraints, vec<rtx> &, vec<rtx> &,
		   HARD_REG_SET &, location_t)
{
  rtx_insn *after = NULL;

  for (unsigned int i = 0; i < constraints.length (); i++)
    {
      const char *c = constraints[i];
      std::string out;
      bool changed = false;

      while (true)
	{
	  size_t len = strcspn (c, ",");
	  std::string alt (c, len);
	  bool reg = alt.find_first_of ("grqt") != std::string::npos;

	  for (char ch : alt)
	    if (ch == 'g')
	      out += "jQi", changed = true;
	    else if (ch == 'r')
	      out += 'j', changed = true;
	    else if (reg && ch == 'm')
	      out += 'Q', changed = true;
	    else
	      out += ch;
	  if (c[len] == '\0')
	    break;
	  out += ',';
	  c += len + 1;
	}
      if (changed)
	constraints[i] = ggc_strdup (out.c_str ());

      if (i < outputs.length () && MEM_P (outputs[i])
	  && !tdm_q_memory_p (outputs[i])
	  && strpbrk (constraints[i], "jqt"))
	{
	  rtx reg = gen_reg_rtx (GET_MODE (outputs[i]));

	  start_sequence ();
	  emit_insn (after);
	  emit_move_insn (outputs[i], reg);
	  after = end_sequence ();
	  outputs[i] = reg;
	}
    }

  for (rtx &in : inputs)
    for (rtx out : outputs)
      if (MEM_P (in) && MEM_P (out) && in != out
	  && GET_MODE (in) == GET_MODE (out)
	  && !tdm_q_memory_p (in) && !tdm_q_memory_p (out)
	  && MEM_EXPR (in) && MEM_EXPR (out)
	  && MEM_OFFSET_KNOWN_P (in) && MEM_OFFSET_KNOWN_P (out)
	  && known_eq (MEM_OFFSET (in), MEM_OFFSET (out))
	  && operand_equal_p (MEM_EXPR (in), MEM_EXPR (out), 0))
	{
	  in = out;
	  break;
	}

  /* Memory-only operands at different addresses through $r0 can't all
     have it: keep one there, and give the others a stack temporary,
     which $sp addresses, copied in before the asm and out after it.
     Memory without a mode (BLKmode) isn't copied, so it is the one
     kept.  The input of an in-out operand is its output's rtx, and is
     replaced with it.  */
  auto mem_only_p = [&] (unsigned int i, rtx x)
    {
      const char *c = constraints[i];
      return (MEM_P (x) && !tdm_q_memory_p (x)
	      && !ISDIGIT (*c) && !strpbrk (c, "jqtk")
	      && strpbrk (c, "mo"));
    };
  unsigned int nout = outputs.length ();
  auto_vec<rtx> mems;
  rtx kept = NULL_RTX;

  for (unsigned int i = 0; i < constraints.length (); i++)
    {
      rtx x = i < nout ? outputs[i] : inputs[i - nout];
      if (!mem_only_p (i, x))
	continue;
      if (GET_MODE (x) == BLKmode)
	{
	  if (!kept)
	    kept = x;
	}
      else
	mems.safe_push (x);
    }
  if (!kept && !mems.is_empty ())
    kept = mems[0];
  for (rtx m : mems)
    {
      if (rtx_equal_p (m, kept))
	continue;

      rtx temp = assign_stack_temp (GET_MODE (m), GET_MODE_SIZE (GET_MODE (m)));
      bool read = false, written = false;

      for (unsigned int i = 0; i < constraints.length (); i++)
	{
	  rtx &x = i < nout ? outputs[i] : inputs[i - nout];
	  if (MEM_P (x) && rtx_equal_p (x, m))
	    {
	      x = temp;
	      if (i < nout)
		written = true;
	      else
		read = true;
	    }
	}
      if (read)
	emit_move_insn (temp, m);
      if (written)
	{
	  start_sequence ();
	  emit_insn (after);
	  emit_move_insn (m, temp);
	  after = end_sequence ();
	}
    }
  return after;
}

/* Implement TARGET_CANNOT_SUBSTITUTE_MEM_EQUIV_P.  Memory addressed
   through a pointer needs $r0, so substituting it for a register could
   leave an instruction needing $r0 twice.  */

static bool
tdm_cannot_substitute_mem_equiv_p (rtx subst)
{
  rtx x;

  if (!MEM_P (subst))
    return false;
  x = XEXP (subst, 0);
  if (GET_CODE (x) == PLUS && CONST_INT_P (XEXP (x, 1)))
    x = XEXP (x, 0);
  if (CONST_INT_P (x))
    return false;
  return !(REG_P (x)
	   && (REGNO (x) == SP_REGNUM || REGNO (x) == AP_REGNUM
	       || REGNO (x) == FP_REGNUM
	       || VIRTUAL_REGISTER_NUM_P (REGNO (x))));
}

/* Expand a nonlocal goto (and __builtin_longjmp): OPERANDS[1] is the
   target label, OPERANDS[2] the saved $sp and OPERANDS[3] the saved
   frame pointer, both byte addresses.  They go to fixed memory
   registers first, as nothing can be reloaded once $sp moves; then a
   single jump sets $sp and $mr6.  */

void
tdm_expand_nonlocal_goto (rtx *operands)
{
  emit_move_insn (gen_rtx_REG (HImode, TDM_GOTO_LABEL_REGNUM), operands[1]);
  emit_move_insn (gen_rtx_REG (HImode, TDM_GOTO_FP_REGNUM), operands[3]);
  emit_move_insn (gen_rtx_REG (HImode, TDM_GOTO_SP_REGNUM), operands[2]);
  emit_clobber (gen_rtx_MEM (BLKmode, gen_rtx_SCRATCH (VOIDmode)));
  emit_clobber (gen_rtx_MEM (BLKmode, hard_frame_pointer_rtx));
  emit_jump_insn (gen_tdm_nonlocal_jump ());
  emit_barrier ();
}

/* Trampolines, written as data on the stack:

	mov	$r1, [$pc+3]
	mov	$mr18, $r1		; the static chain
	mov	$pc, [$pc+2]
	.word	CHAIN
	.word	FUNCTION

   $r1 is free at a call, which loads the target through it.  The
   instructions are stored as numbers rather than copied from a
   template: code may be above word 0x8000, out of reach of the byte
   pointers that copying uses.  */

static void
tdm_trampoline_init (rtx m_tramp, tree fndecl, rtx chain_value)
{
  static const unsigned short code[] = { 0x80c3, 0x8bd2, 0x8002 };
  rtx fnaddr = XEXP (DECL_RTL (fndecl), 0);

  gcc_assert (STATIC_CHAIN_REGNUM == MR0_REGNUM + 18);
  for (unsigned int i = 0; i < ARRAY_SIZE (code); i++)
    emit_move_insn (adjust_address (m_tramp, HImode, 2 * i),
		    gen_int_mode (code[i], HImode));
  emit_move_insn (adjust_address (m_tramp, HImode, 6), chain_value);
  emit_move_insn (adjust_address (m_tramp, HImode, 8), fnaddr);
}

/* The trampoline is written at its octet address and called at its
   word address.  */

static rtx
tdm_trampoline_adjust_address (rtx addr)
{
  return expand_simple_binop (Pmode, LSHIFTRT, addr, const1_rtx,
			      NULL_RTX, true, OPTAB_LIB_WIDEN);
}

/* Make the first N OPERANDS of an insn pairwise acceptable to
   tdm_operand_pair_ok_p by loading inputs into registers.  Operand 0
   is the output.  */

void
tdm_fix_operand_pairs (rtx *operands, int n)
{
  for (int i = n - 1; i > 0; i--)
    for (int j = 0; j < i; j++)
      if (!tdm_operand_pair_ok_p (operands[j], operands[i]))
	{
	  operands[i] = force_reg (GET_MODE (operands[0]), operands[i]);
	  break;
	}
}

/* Frame layout, from $sp up: a free word, the outgoing arguments, the
   saved registers and the local variables.  A leaf function without
   any of these has no frame.  */

struct tdm_frame_info
{
  HOST_WIDE_INT outgoing;
  HOST_WIDE_INT saved;
  HOST_WIDE_INT locals;
  HOST_WIDE_INT total;
};

static bool
tdm_save_reg_p (unsigned int regno)
{
  if (regno == HARD_FRAME_POINTER_REGNUM && frame_pointer_needed)
    return true;
  return (TDM_MR_P (regno) && df_regs_ever_live_p (regno)
	  && !call_used_or_fixed_reg_p (regno));
}

static void
tdm_compute_frame (tdm_frame_info *f)
{
  f->outgoing = ROUND_UP (crtl->outgoing_args_size, 2);
  f->locals = ROUND_UP (get_frame_size (), 2);
  f->saved = 0;
  for (unsigned int regno = MR0_REGNUM; regno <= MR31_REGNUM; regno++)
    if (tdm_save_reg_p (regno))
      f->saved += 2;
  if (f->outgoing || f->saved || f->locals || !crtl->is_leaf)
    f->total = 2 + f->outgoing + f->saved + f->locals;
  else
    f->total = 0;
}

HOST_WIDE_INT
tdm_initial_elimination_offset (int from, int to)
{
  tdm_frame_info f;

  /* The frame pointer is $sp after the prologue.  */
  gcc_assert (to == STACK_POINTER_REGNUM
	      || to == HARD_FRAME_POINTER_REGNUM);
  tdm_compute_frame (&f);
  if (from == ARG_POINTER_REGNUM)
    return f.total;
  gcc_assert (from == FRAME_POINTER_REGNUM);
  return f.total ? 2 + f.outgoing + f.saved : 0;
}

/* Call frame information counts words (see tdm.h), so the frame
   related insns below carry notes with word offsets: the compiler's
   octet offsets halved.  */

/* Emit $sp += BYTES, described for the CFI.  */

static void
tdm_emit_sp_adjust (HOST_WIDE_INT bytes)
{
  rtx_insn *insn = emit_insn (gen_addhi3 (stack_pointer_rtx,
					  stack_pointer_rtx,
					  GEN_INT (bytes)));
  RTX_FRAME_RELATED_P (insn) = 1;
  add_reg_note (insn, REG_CFA_ADJUST_CFA,
		gen_rtx_SET (stack_pointer_rtx,
			     plus_constant (Pmode, stack_pointer_rtx,
					    bytes / 2)));
}

/* The saved register slot at OFF octets above $sp, and the same slot
   in words for the CFI.  */

static rtx
tdm_save_slot (HOST_WIDE_INT off, bool words)
{
  return gen_frame_mem (HImode, plus_constant (Pmode, stack_pointer_rtx,
					       words ? off / 2 : off));
}

void
tdm_expand_prologue (void)
{
  tdm_frame_info f;
  HOST_WIDE_INT off;

  tdm_compute_frame (&f);
  if (flag_stack_usage_info)
    current_function_static_stack_size = f.total;
  if (f.total)
    tdm_emit_sp_adjust (-f.total);

  off = 2 + f.outgoing;
  for (unsigned int regno = MR0_REGNUM; regno <= MR31_REGNUM; regno++)
    if (tdm_save_reg_p (regno))
      {
	rtx reg = gen_rtx_REG (HImode, regno);
	rtx slot = tdm_save_slot (off, false);
	rtx_insn *insn;

	/* The caller's frame pointer is a word address: keep it so.  */
	if (regno == HARD_FRAME_POINTER_REGNUM && frame_pointer_needed)
	  insn = emit_insn (gen_tdm_save_fp (slot));
	else
	  insn = emit_move_insn (slot, reg);
	RTX_FRAME_RELATED_P (insn) = 1;
	add_reg_note (insn, REG_CFA_OFFSET,
		      gen_rtx_SET (tdm_save_slot (off, true), reg));
	off += 2;
      }

  if (frame_pointer_needed)
    {
      rtx_insn *insn = emit_move_insn (hard_frame_pointer_rtx,
				       stack_pointer_rtx);
      RTX_FRAME_RELATED_P (insn) = 1;
      add_reg_note (insn, REG_CFA_DEF_CFA,
		    plus_constant (Pmode, hard_frame_pointer_rtx,
				   f.total / 2));
      emit_insn (gen_blockage ());
    }
}

void
tdm_expand_epilogue (void)
{
  tdm_frame_info f;
  HOST_WIDE_INT off;

  tdm_compute_frame (&f);
  if (frame_pointer_needed)
    {
      /* Drop the dynamic allocations.  */
      rtx_insn *insn;

      emit_insn (gen_blockage ());
      insn = emit_insn (gen_tdm_sp_from_fp ());
      RTX_FRAME_RELATED_P (insn) = 1;
      add_reg_note (insn, REG_CFA_DEF_CFA,
		    plus_constant (Pmode, stack_pointer_rtx, f.total / 2));
    }
  off = 2 + f.outgoing;
  for (unsigned int regno = MR0_REGNUM; regno <= MR31_REGNUM; regno++)
    if (tdm_save_reg_p (regno))
      {
	rtx reg = gen_rtx_REG (HImode, regno);
	rtx slot = tdm_save_slot (off, false);
	rtx_insn *insn;

	if (regno == HARD_FRAME_POINTER_REGNUM && frame_pointer_needed)
	  insn = emit_insn (gen_tdm_restore_fp (slot));
	else
	  insn = emit_move_insn (reg, slot);

	RTX_FRAME_RELATED_P (insn) = 1;
	add_reg_note (insn, REG_CFA_RESTORE, reg);
	off += 2;
      }
  if (f.total)
    {
      emit_insn (gen_blockage ());
      tdm_emit_sp_adjust (f.total);
    }
  emit_jump_insn (gen_return_internal ());
}

/* Registers.  */

static bool
tdm_hard_regno_mode_ok (unsigned int regno, machine_mode mode)
{
  unsigned int n = CEIL (GET_MODE_SIZE (mode), UNITS_PER_WORD);

  if (regno == R0_REGNUM)
    return n <= 2;
  if (TDM_MR_P (regno))
    return regno + n - 1 <= MR31_REGNUM;
  return n == 1;
}

static void
tdm_conditional_register_usage (void)
{
  if (TARGET_USER_MODE)
    for (unsigned int regno = MR0_REGNUM; regno <= MR0_REGNUM + 2; regno++)
      fixed_regs[regno] = 0;
}

static bool
tdm_class_likely_spilled_p (reg_class_t rclass)
{
  return rclass == R0_REGS || rclass == R1_REGS || rclass == R_REGS;
}

static int
tdm_register_move_cost (machine_mode, reg_class_t from, reg_class_t to)
{
  /* A move between two memory registers goes through $r1.  */
  if (from == MR_REGS && to == MR_REGS)
    return 6;
  if (from == SP_REGS || to == SP_REGS)
    return 4;
  return 2;
}

static bool
tdm_can_eliminate (const int, const int to)
{
  return to == HARD_FRAME_POINTER_REGNUM || !frame_pointer_needed;
}

/* Implement TARGET_FRAME_POINTER_REQUIRED.  $sp moves in functions
   with dynamic allocations, and a nonlocal goto restores the frame
   pointer of its target.  */

static bool
tdm_frame_pointer_required (void)
{
  return cfun->calls_alloca || cfun->has_nonlocal_label;
}

/* Implement REGNO_OK_FOR_BASE_P.  */

bool
tdm_regno_ok_for_base_p (unsigned int regno)
{
  if (regno >= FIRST_PSEUDO_REGISTER && reg_renumber
      && reg_renumber[regno] >= 0)
    regno = reg_renumber[regno];
  return (TDM_BASE_REGNO_P (regno)
	  || (regno == HARD_FRAME_POINTER_REGNUM && frame_pointer_needed));
}

/* Addresses: $sp or $r0 plus a 6-bit word offset, or an absolute word
   address from 0 to 31.  Through $r0, which holds a byte address, the
   offset has to be even.  */

static bool
tdm_legitimate_address_p (machine_mode mode, rtx x, bool strict,
			  code_helper = ERROR_MARK)
{
  HOST_WIDE_INT off = 0;
  HOST_WIDE_INT size = GET_MODE_SIZE (mode);
  unsigned int regno;

  if (size < 1)
    size = 2;
  if (GET_CODE (x) == PLUS && CONST_INT_P (XEXP (x, 1)))
    {
      off = INTVAL (XEXP (x, 1));
      x = XEXP (x, 0);
      if (CONST_INT_P (x))
	return false;
    }
  if (CONST_INT_P (x))
    return INTVAL (x) >= 0 && (INTVAL (x) + size - 1) >> 1 <= 31;
  if (!REG_P (x))
    return false;

  regno = REGNO (x);
  if (regno == AP_REGNUM || regno == FP_REGNUM
      || VIRTUAL_REGISTER_NUM_P (regno)
      || (regno == HARD_FRAME_POINTER_REGNUM && frame_pointer_needed))
    return true;
  if (regno >= FIRST_PSEUDO_REGISTER)
    {
      if (strict)
	return false;
    }
  else if (regno == SP_REGNUM)
    return true;
  else if (regno != R0_REGNUM)
    return false;
  if ((off & 1) != 0)
    return false;
  return off >> 1 >= -32 && (off + size - 1) >> 1 <= 31;
}

/* Calls.  */

static rtx
tdm_function_arg (cumulative_args_t cum_v, const function_arg_info &arg)
{
  CUMULATIVE_ARGS *cum = get_cumulative_args (cum_v);

  if (arg.end_marker_p () || *cum != 0 || !arg.named
      || arg.mode == BLKmode || GET_MODE_SIZE (arg.mode) > UNITS_PER_WORD)
    return NULL_RTX;
  return gen_rtx_REG (arg.mode, R0_REGNUM);
}

static void
tdm_function_arg_advance (cumulative_args_t cum_v,
			  const function_arg_info &)
{
  (*get_cumulative_args (cum_v))++;
}

static rtx
tdm_function_value (const_tree valtype, const_tree, bool)
{
  return gen_rtx_REG (TYPE_MODE (valtype), R0_REGNUM);
}

static rtx
tdm_libcall_value (machine_mode mode, const_rtx)
{
  return gen_rtx_REG (mode, R0_REGNUM);
}

static bool
tdm_function_value_regno_p (const unsigned int regno)
{
  return regno == R0_REGNUM;
}

static bool
tdm_return_in_memory (const_tree type, const_tree)
{
  return (TYPE_MODE (type) == BLKmode
	  || int_size_in_bytes (type) > 2 * UNITS_PER_WORD);
}

/* Assembler output.  */

static void
tdm_print_operand_address (FILE *file, machine_mode, rtx x)
{
  HOST_WIDE_INT off = 0;

  if (GET_CODE (x) == PLUS && CONST_INT_P (XEXP (x, 1)))
    {
      off = INTVAL (XEXP (x, 1));
      x = XEXP (x, 0);
    }
  if (CONST_INT_P (x))
    fprintf (file, "[%d]", (int) ((INTVAL (x) + off) >> 1));
  else if (REG_P (x))
    fprintf (file, "[%s%+d]", reg_names[REGNO (x)], (int) (off >> 1));
  else
    output_operand_lossage ("invalid address");
}

static void
tdm_print_operand (FILE *file, rtx x, int code)
{
  char buf[128];

  if (code != 0)
    {
      output_operand_lossage ("invalid operand code '%c'", code);
      return;
    }
  switch (GET_CODE (x))
    {
    case REG:
      fputs (reg_names[REGNO (x)], file);
      break;
    case MEM:
      tdm_print_operand_address (file, GET_MODE (x), XEXP (x, 0));
      break;
    case CONST_INT:
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (x));
      break;
    case SYMBOL_REF:
    case LABEL_REF:
    case CONST:
      tdm_symbolic_text (x, buf, sizeof buf);
      fputs (buf, file);
      break;
    default:
      output_operand_lossage ("invalid operand");
      break;
    }
}

/* Data: the assembler emits 16-bit words for .word and pairs of them
   for .long; .octet emits octets, packed two per word.  */

static bool
tdm_assemble_integer (rtx x, unsigned int size, int aligned_p)
{
  if (CONST_INT_P (x))
    {
      unsigned HOST_WIDE_INT v = UINTVAL (x);

      if (size == 1 || !aligned_p)
	{
	  fputs ("\t.octet\t", asm_out_file);
	  for (unsigned int i = 0; i < size; i++)
	    fprintf (asm_out_file, "%s%u", i ? ", " : "",
		     (unsigned int) (v >> (8 * i)) & 0xff);
	  putc ('\n', asm_out_file);
	  return true;
	}
      if (size == 2)
	{
	  fprintf (asm_out_file, "\t.word\t%u\n", (unsigned int) (v & 0xffff));
	  return true;
	}
      if (size == 4)
	{
	  fprintf (asm_out_file, "\t.long\t%lu\n",
		   (unsigned long) (v & 0xffffffff));
	  return true;
	}
      return false;
    }

  if (size == 2 && aligned_p
      && (SYMBOL_REF_P (x) || LABEL_REF_P (x) || GET_CODE (x) == CONST))
    {
      char buf[128];
      tdm_symbolic_text (x, buf, sizeof buf);
      fprintf (asm_out_file, "\t.word\t%s\n", buf);
      return true;
    }
  return default_assemble_integer (x, size, aligned_p);
}

/* .zero counts words; this may follow octet data.  */

void
tdm_output_skip (FILE *file, unsigned HOST_WIDE_INT size)
{
  if (size == 0)
    return;
  if (size <= 16)
    {
      fputs ("\t.octet\t0", file);
      for (unsigned HOST_WIDE_INT i = 1; i < size; i++)
	fputs (", 0", file);
      putc ('\n', file);
    }
  else
    fprintf (file, "\t.rept\t" HOST_WIDE_INT_PRINT_UNSIGNED
	     "\n\t.octet\t0\n\t.endr\n", size);
}

void
tdm_output_ascii (FILE *file, const char *p, int len)
{
  int col = 0;

  for (int i = 0; i < len; i++)
    {
      unsigned char c = p[i];

      if (col == 0)
	fputs ("\t.ascii\t\"", file);
      if (c == '"' || c == '\\')
	{
	  putc ('\\', file);
	  putc (c, file);
	  col += 2;
	}
      else if (c >= ' ' && c < 0x7f)
	{
	  putc (c, file);
	  col++;
	}
      else
	{
	  fprintf (file, "\\%03o", c);
	  col += 4;
	}
      if (col >= 60 || i == len - 1)
	{
	  fputs ("\"\n", file);
	  col = 0;
	}
    }
}

static HOST_WIDE_INT
tdm_constant_alignment (const_tree, HOST_WIDE_INT align)
{
  return MAX (align, 16);
}

/* Miscellaneous.  */

static void
tdm_option_override (void)
{
  /* There's no frame pointer.  */
  flag_omit_frame_pointer = 1;
  /* Mergeable sections would count their entries in words.  */
  flag_merge_constants = 0;
}

static void
tdm_init_libfuncs (void)
{
  set_optab_libfunc (ashl_optab, HImode, "__ashlhi3");
  set_optab_libfunc (lshr_optab, HImode, "__lshrhi3");
  set_optab_libfunc (ashr_optab, HImode, "__ashrhi3");
}

#undef  TARGET_OPTION_OVERRIDE
#define TARGET_OPTION_OVERRIDE tdm_option_override
#undef  TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS tdm_init_libfuncs

#undef  TARGET_HARD_REGNO_MODE_OK
#define TARGET_HARD_REGNO_MODE_OK tdm_hard_regno_mode_ok
#undef  TARGET_CONDITIONAL_REGISTER_USAGE
#define TARGET_CONDITIONAL_REGISTER_USAGE tdm_conditional_register_usage
#undef  TARGET_CLASS_LIKELY_SPILLED_P
#define TARGET_CLASS_LIKELY_SPILLED_P tdm_class_likely_spilled_p
#undef  TARGET_REGISTER_MOVE_COST
#define TARGET_REGISTER_MOVE_COST tdm_register_move_cost
#undef  TARGET_CAN_ELIMINATE
#define TARGET_CAN_ELIMINATE tdm_can_eliminate
#undef  TARGET_LEGITIMATE_ADDRESS_P
#define TARGET_LEGITIMATE_ADDRESS_P tdm_legitimate_address_p
#undef  TARGET_FRAME_POINTER_REQUIRED
#define TARGET_FRAME_POINTER_REQUIRED tdm_frame_pointer_required
#undef  TARGET_MD_ASM_ADJUST
#define TARGET_MD_ASM_ADJUST tdm_md_asm_adjust
#undef  TARGET_CANNOT_SUBSTITUTE_MEM_EQUIV_P
#define TARGET_CANNOT_SUBSTITUTE_MEM_EQUIV_P tdm_cannot_substitute_mem_equiv_p

#undef  TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES hook_bool_const_tree_true
#undef  TARGET_FUNCTION_ARG
#define TARGET_FUNCTION_ARG tdm_function_arg
#undef  TARGET_FUNCTION_ARG_ADVANCE
#define TARGET_FUNCTION_ARG_ADVANCE tdm_function_arg_advance
#undef  TARGET_FUNCTION_VALUE
#define TARGET_FUNCTION_VALUE tdm_function_value
#undef  TARGET_LIBCALL_VALUE
#define TARGET_LIBCALL_VALUE tdm_libcall_value
#undef  TARGET_FUNCTION_VALUE_REGNO_P
#define TARGET_FUNCTION_VALUE_REGNO_P tdm_function_value_regno_p
#undef  TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY tdm_return_in_memory

#undef  TARGET_PRINT_OPERAND
#define TARGET_PRINT_OPERAND tdm_print_operand
#undef  TARGET_PRINT_OPERAND_ADDRESS
#define TARGET_PRINT_OPERAND_ADDRESS tdm_print_operand_address
#undef  TARGET_ASM_INTEGER
#define TARGET_ASM_INTEGER tdm_assemble_integer
#undef  TARGET_ASM_BYTE_OP
#define TARGET_ASM_BYTE_OP "\t.octet\t"
#undef  TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.word\t"
#undef  TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP "\t.long\t"
#undef  TARGET_ASM_ALIGNED_DI_OP
#define TARGET_ASM_ALIGNED_DI_OP NULL
/* Unaligned data only occurs in the debug sections, which the
   assembler addresses in octets.  */
#undef  TARGET_ASM_UNALIGNED_HI_OP
#define TARGET_ASM_UNALIGNED_HI_OP "\t.2byte\t"
#undef  TARGET_ASM_UNALIGNED_SI_OP
#define TARGET_ASM_UNALIGNED_SI_OP "\t.4byte\t"
#undef  TARGET_ASM_UNALIGNED_DI_OP
#define TARGET_ASM_UNALIGNED_DI_OP NULL
#undef  TARGET_CONSTANT_ALIGNMENT
#define TARGET_CONSTANT_ALIGNMENT tdm_constant_alignment

#undef  TARGET_TRAMPOLINE_INIT
#define TARGET_TRAMPOLINE_INIT tdm_trampoline_init
#undef  TARGET_TRAMPOLINE_ADJUST_ADDRESS
#define TARGET_TRAMPOLINE_ADJUST_ADDRESS tdm_trampoline_adjust_address

struct gcc_target targetm = TARGET_INITIALIZER;
