/* Out of line register saves and restores for TDM.
   Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

/* The prologue of a function that saves the callee-saved registers
   $mr19 to $mr18+N calls __tdm_save_N, which stores $mr18+K in
   [$sp+K]; it changes $r1 only.  The epilogue jumps to __tdm_restore_N
   with $sp where it was at that call and the number of words left to
   free in [$sp]: it loads $mr18+K from [$sp+K], frees the rest of the
   frame and returns for the function.  It changes $r1 only.  See
   tdm_expand_prologue in gcc/config/tdm/tdm.cc.  */

	.section	.text.__tdm_save,"ax",@progbits
	.p2align	0
	.global	__tdm_save_13
	.type	__tdm_save_13, @function
__tdm_save_13:
	mov	$r1, $mr31
	mov	[$sp+13], $r1
	.global	__tdm_save_12
	.type	__tdm_save_12, @function
__tdm_save_12:
	mov	$r1, $mr30
	mov	[$sp+12], $r1
	.global	__tdm_save_11
	.type	__tdm_save_11, @function
__tdm_save_11:
	mov	$r1, $mr29
	mov	[$sp+11], $r1
	.global	__tdm_save_10
	.type	__tdm_save_10, @function
__tdm_save_10:
	mov	$r1, $mr28
	mov	[$sp+10], $r1
	.global	__tdm_save_9
	.type	__tdm_save_9, @function
__tdm_save_9:
	mov	$r1, $mr27
	mov	[$sp+9], $r1
	.global	__tdm_save_8
	.type	__tdm_save_8, @function
__tdm_save_8:
	mov	$r1, $mr26
	mov	[$sp+8], $r1
	.global	__tdm_save_7
	.type	__tdm_save_7, @function
__tdm_save_7:
	mov	$r1, $mr25
	mov	[$sp+7], $r1
	.global	__tdm_save_6
	.type	__tdm_save_6, @function
__tdm_save_6:
	mov	$r1, $mr24
	mov	[$sp+6], $r1
	.global	__tdm_save_5
	.type	__tdm_save_5, @function
__tdm_save_5:
	mov	$r1, $mr23
	mov	[$sp+5], $r1
	.global	__tdm_save_4
	.type	__tdm_save_4, @function
__tdm_save_4:
	mov	$r1, $mr22
	mov	[$sp+4], $r1
	.global	__tdm_save_3
	.type	__tdm_save_3, @function
__tdm_save_3:
	mov	$r1, $mr21
	mov	[$sp+3], $r1
	.global	__tdm_save_2
	.type	__tdm_save_2, @function
__tdm_save_2:
	mov	$r1, $mr20
	mov	[$sp+2], $r1
	.global	__tdm_save_1
	.type	__tdm_save_1, @function
__tdm_save_1:
	mov	$r1, $mr19
	mov	[$sp+1], $r1
	mov	$pc, [$sp]
	.size	__tdm_save_13, .-__tdm_save_13
	.size	__tdm_save_12, .-__tdm_save_12
	.size	__tdm_save_11, .-__tdm_save_11
	.size	__tdm_save_10, .-__tdm_save_10
	.size	__tdm_save_9, .-__tdm_save_9
	.size	__tdm_save_8, .-__tdm_save_8
	.size	__tdm_save_7, .-__tdm_save_7
	.size	__tdm_save_6, .-__tdm_save_6
	.size	__tdm_save_5, .-__tdm_save_5
	.size	__tdm_save_4, .-__tdm_save_4
	.size	__tdm_save_3, .-__tdm_save_3
	.size	__tdm_save_2, .-__tdm_save_2
	.size	__tdm_save_1, .-__tdm_save_1

	.section	.text.__tdm_restore,"ax",@progbits
	.p2align	0
	.global	__tdm_restore_13
	.type	__tdm_restore_13, @function
__tdm_restore_13:
	mov	$r1, [$sp+13]
	mov	$mr31, $r1
	.global	__tdm_restore_12
	.type	__tdm_restore_12, @function
__tdm_restore_12:
	mov	$r1, [$sp+12]
	mov	$mr30, $r1
	.global	__tdm_restore_11
	.type	__tdm_restore_11, @function
__tdm_restore_11:
	mov	$r1, [$sp+11]
	mov	$mr29, $r1
	.global	__tdm_restore_10
	.type	__tdm_restore_10, @function
__tdm_restore_10:
	mov	$r1, [$sp+10]
	mov	$mr28, $r1
	.global	__tdm_restore_9
	.type	__tdm_restore_9, @function
__tdm_restore_9:
	mov	$r1, [$sp+9]
	mov	$mr27, $r1
	.global	__tdm_restore_8
	.type	__tdm_restore_8, @function
__tdm_restore_8:
	mov	$r1, [$sp+8]
	mov	$mr26, $r1
	.global	__tdm_restore_7
	.type	__tdm_restore_7, @function
__tdm_restore_7:
	mov	$r1, [$sp+7]
	mov	$mr25, $r1
	.global	__tdm_restore_6
	.type	__tdm_restore_6, @function
__tdm_restore_6:
	mov	$r1, [$sp+6]
	mov	$mr24, $r1
	.global	__tdm_restore_5
	.type	__tdm_restore_5, @function
__tdm_restore_5:
	mov	$r1, [$sp+5]
	mov	$mr23, $r1
	.global	__tdm_restore_4
	.type	__tdm_restore_4, @function
__tdm_restore_4:
	mov	$r1, [$sp+4]
	mov	$mr22, $r1
	.global	__tdm_restore_3
	.type	__tdm_restore_3, @function
__tdm_restore_3:
	mov	$r1, [$sp+3]
	mov	$mr21, $r1
	.global	__tdm_restore_2
	.type	__tdm_restore_2, @function
__tdm_restore_2:
	mov	$r1, [$sp+2]
	mov	$mr20, $r1
	.global	__tdm_restore_1
	.type	__tdm_restore_1, @function
__tdm_restore_1:
	mov	$r1, [$sp+1]
	mov	$mr19, $r1
	add	$sp, [$sp]
	mov	$pc, [$sp]
	.size	__tdm_restore_13, .-__tdm_restore_13
	.size	__tdm_restore_12, .-__tdm_restore_12
	.size	__tdm_restore_11, .-__tdm_restore_11
	.size	__tdm_restore_10, .-__tdm_restore_10
	.size	__tdm_restore_9, .-__tdm_restore_9
	.size	__tdm_restore_8, .-__tdm_restore_8
	.size	__tdm_restore_7, .-__tdm_restore_7
	.size	__tdm_restore_6, .-__tdm_restore_6
	.size	__tdm_restore_5, .-__tdm_restore_5
	.size	__tdm_restore_4, .-__tdm_restore_4
	.size	__tdm_restore_3, .-__tdm_restore_3
	.size	__tdm_restore_2, .-__tdm_restore_2
	.size	__tdm_restore_1, .-__tdm_restore_1
