/* Target definitions for TDM (The Disintegrated Machine).
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

#ifndef GCC_TDM_H
#define GCC_TDM_H

/* TDM is a 16-bit machine that addresses 16-bit words.  C uses 8-bit
   chars: data pointers are byte addresses, twice the word address plus
   one for the high octet of the word, so C data lives in the first 32K
   words.  Code addresses (function pointers, labels) are word
   addresses.  The stack pointer holds a word address; the compiler
   treats it as a byte address and converts where needed.  */

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC "crt0%O%s"
#undef  ENDFILE_SPEC
#define ENDFILE_SPEC ""
#undef  LIB_SPEC
#define LIB_SPEC "%{!shared:%{!symbolic:-lc}}"
#undef  LINK_SPEC
#define LINK_SPEC ""

/* Run-time target specification.  */

#define TARGET_CPU_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define_std ("tdm");		\
      builtin_define_std ("TDM");		\
      if (TARGET_USER_MODE)			\
	builtin_define ("__TDM_USER_MODE__");	\
    }						\
  while (0)

/* Storage layout.  */

#define BITS_BIG_ENDIAN 0
#define BYTES_BIG_ENDIAN 0
#define WORDS_BIG_ENDIAN 0

#define UNITS_PER_WORD 2
#define POINTER_SIZE 16
#define Pmode HImode
#define FUNCTION_MODE HImode
#define CASE_VECTOR_MODE HImode

#define PARM_BOUNDARY 16
#define STACK_BOUNDARY 16
#define FUNCTION_BOUNDARY 16
#define BIGGEST_ALIGNMENT 16
#define EMPTY_FIELD_BOUNDARY 16
#define STRUCTURE_SIZE_BOUNDARY 8
#define PCC_BITFIELD_TYPE_MATTERS 1
#define STRICT_ALIGNMENT 1
#define SLOW_BYTE_ACCESS 1
#define MOVE_MAX 2
#define MAX_REGS_PER_ADDRESS 1
#define LOAD_EXTEND_OP(MEM) ZERO_EXTEND

/* Objects, including char objects, are word aligned (an ABI rule).  */
#define DATA_ABI_ALIGNMENT(TYPE, ALIGN) ((ALIGN) < 16 ? 16 : (ALIGN))
#define LOCAL_ALIGNMENT(TYPE, ALIGN) ((ALIGN) < 16 ? 16 : (ALIGN))

/* Layout of source language data types.  */

#define SHORT_TYPE_SIZE 16
#define INT_TYPE_SIZE 16
#define LONG_TYPE_SIZE 32
#define LONG_LONG_TYPE_SIZE 64
#define DEFAULT_SIGNED_CHAR 0

#undef  SIZE_TYPE
#define SIZE_TYPE "unsigned int"
#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"
#undef  WCHAR_TYPE
#define WCHAR_TYPE "unsigned int"
#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 16

/* Registers.

   $r0 and $r1 are the real registers; only $r0 can be the base of an
   address.  $mr0..$mr31 are the memory words 0 to 31, which every
   instruction can reach through its immediate field, used as
   registers:
     $mr0-$mr2	 hardware (reset/interrupt vector, SWAPI scratch); free
		 with -muser-mode
     $mr3-$mr5	 constants 0x00ff, 0xff00 and 0x7fff, set up by crt0
     $mr6-$mr18	 caller saved
     $mr19-$mr31 callee saved
   The word below the stack pointer, [$sp-1], is a scratch location
   for the instruction sequences the compiler emits.  */

#define R0_REGNUM	0
#define R1_REGNUM	1
#define MR0_REGNUM	2
#define MR31_REGNUM	33
#define SP_REGNUM	34
#define PC_REGNUM	35
#define AP_REGNUM	36
#define FP_REGNUM	37
#define FIRST_PSEUDO_REGISTER 38

#define TDM_MR_P(REGNO) ((REGNO) >= MR0_REGNUM && (REGNO) <= MR31_REGNUM)

#define REGISTER_NAMES						\
{								\
  "$r0", "$r1",							\
  "$mr0", "$mr1", "$mr2", "$mr3", "$mr4", "$mr5", "$mr6", "$mr7",	\
  "$mr8", "$mr9", "$mr10", "$mr11", "$mr12", "$mr13", "$mr14", "$mr15",	\
  "$mr16", "$mr17", "$mr18", "$mr19", "$mr20", "$mr21", "$mr22", "$mr23", \
  "$mr24", "$mr25", "$mr26", "$mr27", "$mr28", "$mr29", "$mr30", "$mr31", \
  "$sp", "$pc", "?ap", "?fp"					\
}

#define FIXED_REGISTERS				\
{						\
  0, 0,						\
  1, 1, 1, 1, 1, 1, 0, 0,			\
  0, 0, 0, 0, 0, 0, 0, 0,			\
  0, 0, 0, 0, 0, 0, 0, 0,			\
  0, 0, 0, 0, 0, 0, 0, 0,			\
  1, 1, 1, 1					\
}

