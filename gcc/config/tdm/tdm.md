;; Machine description for TDM (The Disintegrated Machine).
;; Copyright (C) 2026 Free Software Foundation, Inc.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published
;; by the Free Software Foundation; either version 3, or (at your
;; option) any later version.
;;
;; GCC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING3.  If not see
;; <http://www.gnu.org/licenses/>.

;; Every instruction combines a register ($r0, $r1, $sp or $pc) with
;; an immediate or a memory word, and can write either of them.  The
;; memory registers $mr0..$mr31 are memory words too.  To keep register
;; allocation simple, the patterns accept most operand combinations and
;; tdm.cc expands them into instruction sequences, using $r1 (saved in
;; the scratch word [$sp-1]) where an operation needs a register.

(include "constraints.md")
(include "predicates.md")

(define_constants
  [(R0_REGNUM	0)
   (R1_REGNUM	1)
   (HFP_REGNUM	8)	; $mr6
   (GOTO_LABEL_REGNUM 9)	; $mr7
   (GOTO_FP_REGNUM 10)	; $mr8
   (GOTO_SP_REGNUM 11)	; $mr9
   (SP_REGNUM	34)
   (PC_REGNUM	35)])

(define_c_enum "unspec"
  [UNSPEC_RAW_FP])

(define_c_enum "unspecv"
  [UNSPECV_BLOCKAGE
   UNSPECV_NONLOCAL_JUMP])

;; Lengths are in octets, two per instruction.  tdm_insn_length counts
;; the instructions the output functions emit.
(define_attr "length" "" (symbol_ref "tdm_insn_length (insn)"))

;; -------------------------------------------------------------------
;; Moves
;; -------------------------------------------------------------------

(define_expand "movhi"
  [(set (match_operand:HI 0 "nonimmediate_operand")
	(match_operand:HI 1 "general_operand"))]
  ""
{
  if (MEM_P (operands[0]) && can_create_pseudo_p ()
      && !register_operand (operands[1], HImode))
    operands[1] = force_reg (HImode, operands[1]);
})

;; The frame pointer $mr6 holds a word address, which moves double
;; and halve like those of $sp (see tdm_classify).  The prologue and
;; the epilogue copy it as it is.

(define_insn "tdm_sp_from_fp"
  [(set (reg:HI SP_REGNUM) (reg:HI HFP_REGNUM))]
  "frame_pointer_needed"
  "mov\t$sp, $mr6")

(define_insn "tdm_save_fp"
  [(set (match_operand:HI 0 "memory_operand" "=m")
	(unspec:HI [(reg:HI HFP_REGNUM)] UNSPEC_RAW_FP))]
  ""
  "* return tdm_output_raw_fp (operands[0], true);")

(define_insn "tdm_restore_fp"
  [(set (reg:HI HFP_REGNUM)
	(unspec:HI [(match_operand:HI 0 "memory_operand" "m")]
		   UNSPEC_RAW_FP))]
  ""
  "* return tdm_output_raw_fp (operands[0], false);")

(define_insn "*movhi"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=r,m,Q,r,k")
	(match_operand:HI 1 "general_operand" "rmi,rQi,m,k,q"))]
  "tdm_operand_pair_ok_p (operands[0], operands[1])"
  "* return tdm_output_move (operands, HImode);")

(define_expand "movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand")
	(match_operand:QI 1 "general_operand"))]
  ""
{
  if (MEM_P (operands[0]) && can_create_pseudo_p ()
      && !register_operand (operands[1], QImode))
    operands[1] = force_reg (QImode, operands[1]);
})

(define_insn "*movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=r,m")
	(match_operand:QI 1 "general_operand" "rmi,ri"))]
  "tdm_operand_pair_ok_p (operands[0], operands[1])"
  "* return tdm_output_move (operands, QImode);")

(define_insn "zero_extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(zero_extend:HI (match_operand:QI 1 "nonimmediate_operand" "r,m")))]
  ""
  "* return tdm_output_extend (ZERO_EXTEND, operands);")

(define_insn "extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(sign_extend:HI (match_operand:QI 1 "register_operand" "r")))]
  ""
  "* return tdm_output_extend (SIGN_EXTEND, operands);")

;; -------------------------------------------------------------------
;; Arithmetic and logic
;; -------------------------------------------------------------------

;; The insns don't take two memory operands addressed through pointers
;; (see tdm_operand_pair_ok_p); the expanders load one into a register.

(define_code_iterator arith_op [plus minus and ior xor])
(define_code_attr arith_name [(plus "add") (minus "sub") (and "and")
			      (ior "ior") (xor "xor")])

(define_expand "<arith_name>hi3"
  [(set (match_operand:HI 0 "nonimmediate_operand")
	(arith_op:HI (match_operand:HI 1 "nonimmediate_operand")
		     (match_operand:HI 2 "general_operand")))]
  ""
  "tdm_fix_operand_pairs (operands, 3);")

