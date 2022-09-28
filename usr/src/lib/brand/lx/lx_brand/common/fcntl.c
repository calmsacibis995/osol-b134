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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/filio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stropts.h>
#include <libintl.h>
#include <errno.h>
#include <string.h>

#include <sys/lx_fcntl.h>
#include <sys/lx_debug.h>
#include <sys/lx_misc.h>

static int lx_fcntl_com(int fd, int cmd, ulong_t arg);
static void ltos_flock(struct lx_flock *l, struct flock *s);
static void stol_flock(struct flock *s, struct lx_flock *l);
static void ltos_flock64(struct lx_flock64 *l, struct flock64 *s);
static void stol_flock64(struct flock64 *s, struct lx_flock64 *l);
static short ltos_type(short l_type);
static short stol_type(short l_type);
static int lx_fcntl_getfl(int fd);
static int lx_fcntl_setfl(int fd, ulong_t arg);

int
lx_dup2(uintptr_t p1, uintptr_t p2)
{
	int oldfd = (int)p1;
	int newfd = (int)p2;
	int rc;

	rc = fcntl(oldfd, F_DUP2FD, newfd);
	return ((rc == -1) ? -errno : rc);
}

int
lx_fcntl(uintptr_t p1, uintptr_t p2, uintptr_t p3)
{
	int		fd = (int)p1;
	int		cmd = (int)p2;
	ulong_t		arg = (ulong_t)p3;
	struct lx_flock lxflk;
	struct flock	fl;
	int		lk = 0;
	int		rc;

	/*
	 * The 64-bit fcntl commands must go through fcntl64().
	 */
	if (cmd == LX_F_GETLK64 || cmd == LX_F_SETLK64 ||
	    cmd == LX_F_SETLKW64)
		return (-EINVAL);

	if (cmd == LX_F_SETSIG || cmd == LX_F_GETSIG || cmd == LX_F_SETLEASE ||
	    cmd == LX_F_GETLEASE) {
		lx_unsupported(gettext("%s(): unsupported command: %d"),
		    "fcntl", cmd);
		return (-ENOTSUP);
	}

	if (cmd == LX_F_GETLK || cmd == LX_F_SETLK ||
	    cmd == LX_F_SETLKW) {
		if (uucopy((void *)p3, (void *)&lxflk,
		    sizeof (struct lx_flock)) != 0)
			return (-errno);
		lk = 1;
		ltos_flock(&lxflk, &fl);
		arg = (ulong_t)&fl;
	}

	rc = lx_fcntl_com(fd, cmd, arg);

	if (lk)
		stol_flock(&fl, (struct lx_flock *)p3);

	return (rc);
}

int
lx_fcntl64(uintptr_t p1, uintptr_t p2, uintptr_t p3)
{
	int		fd = (int)p1;
	int		cmd = (int)p2;
	struct lx_flock lxflk;
	struct lx_flock64 lxflk64;
	struct flock	fl;
	struct flock64	fl64;
	int		rc;

	if (cmd == LX_F_SETSIG || cmd == LX_F_GETSIG || cmd == LX_F_SETLEASE ||
	    cmd == LX_F_GETLEASE) {
		lx_unsupported(gettext("%s(): unsupported command: %d"),
		    "fcntl64", cmd);
		return (-ENOTSUP);
	}

	if (cmd == LX_F_GETLK || cmd == LX_F_SETLK || cmd == LX_F_SETLKW) {
		if (uucopy((void *)p3, (void *)&lxflk,
		    sizeof (struct lx_flock)) != 0)
			return (-errno);
		ltos_flock(&lxflk, &fl);
		rc = lx_fcntl_com(fd, cmd, (ulong_t)&fl);
		stol_flock(&fl, (struct lx_flock *)p3);
	} else if (cmd == LX_F_GETLK64 || cmd == LX_F_SETLKW64 || \
	    cmd == LX_F_SETLK64) {
		if (uucopy((void *)p3, (void *)&lxflk64,
		    sizeof (struct lx_flock64)) != 0)
			return (-errno);
		ltos_flock64(&lxflk64, &fl64);
		rc = lx_fcntl_com(fd, cmd, (ulong_t)&fl64);
		stol_flock64(&fl64, (struct lx_flock64 *)p3);
	} else {
		rc = lx_fcntl_com(fd, cmd, (ulong_t)p3);
	}

	return (rc);
}

static int
lx_fcntl_com(int fd, int cmd, ulong_t arg)
{
	int		rc = 0;

	switch (cmd) {
	case LX_F_DUPFD:
		rc = fcntl(fd, F_DUPFD, arg);
		break;

	case LX_F_GETFD:
		rc = fcntl(fd, F_GETFD, 0);
		break;

	case LX_F_SETFD:
		rc = fcntl(fd, F_SETFD, arg);
		break;

	case LX_F_GETFL:
		rc = lx_fcntl_getfl(fd);
		break;

	case LX_F_SETFL:
		rc = lx_fcntl_setfl(fd, arg);
		break;

	case LX_F_GETLK:
		rc = fcntl(fd, F_GETLK, arg);
		break;

	case LX_F_SETLK:
		rc = fcntl(fd, F_SETLK, arg);
		break;

	case LX_F_SETLKW:
		rc = fcntl(fd, F_SETLKW, arg);
		break;

	case LX_F_GETLK64:
		rc = fcntl(fd, F_GETLK64, arg);
		break;

	case LX_F_SETLK64:
		rc = fcntl(fd, F_SETLK64, arg);
		break;

	case LX_F_SETLKW64:
		rc = fcntl(fd, F_SETLKW64, arg);
		break;

	case LX_F_SETOWN:
		rc = fcntl(fd, F_SETOWN, arg);
		break;

	case LX_F_GETOWN:
		rc = fcntl(fd, F_GETOWN, arg);
		break;

	default:
		return (-EINVAL);
	}

	return ((rc == -1) ? -errno : rc);
}


