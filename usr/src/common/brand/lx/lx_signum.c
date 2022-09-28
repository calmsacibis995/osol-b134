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

#include <sys/signal.h>
#include <lx_signum.h>

/*
 * Delivering signals to a Linux process is complicated by differences in
 * signal numbering, stack structure and contents, and the action taken when a
 * signal handler exits.  In addition, many signal-related structures, such as
 * sigset_ts, vary between Solaris and Linux.
 *
 * The simplest transformation that must be done when sending signals is to
 * translate between Linux and Solaris signal numbers.
 *
 * These are the major signal number differences between Linux and Solaris:
 *
 * 	====================================
 * 	| Number |   Linux    |  Solaris   |
 * 	| ====== | =========  | ========== |
 *	|    7   | SIGBUS     | SIGEMT     |
 *	|   10   | SIGUSR1    | SIGBUS     |
 *	|   12   | SIGUSR2    | SIGSYS     |
 *	|   16   | SIGSTKFLT  | SIGUSR1    |
 *	|   17   | SIGCHLD    | SIGUSR2    |
 * 	|   18   | SIGCONT    | SIGCHLD    |
 *	|   19   | SIGSTOP    | SIGPWR     |
 * 	|   20   | SIGTSTP    | SIGWINCH   |
 * 	|   21   | SIGTTIN    | SIGURG     |
 * 	|   22   | SIGTTOU    | SIGPOLL    |
 *	|   23   | SIGURG     | SIGSTOP    |
 * 	|   24   | SIGXCPU    | SIGTSTP    |
 *	|   25   | SIGXFSZ    | SIGCONT    |
 *	|   26   | SIGVTALARM | SIGTTIN    |
 *	|   27   | SIGPROF    | SIGTTOU    |
 *	|   28   | SIGWINCH   | SIGVTALARM |
 *	|   29   | SIGPOLL    | SIGPROF    |
 *	|   30   | SIGPWR     | SIGXCPU    |
 *	|   31   | SIGSYS     | SIGXFSZ    |
 * 	====================================
 *
 * Not every Linux signal maps to a Solaris signal, nor does every Solaris
 * signal map to a Linux counterpart. However, when signals do map, the
 * mapping is unique.
 *
 * One mapping issue is that Linux supports 32 real time signals, with SIGRTMIN
 * typically starting at or near 32 (SIGRTMIN) and proceeding to 63 (SIGRTMAX)
 * (SIGRTMIN is "at or near" 32 because glibc usually "steals" one ore more of
 * these signals for its own internal use, adjusting SIGRTMIN and SIGRTMAX as
 * needed.)  Conversely, Solaris actively uses signals 32-40 for other purposes
 * and and only supports seven real time signals, in the range 41 (SIGRTMIN)
 * to 48 (SIGRTMAX).
 *
 * At present, attempting to translate a Linux signal greater than 39 but
 * less than 62 will generate an error (we allow SIGRTMAX because a program
 * should be able to send SIGRTMAX without getting an EINVAL, though obviously
 * anything that loops through the signals from SIGRTMIN to SIGRTMAX will
 * fail.)
 *
 * Similarly, attempting to translate a native Solaris signal in the range
 * 32-40 will also generate an error as we don't want to support the receipt of
 * those signals from the Solaris global zone.
 */

/*
 * Linux to Solaris signal map
 *
 * Usage:  solaris_signal = ltos_signum[lx_signal];
 */
const int
ltos_signo[LX_NSIG] = {
	0,
	SIGHUP,
	SIGINT,
	SIGQUIT,
	SIGILL,
	SIGTRAP,
	SIGABRT,
	SIGBUS,
	SIGFPE,
	SIGKILL,
	SIGUSR1,
	SIGSEGV,
	SIGUSR2,
	SIGPIPE,
	SIGALRM,
	SIGTERM,
	SIGEMT,			/* 16:  Linux SIGSTKFLT; use Solaris SIGEMT */
	SIGCHLD,
	SIGCONT,
	SIGSTOP,
	SIGTSTP,
	SIGTTIN,
	SIGTTOU,
	SIGURG,
	SIGXCPU,
	SIGXFSZ,
	SIGVTALRM,
	SIGPROF,
	SIGWINCH,
	SIGPOLL,
	SIGPWR,
	SIGSYS,
	_SIGRTMIN,		/* 32:  Linux SIGRTMIN */
	_SIGRTMIN + 1,
	_SIGRTMIN + 2,
	_SIGRTMIN + 3,
	_SIGRTMIN + 4,
	_SIGRTMIN + 5,
	_SIGRTMIN + 6,
	-1,			/* 38   Linux SIGRTMIN + 7 */
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,			/* 63:  Linux SIGRTMIN + 32, or SIGRTMAX - 1 */
	_SIGRTMAX,		/* 64:  Linux SIGRTMAX */
};

/*
 * Solaris to Linux signal map
 *
 * Usage:  lx_signal = stol_signo[solaris_signal];
 */
const int
stol_signo[NSIG] = {
	0,
	LX_SIGHUP,
	LX_SIGINT,
	LX_SIGQUIT,
	LX_SIGILL,
	LX_SIGTRAP,
	LX_SIGABRT,
	LX_SIGSTKFLT,		/* 7:  Solaris SIGEMT; use for LX_SIGSTKFLT */
	LX_SIGFPE,
	LX_SIGKILL,
	LX_SIGBUS,
	LX_SIGSEGV,
	LX_SIGSYS,
	LX_SIGPIPE,
	LX_SIGALRM,
	LX_SIGTERM,
	LX_SIGUSR1,
	LX_SIGUSR2,
	LX_SIGCHLD,
	LX_SIGPWR,
	LX_SIGWINCH,
	LX_SIGURG,
	LX_SIGPOLL,
	LX_SIGSTOP,
	LX_SIGTSTP,
	LX_SIGCONT,
	LX_SIGTTIN,
	LX_SIGTTOU,
	LX_SIGVTALRM,
	LX_SIGPROF,
	LX_SIGXCPU,
	LX_SIGXFSZ,
	-1,			/* 32:  Solaris SIGWAITING */
	-1,			/* 33:  Solaris SIGLWP */
	-1,			/* 34:  Solaris SIGFREEZE */
	-1,			/* 35:  Solaris SIGTHAW */
	-1,			/* 36:  Solaris SIGCANCEL */
	-1,			/* 37:  Solaris SIGLOST */
	-1,			/* 38:  Solaris SIGXRES */
	-1,			/* 39:  Solaris SIGJVM1 */
	-1,			/* 40:  Solaris SIGJVM2 */
	LX_SIGRTMIN,		/* 41:  Solaris _SIGRTMIN */
	LX_SIGRTMIN + 1,
	LX_SIGRTMIN + 2,
	LX_SIGRTMIN + 3,
	LX_SIGRTMIN + 4,
	LX_SIGRTMIN + 5,
	LX_SIGRTMIN + 6,
	LX_SIGRTMAX,		/* 48: Solaris _SIGRTMAX */
};
