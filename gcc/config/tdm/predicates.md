;; Predicate definitions for TDM.
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

;; The conditions of the predicate instructions, with the register
;; operand first.  There are no unsigned <= and >.
(define_predicate "tdm_cbranch_operator"
  (match_code "eq,ne,ltu,geu,lt,ge,le,gt"))

(define_predicate "call_address_operand"
  (ior (match_code "symbol_ref")
       (match_operand 0 "register_operand")))

;; The PARALLELs of tdm_save_call and tdm_restore_return, built by
;; tdm_expand_prologue and tdm_expand_epilogue.
(define_special_predicate "tdm_save_operation"
  (match_code "parallel"))

(define_special_predicate "tdm_restore_operation"
  (match_code "parallel"))
