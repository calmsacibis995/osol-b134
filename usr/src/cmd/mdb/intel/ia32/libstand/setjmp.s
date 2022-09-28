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
	
#pragma ident	"%Z%%M%	%I%	%E% SMI"

#if defined(__lint)
#include <setjmp.h>
#endif

#include <sys/asm_linkage.h>

/*
 *	longjmp(env, val)
 * will generate a "return(val)" from
 * the last call to
 *	setjmp(env)
 * by restoring registers ip, sp, bp, bx, si, and di from 'env'
 * and doing a return.
 *
 * entry    reg	offset from (%si)
 * env[0] = %ebx	 0	/ register variables
 * env[1] = %esi	 4
 * env[2] = %edi	 8
 * env[3] = %ebp	 12	/ stack frame
 * env[4] = %esp	 16
 * env[5] = %eip	 20
 */

#if defined(__lint)
/* ARGSUSED */
int 
setjmp(jmp_buf env)
{
	return (0);
}

/* ARGSUSED */
int
sigsetjmp(sigjmp_buf env, int savemask)
{
	return (0);
}
#else	/* __lint */

	ENTRY(setjmp)
	ALTENTRY(sigsetjmp)
	movl	4(%esp),%eax	/ jmpbuf address
	movl	%ebx,0(%eax)	/ save ebx
	movl	%esi,4(%eax)	/ save esi
	movl	%edi,8(%eax)	/ save edi
	movl	%ebp,12(%eax)	/ save caller's ebp
	popl	%edx		/ return address
	movl	%esp,16(%eax)	/ save caller's esp
	movl	%edx,20(%eax)
	subl	%eax,%eax	/ return 0
	jmp	*%edx
	SET_SIZE(sigsetjmp)
	SET_SIZE(setjmp)

#endif	/* __lint */

#if defined(__lint)
/* ARGSUSED */
void 
longjmp(jmp_buf env, int val)
{
}

/* ARGSUSED */
void 
siglongjmp(sigjmp_buf env, int val)
{
}
#else	/* __lint */

	ENTRY(longjmp)
	ALTENTRY(siglongjmp)
	movl	4(%esp),%edx	/ first parameter after return addr
	movl	8(%esp),%eax	/ second parameter
	movl	0(%edx),%ebx	/ restore ebx
	movl	4(%edx),%esi	/ restore esi
	movl	8(%edx),%edi	/ restore edi
	movl	12(%edx),%ebp	/ restore caller's ebp
	movl	16(%edx),%esp	/ restore caller's esp
	test	%eax,%eax	/ if val != 0
	jnz	.ret		/ 	return val
	incl	%eax		/ else return 1
.ret:
	jmp	*20(%edx)	/ return to caller
	SET_SIZE(siglongjmp)
	SET_SIZE(longjmp)

#endif	/* __lint */
