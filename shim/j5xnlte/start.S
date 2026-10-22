/* lk2nd enters at EL1 (its arm64 boot goes through the TZ milestone call,
 * scm_elexec_call: el1_x0 = DTB, el1_elr = entry), MMU/D-cache off. x0 is
 * the boot image's appended DTB copy at DDR_START+0x1E00000, patched by
 * lk2nd (cmdline, kaslr-seed, linux,initrd-start/end). No console and no
 * vectors: an unexpected exception wedges, which is an observable distinct
 * from the shim's own reset-on-failure path.
 */
	.section .text.head, "ax"
	.globl _head
_head:
	b	_start			/* code0 */
	.long	0			/* code1 */
	.quad	0			/* text_offset */
	.quad	_end			/* image_size (linked at 0) */
	.quad	0xa			/* flags: LE, 4K pages, place anywhere */
	.quad	0			/* res2 */
	.quad	0			/* res3 */
	.quad	0			/* res4 */
	.ascii	"ARM\x64"		/* magic */
	.long	0			/* res5 (PE offset) */

	.text
	.globl _start
_start:
	msr	daifset, #0xf
	mov	x19, x0			/* DTB pointer from lk2nd */
	msr	spsel, #1
	adr	x1, _stack_top
	mov	sp, x1
	isb
	/* zero .bss */
	adr	x1, _bss_start
	adr	x2, _bss_end
1:	cmp	x1, x2
	b.hs	2f
	str	xzr, [x1], #8
	b	1b
2:	mov	x0, x19
	bl	shim_main
3:	wfe
	b	3b
