/* Prototypes for TDM (The Disintegrated Machine).
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
extern HOST_WIDE_INT tdm_initial_elimination_offset (int, int);
extern void tdm_expand_prologue (void);
extern void tdm_expand_epilogue (void);
extern void tdm_expand_cbranch (rtx *);
extern bool tdm_q_memory_p (rtx);
extern bool tdm_operand_pair_ok_p (rtx, rtx);
extern void tdm_fix_operand_pairs (rtx *, int);
extern int tdm_insn_length (rtx_insn *);
extern const char *tdm_output_move (rtx *, machine_mode);
extern const char *tdm_output_binop (const char *, rtx *);
extern const char *tdm_output_unop (enum rtx_code, rtx *);
extern const char *tdm_output_shift (enum rtx_code, rtx *);
extern const char *tdm_output_extend (enum rtx_code, rtx *);
extern const char *tdm_output_cbranch (rtx *, rtx_insn *);
extern const char *tdm_output_jump (rtx, rtx_insn *);
extern const char *tdm_output_indirect_jump (rtx);
extern const char *tdm_output_call (rtx);
extern void tdm_output_skip (FILE *, unsigned HOST_WIDE_INT);
extern void tdm_output_ascii (FILE *, const char *, int);
