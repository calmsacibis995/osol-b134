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
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <thread.h>
#include <unistd.h>

#include <sys/modctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <sys/lx_brand.h>
#include <sys/lx_debug.h>
#include <sys/lx_misc.h>

/* internal debugging state */
static char	*lx_debug_path = NULL;		/* debug output file path */
static char	lx_debug_path_buf[MAXPATHLEN];

void
lx_debug_enable(void)
{
	/* send all debugging output to /dev/tty */
	lx_debug_path = "/dev/tty";
	lx_debug("lx_debug: debugging output enabled: %s", lx_debug_path);
}

void
lx_debug_init(void)
{
	if (getenv("LX_DEBUG") == NULL)
		return;

	/*
	 * It's OK to use this value without any locking, as all callers can
	 * use the return value to decide whether extra work should be done
	 * before calling lx_debug().
	 *
	 * If debugging is disabled after a routine calls this function it
	 * doesn't really matter as lx_debug() will see debugging is disabled
	 * and will not output anything.
	 */
	lx_debug_enabled = 1;

	/* check if there's a debug log file specified */
	lx_debug_path = getenv("LX_DEBUG_FILE");
	if (lx_debug_path == NULL) {
		/* send all debugging output to /dev/tty */
		lx_debug_path = "/dev/tty";
	}

	(void) strlcpy(lx_debug_path_buf, lx_debug_path,
	    sizeof (lx_debug_path_buf));
	lx_debug_path = lx_debug_path_buf;

	lx_debug("lx_debug: debugging output ENABLED to path: \"%s\"",
	    lx_debug_path);
}

void
lx_debug(const char *msg, ...)
{
	va_list		ap;
	char		buf[LX_MSG_MAXLEN + 1];
	int		rv, fd, n;
	int		errno_backup;

	if (lx_debug_enabled == 0)
		return;

	errno_backup = errno;

	/* prefix the message with pid/tid */
	if ((n = snprintf(buf, sizeof (buf), "%u/%u: ",
	    getpid(), thr_self())) == -1) {
		errno = errno_backup;
		return;
	}

	/* format the message */
	va_start(ap, msg);
	rv = vsnprintf(&buf[n], sizeof (buf) - n, msg, ap);
	va_end(ap);
	if (rv == -1) {
		errno = errno_backup;
		return;
	}

	/* add a carrige return if there isn't one already */
	if ((buf[strlen(buf) - 1] != '\n') &&
	    (strlcat(buf, "\n", sizeof (buf)) >= sizeof (buf))) {
		errno = errno_backup;
		return;
	}

	/*
	 * Open the debugging output file.  note that we don't protect
	 * ourselves against exec or fork1 here.  if an mt process were
	 * to exec/fork1 while we're doing this they'd end up with an
	 * extra open desciptor in their fd space.  a'well.  shouldn't
	 * really matter.
	 */
	if ((fd = open(lx_debug_path,
	    O_WRONLY|O_APPEND|O_CREAT|O_NDELAY|O_NOCTTY, 0666)) == -1) {
		return;
	}
	(void) fchmod(fd, 0666);

	/* we retry in case of EINTR */
	do {
		rv = write(fd, buf, strlen(buf));
	} while ((rv == -1) && (errno == EINTR));
	(void) fsync(fd);

	(void) close(fd);
	errno = errno_backup;
}