#define CALL_USED_REGISTERS			\
{						\
  1, 1,						\
  1, 1, 1, 1, 1, 1, 1, 1,			\
  1, 1, 1, 1, 1, 1, 1, 1,			\
  1, 1, 1, 0, 0, 0, 0, 0,			\
  0, 0, 0, 0, 0, 0, 0, 0,			\
  1, 1, 1, 1					\
}

#define REG_ALLOC_ORDER						\
{								\
  0, 1,								\
  8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,		\
  21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,		\
  2, 3, 4, 5, 6, 7, 34, 35, 36, 37				\
}

enum reg_class
{
  NO_REGS,
  R0_REGS,
  SP_REGS,
  R_REGS,
  MR_REGS,
  BASE_REGS,
  GENERAL_REGS,
  ALL_REGS,
  LIM_REG_CLASSES
};

#define N_REG_CLASSES LIM_REG_CLASSES

#define REG_CLASS_NAMES \
{ "NO_REGS", "R0_REGS", "SP_REGS", "R_REGS", "MR_REGS", "BASE_REGS",	\
  "GENERAL_REGS", "ALL_REGS" }

#define REG_CLASS_CONTENTS			\
{						\
  { 0x00000000, 0x00000000 },	/* NO_REGS */	\
  { 0x00000001, 0x00000000 },	/* R0_REGS */	\
  { 0x00000000, 0x00000004 },	/* SP_REGS */	\
  { 0x00000003, 0x00000000 },	/* R_REGS */	\
  { 0xfffffffc, 0x00000003 },	/* MR_REGS */	\
  { 0x00000001, 0x00000034 },	/* BASE_REGS: $r0, $sp, ?ap, ?fp */ \
  { 0xffffffff, 0x00000033 },	/* GENERAL_REGS: + ?ap, ?fp */ \
  { 0xffffffff, 0x0000003f }	/* ALL_REGS */	\
}

#define REGNO_REG_CLASS(REGNO)				\
  ((REGNO) == R0_REGNUM ? R0_REGS			\
   : (REGNO) == R1_REGNUM ? R_REGS			\
   : TDM_MR_P (REGNO) ? MR_REGS				\
   : (REGNO) == SP_REGNUM ? SP_REGS			\
   : (REGNO) == PC_REGNUM ? ALL_REGS : GENERAL_REGS)

#define BASE_REG_CLASS BASE_REGS
#define INDEX_REG_CLASS NO_REGS

#define TDM_BASE_REGNO_P(REGNO)					\
  ((REGNO) == R0_REGNUM || (REGNO) == SP_REGNUM			\
   || (REGNO) == AP_REGNUM || (REGNO) == FP_REGNUM)

#define REGNO_OK_FOR_BASE_P(REGNO)					\
  (TDM_BASE_REGNO_P (REGNO)						\
   || (reg_renumber && (REGNO) >= FIRST_PSEUDO_REGISTER			\
       && reg_renumber[REGNO] >= 0					\
       && TDM_BASE_REGNO_P ((unsigned) reg_renumber[REGNO])))

#define REGNO_OK_FOR_INDEX_P(REGNO) 0

/* Frame layout.

   The caller stores the address of the callee in the free word at
   [$sp] and swaps it with $pc, which leaves the return address there.
   Stack arguments follow it.  A function that needs a frame lowers $sp
   by its size; from $sp up the frame holds a free word (where calls
   store the return address), the outgoing arguments, the saved
   registers and the local variables.  */

#define STACK_GROWS_DOWNWARD 1
#define FRAME_GROWS_DOWNWARD 0
#define STACK_PUSH_CODE POST_DEC
#define STACK_POINTER_OFFSET 2
#define FIRST_PARM_OFFSET(FNDECL) 2
#define ACCUMULATE_OUTGOING_ARGS 1

#define STACK_POINTER_REGNUM SP_REGNUM
#define FRAME_POINTER_REGNUM FP_REGNUM
#define HARD_FRAME_POINTER_REGNUM FP_REGNUM
#define ARG_POINTER_REGNUM AP_REGNUM
#define STATIC_CHAIN_REGNUM (MR0_REGNUM + 18)

#define ELIMINABLE_REGS				\
{ { ARG_POINTER_REGNUM, STACK_POINTER_REGNUM },	\
  { FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM } }

#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET) \
  ((OFFSET) = tdm_initial_elimination_offset ((FROM), (TO)))

/* Arguments: the first one in $r0 if it fits in a word, the others on
   the stack.  Results of up to 32 bits come back in $r0 (and $r1).  */

#define CUMULATIVE_ARGS int
#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, FNDECL, N_NAMED_ARGS) \
  ((CUM) = 0)
#define FUNCTION_ARG_REGNO_P(REGNO) ((REGNO) == R0_REGNUM)

