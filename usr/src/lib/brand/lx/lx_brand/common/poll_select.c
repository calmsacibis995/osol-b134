/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <alloca.h>
#include <signal.h>
#include <strings.h>
#include <sys/param.h>
#include <sys/brand.h>
#include <sys/poll.h>
#include <sys/syscall.h>
#include <sys/lx_debug.h>
#include <sys/lx_poll.h>
#include <sys/lx_syscall.h>
#include <sys/lx_brand.h>
#include <sys/lx_misc.h>

extern int select_large_fdset(int nfds, fd_set *in0, fd_set *out0, fd_set *ex0,
	struct timeval *tv);

int
lx_select(uintptr_t p1, uintptr_t p2, uintptr_t p3, uintptr_t p4,
	uintptr_t p5)
{
	int nfds = (int)p1;
	fd_set *rfdsp = NULL;
	fd_set *wfdsp = NULL;
	fd_set *efdsp = NULL;
	struct timeval tv, *tvp = NULL;
	int fd_set_len = howmany(nfds, 8);
	int r;
	hrtime_t start, end;

	lx_debug("\tselect(%d, 0x%p, x%p, 0x%p. 0x%p, 0x%p)",
	    nfds, rfdsp, wfdsp, efdsp, tvp);

	if (nfds > 0) {
		if (p2 != NULL) {
			rfdsp = SAFE_ALLOCA(fd_set_len);
			if (rfdsp == NULL)
				return (-ENOMEM);
			if (uucopy((void *)p2, rfdsp, fd_set_len) != 0)
				return (-errno);
		}
		if (p3 != NULL) {
			wfdsp = SAFE_ALLOCA(fd_set_len);
			if (wfdsp == NULL)
				return (-ENOMEM);
			if (uucopy((void *)p3, wfdsp, fd_set_len) != 0)
				return (-errno);
		}
		if (p4 != NULL) {
			efdsp = SAFE_ALLOCA(fd_set_len);
			if (efdsp == NULL)
				return (-ENOMEM);
			if (uucopy((void *)p4, efdsp, fd_set_len) != 0)
				return (-errno);
		}
	}
	if (p5 != NULL) {
		tvp = &tv;
		if (uucopy((void *)p5, &tv, sizeof (tv)) != 0)
			return (-errno);
		start = gethrtime();
	}

	if (nfds >= FD_SETSIZE)
		r = select_large_fdset(nfds, rfdsp, wfdsp, efdsp, tvp);
	else
		r = select(nfds, rfdsp, wfdsp, efdsp, tvp);
	if (r < 0)
		return (-errno);

	if (tvp != NULL) {
		long long tv_total;

		/*
		 * Linux updates the timeval parameter for select() calls
		 * with the amount of time that left before the select
		 * would have timed out.
		 */
		end = gethrtime();
		tv_total = (tv.tv_sec * MICROSEC) + tv.tv_usec;
		tv_total -= ((end - start) / (NANOSEC / MICROSEC));
		if (tv_total < 0) {
			tv.tv_sec = 0;
			tv.tv_usec = 0;
		} else {
			tv.tv_sec = tv_total / MICROSEC;
			tv.tv_usec = tv_total % MICROSEC;
		}

		if (uucopy(&tv, (void *)p5, sizeof (tv)) != 0)
			return (-errno);
	}

	if ((rfdsp != NULL) && (uucopy(rfdsp, (void *)p2, fd_set_len) != 0))
		return (-errno);
	if ((wfdsp != NULL) && (uucopy(wfdsp, (void *)p3, fd_set_len) != 0))
		return (-errno);
	if ((efdsp != NULL) && (uucopy(efdsp, (void *)p4, fd_set_len) != 0))
		return (-errno);

	return (r);
}

int
lx_poll(uintptr_t p1, uintptr_t p2, uintptr_t p3)
{
	struct pollfd	*lfds, *sfds;
	nfds_t		nfds = (nfds_t)p2;
	int		fds_size, i, rval, revents;

	/*
	 * Note: we are assuming that the Linux and Solaris pollfd
	 * structures are identical.  Copy in the linux poll structure.
	 */
	fds_size = sizeof (struct pollfd) * nfds;
	lfds = (struct pollfd *)SAFE_ALLOCA(fds_size);
	if (lfds == NULL)
		return (-ENOMEM);
	if (uucopy((void *)p1, lfds, fds_size) != 0)
		return (-errno);

	/*
	 * The poll system call modifies the poll structures passed in
	 * so we'll need to make an exra copy of them.
	 */
	sfds = (struct pollfd *)SAFE_ALLOCA(fds_size);
	if (sfds == NULL)
		return (-ENOMEM);

	/* Convert the Linux events bitmask into the Solaris equivalent. */
	for (i = 0; i < nfds; i++) {
		/*
		 * If the caller is polling for an unsupported event, we
		 * have to bail out.
		 */
		if (lfds[i].events & ~LX_POLL_SUPPORTED_EVENTS) {
			lx_unsupported("unsupported poll events requested: "
			    "events=0x%x", lfds[i].events);
			return (-ENOTSUP);
		}

		sfds[i].fd = lfds[i].fd;
		sfds[i].events = lfds[i].events & LX_POLL_COMMON_EVENTS;
		if (lfds[i].events & LX_POLLWRNORM)
			sfds[i].events |= POLLWRNORM;
		if (lfds[i].events & LX_POLLWRBAND)
			sfds[i].events |= POLLWRBAND;
		sfds[i].revents = 0;
	}

	lx_debug("\tpoll(0x%p, %u, %d)", sfds, nfds, (int)p3);

	if ((rval = poll(sfds, nfds, (int)p3)) < 0)
		return (-errno);

	/* Convert the Solaris revents bitmask into the Linux equivalent */
	for (i = 0; i < nfds; i++) {
		revents = sfds[i].revents & LX_POLL_COMMON_EVENTS;
		if (sfds[i].revents & POLLWRBAND)
			revents |= LX_POLLWRBAND;

		/*
		 * Be carefull because on solaris POLLOUT and POLLWRNORM
		 * are defined to the same values but on linux they
		 * are not.
		 */
		if (sfds[i].revents & POLLOUT) {
			if ((lfds[i].events & LX_POLLOUT) == 0)
				revents &= ~LX_POLLOUT;
			if (lfds[i].events & LX_POLLWRNORM)
				revents |= LX_POLLWRNORM;
		}

		lfds[i].revents = revents;
	}

	/* Copy out the results */
	if (uucopy(lfds, (void *)p1, fds_size) != 0)
		return (-errno);

	return (rval);
}
