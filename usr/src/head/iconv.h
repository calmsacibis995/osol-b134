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
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_ICONV_H
#define	_ICONV_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/feature_tests.h>
#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct _iconv_info *iconv_t;

#if defined(__STDC__)
extern iconv_t	iconv_open(const char *, const char *);
#ifdef _XPG6
extern size_t	iconv(iconv_t, char **_RESTRICT_KYWD,
		size_t *_RESTRICT_KYWD, char **_RESTRICT_KYWD,
		size_t *_RESTRICT_KYWD);
#else
extern size_t	iconv(iconv_t, const char **_RESTRICT_KYWD,
		size_t *_RESTRICT_KYWD, char **_RESTRICT_KYWD,
		size_t *_RESTRICT_KYWD);
#endif
extern int	iconv_close(iconv_t);
#else /* __STDC__ */
extern iconv_t	iconv_open();
extern size_t	iconv();
extern int	iconv_close();
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _ICONV_H */
