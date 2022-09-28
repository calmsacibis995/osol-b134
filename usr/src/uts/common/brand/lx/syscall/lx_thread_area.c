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
#include <sys/cpuvar.h>
#include <sys/archsystm.h>
#include <sys/proc.h>
#include <sys/brand.h>
#include <sys/lx_brand.h>
#include <sys/lx_ldt.h>

long
lx_get_thread_area(struct ldt_info *inf)
{
	struct lx_lwp_data *jlwp = ttolxlwp(curthread);
	struct ldt_info ldt_inf;
	user_desc_t *dscrp;
	int entry;

	if (fuword32(&inf->entry_number, (uint32_t *)&entry))
		return (set_errno(EFAULT));

	if (entry < GDT_TLSMIN || entry > GDT_TLSMAX)
		return (set_errno(EINVAL));

	dscrp = jlwp->br_tls + entry - GDT_TLSMIN;

	/*
	 * convert the solaris ldt to the linux format expected by the
	 * caller
	 */
	DESC_TO_LDT_INFO(dscrp, &ldt_inf);
	ldt_inf.entry_number = entry;

	if (copyout(&ldt_inf, inf, sizeof (struct ldt_info)))
		return (set_errno(EFAULT));

	return (0);
}

long
lx_set_thread_area(struct ldt_info *inf)
{
	struct lx_lwp_data *jlwp = ttolxlwp(curthread);
	struct ldt_info ldt_inf;
	user_desc_t *dscrp;
	int entry;
	int i;

	if (copyin(inf, &ldt_inf, sizeof (ldt_inf)))
		return (set_errno(EFAULT));

	entry = ldt_inf.entry_number;
	if (entry == -1) {
		/*
		 * find an empty entry in the tls for this thread
		 */
		for (i = 0, dscrp = jlwp->br_tls;
					i < LX_TLSNUM; i++, dscrp++)
			if (((unsigned long *)dscrp)[0] == 0 &&
			    ((unsigned long *)dscrp)[1] == 0)
				break;

		if (i < LX_TLSNUM) {
			/*
			 * found one
			 */
			entry = i + GDT_TLSMIN;
			if (suword32(&inf->entry_number, entry))
				return (set_errno(EFAULT));
		} else {
			return (set_errno(ESRCH));
		}
	}

	if (entry < GDT_TLSMIN || entry > GDT_TLSMAX)
		return (set_errno(EINVAL));

	/*
	 * convert the linux ldt info to standard intel descriptor
	 */
	dscrp = jlwp->br_tls + entry - GDT_TLSMIN;

	if (LDT_INFO_EMPTY(&ldt_inf)) {
		((unsigned long *)dscrp)[0] = 0;
		((unsigned long *)dscrp)[1] = 0;
	} else {
		LDT_INFO_TO_DESC(&ldt_inf, dscrp);
	}

	/*
	 * update the gdt with the new descriptor
	 */
	kpreempt_disable();

	for (i = 0, dscrp = jlwp->br_tls; i < LX_TLSNUM; i++, dscrp++)
		lx_set_gdt(GDT_TLSMIN + i, dscrp);

	kpreempt_enable();

	return (0);
}
