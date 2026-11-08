/* Octet access through a byte pointer for TDM, out of line.
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

/* The compiler calls these for an octet whose half of the word it
   doesn't know (see tdm_emit_load_octet_dynamic in gcc/config/tdm/tdm.cc).
   They have a convention of their own: $r0 holds the byte address, and
   comes back unchanged, as does everything else but $r1; the return
   address is in [$sp], as for any call.

   A byte address is twice the word address, plus 1 for the high octet.
   C data lives in the first 32K words, so after "ror $r0" bit 15 holds
   the half and the rest the word address; $mr5 (0x7fff) clears bit 15,
   and "rol; add 1" puts $r0 back.  $mr3 is 0x00ff, $mr4 0xff00.  */

/* Load the octet at the byte address in $r0, zero extended, into $r1.  */

	.section	.text.__tdm_load8,"ax",@progbits
	.p2align	0
	.global	__tdm_load8
	.type	__tdm_load8, @function
__tdm_load8:
	ror	$r0
	if_ges	$r0, 0
	mov	$pc, $pc+(.Lload_low-.)
	; High octet.
	and	$r0, $mr5
	mov	$r1, [$r0]
	ror	$r1
	ror	$r1
	ror	$r1
	ror	$r1
	ror	$r1
	ror	$r1
	ror	$r1
	ror	$r1
	and	$r1, $mr3
	rol	$r0
	add	$r0, 1
	mov	$pc, [$sp]
.Lload_low:
	mov	$r1, [$r0]
	and	$r1, $mr3
	rol	$r0
	mov	$pc, [$sp]
	.size	__tdm_load8, .-__tdm_load8

/* Store the low octet of [$sp-1] at the byte address in $r0.  Changes
   $r1; leaves [$sp-1] as it was.  */

	.section	.text.__tdm_store8,"ax",@progbits
	.p2align	0
	.global	__tdm_store8
	.type	__tdm_store8, @function
__tdm_store8:
	mov	$r1, [$sp-1]
	ror	$r0
	if_ges	$r0, 0
	mov	$pc, $pc+(.Lstore_low-.)
	; High octet: mem ^= (value << 8 ^ mem) & 0xff00.
	and	$r0, $mr5
	rol	$r1
	rol	$r1
	rol	$r1
	rol	$r1
	rol	$r1
	rol	$r1
	rol	$r1
	rol	$r1
	xor	$r1, [$r0]
	and	$r1, $mr4
	xor	[$r0], $r1
	rol	$r0
	add	$r0, 1
	mov	$pc, [$sp]
.Lstore_low:
	; Low octet: mem ^= (value ^ mem) & 0x00ff.
	xor	$r1, [$r0]
	and	$r1, $mr3
	xor	[$r0], $r1
	rol	$r0
	mov	$pc, [$sp]
	.size	__tdm_store8, .-__tdm_store8
