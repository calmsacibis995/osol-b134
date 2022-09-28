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

#include <errno.h>
#include <unistd.h>
#include <sys/lx_misc.h>

/*
 * fork() and vfork()
 *
 * These cannot be pass thru system calls because we need libc to do its own
 * initialization or else bad things will happen (i.e. ending up with a bad
 * schedctl page).  On Linux, there is no such thing as forkall(), so we use
 * fork1() here.
 */
int
lx_fork(void)
{
	int ret = fork1();

	if (ret == 0 && lx_is_rpm)
		(void) sleep(lx_rpm_delay);

	return (ret == -1 ? -errno : ret);
}

/*
 * For vfork(), we have a serious problem because the child is not allowed to
 * return from the current frame because it will corrupt the parent's stack.
 * Since the semantics of vfork() are rather ill-defined (other than "it's
 * faster than fork"), we should theoretically be safe by falling back to
 * fork1().
 */
int
lx_vfork(void)
{
	int ret = fork1();

	return (ret == -1 ? -errno : ret);
}
