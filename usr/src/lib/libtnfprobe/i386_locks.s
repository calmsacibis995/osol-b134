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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

	.ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/asm_linkage.h>
	.file		"i386_locks.s"
	ENTRY(tnfw_b_get_lock)
#if defined (__amd64)
	/* XX64 - fix me */
#else
	movl	4(%esp), %edx
	subl	%eax, %eax
	lock
	btsl	$0, (%edx)
	jnc	.L1
	incl	%eax
.L1:
#endif
	ret

	ENTRY(tnfw_b_clear_lock)
#if defined (__amd64)
	/* XX64 - fix me */
#else
	movl	4(%esp), %eax
	movb	$0, (%eax)
#endif
	ret

	ENTRY(tnfw_b_atomic_swap)
#if defined (__amd64)
	/* XX64 - fix me */
#else
	movl	4(%esp), %edx
	movl	8(%esp), %eax
	xchgl	%eax, (%edx)
#endif
	ret
