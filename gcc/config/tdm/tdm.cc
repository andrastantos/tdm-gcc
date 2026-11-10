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
#include "dumpfile.h"
#include "insn-addr.h"
#include "tree-pass.h"
#include "rtl-iter.h"

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

/* Literals: words in the code that an instruction loads through
   [$pc+N], for a wide operand or a far branch target.  */

enum tdm_lit_kind
{
  LIT_CONST,				/* A wide operand.  */
  LIT_JUMP,				/* mov $pc, [lit]: a far jump, or a
					   jump to a constant address.  */
  LIT_CBRANCH				/* The far jump of a cbranch.  */
};

/* The longest literal text (see tdm_symbolic_text).  */
#define TDM_LIT_LEN 128

struct tdm_lit
{
  enum tdm_lit_kind kind;
  /* The word of the using instruction within its insn's output.  */
  int word;
  char text[TDM_LIT_LEN];
};

/* While counting, the literals of the insn are added here if this is
   set (see tdm_reorg).  */
static vec<tdm_lit> *tdm_lits;

/* Literal pools: words after a barrier, where the code doesn't fall
   through, each loaded with [$pc+(label-.)].  tdm_reorg sets them up
   for the current function.  An entry is dropped (DEAD) when its use
   ends up out of reach and goes back to an inline literal.  */

struct tdm_pool_entry
{
  char text[TDM_LIT_LEN];
  int label;
  bool dead;
};

/* The entries of all pools, pool by pool: pool I has the entries from
   tdm_pool_start[I] up to tdm_pool_start[I + 1].  */
static auto_vec<tdm_pool_entry> tdm_pool_entries;
static auto_vec<int> tdm_pool_start;

/* The pool label of each pooled literal, by the UID of its insn and its
   index among the literals of the insn, in output order.  */
typedef int_hash<int, INT_MIN, INT_MAX> tdm_int_hash;
typedef pair_hash<tdm_int_hash, tdm_int_hash> tdm_lit_key_hash;
static hash_map<tdm_lit_key_hash, int> *tdm_lit_labels;

/* Pool labels are .LlitN, numbered across the file.  */
static int tdm_lit_label_no;

/* The insn being counted or output, and the index of its next
   literal.  */
static rtx_insn *tdm_lit_insn;
static int tdm_lit_index;

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

/* If the memory reference EXPR is part of an object accessed through a
   pointer whose type is aligned to a word, set *PARITY to the parity of
   its address and return true.  GCC doesn't take the alignment of an
   object from the type of a pointer to it, but a word field of that
   object is accessed as a word, so the object has to be aligned.  */

static bool
tdm_pointed_to_parity (tree expr, HOST_WIDE_INT *parity)
{
  poly_int64 bitsize, bitpos;
  HOST_WIDE_INT const_bitpos;
  tree offset;
  machine_mode mode;
  int unsignedp, reversep, volatilep = 0;
  tree base = get_inner_reference (expr, &bitsize, &bitpos, &offset, &mode,
				   &unsignedp, &reversep, &volatilep);

  if (TREE_CODE (base) != MEM_REF
      || TYPE_ALIGN (TREE_TYPE (base)) < 16
      || !bitpos.is_constant (&const_bitpos)
      || const_bitpos % BITS_PER_UNIT != 0
      || (offset != NULL_TREE && highest_pow2_factor (offset) < 2))
    return false;
  *parity = (const_bitpos / BITS_PER_UNIT) & 1;
  return true;
}

/* Whether $r0 is known to hold an even byte pointer for the QImode
   memory X at [$r0 + OFF].  The octet is then in the word at
   [$r0/2 + OFF/2], in the half the parity of OFF tells; otherwise the
   access tests the parity at run time.  The parity of the address comes
   from the alignment of X or, as an odd offset in an aligned object
   gives X an alignment of a byte, from that of the object it is part
   of (or of the type of the pointer to it).  */

static bool
tdm_r0_even_p (rtx x, HOST_WIDE_INT off)
{
  HOST_WIDE_INT parity;

  if (MEM_ALIGN (x) >= 16)
    parity = 0;
  else
    {
      unsigned int align;
      unsigned HOST_WIDE_INT bitpos;
      HOST_WIDE_INT moff;

      if (!MEM_EXPR (x) || !MEM_OFFSET_KNOWN_P (x)
	  || !MEM_OFFSET (x).is_constant (&moff))
	return false;
      if (get_object_alignment_1 (MEM_EXPR (x), &align, &bitpos)
	  && align >= 16)
	parity = (bitpos / BITS_PER_UNIT + moff) & 1;
      else if (tdm_pointed_to_parity (MEM_EXPR (x), &parity))
	parity = (parity + moff) & 1;
      else
	return false;
    }
  return ((parity ^ off) & 1) == 0;
}

/* Whether the function may access an octet through $r0 whose half of
   the word isn't known, or that is the high half, which calls a byte
   access helper (see tdm_emit_byte_helper_call).  Such a function needs a frame, whose free
   word at [$sp] takes the return address.  The frame can't change once
   register allocation starts, so the answer is kept from then on; it
   comes from the octets in memory not addressed through $sp, the frame
   or a constant, whose bases go into $r0.  If the output still finds such
   an access in a function without a frame, it does it inline.  */

