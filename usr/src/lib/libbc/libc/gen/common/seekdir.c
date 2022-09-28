/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 1992 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI" 

#include <sys/param.h>
#include <dirent.h>

/*
 * seek to an entry in a directory.
 * Only values returned by "telldir" should be passed to seekdir.
 */
void
seekdir(dirp, tell)
	register DIR *dirp;
	register long tell;
{
	extern long lseek();
	long curloc;

	curloc = telldir(dirp);
	if (curloc == tell)
		return;
        dirp->dd_loc = 0;
        (void) lseek(dirp->dd_fd, tell, 0);
        dirp->dd_size = 0;
        dirp->dd_off = tell;
}

#undef	rewinddir

void
rewinddir(dirp)
	DIR *dirp;
{
	seekdir(dirp, 0);
}
