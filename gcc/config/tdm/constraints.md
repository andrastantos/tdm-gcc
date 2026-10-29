;; Constraint definitions for TDM.
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

;; "r" is $r0, $r1 and the memory registers $mr0..$mr31.

(define_register_constraint "q" "R_REGS"
  "The real registers, $r0 and $r1.")

(define_register_constraint "t" "R1_REGS"
  "$r1.")

(define_register_constraint "j" "NOR0_REGS"
  "Any register but $r0: for asm operands, see tdm_md_asm_adjust.")

(define_register_constraint "k" "SP_REGS"
  "The stack pointer.")

(define_constraint "I"
  "A signed 6-bit immediate."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, -32, 31)")))

(define_special_memory_constraint "Q"
  "A memory operand that is not addressed through $r0."
  (and (match_code "mem")
       (match_test "tdm_q_memory_p (op)")))