static bool
tdm_may_call_byte_helper (void)
{
  static int funcdef_no = -1;
  static bool result;
  bool keep = reload_completed || reload_in_progress || lra_in_progress;

  if (keep && funcdef_no == cfun->funcdef_no)
    return result;

  bool found = false;
  for (rtx_insn *insn = get_insns (); insn && !found; insn = NEXT_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      subrtx_var_iterator::array_type array;
      FOR_EACH_SUBRTX_VAR (iter, array, PATTERN (insn), NONCONST)
	{
	  rtx x = *iter;
	  if (!MEM_P (x) || GET_MODE (x) != QImode)
	    continue;
	  rtx addr = XEXP (x, 0);
	  HOST_WIDE_INT off = 0;
	  if (GET_CODE (addr) == PLUS && CONST_INT_P (XEXP (addr, 1)))
	    {
	      off = INTVAL (XEXP (addr, 1));
	      addr = XEXP (addr, 0);
	    }
	  if (CONSTANT_P (addr))
	    continue;
	  if (REG_P (addr)
	      && (REGNO (addr) == STACK_POINTER_REGNUM
		  || REGNO (addr) == ARG_POINTER_REGNUM
		  || REGNO (addr) == FRAME_POINTER_REGNUM
		  || REGNO (addr) == HARD_FRAME_POINTER_REGNUM
		  || VIRTUAL_REGISTER_NUM_P (REGNO (addr))))
	    continue;
	  /* An octet whose half isn't known, or the high one.  */
	  if (!tdm_r0_even_p (x, off) || (off & 1) != 0)
	    {
	      found = true;
	      break;
	    }
	}
    }
  if (keep)
    {
      funcdef_no = cfun->funcdef_no;
      result = found;
    }
  return found;
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
	    l.dynamic = GET_MODE (x) == QImode && !tdm_r0_even_p (x, off);
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

/* Scratch registers.

   The output of an insn often needs $r1 as a scratch register, and
   restores it afterwards; and an access through a byte pointer in $r0
   rotates it right and back.  tdm_mark_scratch emits a clobber of $r1
   (or $r0) after an insn where it is dead, so that the output can leave
   it changed, and variable tracking, which comes later, knows that its
   value is gone.

   The output of an insn other than a jump, a call or a pool goes to a
   buffer, and tdm_scratch_peephole drops what isn't needed:

   - "rol $r0; ror $r0" (or the other way round), which changes nothing;
   - the final "rol $r0", if $r0 is clobbered;
   - a save of $r1 in [$sp-1] and its restore, if $r1 is clobbered;
   - "swap $r1, X ... swap $r1, X" becomes "mov $r1, X ... mov X, $r1"
     if $r1 is clobbered, and a "mov $r1, X" goes if the next
     instruction overwrites $r1.

   Counting for the length does the same.

   Two insns next to each other that access memory through the same
   byte pointer in $r0 join: the "rol $r0" that ends the first and the
   "ror $r0" that starts the second go, and $r0 stays rotated in
   between (see tdm_r0_join_p).  The length of each insn then counts one
   less word.  */

/* The insn whose output is in the buffer, and the buffer.  */
static rtx_insn *tdm_buf_insn;
static auto_vec<char *> tdm_buf;

/* Whether $r0 and $r1 are clobbered after the insn in the buffer.  */
static bool tdm_buf_r0_free, tdm_buf_r1_free;

/* Whether the output of INSN goes to the buffer.  */

static bool
tdm_buffered_insn_p (rtx_insn *insn)
{
  return (insn
	  && NONJUMP_INSN_P (insn)
	  && recog_memoized (insn) >= 0
	  && recog_memoized (insn) != CODE_FOR_tdm_pool);
}

/* Whether a clobber of hard register REGNO follows INSN.  */

static bool
tdm_clobbered_after_p (rtx_insn *insn, unsigned int regno)
{
  for (rtx_insn *i = NEXT_INSN (insn); i; i = NEXT_INSN (i))
    {
      if (NOTE_P (i) || DEBUG_INSN_P (i))
	continue;
      if (!NONJUMP_INSN_P (i) || GET_CODE (PATTERN (i)) != CLOBBER)
	return false;
      rtx x = XEXP (PATTERN (i), 0);
      if (REG_P (x) && REGNO (x) == regno)
	return true;
    }
  return false;
}

/* Start buffering the output of INSN.  */

static void
tdm_buf_start (rtx_insn *insn)
{
  gcc_checking_assert (tdm_buf.is_empty ());
  tdm_buf_insn = insn;
  tdm_buf_r0_free = tdm_clobbered_after_p (insn, R0_REGNUM);
  tdm_buf_r1_free = tdm_clobbered_after_p (insn, R1_REGNUM);
}

/* Whether LINE mentions X: X not followed by a digit, so that $mr1
   isn't found in $mr19.  */

static bool
tdm_mentions_p (const char *line, const char *x)
{
  size_t n = strlen (x);
  for (const char *p = strstr (line, x); p; p = strstr (p + 1, x))
    if (!ISDIGIT (p[n]))
      return true;
  return false;
}

/* Whether LINE jumps by a number of words, within the output of the
   insn, other than over its own literal.  Lines can't be dropped then.  */

static bool
tdm_local_jump_p (const char *line)
{
  if (strcmp (line, "mov\t$pc, $pc+2") == 0)
    return false;
  for (const char *p = strstr (line, "$pc+"); p; p = strstr (p + 1, "$pc+"))
    if (ISDIGIT (p[4])
	&& !(p > line && p[-1] == '[' && (p[4] == '1' || p[4] == '2')
	     && p[5] == ']'))
      return true;
  return false;
}

static int tdm_count_insn (rtx_insn *, vec<tdm_lit> *);

/* While tdm_r0_ends counts an insn: its result.  */
static bool tdm_r0_querying;
static int tdm_r0_query_head, tdm_r0_query_tail;

/* The insn that the one output last joined (see above).  */
static rtx_insn *tdm_r0_joined;

/* The insn whose output comes right after that of INSN, if it is
   buffered, and nothing else that runs is output in between: a pool
   between them, with a jump over it, keeps them next to each other,
   so that placing pools doesn't change the lengths of the insns around
   them.  */

static rtx_insn *
tdm_r0_next (rtx_insn *insn)
{
  for (rtx_insn *i = NEXT_INSN (insn); i; i = NEXT_INSN (i))
    {
      if (NOTE_P (i))
	{
	  if (NOTE_KIND (i) == NOTE_INSN_DELETED_LABEL
	      || NOTE_KIND (i) == NOTE_INSN_DELETED_DEBUG_LABEL
	      || NOTE_KIND (i) == NOTE_INSN_SWITCH_TEXT_SECTIONS)
	    return NULL;
	  continue;
	}
      if (DEBUG_INSN_P (i))
	continue;
      if (NONJUMP_INSN_P (i)
	  && (GET_CODE (PATTERN (i)) == USE
	      || GET_CODE (PATTERN (i)) == CLOBBER
	      || recog_memoized (i) == CODE_FOR_tdm_pool))
	continue;
      return tdm_buffered_insn_p (i) ? i : NULL;
    }
  return NULL;
}

/* The same for the insn whose output comes right before.  */

static rtx_insn *
tdm_r0_prev (rtx_insn *insn)
{
  for (rtx_insn *i = PREV_INSN (insn); i; i = PREV_INSN (i))
    {
      if (NOTE_P (i))
	{
	  if (NOTE_KIND (i) == NOTE_INSN_DELETED_LABEL
	      || NOTE_KIND (i) == NOTE_INSN_DELETED_DEBUG_LABEL
	      || NOTE_KIND (i) == NOTE_INSN_SWITCH_TEXT_SECTIONS)
	    return NULL;
	  continue;
	}
      if (DEBUG_INSN_P (i))
	continue;
      if (NONJUMP_INSN_P (i)
	  && (GET_CODE (PATTERN (i)) == USE
	      || GET_CODE (PATTERN (i)) == CLOBBER
	      || recog_memoized (i) == CODE_FOR_tdm_pool))
	continue;
      return tdm_buffered_insn_p (i) ? i : NULL;
    }
  return NULL;
}

/* Count INSN, and return whether its output, after the peephole, starts
   with a "ror $r0" (HEAD) and ends with a "rol $r0" (TAIL) that a join
   may drop.  */

static void
tdm_r0_ends (rtx_insn *insn, bool *head, bool *tail)
{
  bool counting = tdm_counting;
  int words = tdm_words;
  vec<tdm_lit> *lits = tdm_lits;
  rtx_insn *lit_insn = tdm_lit_insn;
  int lit_index = tdm_lit_index;

  gcc_checking_assert (!tdm_r0_querying);
  tdm_r0_querying = true;
  tdm_r0_query_head = tdm_r0_query_tail = -1;
  tdm_count_insn (insn, NULL);
  tdm_r0_querying = false;
  *head = tdm_r0_query_head >= 0;
  *tail = tdm_r0_query_tail >= 0;

  tdm_counting = counting;
  tdm_words = words;
  tdm_lits = lits;
  tdm_lit_insn = lit_insn;
  tdm_lit_index = lit_index;
  /* The operands of INSN are in recog_data now.  */
  recog_data.insn = NULL;
}

/* Whether A and B, whose output follows that of A, join.  A source
   line starts with B otherwise: a debugger stopping there would find
   $r0 rotated.  */

static bool
tdm_r0_join_p (rtx_insn *a, rtx_insn *b)
{
  location_t la = INSN_LOCATION (a), lb = INSN_LOCATION (b);
  if (lb == UNKNOWN_LOCATION)
    return true;
  if (la == UNKNOWN_LOCATION)
    return false;
  expanded_location ea = expand_location (la), eb = expand_location (lb);
  return (ea.line == eb.line
	  && (ea.file == eb.file
	      || (ea.file && eb.file && strcmp (ea.file, eb.file) == 0)));
}

/* Set *HEAD and *TAIL to the first and the last of the lines in the
   buffer that stay and mention $r0, if they are a "ror $r0" and a
   "rol $r0" that a join may drop, or to -1.  */

static void
tdm_r0_buf_ends (const vec<bool> &keep, int *head, int *tail)
{
  unsigned int n = tdm_buf.length ();
  int first = -1, last = -1;

  *head = *tail = -1;
  for (unsigned int i = 0; i < n; i++)
    if (tdm_local_jump_p (tdm_buf[i]))
      return;
  for (unsigned int i = 0; i < n; i++)
    if (keep[i] && tdm_mentions_p (tdm_buf[i], "$r0"))
      {
	if (first < 0)
	  first = i;
	last = i;
      }
  if (first < 0)
    return;

  /* Whether the line I is conditional: the line that stays before it is
     a predicate.  */
  auto conditional = [&] (int i)
    {
      for (int j = i - 1; j >= 0; j--)
	if (keep[j])
	  return startswith (tdm_buf[j], "if_");
      return false;
    };
  if (strcmp (tdm_buf[first], "ror\t$r0") == 0 && !conditional (first))
    *head = first;
  if (last != first
      && strcmp (tdm_buf[last], "rol\t$r0") == 0 && !conditional (last))
    *tail = last;
}

/* Set KEEP[I] for the lines in the buffer that stay (see above).  */

static void
tdm_scratch_peephole (vec<bool> &keep)
{
  unsigned int n = tdm_buf.length ();
  bool jumps = false;

  for (unsigned int i = 0; i < n; i++)
    {
      keep.quick_push (true);
      jumps |= tdm_local_jump_p (tdm_buf[i]);
    }
  if (jumps)
    return;

  auto_vec<int> l;
  bool changed = true;
  while (changed)
    {
      changed = false;
      /* The lines that stay.  */
      l.truncate (0);
      for (unsigned int i = 0; i < n; i++)
	if (keep[i])
	  l.safe_push (i);
      int m = l.length ();
#define LINE(I) (tdm_buf[l[I]])
      /* A line that a predicate makes conditional stays as it is.  */
#define CONDITIONAL(I) ((I) > 0 && startswith (LINE ((I) - 1), "if_"))
#define DROP(I) (keep[l[I]] = false, changed = true)

      for (int i = 0; i + 1 < m && !changed; i++)
	if (((strcmp (LINE (i), "rol\t$r0") == 0
	      && strcmp (LINE (i + 1), "ror\t$r0") == 0)
	     || (strcmp (LINE (i), "ror\t$r0") == 0
		 && strcmp (LINE (i + 1), "rol\t$r0") == 0))
	    && !CONDITIONAL (i))
	  {
	    DROP (i);
	    DROP (i + 1);
	  }
      if (changed)
	continue;

      if (tdm_buf_r0_free)
	for (int i = m - 1; i >= 0; i--)
	  if (tdm_mentions_p (LINE (i), "$r0"))
	    {
	      if (strcmp (LINE (i), "rol\t$r0") == 0 && !CONDITIONAL (i))
		DROP (i);
	      break;
	    }
      if (changed || !tdm_buf_r1_free)
	continue;

      /* The last line that mentions $r1, and whether a later one
	 mentions the scratch word.  */
      int last = -1;
      bool scratch_after = false;
      for (int i = 0; i < m; i++)
	if (tdm_mentions_p (LINE (i), "$r1"))
	  last = i, scratch_after = false;
	else if (tdm_mentions_p (LINE (i), "[$sp-1]"))
	  scratch_after = true;

      for (int i = 0; i < last && !changed; i++)
	{
	  const char *a = LINE (i);
	  int j;

	  if (CONDITIONAL (i))
	    continue;

	  /* A save of $r1 and the restore that is its last mention.  */
	  if (strcmp (a, "mov\t[$sp-1], $r1") == 0)
	    {
	      if (strcmp (LINE (last), "mov\t$r1, [$sp-1]") != 0
		  || CONDITIONAL (last) || scratch_after)
		continue;
	      for (j = i + 1; j < last; j++)
		if (tdm_mentions_p (LINE (j), "[$sp-1]"))
		  break;
	      if (j == last)
		{
		  DROP (i);
		  DROP (last);
		}
	    }

	  /* A swap with X and the one back that is the last mention.  */
	  else if (startswith (a, "swap\t$r1, ") && strcmp (a, LINE (last)) == 0)
	    {
	      const char *x = a + strlen ("swap\t$r1, ");
	      for (j = i + 1; j < last; j++)
		if (tdm_mentions_p (LINE (j), x))
		  break;
	      if (j == last)
		{
		  char *back = xasprintf ("mov\t%s, $r1", x);
		  char *load = xasprintf ("mov\t$r1, %s", x);
		  free (LINE (last));
		  LINE (last) = back;
		  free (LINE (i));
		  LINE (i) = load;
		  changed = true;
		}
	    }

	  /* A load of $r1 that the next line overwrites.  */
	  else if (startswith (a, "mov\t$r1, ")
		   && startswith (LINE (i + 1), "mov\t$r1, ")
		   && !tdm_mentions_p (LINE (i + 1) + strlen ("mov\t$r1, "),
				       "$r1"))
	    DROP (i);
	}
#undef LINE
#undef CONDITIONAL
#undef DROP
    }
}

/* Finish the output of the insn in the buffer: drop what isn't needed,
   count it, give the literals in tdm_lits their final words, and unless
   counting, output it.  */

static void
tdm_buf_finish (void)
{
  if (!tdm_buf_insn)
    return;

  unsigned int n = tdm_buf.length ();
  auto_vec<bool> keep (n);
  auto_vec<int> word (n + 1);
  int w = 0;

  tdm_scratch_peephole (keep);

  int head, tail;
  tdm_r0_buf_ends (keep, &head, &tail);
  if (tdm_r0_querying)
    {
      tdm_r0_query_head = head;
      tdm_r0_query_tail = tail;
    }
  else
    {
      rtx_insn *insn = tdm_buf_insn;
      bool head_ok, tail_ok;

      /* The insn before INSN has joined it.  */
      bool joined;
      if (tdm_counting)
	{
	  rtx_insn *prev = head >= 0 ? tdm_r0_prev (insn) : NULL;
	  joined = false;
	  if (prev && tdm_r0_join_p (prev, insn))
	    {
	      tdm_r0_ends (prev, &head_ok, &tail_ok);
	      joined = tail_ok;
	    }
	}
      else
	{
	  joined = tdm_r0_joined == insn;
	  gcc_checking_assert (!joined || head >= 0);
	}
      if (joined)
	keep[head] = false;

      /* INSN joins the next one.  */
      rtx_insn *next = tail >= 0 ? tdm_r0_next (insn) : NULL;
      bool joins = false;
      if (next && tdm_r0_join_p (insn, next))
	{
	  tdm_r0_ends (next, &head_ok, &tail_ok);
	  joins = head_ok;
	}
      if (joins)
	keep[tail] = false;
      if (!tdm_counting)
	tdm_r0_joined = joins ? next : NULL;
    }

  for (unsigned int i = 0; i < n; i++)
    {
      word.quick_push (w);
      w += keep[i];
    }
  word.quick_push (w);
  if (tdm_lits)
    for (tdm_lit &l : *tdm_lits)
      l.word = word[l.word];
  tdm_words = w;

  for (unsigned int i = 0; i < n; i++)
    {
      if (keep[i] && !tdm_counting)
	fprintf (asm_out_file, "\t%s\n", tdm_buf[i]);
      free (tdm_buf[i]);
    }
  tdm_buf.truncate (0);
  tdm_buf_insn = NULL;
}

/* TARGET_ASM_FINAL_POSTSCAN_INSN: the output of an insn is complete.  */

static void
tdm_final_postscan_insn (FILE *, rtx_insn *, rtx *, int)
{
  tdm_buf_finish ();
}

/* Emit one instruction or data word.  When only counting (for the
   length attribute), count it.  */

/* In final, start buffering the output of the insn being output if that
   hasn't happened yet (counting starts it in tdm_count_insn).  */

static void
tdm_buf_sync (void)
{
  if (!tdm_counting && current_output_insn != tdm_buf_insn)
    {
      tdm_buf_finish ();
      if (tdm_buffered_insn_p (current_output_insn))
	tdm_buf_start (current_output_insn);
    }
}

/* Whether $r1 is dead after the insn being counted or output, so that
   its output may leave it changed.  */

static bool
tdm_r1_free_p (void)
{
  tdm_buf_sync ();
  return tdm_buf_insn != NULL && tdm_buf_r1_free;
}

static void ATTRIBUTE_PRINTF_1
tdm_emit (const char *fmt, ...)
{
  va_list ap;

  tdm_buf_sync ();
  va_start (ap, fmt);
  if (tdm_buf_insn)
    {
      /* Until tdm_buf_finish, the word of a literal in tdm_lits is its
	 index in the buffer.  */
      gcc_checking_assert (!tdm_counting
			   || tdm_words == (int) tdm_buf.length ());
      tdm_buf.safe_push (xvasprintf (fmt, ap));
      tdm_words++;
    }
  else
    {
      tdm_words++;
      if (!tdm_counting)
	{
	  putc ('\t', asm_out_file);
	  vfprintf (asm_out_file, fmt, ap);
	  putc ('\n', asm_out_file);
	}
    }
  va_end (ap);
}

static void
tdm_sorry (const char *msg)
{
  if (!tdm_counting)
    sorry ("%s", msg);
}

/* The pool label of the next literal of the insn being counted or
   output, or -1 if it is inline.  If ADVANCE, move on to the one after
   it.  */

static int
tdm_next_literal_label (bool advance)
{
  int label = -1;

  if (!tdm_counting && current_output_insn != tdm_lit_insn)
    {
      tdm_lit_insn = current_output_insn;
      tdm_lit_index = 0;
    }
  if (tdm_lit_insn)
    {
      if (tdm_lit_labels)
	if (int *l = tdm_lit_labels->get (std::make_pair (INSN_UID (tdm_lit_insn),
							   tdm_lit_index)))
	  label = *l;
      if (advance)
	tdm_lit_index++;
    }
  return label;
}

/* Whether literal INDEX of INSN is in a pool.  */

static bool
tdm_literal_pooled_p (rtx_insn *insn, int index)
{
  return (tdm_lit_labels
	  && tdm_lit_labels->get (std::make_pair (INSN_UID (insn), index)));
}

/* Emit OP with the word WORD as second operand, from a literal that
   the code jumps over.  A "mov $pc" loads the literal directly, as
   KIND, which is LIT_JUMP or LIT_CBRANCH.  */

static void
tdm_emit_literal (const char *op, const char *dest, const char *word,
		  enum tdm_lit_kind kind = LIT_JUMP)
{
  bool jump = strcmp (op, "mov") == 0 && strcmp (dest, "$pc") == 0;
  int label = tdm_next_literal_label (true);

  if (tdm_lits)
    {
      tdm_lit l;
      l.kind = jump ? kind : LIT_CONST;
      l.word = tdm_words;
      snprintf (l.text, sizeof l.text, "%s", word);
      tdm_lits->safe_push (l);
    }
  if (label >= 0)
    {
      tdm_emit ("%s\t%s, [$pc+(.Llit%d-.)]", op, dest, label);
      return;
    }
  if (jump)
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
   known at run time.  The work is done out of line, in libgcc
   (config/tdm/tdm-byte.S), with a convention of its own:

   - __tdm_load8 loads the octet at the byte address in $r0, zero
     extended, into $r1;
   - __tdm_store8 stores the low octet of [$sp-1] at the byte address
     in $r0, and changes $r1;

   and nothing else changes, but the return address in [$sp].  They are
   called as other functions are, with the address loaded into $r1.
   OFF is the byte offset of the access, added to $r0 around the call.  */

static bool tdm_frame_free_word_p (void);

/* Call the byte access helper NAME.  */

static void
tdm_emit_byte_helper_call (const char *name)
{
  tdm_loc sym = tdm_make_loc (LOC_SYM);

  sym.sym = gen_rtx_SYMBOL_REF (Pmode, name);
  SYMBOL_REF_FLAGS (sym.sym) |= SYMBOL_FLAG_FUNCTION;
  tdm_emit_to_reg ("mov", tdm_r1, sym);
  tdm_emit ("mov\t[$sp], $r1");
  tdm_emit ("swap\t$pc, [$sp]");
}

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
  if (tdm_frame_free_word_p ())
    tdm_emit_byte_helper_call ("__tdm_load8");
  else
    {
      /* No frame: [$sp] holds the return address.  Inline, as
	 __tdm_load8 does.  */
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
    }

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

/* Store the low octet of S at [$r0 + OFF].  The value goes to the helper
   in [$sp-1], by way of $r1.  If $r1 is live and S isn't $r1, or the
   function has no frame, do it inline: mem ^= (value ^ mem) & mask, in
   $r1.  */

static void
tdm_emit_store_octet_dynamic (const tdm_loc &s, HOST_WIDE_INT off)
{
  tdm_loc r0 = tdm_make_loc (LOC_R0);
  bool r1_free = tdm_r1_free_p ();

  if ((s.kind == LOC_R1 || r1_free) && tdm_frame_free_word_p ())
    {
      tdm_emit_to_reg ("mov", tdm_r1, s);
      tdm_emit ("mov\t[$sp-1], $r1");
      if (off)
	tdm_emit_to_reg ("add", r0, tdm_make_loc (LOC_IMM, tdm_sext16 (off)));
      tdm_emit_byte_helper_call ("__tdm_store8");
      if (off)
	tdm_emit_to_reg ("add", r0, tdm_make_loc (LOC_IMM, tdm_sext16 (-off)));
      /* The helper leaves the value, which was $r1, in [$sp-1].  */
      if (!r1_free)
	tdm_emit ("mov\t$r1, [$sp-1]");
      return;
    }

  tdm_emit ("mov\t[$sp-1], $r1");
  tdm_emit_to_reg ("mov", tdm_r1, s);
  if (off)
    tdm_emit_to_reg ("add", r0, tdm_make_loc (LOC_IMM, tdm_sext16 (off)));
  tdm_emit ("ror\t$r0");
  tdm_emit ("if_ges\t$r0, 0");
  tdm_emit ("mov\t$pc, $pc+16");
  /* High half: C data lives in the first 32K words, so after "ror $r0"
     bit 15 holds the half and the rest the word address; $mr5 (0x7fff)
     clears bit 15.  */
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

      /* The helper is smaller than shifting the high half down, if it
	 can be called (see tdm_emit_load_octet_dynamic).  */
      if (s.dynamic
	  || (s.kind == LOC_R0MEM && s.high && !s.word
	      && tdm_frame_free_word_p ()))
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

      /* For the high half, as for loads above, if the helper can be
	 called (see tdm_emit_store_octet_dynamic).  */
      if (d.dynamic
	  || (d.kind == LOC_R0MEM && d.high && !d.word
	      && tdm_frame_free_word_p ()
	      && (s.kind == LOC_R1 || tdm_r1_free_p ())))
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
   over a long jump with the inverted condition.

   Emit what a cbranch needs before its predicate, and return the
   operand the predicate compares the register operand with.  */

static tdm_loc
tdm_cbranch_operand (rtx *operands)
{
  tdm_loc a = tdm_classify (operands[1]);
  tdm_loc b = tdm_classify (operands[2]);

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
  return b;
}

/* While counting the length of an insn, the buffered output of another
   one being output is put aside.  */

class tdm_buf_stash
{
public:
  tdm_buf_stash ()
    : m_insn (tdm_buf_insn), m_r0 (tdm_buf_r0_free), m_r1 (tdm_buf_r1_free)
  {
    m_buf.safe_splice (tdm_buf);
    tdm_buf.truncate (0);
    tdm_buf_insn = NULL;
  }
  ~tdm_buf_stash ()
  {
    gcc_checking_assert (!tdm_buf_insn && tdm_buf.is_empty ());
    tdm_buf.safe_splice (m_buf);
    tdm_buf_insn = m_insn;
    tdm_buf_r0_free = m_r0;
    tdm_buf_r1_free = m_r1;
  }

private:
  rtx_insn *m_insn;
  bool m_r0, m_r1;
  auto_vec<char *> m_buf;
};

/* The length in octets of what tdm_cbranch_operand emits for INSN.
   Set *NLITS, if given, to the number of literals in it.  */

static int
tdm_cbranch_prefix_length (rtx_insn *insn, int *nlits = NULL)
{
  bool counting = tdm_counting;
  int words = tdm_words;
  vec<tdm_lit> *lits = tdm_lits;
  rtx_insn *lit_insn = tdm_lit_insn;
  int lit_index = tdm_lit_index;
  int n;

  if (nlits)
    *nlits = 0;
  if (!reload_completed)
    return 2;
  tdm_buf_stash stash;
  extract_constrain_insn_cached (insn);
  tdm_counting = true;
  tdm_words = 0;
  tdm_lits = NULL;
  /* The operand comes first in the insn's output: its literals are the
     first ones.  */
  tdm_lit_insn = insn;
  tdm_lit_index = 0;
  tdm_cbranch_operand (recog_data.operand);
  n = tdm_words;
  if (nlits)
    *nlits = tdm_lit_index;
  tdm_counting = counting;
  tdm_words = words;
  tdm_lits = lits;
  tdm_lit_insn = lit_insn;
  tdm_lit_index = lit_index;
  return n * 2;
}

/* ADJUST_INSN_LENGTH for INSN with length attribute LENGTH: add the
   cbranch prefix.  A jump or cbranch whose target is in a pool always
   loads it from there, near or far, and is as long as a near one.  A
   branch between the hot and the cold part of a function is far
   otherwise: the distance between the sections is only known when
   linking.  */

int
tdm_adjust_insn_length (rtx_insn *insn, int length)
{
  if (!INSN_P (insn))
    return length;
  int code = recog_memoized (insn);
  bool crossing = JUMP_P (insn) && CROSSING_JUMP_P (insn);
  if (code == CODE_FOR_tdm_cbranchhi4)
    {
      int nlits;
      int prefix = tdm_cbranch_prefix_length (insn, &nlits);
      if (tdm_literal_pooled_p (insn, nlits))
	length = 4;
      else if (crossing)
	length = 8;
      length += prefix;
    }
  else if (code == CODE_FOR_jump)
    {
      if (tdm_literal_pooled_p (insn, 0))
	length = 2;
      else if (crossing)
	length = 4;
    }
  return length;
}

/* Output a conditional branch: the predicate, then a short jump, a
   jump loading the target from its pool, or for a far target that
   isn't in a pool, a skip over a long jump with the inverted
   condition.  */

const char *
tdm_output_cbranch (rtx *operands, rtx_insn *insn)
{
  enum rtx_code code = GET_CODE (operands[0]);
  tdm_loc a = tdm_classify (operands[1]);
  tdm_loc b = tdm_cbranch_operand (operands);
  bool pooled = tdm_next_literal_label (false) >= 0;
  bool far = (!pooled
	      && get_attr_length (insn) - tdm_cbranch_prefix_length (insn) > 4);
  char lab[32];

  if (far)
    code = reverse_condition (code);
  tdm_label_text (operands[3], lab, sizeof lab);
  tdm_emit ("if_%s\t%s, %s", tdm_condition_name (code), tdm_reg_text (a),
	    tdm_opb_text (b));
  if (pooled)
    tdm_emit_literal ("mov", "$pc", lab, LIT_CBRANCH);
  else if (!far)
    tdm_emit ("mov\t$pc, $pc+(%s-.)", lab);
  else
    {
      tdm_emit ("mov\t$pc, $pc+3");
      tdm_emit_literal ("mov", "$pc", lab, LIT_CBRANCH);
    }
  return "";
}

const char *
tdm_output_jump (rtx label, rtx_insn *insn)
{
  char lab[32];

  tdm_label_text (label, lab, sizeof lab);
  if (tdm_next_literal_label (false) >= 0 || get_attr_length (insn) > 2)
    tdm_emit_literal ("mov", "$pc", lab);
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

/* Call SYM, a __tdm_save_N routine.  */

const char *
tdm_output_save_call (rtx sym)
{
  return tdm_output_call (sym);
}

/* Jump to SYM, a __tdm_restore_N routine, with INC in [$sp].  */

const char *
tdm_output_restore_return (rtx sym, rtx inc)
{
  tdm_emit_to_reg ("mov", tdm_r1, tdm_make_loc (LOC_IMM, INTVAL (inc)));
  tdm_emit ("mov\t[$sp], $r1");
  tdm_emit_to_reg ("mov", tdm_make_loc (LOC_PC), tdm_classify (sym));
  return "";
}

/* The length of INSN, in octets: count what its output function
   emits.  If LITS is given, add the literals it emits there.  */

static int
tdm_count_insn (rtx_insn *insn, vec<tdm_lit> *lits)
{
  int code = recog_memoized (insn);
  const char *tmpl;
  int words;

  if (code < 0 || !reload_completed)
    return 2;

  tdm_buf_stash stash;
  extract_constrain_insn_cached (insn);
  tdm_counting = true;
  tdm_words = 0;
  tdm_lits = lits;
  tdm_lit_insn = insn;
  tdm_lit_index = 0;
  if (tdm_buffered_insn_p (insn))
    tdm_buf_start (insn);
  tmpl = get_insn_template (code, insn);
  tdm_buf_finish ();
  tdm_lit_insn = NULL;
  tdm_lits = NULL;
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

int
tdm_insn_length (rtx_insn *insn)
{
  return tdm_count_insn (insn, NULL);
}

/* A pool of the current function.  */

struct tdm_pool
{
  rtx_insn *insn;
  /* The number of entries; whether a jump over them comes first; the
     estimated word address of the pool and its segment (see
     tdm_pool_use); whether tdm_reorg's sweep has passed it.  */
  int size;
  bool skip;
  int addr, seg;
  bool passed;
};

static auto_vec<tdm_pool> tdm_pools;

/* Output pool ID of the current function: a label and a word for each
   live entry, after a jump over them if SKIP.  While tdm_reorg places
   the pools, before the entries are laid out, only count them.  */

const char *
tdm_output_pool (rtx id, rtx skip)
{
  unsigned int pool = INTVAL (id);
  int live = 0;

  gcc_assert (pool < tdm_pools.length ());
  if (pool + 1 >= tdm_pool_start.length ())
    {
      gcc_assert (tdm_counting);
      if (INTVAL (skip) && tdm_pools[pool].size)
	tdm_emit ("mov\t$pc, $pc+%d", tdm_pools[pool].size + 1);
      for (int i = 0; i < tdm_pools[pool].size; i++)
	tdm_emit (".word\t0");
      return "";
    }
  for (int i = tdm_pool_start[pool]; i < tdm_pool_start[pool + 1]; i++)
    if (!tdm_pool_entries[i].dead)
      live++;
  if (INTVAL (skip) && live)
    tdm_emit ("mov\t$pc, $pc+%d", live + 1);
  for (int i = tdm_pool_start[pool]; i < tdm_pool_start[pool + 1]; i++)
    {
      const tdm_pool_entry &e = tdm_pool_entries[i];
      if (e.dead)
	continue;
      if (!tdm_counting)
	fprintf (asm_out_file, ".Llit%d:\n", e.label);
      tdm_emit (".word\t%s", e.text);
    }
  return "";
}

/* The word address of INSN, from shorten_branches.  */

static int
tdm_insn_word (rtx_insn *insn)
{
  return INSN_ADDRESSES (INSN_UID (insn)) / 2;
}

/* When choosing a pool, the distance to it is estimated.  Allow for
   some change; the distances are checked once the pools are in
   place.  */
#define TDM_POOL_MARGIN 2

/* The same for the pools after barriers, chosen from addresses that
   assume all the wide operands are in pools.  */
#define TDM_BARRIER_POOL_MARGIN 4

/* The most entries of a pool with a jump over it, which reaches 31
   words.  */
#define TDM_POOL_MAX 24

/* A wide operand that may load its value from a pool.  */

struct tdm_pool_use
{
  rtx_insn *insn;
  /* Its index among the literals of INSN.  */
  int index;
  /* Its estimated word address, and its segment: code between an asm
     statement or a section switch and the next one.  A use and its pool
     must be in the same segment: the length of an asm statement is only
     an estimate.  */
  int addr, seg;
  char text[TDM_LIT_LEN];
  /* Its pool entry (see tdm_pool_builder), or -1.  */
  int ent;
  /* The words it saves in a pool.  */
  int save;
};

/* The words saved by putting a literal of KIND in a pool: a wide
   operand isn't jumped over, a far jump loads its target directly, and
   a far cbranch doesn't skip over a long jump.  */

static int
tdm_pool_saving (enum tdm_lit_kind kind)
{
  return kind == LIT_JUMP ? 1 : 2;
}

/* A place for a pool: after a barrier.  */

struct tdm_pool_site
{
  rtx_insn *barrier;
  int addr, seg;
  /* The pool placed there, or -1.  */
  int pool;
};

/* An entry of the pools being set up, shared by the uses with the same
   text.  */

struct tdm_pool_ent
{
  /* Its pool, or -1 while it waits for one, and its position there.  */
  int pool, pos;
  const char *text;
  int label;
  /* The number of uses, and the estimated address of the first.  */
  int nuses, first_addr;
  /* The entry before it with the same text, or -1.  */
  int prev;
};

/* The pools and entries tdm_reorg sets up.  */

class tdm_pool_builder
{
public:
  tdm_pool_builder (vec<tdm_pool> *pools) : m_pools (pools) {}

  int add_pool (rtx_insn *, bool, bool, int, int);
  int new_entry (tdm_pool_use *, int);
  void join (tdm_pool_use *, int);
  void place (int, int);
  /* The last entry with TEXT, or -1; go on with tdm_pool_ent::prev.  */
  int lookup (const char *text)
  {
    int *e = m_last.get (text);
    return e ? *e : -1;
  }

  vec<tdm_pool> *m_pools;
  auto_vec<tdm_pool_ent> m_ents;
  hash_map<nofree_string_hash, int> m_last;
};

/* Add a pool, SKIP or not, before (or, if AFTER, after) WHERE, with
   estimated address ADDR in segment SEG.  */

int
tdm_pool_builder::add_pool (rtx_insn *where, bool after, bool skip,
			    int addr, int seg)
{
  int id = m_pools->length ();
  rtx pat = gen_tdm_pool (GEN_INT (id), GEN_INT (skip));
  rtx_insn *p = after ? emit_insn_after (pat, where)
		      : emit_insn_before (pat, where);

  if (!skip)
    emit_barrier_after (p);
  m_pools->safe_push ({p, 0, skip, addr, seg, false});
  return id;
}

/* Add an entry for use U to POOL, or to none yet if POOL is -1.  */

int
tdm_pool_builder::new_entry (tdm_pool_use *u, int pool)
{
  int id = m_ents.length ();
  /* Uses are added after the sweep: keep the text elsewhere.  */
  const char *text = ggc_strdup (u->text);
  tdm_pool_ent e = { -1, -1, text, ++tdm_lit_label_no, 0, u->addr,
		     lookup (text) };

  m_ents.safe_push (e);
  m_last.put (text, id);
  if (pool >= 0)
    place (id, pool);
  join (u, id);
  return id;
}

/* Put entry ENT, which has none yet, in POOL.  */

void
tdm_pool_builder::place (int ent, int pool)
{
  m_ents[ent].pool = pool;
  m_ents[ent].pos = (*m_pools)[pool].size++;
}

/* Let use U load its value from entry ENT.  */

void
tdm_pool_builder::join (tdm_pool_use *u, int ent)
{
  u->ent = ent;
  m_ents[ent].nuses++;
  tdm_lit_labels->put (std::make_pair (INSN_UID (u->insn), u->index),
		       m_ents[ent].label);
}

/* Whether the entries PENDING would be out of reach of their first
   uses in a pool with a jump at ADDR.  */

static bool
tdm_pending_out_of_reach (const tdm_pool_builder &b, const vec<int> &pending,
			  int addr)
{
  for (unsigned int j = 0; j < pending.length (); j++)
    if (addr + 1 + (int) j - b.m_ents[pending[j]].first_addr
	> 31 - TDM_POOL_MARGIN)
      return true;
  return false;
}

/* Whether use U reaches pool entry FLAT of pool P with the current
   addresses: 0 if it does, else the distance.  */

static int
tdm_pool_reach (const tdm_pool_use &u, int flat, const tdm_pool &p)
{
  auto_vec<tdm_lit> lits;
  int pool = INTVAL (XVECEXP (PATTERN (p.insn), 0, 0));

  tdm_count_insn (u.insn, &lits);
  if (u.index >= (int) lits.length ())
    return INT_MAX;
  int from = tdm_insn_word (u.insn) + lits[u.index].word;
  int to = tdm_insn_word (p.insn) + p.skip;
  for (int e = tdm_pool_start[pool]; e < flat; e++)
    if (!tdm_pool_entries[e].dead)
      to++;
  return IN_RANGE (to - from, -32, 31) ? 0 : to - from;
}

/* Whether INSN accesses memory through $r0.  */

static bool
tdm_r0_base_p (rtx_insn *insn)
{
  subrtx_iterator::array_type array;
  FOR_EACH_SUBRTX (iter, array, PATTERN (insn), NONCONST)
    if (MEM_P (*iter) && reg_mentioned_p (gen_rtx_REG (Pmode, R0_REGNUM),
					   XEXP (*iter, 0)))
      return true;
  return false;
}

/* Emit a clobber of $r1 after each insn where it is dead and that
   doesn't use it, and of $r0 after each insn where it is dead and that
   accesses memory through it (see "Scratch registers").  Not after
   jumps and calls, whose output doesn't make use of it.  */

static unsigned int
tdm_mark_scratch (void)
{
  rtx r0 = gen_rtx_REG (HImode, R0_REGNUM);
  rtx r1 = gen_rtx_REG (HImode, R1_REGNUM);
  auto_bitmap live;
  basic_block bb;

  df_analyze ();
  FOR_EACH_BB_FN (bb, cfun)
    {
      rtx_insn *insn, *prev;

      bitmap_copy (live, df_get_live_out (bb));
      df_simulate_initialize_backwards (bb, live);
      FOR_BB_INSNS_REVERSE_SAFE (bb, insn, prev)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  if (NONJUMP_INSN_P (insn) && recog_memoized (insn) >= 0)
	    {
	      if (!bitmap_bit_p (live, R1_REGNUM)
		  && !reg_mentioned_p (r1, PATTERN (insn)))
		emit_insn_after (gen_rtx_CLOBBER (VOIDmode, r1), insn);
	      if (!bitmap_bit_p (live, R0_REGNUM)
		  && !reg_set_p (r0, insn)
		  && tdm_r0_base_p (insn))
		emit_insn_after (gen_rtx_CLOBBER (VOIDmode, r0), insn);
	    }
	  df_simulate_one_insn_backwards (bb, insn, live);
	}
    }
  return 0;
}

namespace {

const pass_data pass_data_tdm_scratch =
{
  RTL_PASS, /* type */
  "tdm_scratch", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_MACH_DEP, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_tdm_scratch : public rtl_opt_pass
{
public:
  pass_tdm_scratch (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_tdm_scratch, ctxt)
  {}

  unsigned int execute (function *) final override
  {
    return tdm_mark_scratch ();
  }
};

} // anon namespace

rtl_opt_pass *
make_pass_tdm_scratch (gcc::context *ctxt)
{
  return new pass_tdm_scratch (ctxt);
}

/* Machine-dependent reorg: put the wide operands of the function in
   pools, in reach of their uses (-32..31 words), one entry for the
   uses of a value that reach it.  A pool goes after a barrier where
   there is one in reach, else in the middle of the code with a jump
   over it.  Operands that end up out of reach stay inline.  The mach
   dump lists the literals and pools.  */

static void
tdm_reorg (void)
{
  static const char *const kinds[] = { "const", "jump", "cbranch" };
  auto_vec<tdm_lit> lits;
  auto_vec<tdm_pool_use> uses;
  auto_vec<tdm_pool_site> sites;
  auto_vec<tdm_pool> &pools = tdm_pools;
  tdm_pool_builder b (&pools);
  int n[3] = {}, nasm = 0, seg = 0, pooled = 0, reverted = 0, rejoined = 0;
  /* The addresses in the scan below are estimated as if all the wide
     operands before were in pools: two words shorter each.  */
  int shrink = 0;

  tdm_pools.truncate (0);
  tdm_pool_entries.truncate (0);
  tdm_pool_start.truncate (0);
  if (tdm_lit_labels)
    tdm_lit_labels->empty ();
  else
    tdm_lit_labels = new hash_map<tdm_lit_key_hash, int>;
  tdm_lit_insn = NULL;

  /* Find the literals and the places for pools.  */
  shorten_branches (get_insns ());
  for (rtx_insn *insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      if (NOTE_P (insn) && NOTE_KIND (insn) == NOTE_INSN_SWITCH_TEXT_SECTIONS)
	seg++;
      if (BARRIER_P (insn))
	sites.safe_push ({insn, tdm_insn_word (insn) - shrink, seg, -1});
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (recog_memoized (insn) < 0)
	{
	  if (asm_noperands (PATTERN (insn)) >= 0
	      || GET_CODE (PATTERN (insn)) == ASM_INPUT)
	    {
	      nasm++;
	      seg++;
	    }
	  continue;
	}
      lits.truncate (0);
      int len = tdm_count_insn (insn, &lits);
      if (dump_file && len != get_attr_length (insn))
	fprintf (dump_file, ";; length: insn %d (%s), attribute %d, emitted %d\n",
		 INSN_UID (insn), get_insn_name (INSN_CODE (insn)),
		 get_attr_length (insn), len);
      for (unsigned int i = 0; i < lits.length (); i++)
	{
	  const tdm_lit &l = lits[i];
	  int addr = tdm_insn_word (insn) + l.word;

	  n[l.kind]++;
	  if (dump_file)
	    fprintf (dump_file, ";; literal %s: insn %d, word %d: %s\n",
		     kinds[l.kind], INSN_UID (insn), addr, l.text);
	  tdm_pool_use u = { insn, (int) i, addr - shrink, seg, "", -1,
			     tdm_pool_saving (l.kind) };
	  snprintf (u.text, sizeof u.text, "%s", l.text);
	  uses.safe_push (u);
	  shrink += u.save;
	}
    }
  if (dump_file)
    fprintf (dump_file, ";; literals: const %d jump %d cbranch %d, asm %d\n",
	     n[LIT_CONST], n[LIT_JUMP], n[LIT_CBRANCH], nasm);

  /* Pools after barriers.  Each use takes the nearest entry with its
     value in reach, else a new entry in the nearest pool in reach.  */
  for (tdm_pool_use &u : uses)
    {
      int best_ent = -1, best_dist = INT_MAX;
      for (int e = b.lookup (u.text); e >= 0; e = b.m_ents[e].prev)
	{
	  const tdm_pool &p = pools[b.m_ents[e].pool];
	  int off = p.addr + b.m_ents[e].pos - u.addr;
	  if (p.seg == u.seg
	      && off >= -32 + TDM_BARRIER_POOL_MARGIN
	      && off <= 31 - TDM_BARRIER_POOL_MARGIN
	      && abs (off) < best_dist)
	    {
	      best_ent = e;
	      best_dist = abs (off);
	    }
	}
      if (best_ent >= 0)
	{
	  b.join (&u, best_ent);
	  pooled++;
	  continue;
	}

      tdm_pool_site *best = NULL;
      for (tdm_pool_site &s : sites)
	{
	  int size = s.pool < 0 ? 0 : pools[s.pool].size;
	  int off = s.addr + size - u.addr;

	  if (s.seg == u.seg
	      && off >= -32 + TDM_BARRIER_POOL_MARGIN
	      && off <= 31 - TDM_BARRIER_POOL_MARGIN
	      && abs (off) < best_dist)
	    {
	      best = &s;
	      best_dist = abs (off);
	    }
	}
      if (!best)
	continue;
      if (best->pool < 0)
	best->pool = b.add_pool (best->barrier, true, false, best->addr,
				 best->seg);
      b.new_entry (&u, best->pool);
      pooled++;
    }
  int barrier_pools = pools.length ();

  /* The others: sweep through the code with the pools above in place,
     keeping the entries not yet in a pool.  Before the first use of one
     of them would get out of reach, put them in a pool with a jump over
     it, before the labels in front of the next insn, so that only the
     fallthrough path takes the jump; after a barrier, without one.  A
     use takes an entry with its value in a pool behind it in reach, or
     one waiting for a pool; else a new entry in the last pool if that's
     in reach, or a new waiting one.  SHIFT corrects the addresses for
     the pools put in so far and for the uses that moved to one or will
     (an inline literal is two words longer).  */
  if (pooled < (int) uses.length ())
    {
      auto_vec<int> pending;
      int forced = -1, shift = 0;
      unsigned int ui = 0;
      rtx_insn *point = NULL, *barrier = NULL;

      seg = 0;
      shorten_branches (get_insns ());
      auto flush = [&] (rtx_insn *where, int addr)
	{
	  if (pending.is_empty ())
	    return;
	  int id = (barrier
		    ? b.add_pool (barrier, true, false, addr, seg)
		    : b.add_pool (point ? point : where, false, true, addr, seg));
	  pools[id].passed = true;
	  for (int e : pending)
	    b.place (e, id);
	  shift += pools[id].skip + pools[id].size;
	  pending.truncate (0);
	  forced = id;

	  /* The uses before it of the pools after barriers further on are
	     further from them now: move those that get out of reach to
	     it.  */
	  for (unsigned int i = 0; i < ui; i++)
	    {
	      tdm_pool_use &u = uses[i];
	      if (u.ent < 0 || u.seg != seg)
		continue;
	      const tdm_pool_ent &x = b.m_ents[u.ent];
	      const tdm_pool &q = pools[x.pool];
	      if (q.passed || x.pool == id || q.seg != seg
		  || pools[id].size >= TDM_POOL_MAX
		  || (tdm_insn_word (q.insn) + shift + q.skip + x.pos - u.addr
		      <= 31 - TDM_POOL_MARGIN))
		continue;
	      b.m_ents[u.ent].nuses--;
	      tdm_lit_labels->remove (std::make_pair (INSN_UID (u.insn),
						      u.index));
	      b.new_entry (&u, id);
	      shift++;
	    }
	};

      for (rtx_insn *insn = get_insns (); insn; insn = NEXT_INSN (insn))
	{
	  if (LABEL_P (insn) && !point)
	    point = insn;
	  if (BARRIER_P (insn))
	    barrier = insn;
	  bool asm_p = (NONDEBUG_INSN_P (insn) && recog_memoized (insn) < 0
			&& (asm_noperands (PATTERN (insn)) >= 0
			    || GET_CODE (PATTERN (insn)) == ASM_INPUT));
	  if (asm_p
	      || (NOTE_P (insn)
		  && NOTE_KIND (insn) == NOTE_INSN_SWITCH_TEXT_SECTIONS))
	    {
	      /* The end of a segment.  */
	      flush (insn, tdm_insn_word (insn) + shift);
	      forced = -1;
	      point = barrier = NULL;
	      seg++;
	      continue;
	    }
	  if (!NONDEBUG_INSN_P (insn) || recog_memoized (insn) < 0)
	    continue;

	  int addr = tdm_insn_word (insn) + shift;
	  if (INSN_CODE (insn) == CODE_FOR_tdm_pool)
	    {
	      /* A pool after a barrier.  */
	      tdm_pool &p = pools[INTVAL (XVECEXP (PATTERN (insn), 0, 0))];
	      p.addr = addr;
	      p.passed = true;
	    }
	  int next = addr + get_attr_length (insn) / 2;
	  if (pending.length () >= TDM_POOL_MAX
	      || (!pending.is_empty ()
		  && tdm_pending_out_of_reach (b, pending, next)))
	    flush (insn, addr);
	  point = barrier = NULL;

	  bool counted = false;
	  for (; ui < uses.length () && uses[ui].insn == insn; ui++)
	    {
	      tdm_pool_use &u = uses[ui];
	      if (!counted)
		{
		  lits.truncate (0);
		  tdm_count_insn (insn, &lits);
		  counted = true;
		}
	      if (u.ent >= 0)
		{
		  /* In a pool after a barrier: see flush.  */
		  if (u.index < (int) lits.length ())
		    u.addr = tdm_insn_word (insn) + shift + lits[u.index].word;
		  continue;
		}
	      /* A branch that the pools above brought in reach of its
		 target has no literal any more.  */
	      if (u.index >= (int) lits.length ())
		continue;
	      u.addr = tdm_insn_word (insn) + shift + lits[u.index].word;
	      shift -= u.save;
	      pooled++;

	      int ent = -1;
	      for (int e = b.lookup (u.text); e >= 0 && ent < 0;
		   e = b.m_ents[e].prev)
		{
		  const tdm_pool_ent &x = b.m_ents[e];
		  if (x.pool < 0)
		    ent = e;
		  else
		    {
		      const tdm_pool &p = pools[x.pool];
		      int off = (p.passed ? p.addr
				 : tdm_insn_word (p.insn) + shift) + p.skip + x.pos
				- u.addr;
		      if (p.seg == seg
			  && off >= -32 + TDM_POOL_MARGIN
			  && off <= 31 - TDM_POOL_MARGIN)
			ent = e;
		    }
		}
	      if (ent >= 0)
		b.join (&u, ent);
	      else if (forced >= 0
		       && pools[forced].size < TDM_POOL_MAX
		       && (pools[forced].addr + pools[forced].skip
			   + pools[forced].size - u.addr
			   >= -32 + TDM_POOL_MARGIN))
		{
		  b.new_entry (&u, forced);
		  shift++;
		}
	      else
		pending.safe_push (b.new_entry (&u, -1));
	    }
	}
      if (!pending.is_empty ())
	{
	  rtx_insn *last = get_last_insn ();
	  while (NOTE_P (last) && PREV_INSN (last))
	    last = PREV_INSN (last);
	  int id = b.add_pool (last, true, !BARRIER_P (last), 0, seg);
	  for (int e : pending)
	    b.place (e, id);
	}
    }

  /* Lay out the entries pool by pool.  */
  tdm_pool_start.safe_push (0);
  for (const tdm_pool &p : pools)
    tdm_pool_start.safe_push (tdm_pool_start.last () + p.size);
  tdm_pool_entries.safe_grow_cleared (tdm_pool_start.last ());
  for (const tdm_pool_ent &x : b.m_ents)
    {
      tdm_pool_entry &e = tdm_pool_entries[tdm_pool_start[x.pool] + x.pos];
      snprintf (e.text, sizeof e.text, "%s", x.text);
      e.label = x.label;
      /* An entry whose uses moved to another pool.  */
      e.dead = x.nuses == 0;
    }

  /* Check the distances with the pools in place, and put the uses out
     of reach back inline, until all are in reach.  Each round only
     drops uses, and entries without any.  Then let the inline uses take
     a live entry with their value in reach: that only makes the code
     shorter, which leaves the others in reach, but check again.  */
  hash_set<tdm_lit_key_hash> tried;
  for (bool check = pooled > 0; check; )
    {
      for (bool changed = true; changed; )
	{
	  changed = false;
	  shorten_branches (get_insns ());
	  for (tdm_pool_use &u : uses)
	    {
	      if (u.ent < 0)
		continue;
	      tdm_pool_ent &x = b.m_ents[u.ent];
	      int flat = tdm_pool_start[x.pool] + x.pos;
	      if (tdm_pool_reach (u, flat, pools[x.pool]) == 0)
		continue;
	      if (--x.nuses == 0)
		tdm_pool_entries[flat].dead = true;
	      tdm_lit_labels->remove (std::make_pair (INSN_UID (u.insn),
						      u.index));
	      u.ent = -1;
	      pooled--;
	      reverted++;
	      changed = true;
	    }
	}

      /* Every inline literal: one that went back inline, or a branch
	 that became far.  */
      check = false;
      seg = 0;
      for (rtx_insn *insn = get_insns (); insn; insn = NEXT_INSN (insn))
	{
	  if (NOTE_P (insn)
	      && NOTE_KIND (insn) == NOTE_INSN_SWITCH_TEXT_SECTIONS)
	    seg++;
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  if (recog_memoized (insn) < 0)
	    {
	      if (asm_noperands (PATTERN (insn)) >= 0
		  || GET_CODE (PATTERN (insn)) == ASM_INPUT)
		seg++;
	      continue;
	    }
	  if (INSN_CODE (insn) == CODE_FOR_tdm_pool)
	    continue;
	  lits.truncate (0);
	  tdm_count_insn (insn, &lits);
	  for (unsigned int i = 0; i < lits.length (); i++)
	    {
	      auto key = std::make_pair (INSN_UID (insn), (int) i);
	      if (tdm_lit_labels->get (key) || tried.add (key))
		continue;
	      tdm_pool_use u = { insn, (int) i, 0, seg, "", -1,
				 tdm_pool_saving (lits[i].kind) };
	      snprintf (u.text, sizeof u.text, "%s", lits[i].text);
	      for (int e = b.lookup (u.text); e >= 0; e = b.m_ents[e].prev)
		{
		  tdm_pool_ent &x = b.m_ents[e];
		  int flat = tdm_pool_start[x.pool] + x.pos;
		  if (tdm_pool_entries[flat].dead
		      || pools[x.pool].seg != seg
		      || tdm_pool_reach (u, flat, pools[x.pool]) != 0)
		    continue;
		  b.join (&u, e);
		  uses.safe_push (u);
		  pooled++;
		  rejoined++;
		  check = true;
		  break;
		}
	    }
	}
    }

  if (dump_file)
    {
      int live = 0;
      for (const tdm_pool_entry &e : tdm_pool_entries)
	live += !e.dead;
      for (const tdm_pool_use &u : uses)
	if (u.ent >= 0)
	  fprintf (dump_file, ";; pooled: insn %d literal %d in pool %d: %s\n",
		   INSN_UID (u.insn), u.index, b.m_ents[u.ent].pool, u.text);
      fprintf (dump_file, ";; pools %d (%d after barriers), entries %d, "
	       "pooled %d, back inline %d, rejoined %d, inline %d\n",
	       (int) pools.length (), barrier_pools, live, pooled, reverted,
	       rejoined, (int) uses.length () - pooled);
    }
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
   any of these, that calls no byte access helper either, has no frame:
   its return address is at [$sp].

   Out of line saves: the callee-saved registers $mr19 to $mr18+N (N
   at least tdm_ool_restore_min, together with any unused ones in
   between) form a block that libgcc's __tdm_save_N stores at [$sp+1] to [$sp+N] and
   __tdm_restore_N loads from there.  In a function without outgoing
   arguments the block comes right after the free word, and the
   prologue calls __tdm_save_N after allocating the frame.  Otherwise
   it is at the top of the frame, below the return address: the
   prologue allocates N+1 words, calls __tdm_save_N and allocates the
   rest.  Other saved registers ($mr6) are saved inline, after the
   outgoing arguments (or the block).

   The epilogue frees the frame down to where $sp was at the call,
   stores what is left to free in [$sp] and jumps to __tdm_restore_N,
   which adds it to $sp and returns.  It changes $r1, so a function
   whose return value is in $r1 restores inline, as does a sibling
   call epilogue.

   A save with fewer than tdm_ool_save_min registers stores the block
   inline.  */

struct tdm_frame_info
{
  HOST_WIDE_INT outgoing;
  HOST_WIDE_INT saved;		/* Registers saved one by one.  */
  HOST_WIDE_INT locals;
  HOST_WIDE_INT total;
  int ool_n;			/* Size of the block in words, or 0.  */
  bool ool_save;		/* Whether the prologue calls __tdm_save_N.  */
  bool bottom;			/* Whether the block follows the free word.  */
};

/* The least N for an out of line save, and for a restore: a call to
   __tdm_save_N costs 4 words (with the literal) against 2N inline, and
   the jump to __tdm_restore_N 4 against 2N + 1.  N = 2 only ties for the
   save: measured, saving inline then is smaller than calling.  */

static const int tdm_ool_save_min = 3, tdm_ool_restore_min = 2;

static bool
tdm_save_reg_p (unsigned int regno)
{
  if (regno == HARD_FRAME_POINTER_REGNUM && frame_pointer_needed)
    return true;
  return (TDM_MR_P (regno) && df_regs_ever_live_p (regno)
	  && !call_used_or_fixed_reg_p (regno));
}

#define TDM_OOL_FIRST (MR0_REGNUM + 19)

/* Whether REGNO is saved in the block of F.  */

static bool
tdm_ool_reg_p (const tdm_frame_info *f, unsigned int regno)
{
  return regno >= TDM_OOL_FIRST && regno < TDM_OOL_FIRST + f->ool_n;
}

static void
tdm_compute_frame (tdm_frame_info *f)
{
  f->outgoing = ROUND_UP (crtl->outgoing_args_size, 2);
  f->locals = ROUND_UP (get_frame_size (), 2);
  f->ool_n = 0;
  for (int n = MR31_REGNUM - TDM_OOL_FIRST + 1; n > 0; n--)
    if (tdm_save_reg_p (TDM_OOL_FIRST + n - 1))
      {
	if (n >= MIN (tdm_ool_save_min, tdm_ool_restore_min))
	  f->ool_n = n;
	break;
      }
  f->ool_save = f->ool_n >= tdm_ool_save_min;
  f->bottom = f->ool_n && !f->outgoing;
  f->saved = 0;
  for (unsigned int regno = MR0_REGNUM; regno <= MR31_REGNUM; regno++)
    if (tdm_save_reg_p (regno) && !tdm_ool_reg_p (f, regno))
      f->saved += 2;
  if (f->outgoing || f->saved || f->locals || f->ool_n || !crtl->is_leaf
      || tdm_may_call_byte_helper ())
    f->total = 2 + f->outgoing + f->saved + f->locals + 2 * f->ool_n;
  else
    f->total = 0;
}

/* Where $sp is, in octets above its final value, when __tdm_save_N
   and __tdm_restore_N run: register $mr18+K of the block is at
   [$sp+K] there.  */

static HOST_WIDE_INT
tdm_ool_base (const tdm_frame_info *f)
{
  return f->bottom ? 0 : f->total - 2 * (f->ool_n + 1);
}

/* The offset of the first register saved one by one.  */

static HOST_WIDE_INT
tdm_inline_saves (const tdm_frame_info *f)
{
  return 2 + (f->bottom ? 2 * f->ool_n : f->outgoing);
}

/* Whether the function has a frame, whose free word at [$sp] a call can
   use for its return address.  */

static bool
tdm_frame_free_word_p (void)
{
  tdm_frame_info f;

  tdm_compute_frame (&f);
  return f.total != 0;
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
  return f.total ? tdm_inline_saves (&f) + f.saved : 0;
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

/* The libgcc routine __tdm_KIND_N.  */

static rtx
tdm_ool_symbol (const char *kind, int n)
{
  char buf[32];

  snprintf (buf, sizeof buf, "__tdm_%s_%d", kind, n);
  rtx sym = gen_rtx_SYMBOL_REF (Pmode, ggc_strdup (buf));
  SYMBOL_REF_FLAGS (sym) |= SYMBOL_FLAG_FUNCTION;
  return sym;
}

/* Save REGNO in the slot OFF octets above $sp.  */

static void
tdm_emit_save (unsigned int regno, HOST_WIDE_INT off)
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
}

/* Restore REGNO from the slot OFF octets above $sp.  */

static void
tdm_emit_restore (unsigned int regno, HOST_WIDE_INT off)
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
}

void
tdm_expand_prologue (void)
{
  tdm_frame_info f;
  HOST_WIDE_INT off;

  tdm_compute_frame (&f);
  if (flag_stack_usage_info)
    current_function_static_stack_size = f.total;

  HOST_WIDE_INT base = tdm_ool_base (&f);
  if (f.ool_save)
    {
      int n = f.ool_n;
      rtvec v = rtvec_alloc (2 + n);

      tdm_emit_sp_adjust (-(f.total - base));
      RTVEC_ELT (v, 0)
	= gen_rtx_UNSPEC_VOLATILE (VOIDmode,
				   gen_rtvec (1, tdm_ool_symbol ("save", n)),
				   UNSPECV_SAVE);
      RTVEC_ELT (v, 1) = gen_rtx_CLOBBER (VOIDmode,
					  gen_rtx_REG (HImode, R1_REGNUM));
      for (int k = 1; k <= n; k++)
	RTVEC_ELT (v, 1 + k)
	  = gen_rtx_USE (VOIDmode, gen_rtx_REG (HImode, TDM_OOL_FIRST + k - 1));
      rtx_insn *insn = emit_insn (gen_rtx_PARALLEL (VOIDmode, v));
      RTX_FRAME_RELATED_P (insn) = 1;
      for (int k = 1; k <= n; k++)
	add_reg_note (insn, REG_CFA_OFFSET,
		      gen_rtx_SET (tdm_save_slot (2 * k, true),
				   gen_rtx_REG (HImode,
						TDM_OOL_FIRST + k - 1)));
      if (base)
	tdm_emit_sp_adjust (-base);
    }
  else
    {
      if (f.total)
	tdm_emit_sp_adjust (-f.total);
      for (int k = 1; k <= f.ool_n; k++)
	tdm_emit_save (TDM_OOL_FIRST + k - 1, base + 2 * k);
    }

  off = tdm_inline_saves (&f);
  for (unsigned int regno = MR0_REGNUM; regno <= MR31_REGNUM; regno++)
    if (tdm_save_reg_p (regno) && !tdm_ool_reg_p (&f, regno))
      {
	tdm_emit_save (regno, off);
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

/* Whether the function returns a value in $r1, which __tdm_restore_N
   changes.  */

static bool
tdm_return_in_r1_p (void)
{
  rtx r = crtl->return_rtx;

  if (!r)
    return false;
  return !(REG_P (r) && REGNO (r) == R0_REGNUM
	   && GET_MODE_SIZE (GET_MODE (r)) <= UNITS_PER_WORD);
}

/* Restore the registers and $sp, then return.  For a sibling call
   (SIBCALL), stop before the return: $sp is back where it was on entry,
   with the return address in [$sp], and the stack arguments of the
   callee, which the caller stored over its own, follow it, as at a
   call.  The callee returns to the caller's caller.  */

void
tdm_expand_epilogue (bool sibcall)
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
  off = tdm_inline_saves (&f);
  for (unsigned int regno = MR0_REGNUM; regno <= MR31_REGNUM; regno++)
    if (tdm_save_reg_p (regno) && !tdm_ool_reg_p (&f, regno))
      {
	tdm_emit_restore (regno, off);
	off += 2;
      }

  HOST_WIDE_INT base = tdm_ool_base (&f);
  if (!sibcall && f.ool_n >= tdm_ool_restore_min && !tdm_return_in_r1_p ())
    {
      int n = f.ool_n;

      emit_insn (gen_blockage ());
      if (base)
	tdm_emit_sp_adjust (base);
      rtvec v = rtvec_alloc (3);
      RTVEC_ELT (v, 0) = ret_rtx;
      RTVEC_ELT (v, 1)
	= gen_rtx_UNSPEC_VOLATILE (VOIDmode,
				   gen_rtvec (2, tdm_ool_symbol ("restore", n),
					      GEN_INT ((f.total - base) / 2)),
				   UNSPECV_RESTORE);
      RTVEC_ELT (v, 2) = gen_rtx_CLOBBER (VOIDmode,
					  gen_rtx_REG (HImode, R1_REGNUM));
      rtx_insn *jump = emit_jump_insn (gen_rtx_PARALLEL (VOIDmode, v));
      JUMP_LABEL (jump) = ret_rtx;
      return;
    }
  for (int k = 1; k <= f.ool_n; k++)
    tdm_emit_restore (TDM_OOL_FIRST + k - 1, base + 2 * k);
  if (f.total)
    {
      emit_insn (gen_blockage ());
      tdm_emit_sp_adjust (f.total);
    }
  if (!sibcall)
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
   offset has to be even, except for an octet: an odd offset is the high
   half of the word (see tdm_classify), which keeps the parity of $r0
   known where that of the octet's address is.  */

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
  if ((off & 1) != 0 && size != 1)
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

/* By default a vector is aligned to its size, which can be more than
   the stack gives (STACK_BOUNDARY) and than GCC assumes of memory with
   no alignment of its own (BIGGEST_ALIGNMENT).  There's no vector unit
   to want more.  */

static HOST_WIDE_INT
tdm_vector_alignment (const_tree type)
{
  return MIN (default_vector_alignment (type), BIGGEST_ALIGNMENT);
}

/* Block operations.  Whether a copy, clear or fill of SIZE octets at
   ALIGN bits is done inline, piece by piece, or by a call to memcpy or
   memset (no cpymem/setmem patterns).  Speed doesn't matter: smaller
   wins.

   A call costs about 9 words.  A word piece costs 2 words when one
   side is the stack or a constant address, which is common (arguments
   by value, locals), more when both go through $r0; a store of a word
   about 1.  An octet piece of unknown half needs a call to a byte
   helper or the inline run-time test (see tdm_r0_even_p): moving one
   is two of those, at least as much as the call to memcpy.  GCC's
   defaults (MOVE_RATIO etc.: 15 pieces, 3 at -Os) count octet pieces
   like word pieces.  Limits chosen by measuring the execute tests at
   -O2 and -Os, libc and linked programs (2026-11-09): fewer than 8 word
   moves inline makes calls with structs by value bigger, more makes
   libc bigger (copies through two pointers).  */

static bool
tdm_use_by_pieces_infrastructure_p (unsigned HOST_WIDE_INT size,
				    unsigned int align,
				    enum by_pieces_operation op, bool speed_p)
{
  if (op == COMPARE_BY_PIECES)
    return default_use_by_pieces_infrastructure_p (size, align, op, speed_p);

  unsigned HOST_WIDE_INT words = 0, octets = size;
  if (align >= BITS_PER_WORD)
    {
      words = size / UNITS_PER_WORD;
      octets = size % UNITS_PER_WORD;
    }
  if (op == MOVE_BY_PIECES)
    return octets == 0 && words <= 8;
  /* CLEAR_BY_PIECES, SET_BY_PIECES, STORE_BY_PIECES.  */
  return octets <= 1 && words + 7 * octets <= 12;
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
#undef  TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG tdm_reorg
#undef  TARGET_ASM_FINAL_POSTSCAN_INSN
#define TARGET_ASM_FINAL_POSTSCAN_INSN tdm_final_postscan_insn
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
/* The named arguments of a variadic function are passed as in any other
   call, the first in $r0 if it fits; only the unnamed ones always go on
   the stack.  So va_start has no register to save.  */
#undef  TARGET_STRICT_ARGUMENT_NAMING
#define TARGET_STRICT_ARGUMENT_NAMING hook_bool_CUMULATIVE_ARGS_true
#undef  TARGET_FUNCTION_VALUE
#define TARGET_FUNCTION_VALUE tdm_function_value
#undef  TARGET_LIBCALL_VALUE
#define TARGET_LIBCALL_VALUE tdm_libcall_value
#undef  TARGET_FUNCTION_VALUE_REGNO_P
#define TARGET_FUNCTION_VALUE_REGNO_P tdm_function_value_regno_p
#undef  TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY tdm_return_in_memory
/* A sibling call leaves the return address where the caller found it,
   in [$sp] (see tdm_expand_epilogue): any call can be one.  */
#undef  TARGET_FUNCTION_OK_FOR_SIBCALL
#define TARGET_FUNCTION_OK_FOR_SIBCALL hook_bool_tree_tree_true

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
#undef  TARGET_VECTOR_ALIGNMENT
#define TARGET_VECTOR_ALIGNMENT tdm_vector_alignment

#undef  TARGET_USE_BY_PIECES_INFRASTRUCTURE_P
#define TARGET_USE_BY_PIECES_INFRASTRUCTURE_P \
  tdm_use_by_pieces_infrastructure_p

#undef  TARGET_TRAMPOLINE_INIT
#define TARGET_TRAMPOLINE_INIT tdm_trampoline_init
#undef  TARGET_TRAMPOLINE_ADJUST_ADDRESS
#define TARGET_TRAMPOLINE_ADJUST_ADDRESS tdm_trampoline_adjust_address

struct gcc_target targetm = TARGET_INITIALIZER;
