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
#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Copyright (c) 1986 by Sun Microsystems, Inc.
 */

/*
 * this atrocity is necessary on sparc because registers modified
 * by the child get propagated back to the parent via the window
 * save/restore mechanism.
 */

#ifndef _vfork_h
#define _vfork_h

extern int vfork();

#ifdef sparc
#pragma unknown_control_flow(vfork)
#endif

#endif /*!_vfork_h*/