#define	LTOS_FLOCK(l, s)						\
{									\
	s->l_type = ltos_type(l->l_type);				\
	s->l_whence = l->l_whence;					\
	s->l_start = l->l_start;					\
	s->l_len = l->l_len;						\
	s->l_sysid = 0;			/* not defined in linux */	\
	s->l_pid = (pid_t)l->l_pid;					\
}

#define	STOL_FLOCK(s, l)						\
{									\
	l->l_type = stol_type(s->l_type);				\
	l->l_whence = s->l_whence;					\
	l->l_start = s->l_start;					\
	l->l_len = s->l_len;						\
	l->l_pid = (int)s->l_pid;					\
}

static void
ltos_flock(struct lx_flock *l, struct flock *s)
{
	LTOS_FLOCK(l, s)
}

static void
stol_flock(struct flock *s, struct lx_flock *l)
{
	STOL_FLOCK(s, l)
}

static void
ltos_flock64(struct lx_flock64 *l, struct flock64 *s)
{
	LTOS_FLOCK(l, s)
}

static void
stol_flock64(struct flock64 *s, struct lx_flock64 *l)
{
	STOL_FLOCK(s, l)
}

static short
ltos_type(short l_type)
{
	switch (l_type) {
	case LX_F_RDLCK:
		return (F_RDLCK);
	case LX_F_WRLCK:
		return (F_WRLCK);
	case LX_F_UNLCK:
		return (F_UNLCK);
	default:
		return (-1);
	}
}

static short
stol_type(short l_type)
{
	switch (l_type) {
	case F_RDLCK:
		return (LX_F_RDLCK);
	case F_WRLCK:
		return (LX_F_WRLCK);
	case F_UNLCK:
		return (LX_F_UNLCK);
	default:
		/* can't ever happen */
		return (0);
	}
}

int
lx_fcntl_getfl(int fd)
{
	int retval;
	int rc;

	retval = fcntl(fd, F_GETFL, 0);

	if ((retval & O_ACCMODE) == O_RDONLY)
		rc = LX_O_RDONLY;
	else if ((retval & O_ACCMODE) == O_WRONLY)
		rc = LX_O_WRONLY;
	else
		rc = LX_O_RDWR;
	/* O_NDELAY != O_NONBLOCK, so we need to check for both */
	if (retval & O_NDELAY)
		rc |= LX_O_NDELAY;
	if (retval & O_NONBLOCK)
		rc |= LX_O_NONBLOCK;
	if (retval & O_APPEND)
		rc |= LX_O_APPEND;
	if (retval & O_SYNC)
		rc |= LX_O_SYNC;
	if (retval & O_LARGEFILE)
		rc |= LX_O_LARGEFILE;
	if (retval & FASYNC)
		rc |= LX_O_ASYNC;

	return (rc);
}

int
lx_fcntl_setfl(int fd, ulong_t arg)
{
	int new_arg;

	new_arg = 0;
	/* LX_O_NDELAY == LX_O_NONBLOCK, so we only check for one */
	if (arg & LX_O_NDELAY)
		new_arg |= O_NONBLOCK;
	if (arg & LX_O_APPEND)
		new_arg |= O_APPEND;
	if (arg & LX_O_SYNC)
		new_arg |= O_SYNC;
	if (arg & LX_O_LARGEFILE)
		new_arg |= O_LARGEFILE;
	if (arg & LX_O_ASYNC)
		new_arg |= FASYNC;

	return ((fcntl(fd, F_SETFL, new_arg) == 0) ? 0 : -errno);
}

/*
 * flock() applies or removes an advisory lock on the file
 * associated with the file descriptor fd.
 *
 * Stolen verbatim from usr/src/ucblib/libucb/port/sys/flock.c
 *
 * operation is: LX_LOCK_SH, LX_LOCK_EX, LX_LOCK_UN, LX_LOCK_NB
 */
int
lx_flock(uintptr_t p1, uintptr_t p2)
{
	int			fd = (int)p1;
	int			operation = (int)p2;
	struct flock		fl;
	int			cmd;
	int			ret;

	/* In non-blocking lock, use F_SETLK for cmd, F_SETLKW otherwise */
	if (operation & LX_LOCK_NB) {
		cmd = F_SETLK;
		operation &= ~LX_LOCK_NB; /* turn off this bit */
	} else
		cmd = F_SETLKW;

	switch (operation) {
		case LX_LOCK_UN:
			fl.l_type = F_UNLCK;
			break;
		case LX_LOCK_SH:
			fl.l_type = F_RDLCK;
			break;
		case LX_LOCK_EX:
			fl.l_type = F_WRLCK;
			break;
		default:
			return (-EINVAL);
	}

	fl.l_whence = 0;
	fl.l_start = 0;
	fl.l_len = 0;

	ret = fcntl(fd, cmd, &fl);

	if (ret == -1 && errno == EACCES)
		return (-EWOULDBLOCK);

	return ((ret == -1) ? -errno : ret);
}
