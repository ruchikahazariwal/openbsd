/*
 * Copyright (c) 2015 Dale Rahn <drahn@dalerahn.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "machine/asm.h"
#include "assym.h"

//from freebsd/riscv/swtch.S
/*
 * void cpu_switch(struct thread *old, struct thread *new, struct mtx *mtx)
 *
 * a0 = old
 * a1 = new
 * a2 = mtx
 * x3 to x7, x16 and x17 are caller saved
*/

ENTRY(cpu_switchto)

//XXX TODO:
#if 0
	/* Store the new curthread */
	sd	a1, PC_CURTHREAD(tp)
	/* And the new pcb */
	ld	x13, TD_PCB(a1)
	sd	x13, PC_CURPCB(tp)

	/* Save the old context. */
	ld	x13, TD_PCB(a0)

	/* Store ra, sp and the callee-saved registers */
	sd	ra, (PCB_RA)(x13)
	sd	sp, (PCB_SP)(x13)

	/* s[0-11] */
	sd	s0, (PCB_S + 0 * 8)(x13)
	sd	s1, (PCB_S + 1 * 8)(x13)
	sd	s2, (PCB_S + 2 * 8)(x13)
	sd	s3, (PCB_S + 3 * 8)(x13)
	sd	s4, (PCB_S + 4 * 8)(x13)
	sd	s5, (PCB_S + 5 * 8)(x13)
	sd	s6, (PCB_S + 6 * 8)(x13)
	sd	s7, (PCB_S + 7 * 8)(x13)
	sd	s8, (PCB_S + 8 * 8)(x13)
	sd	s9, (PCB_S + 9 * 8)(x13)
	sd	s10, (PCB_S + 10 * 8)(x13)
	sd	s11, (PCB_S + 11 * 8)(x13)

#ifdef FPE
	/*
	 * Is FPE enabled and is it in dirty state
	 * for the old thread?
	 */
	ld	t0, TD_FRAME(a0)
	ld	t1, (TF_SSTATUS)(t0)
	li	t2, SSTATUS_FS_MASK
	and	t3, t1, t2
	li	t2, SSTATUS_FS_DIRTY
	bne	t3, t2, 1f		/* No, skip. */

	/* Yes, mark FPE state clean and save registers. */
	li	t2, ~SSTATUS_FS_MASK
	and	t3, t1, t2
	li	t2, SSTATUS_FS_CLEAN
	or	t3, t3, t2
	sd	t3, (TF_SSTATUS)(t0)

	__fpe_state_save x13
1:
#endif

	/* Activate the new thread's pmap */
	mv	s0, a0
	mv	s1, a1
	mv	s2, a2
	mv	a0, a1
	call	_C_LABEL(pmap_activate_sw)
	mv	a1, s1

	/* Release the old thread */
	sd	s2, TD_LOCK(s0)
#if defined(SCHED_ULE) && defined(SMP)
	/* Spin if TD_LOCK points to a blocked_lock */
	la	s2, _C_LABEL(blocked_lock)
1:
	ld	t0, TD_LOCK(a1)
	beq	t0, s2, 1b
#endif
	/*
	 * Restore the saved context.
	 */
	ld	x13, TD_PCB(a1)

	/* Restore the registers */
	ld	ra, (PCB_RA)(x13)
	ld	sp, (PCB_SP)(x13)

	/* s[0-11] */
	ld	s0, (PCB_S + 0 * 8)(x13)
	ld	s1, (PCB_S + 1 * 8)(x13)
	ld	s2, (PCB_S + 2 * 8)(x13)
	ld	s3, (PCB_S + 3 * 8)(x13)
	ld	s4, (PCB_S + 4 * 8)(x13)
	ld	s5, (PCB_S + 5 * 8)(x13)
	ld	s6, (PCB_S + 6 * 8)(x13)
	ld	s7, (PCB_S + 7 * 8)(x13)
	ld	s8, (PCB_S + 8 * 8)(x13)
	ld	s9, (PCB_S + 9 * 8)(x13)
	ld	s10, (PCB_S + 10 * 8)(x13)
	ld	s11, (PCB_S + 11 * 8)(x13)

#ifdef FPE
	/* Is FPE enabled for new thread? */
	ld	t0, TD_FRAME(a1)
	ld	t1, (TF_SSTATUS)(t0)
	li	t2, SSTATUS_FS_MASK
	and	t3, t1, t2
	beqz	t3, 1f		/* No, skip. */

	/* Restore registers. */
	__fpe_state_load x13
1:
#endif

	ret
.Lcpu_switch_panic_str:
	.asciz "cpu_switch: %p\0"

#endif
END(cpu_switch)

