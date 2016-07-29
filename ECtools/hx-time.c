/*
 *	Copyright Jan Engelhardt, 2012
 *
 *	This file is taken from libHX. libHX is free software; you can
 *	redistribute it and/or modify it under the terms of the GNU Lesser
 *	General Public License as published by the Free Software Foundation;
 *	either version 2.1 or (at your option) any later version.
 */
#include <time.h>
#include "hx-time.h"
#define NANOSECOND 1000000000

struct timespec *
HX_timespec_neg(struct timespec *r, const struct timespec *a)
{
	if (a->tv_sec != 0) {
		r->tv_sec  = -a->tv_sec;
		r->tv_nsec = a->tv_nsec;
	} else {
		r->tv_sec  = 0;
		r->tv_nsec = -a->tv_nsec;
	}
	return r;
}

struct timespec *HX_timespec_add(struct timespec *r,
    const struct timespec *a, const struct timespec *b)
{
	/*
	 * Split the value represented by the struct into two
	 * independent values that can be added individually.
	 */
	long nsec[2];
	nsec[0] = (a->tv_sec < 0) ? -a->tv_nsec : a->tv_nsec;
	nsec[1] = (b->tv_sec < 0) ? -b->tv_nsec : b->tv_nsec;

	r->tv_sec  = a->tv_sec + b->tv_sec;
	r->tv_nsec = nsec[0] + nsec[1];
	if (r->tv_nsec >= NANOSECOND) {
		++r->tv_sec;
		r->tv_nsec -= NANOSECOND;
	} else if (r->tv_nsec <= -NANOSECOND) {
		--r->tv_sec;
		r->tv_nsec += NANOSECOND;
	}

	/* Combine again */
	if (r->tv_sec < 0) {
		if (r->tv_nsec < 0) {
			r->tv_nsec = -r->tv_nsec;
		} else if (r->tv_nsec > 0) {
			if (++r->tv_sec == 0)
				r->tv_nsec = -NANOSECOND + r->tv_nsec;
			else
				r->tv_nsec = NANOSECOND - r->tv_nsec;
		}
	} else if (r->tv_sec > 0 && r->tv_nsec < 0) {
		--r->tv_sec;
		r->tv_nsec = NANOSECOND + r->tv_nsec;
	}
	return r;
}

struct timespec *HX_timespec_sub(struct timespec *r,
    const struct timespec *a, const struct timespec *b)
{
	struct timespec b2;
	return HX_timespec_add(r, a, HX_timespec_neg(&b2, b));
}
