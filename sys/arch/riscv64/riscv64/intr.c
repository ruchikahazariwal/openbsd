/*
 * Copyright (c) 2011 Dale Rahn <drahn@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timetc.h>
#include <sys/malloc.h>

#include <dev/clock_subr.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>

/* provide functions for asm */
#undef splraise
#undef spllower
#undef splx

int
splraise(int ipl)
{
	// XXX Interrupt Priority
	return ipl;
}

int
spllower(int ipl)
{
	// XXX Interrupt Priority
	return ipl;
}

void
splx(int ipl)
{
	// XXX Interrupt Priority
}

void
intr_barrier(void *ih)
{
	sched_barrier(NULL);
}
