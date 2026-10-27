; Minimal startup for run-on-sim.sh, until libgloss provides crt0.
; Sets up the stack and the constant registers $mr3-$mr5, calls main
; and passes its result to the simulator's EXIT port (0xffe0).

	.text
	.global	_start
_start:
	mov	$sp, [$pc+(.Lsp-.)]
	mov	$r0, [$pc+(.Lc3-.)]
	mov	$mr3, $r0
	mov	$r0, [$pc+(.Lc4-.)]
	mov	$mr4, $r0
	mov	$r0, [$pc+(.Lc5-.)]
	mov	$mr5, $r0
	mov	$r1, [$pc+(.Lmain-.)]
	mov	[$sp], $r1
	swap	$pc, [$sp]
	mov	[-32], $r0
	brk
.Lsp:	.word	0x3ff0
.Lc3:	.word	0x00ff
.Lc4:	.word	0xff00
.Lc5:	.word	0x7fff
.Lmain:	.word	main