(define_code_iterator unary_op [neg not])
(define_code_attr unary_name [(neg "neg") (not "one_cmpl")])

(define_expand "<unary_name>hi2"
  [(set (match_operand:HI 0 "nonimmediate_operand")
	(unary_op:HI (match_operand:HI 1 "nonimmediate_operand")))]
  ""
  "tdm_fix_operand_pairs (operands, 2);")

(define_insn "*addhi3"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=r,m,Q,k,k")
	(plus:HI (match_operand:HI 1 "nonimmediate_operand" "%0,0,0,0,0")
		 (match_operand:HI 2 "general_operand" "rmi,rQi,m,n,q")))]
  "tdm_operand_pair_ok_p (operands[0], operands[1])
   && tdm_operand_pair_ok_p (operands[0], operands[2])
   && tdm_operand_pair_ok_p (operands[1], operands[2])"
  "* return tdm_output_binop (\"add\", operands);")

(define_insn "*subhi3"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=r,m,Q,k,k")
	(minus:HI (match_operand:HI 1 "nonimmediate_operand" "0,0,0,0,0")
		  (match_operand:HI 2 "general_operand" "rmi,rQi,m,n,q")))]
  "tdm_operand_pair_ok_p (operands[0], operands[1])
   && tdm_operand_pair_ok_p (operands[0], operands[2])
   && tdm_operand_pair_ok_p (operands[1], operands[2])"
  "* return tdm_output_binop (\"sub\", operands);")

(define_insn "*andhi3"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=r,m,Q")
	(and:HI (match_operand:HI 1 "nonimmediate_operand" "%0,0,0")
		(match_operand:HI 2 "general_operand" "rmi,rQi,m")))]
  "tdm_operand_pair_ok_p (operands[0], operands[1])
   && tdm_operand_pair_ok_p (operands[0], operands[2])
   && tdm_operand_pair_ok_p (operands[1], operands[2])"
  "* return tdm_output_binop (\"and\", operands);")

(define_insn "*iorhi3"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=r,m,Q")
	(ior:HI (match_operand:HI 1 "nonimmediate_operand" "%0,0,0")
		(match_operand:HI 2 "general_operand" "rmi,rQi,m")))]
  "tdm_operand_pair_ok_p (operands[0], operands[1])
   && tdm_operand_pair_ok_p (operands[0], operands[2])
   && tdm_operand_pair_ok_p (operands[1], operands[2])"
  "* return tdm_output_binop (\"or\", operands);")

(define_insn "*xorhi3"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=r,m,Q")
	(xor:HI (match_operand:HI 1 "nonimmediate_operand" "%0,0,0")
		(match_operand:HI 2 "general_operand" "rmi,rQi,m")))]
  "tdm_operand_pair_ok_p (operands[0], operands[1])
   && tdm_operand_pair_ok_p (operands[0], operands[2])
   && tdm_operand_pair_ok_p (operands[1], operands[2])"
  "* return tdm_output_binop (\"xor\", operands);")

(define_insn "*neghi2"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=rm")
	(neg:HI (match_operand:HI 1 "nonimmediate_operand" "0")))]
  "tdm_operand_pair_ok_p (operands[0], operands[1])"
  "* return tdm_output_unop (NEG, operands);")

(define_insn "*one_cmplhi2"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=rm")
	(not:HI (match_operand:HI 1 "nonimmediate_operand" "0")))]
  "tdm_operand_pair_ok_p (operands[0], operands[1])"
  "* return tdm_output_unop (NOT, operands);")

;; Shifts by a constant are rotates and a mask; other shifts are
;; library calls.

(define_code_iterator any_shift [ashift lshiftrt ashiftrt])
(define_code_attr shift_insn [(ashift "ashl") (lshiftrt "lshr")
			      (ashiftrt "ashr")])

(define_expand "<shift_insn>hi3"
  [(set (match_operand:HI 0 "nonimmediate_operand")
	(any_shift:HI (match_operand:HI 1 "nonimmediate_operand")
		      (match_operand:HI 2 "general_operand")))]
  ""
{
  if (!CONST_INT_P (operands[2]))
    FAIL;
  tdm_fix_operand_pairs (operands, 2);
})

(define_insn "*<shift_insn>hi3"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=rm")
	(any_shift:HI (match_operand:HI 1 "nonimmediate_operand" "0")
		      (match_operand:HI 2 "const_int_operand" "n")))]
  "tdm_operand_pair_ok_p (operands[0], operands[1])"
  "* return tdm_output_shift (<CODE>, operands);")

;; -------------------------------------------------------------------
;; Branches
;; -------------------------------------------------------------------

(define_expand "cbranchhi4"
  [(set (pc)
	(if_then_else (match_operator 0 "ordered_comparison_operator"
			[(match_operand:HI 1 "general_operand")
			 (match_operand:HI 2 "general_operand")])
		      (label_ref (match_operand 3))
		      (pc)))]
  ""
{
  tdm_expand_cbranch (operands);
  DONE;
})

