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

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/brand.h>
#include <sys/lx_brand.h>
#include <sys/lx_ldt.h>

#define	LX_CSIGNAL		0x000000ff
#define	LX_CLONE_VM		0x00000100
#define	LX_CLONE_FS		0x00000200
#define	LX_CLONE_FILES		0x00000400
#define	LX_CLONE_SIGHAND	0x00000800
#define	LX_CLONE_PID		0x00001000
#define	LX_CLONE_PTRACE		0x00002000
#define	LX_CLONE_PARENT		0x00008000
#define	LX_CLONE_THREAD		0x00010000
#define	LX_CLONE_SYSVSEM	0x00040000
#define	LX_CLONE_SETTLS		0x00080000
#define	LX_CLONE_PARENT_SETTID	0x00100000
#define	LX_CLONE_CHILD_CLEARTID 0x00200000
#define	LX_CLONE_DETACH		0x00400000
#define	LX_CLONE_CHILD_SETTID	0x01000000

/*
 * Our lwp has already been created at this point, so this routine is
 * responsible for setting up all the state needed to track this as a
 * linux cloned thread.
 */
/* ARGSUSED */
long
lx_clone(int flags, void *stkp, void *ptidp, void *ldtinfo, void *ctidp)
{
	struct lx_lwp_data *lwpd = ttolxlwp(curthread);
	struct ldt_info info;
	struct user_desc descr;
	int tls_index;
	int entry = -1;
	int signo;

	signo = flags & LX_CSIGNAL;
	if (signo < 0 || signo > MAXSIG)
		return (set_errno(EINVAL));

	if (flags & LX_CLONE_SETTLS) {
		if (copyin((caddr_t)ldtinfo, &info, sizeof (info)))
			return (set_errno(EFAULT));

		if (LDT_INFO_EMPTY(&info))
			return (set_errno(EINVAL));

		entry = info.entry_number;
		if (entry < GDT_TLSMIN || entry > GDT_TLSMAX)
			return (set_errno(EINVAL));

		tls_index = entry - GDT_TLSMIN;

		/*
		 * Convert the user-space structure into a real x86
		 * descriptor and copy it into this LWP's TLS array.  We
		 * also load it into the GDT.
		 */
		LDT_INFO_TO_DESC(&info, &descr);
		bcopy(&descr, &lwpd->br_tls[tls_index], sizeof (descr));
		lx_set_gdt(entry, &lwpd->br_tls[tls_index]);
	} else {
		tls_index = -1;
		bzero(&descr, sizeof (descr));
	}

	lwpd->br_clear_ctidp =
	    (flags & LX_CLONE_CHILD_CLEARTID) ?  ctidp : NULL;

	if (signo && ! (flags & LX_CLONE_DETACH))
		lwpd->br_signal = signo;
	else
		lwpd->br_signal = 0;

	if (flags & LX_CLONE_THREAD)
		lwpd->br_tgid = curthread->t_procp->p_pid;

	if (flags & LX_CLONE_PARENT)
		lwpd->br_ppid = 0;

	if ((flags & LX_CLONE_CHILD_SETTID) && (ctidp != NULL) &&
	    (suword32(ctidp, lwpd->br_pid) != 0)) {
		if (entry >= 0)
			lx_clear_gdt(entry);
		return (set_errno(EFAULT));
	}
	if ((flags & LX_CLONE_PARENT_SETTID) && (ptidp != NULL) &&
	    (suword32(ptidp, lwpd->br_pid) != 0)) {
		if (entry >= 0)
			lx_clear_gdt(entry);
		return (set_errno(EFAULT));
	}

	return (lwpd->br_pid);
}

long
lx_set_tid_address(int *tidp)
{
	struct lx_lwp_data *lwpd = ttolxlwp(curthread);

	lwpd->br_clear_ctidp = tidp;

	return (lwpd->br_pid);
}