#define DEFAULT_PCC_STRUCT_RETURN 0

#define TRAMPOLINE_SIZE 8
#define TRAMPOLINE_ALIGNMENT 16

#define FUNCTION_PROFILER(FILE, LABELNO) \
  sorry ("profiling is not supported on TDM")

#define DWARF2_UNWIND_INFO 0

/* Debug information.  The DWARF numbers of $pc, $sp, $r0 and $r1 are
   GDB's (and the simulator's): 0 to 3.  $mrN is 4+N.  */

#define DEBUGGER_REGNO(REGNO)					\
  ((REGNO) == PC_REGNUM ? 0U						\
   : (REGNO) == SP_REGNUM ? 1U						\
   : (REGNO) <= R1_REGNUM ? (unsigned) (REGNO) + 2			\
   : TDM_MR_P (REGNO) ? (unsigned) ((REGNO) - MR0_REGNUM) + 4		\
   : INVALID_REGNUM)

/* Call frame information counts words, as GDB does: the CFA is the word
   address of the return address, the caller's $sp at the call, and the
   return address is the PC column.  The compiler's stack offsets count
   octets, so the prologue and the epilogue describe their effect with
   notes in words, and the data alignment factor is a single word.  */

#define INCOMING_RETURN_ADDR_RTX gen_rtx_MEM (Pmode, stack_pointer_rtx)
#define DWARF_FRAME_RETURN_COLUMN DWARF_FRAME_REGNUM (PC_REGNUM)
#define DWARF_CIE_DATA_ALIGNMENT (-1)

/* The CFA is where the argument pointer points, and the frame base is
   the CFA.  Location expressions count stack offsets in words too.  */
#define ARG_POINTER_CFA_OFFSET(FNDECL) 0
#define DWARF_STACK_UNIT_SIZE UNITS_PER_WORD

#define HAS_LONG_COND_BRANCH true
#define HAS_LONG_UNCOND_BRANCH true

/* Assembler output.  */

#define ASM_COMMENT_START ";"
#define ASM_APP_ON ""
#define ASM_APP_OFF ""

#define TEXT_SECTION_ASM_OP "\t.text"
#define DATA_SECTION_ASM_OP "\t.data"
#define GLOBAL_ASM_OP "\t.global\t"

/* The assembler counts directive arguments in 16-bit words; the
   compiler counts octets.  */

#undef  ASM_OUTPUT_ALIGN
#define ASM_OUTPUT_ALIGN(STREAM, POWER)				\
  do								\
    if ((POWER) > 0)						\
      fprintf ((STREAM), "\t.p2align\t%d\n", (int) (POWER) - 1);	\
  while (0)

#undef  ASM_OUTPUT_SKIP
#define ASM_OUTPUT_SKIP(STREAM, SIZE) tdm_output_skip ((STREAM), (SIZE))

#undef  ASM_OUTPUT_ASCII
#define ASM_OUTPUT_ASCII(STREAM, PTR, LEN) \
  tdm_output_ascii ((STREAM), (PTR), (LEN))
#undef  ASM_OUTPUT_LIMITED_STRING

#undef  ASM_OUTPUT_SIZE_DIRECTIVE
#define ASM_OUTPUT_SIZE_DIRECTIVE(STREAM, NAME, SIZE)			\
  do									\
    {									\
      HOST_WIDE_INT size_ = (SIZE);					\
      fputs (SIZE_ASM_OP, (STREAM));					\
      assemble_name ((STREAM), (NAME));					\
      fprintf ((STREAM), ", " HOST_WIDE_INT_PRINT_DEC "\n",		\
	       (size_ + 1) / 2);					\
    }									\
  while (0)

#define ASM_OUTPUT_MEASURED_SIZE(STREAM, NAME)		\
  do							\
    {							\
      fputs (SIZE_ASM_OP, (STREAM));			\
      assemble_name ((STREAM), (NAME));			\
      fputs (", .-", (STREAM));				\
      assemble_name ((STREAM), (NAME));			\
      putc ('\n', (STREAM));				\
    }							\
  while (0)

#undef  ASM_OUTPUT_ALIGNED_COMMON
#define ASM_OUTPUT_ALIGNED_COMMON(STREAM, NAME, SIZE, ALIGN)		\
  do									\
    {									\
      fputs (COMMON_ASM_OP, (STREAM));					\
      assemble_name ((STREAM), (NAME));					\
      fprintf ((STREAM), "," HOST_WIDE_INT_PRINT_UNSIGNED ",%u\n",	\
	       ((SIZE) + 1) / 2, (ALIGN) > 16 ? (ALIGN) / 16 : 1);	\
    }									\
  while (0)

#define ASM_OUTPUT_ADDR_VEC_ELT(STREAM, VALUE) \
  fprintf ((STREAM), "\t.word\t.L%d\n", (VALUE))

#endif /* GCC_TDM_H */