;; A predicate instruction executes the next one only if its condition
;; holds.  Short branches reach about 30 words; long ones load the
;; target from a literal.
(define_insn "*cbranchhi4"
  [(set (pc)
	(if_then_else (match_operator 0 "tdm_cbranch_operator"
			[(match_operand:HI 1 "register_operand" "q")
			 (match_operand:HI 2 "general_operand" "rQI")])
		      (label_ref (match_operand 3))
		      (pc)))]
  ""
  "* return tdm_output_cbranch (operands, insn);"
  [(set (attr "length")
	(if_then_else (and (ge (minus (match_dup 3) (pc)) (const_int -56))
			   (le (minus (match_dup 3) (pc)) (const_int 56)))
		      (const_int 6)
		      (const_int 10)))])

(define_insn "jump"
  [(set (pc) (label_ref (match_operand 0)))]
  ""
  "* return tdm_output_jump (operands[0], insn);"
  [(set (attr "length")
	(if_then_else (and (ge (minus (match_dup 0) (pc)) (const_int -60))
			   (le (minus (match_dup 0) (pc)) (const_int 60)))
		      (const_int 2)
		      (const_int 4)))])

(define_insn "indirect_jump"
  [(set (pc) (match_operand:HI 0 "nonimmediate_operand" "rQ"))]
  ""
  "* return tdm_output_indirect_jump (operands[0]);")

;; -------------------------------------------------------------------
;; Calls
;; -------------------------------------------------------------------

;; A call stores the address of the callee in [$sp] and swaps it with
;; $pc.  It uses $r1, which calls clobber anyway.

(define_expand "call"
  [(call (match_operand:HI 0 "")
	 (match_operand 1 ""))]
  ""
{
  rtx addr = XEXP (operands[0], 0);
  if (!call_address_operand (addr, Pmode))
    operands[0] = gen_rtx_MEM (HImode, force_reg (Pmode, addr));
})

(define_insn "*call"
  [(call (mem:HI (match_operand:HI 0 "call_address_operand" "r,i"))
	 (match_operand 1 ""))]
  ""
  "* return tdm_output_call (operands[0]);")

(define_expand "call_value"
  [(set (match_operand 0 "")
	(call (match_operand:HI 1 "")
	      (match_operand 2 "")))]
  ""
{
  rtx addr = XEXP (operands[1], 0);
  if (!call_address_operand (addr, Pmode))
    operands[1] = gen_rtx_MEM (HImode, force_reg (Pmode, addr));
})

(define_insn "*call_value"
  [(set (match_operand 0 "register_operand" "=r,r")
	(call (mem:HI (match_operand:HI 1 "call_address_operand" "r,i"))
	      (match_operand 2 "")))]
  ""
  "* return tdm_output_call (operands[1]);")

;; -------------------------------------------------------------------
;; Prologue, epilogue and miscellaneous
;; -------------------------------------------------------------------

(define_expand "prologue"
  [(const_int 0)]
  ""
{
  tdm_expand_prologue ();
  DONE;
})

(define_expand "epilogue"
  [(return)]
  ""
{
  tdm_expand_epilogue ();
  DONE;
})

(define_insn "return_internal"
  [(return)]
  "reload_completed"
  "mov\t$pc, [$sp]"
  [(set_attr "length" "2")])

;; Nonlocal gotos and __builtin_longjmp.  The saved $sp and frame
;; pointer are byte addresses; halve them on the way.  Operand 0, the
;; static chain, isn't needed.

(define_expand "nonlocal_goto"
  [(match_operand 0 "general_operand")
   (match_operand 1 "general_operand")
   (match_operand 2 "general_operand")
   (match_operand 3 "general_operand")]
  ""
{
  tdm_expand_nonlocal_goto (operands);
  DONE;
})

(define_insn "tdm_nonlocal_jump"
  [(set (pc) (unspec_volatile [(reg:HI GOTO_LABEL_REGNUM)
			       (reg:HI GOTO_FP_REGNUM)
			       (reg:HI GOTO_SP_REGNUM)]
			      UNSPECV_NONLOCAL_JUMP))
   (clobber (reg:HI R0_REGNUM))
   (clobber (reg:HI HFP_REGNUM))
   (clobber (reg:HI SP_REGNUM))]
  ""
  "mov\t$r0, $mr8\;ror\t$r0\;mov\t$mr6, $r0\;mov\t$r0, $mr9\;ror\t$r0\;mov\t$sp, $r0\;mov\t$pc, $mr7")

(define_insn "blockage"
  [(unspec_volatile [(const_int 0)] UNSPECV_BLOCKAGE)]
  ""
  ""
  [(set_attr "length" "0")])

(define_insn "nop"
  [(const_int 0)]
  ""
  "or\t$r0, 0"
  [(set_attr "length" "2")])
