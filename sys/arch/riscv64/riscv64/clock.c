/*
 * Copyright (c) 2020 Brian Bamsch <bbamsch@google.com>
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
#include <dev/clock_subr.h>
#include <sys/malloc.h>

#include <machine/cpu.h>
#include <machine/intr.h>

todr_chip_handle_t todr_handle;

/*
 * resettodr:
 *
 *      Reset the time-of-day register with the current time.
 */
void
resettodr(void)
{
	struct timeval rtctime;

	if (time_second == 1)
		return;

	microtime(&rtctime);

	if (todr_handle != NULL &&
	   todr_settime(todr_handle, &rtctime) != 0)
		printf("resettodr: failed to set time\n");
}

/*
 * inittodr:
 *
 *      Initialize time from the time-of-day register.
 */
#define MINYEAR         2003    /* minimum plausible year */
void
inittodr(time_t base)
{
	time_t deltat;
	struct timeval rtctime;
	struct timespec ts;
	int badbase;

	if (base < (MINYEAR - 1970) * SECYR) {
		printf("WARNING: preposterous time in file system\n");
		/* read the system clock anyway */
		base = (MINYEAR - 1970) * SECYR;
		badbase = 1;
	} else
		badbase = 0;

	if (todr_handle == NULL ||
	    todr_gettime(todr_handle, &rtctime) != 0 ||
	    rtctime.tv_sec == 0) {
		/*
		 * Believe the time in the file system for lack of
		 * anything better, resetting the TODR.
		 */
		rtctime.tv_sec = base;
		rtctime.tv_usec = 0;
		if (todr_handle != NULL && !badbase) {
			printf("WARNING: preposterous clock chip time\n");
			resettodr();
		}
		ts.tv_sec = rtctime.tv_sec;
		ts.tv_nsec = rtctime.tv_usec * 1000;
		tc_setclock(&ts);
		goto bad;
	} else {
		ts.tv_sec = rtctime.tv_sec;
		ts.tv_nsec = rtctime.tv_usec * 1000;
		tc_setclock(&ts);
	}

	if (!badbase) {
		/*
		 * See if we gained/lost two or more days; if
		 * so, assume something is amiss.
		 */
		deltat = rtctime.tv_sec - base;
		if (deltat < 0)
			deltat = -deltat;
		if (deltat < 2 * SECDAY)
			return;         /* all is well */
		printf("WARNING: clock %s %ld days\n",
		    rtctime.tv_sec < base ? "lost" : "gained",
		    (long)deltat / SECDAY);
	}
 bad:
	printf("WARNING: CHECK AND RESET THE DATE!\n");
}
