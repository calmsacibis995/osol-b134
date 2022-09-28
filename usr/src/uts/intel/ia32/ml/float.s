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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*      Copyright (c) 1990, 1991 UNIX System Laboratories, Inc. */
/*      Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990 AT&T   */
/*        All Rights Reserved   */

/*      Copyright (c) 1987, 1988 Microsoft Corporation  */
/*        All Rights Reserved   */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/asm_linkage.h>
#include <sys/asm_misc.h>
#include <sys/regset.h>
#include <sys/privregs.h>
#include <sys/x86_archext.h>

#if defined(__lint)
#include <sys/types.h>
#include <sys/fp.h>
#else
#include "assym.h"
#endif

#if defined(__lint)
 
uint_t
fpu_initial_probe(void)
{ return (0); }

#else	/* __lint */

	/*
	 * Returns zero if x87 "chip" is present(!)
	 */
	ENTRY_NP(fpu_initial_probe)
	CLTS
	fninit
	fnstsw	%ax
	movzbl	%al, %eax
	ret
	SET_SIZE(fpu_initial_probe)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
fxsave_insn(struct fxsave_state *fx)
{}

#else	/* __lint */

#if defined(__amd64)

	ENTRY_NP(fxsave_insn)
	FXSAVEQ	((%rdi))
	ret
	SET_SIZE(fxsave_insn)

#elif defined(__i386)

	ENTRY_NP(fxsave_insn)
	movl	4(%esp), %eax
	fxsave	(%eax)
	ret
	SET_SIZE(fxsave_insn)

#endif

#endif	/* __lint */

#if defined(__i386)

/*
 * If (num1/num2 > num1/num3) the FPU has the FDIV bug.
 */

#if defined(__lint)

int
fpu_probe_pentium_fdivbug(void)
{ return (0); }

#else	/* __lint */

	ENTRY_NP(fpu_probe_pentium_fdivbug)
	fldl	.num1
	fldl	.num2
	fdivr	%st(1), %st
	fxch	%st(1)
	fdivl	.num3
	fcompp
	fstsw	%ax
	sahf
	jae	0f
	movl	$1, %eax
	ret

0:	xorl	%eax, %eax
	ret

	.align	4
.num1:	.4byte	0xbce4217d	/* 4.999999 */
	.4byte	0x4013ffff
.num2:	.4byte	0x0		/* 15.0 */
	.4byte	0x402e0000
.num3:	.4byte	0xde7210bf	/* 14.999999 */
	.4byte	0x402dffff
	SET_SIZE(fpu_probe_pentium_fdivbug)

#endif	/* __lint */

/*
 * To cope with processors that do not implement fxsave/fxrstor
 * instructions, patch hot paths in the kernel to use them only
 * when that feature has been detected.
 */

#if defined(__lint)

void
patch_sse(void)
{}

void
patch_sse2(void)
{}

#else	/* __lint */

	ENTRY_NP(patch_sse)
	_HOT_PATCH_PROLOG
	/
	/	frstor (%ebx); nop	-> fxrstor (%ebx)
	/
	_HOT_PATCH(_fxrstor_ebx_insn, _patch_fxrstor_ebx, 3)
	/
	/	lock; xorl $0, (%esp)	-> sfence; ret
	/
	_HOT_PATCH(_sfence_ret_insn, _patch_sfence_ret, 4)
	_HOT_PATCH_EPILOG
	ret
_fxrstor_ebx_insn:			/ see ndptrap_frstor()
	fxrstor	(%ebx)
_ldmxcsr_ebx_insn:			/ see resume_from_zombie()
	ldmxcsr	(%ebx)
_sfence_ret_insn:			/ see membar_producer()
	.byte	0xf, 0xae, 0xf8		/ [sfence instruction]
	ret
	SET_SIZE(patch_sse)

	ENTRY_NP(patch_sse2)
	_HOT_PATCH_PROLOG
	/
	/	lock; xorl $0, (%esp)	-> lfence; ret
	/
	_HOT_PATCH(_lfence_ret_insn, _patch_lfence_ret, 4)
	_HOT_PATCH_EPILOG
	ret
_lfence_ret_insn:			/ see membar_consumer()
	.byte	0xf, 0xae, 0xe8		/ [lfence instruction]	
	ret
	SET_SIZE(patch_sse2)

#endif	/* __lint */
#endif	/* __i386 */

	
/*
 * One of these routines is called from any lwp with floating
 * point context as part of the prolog of a context switch.
 */

#if defined(__lint)

/*ARGSUSED*/
void
fpxsave_ctxt(void *arg)
{}

/*ARGSUSED*/
void
fpnsave_ctxt(void *arg)
{}

#else	/* __lint */

#if defined(__amd64)

	ENTRY_NP(fpxsave_ctxt)
	cmpl	$FPU_EN, FPU_CTX_FPU_FLAGS(%rdi)
	jne	1f

	movl	$_CONST(FPU_VALID|FPU_EN), FPU_CTX_FPU_FLAGS(%rdi)
	FXSAVEQ	(FPU_CTX_FPU_REGS(%rdi))

	/*
	 * On certain AMD processors, the "exception pointers" i.e. the last
	 * instruction pointer, last data pointer, and last opcode
	 * are saved by the fxsave instruction ONLY if the exception summary
	 * bit is set.
	 *
	 * To ensure that we don't leak these values into the next context
	 * on the cpu, we could just issue an fninit here, but that's
	 * rather slow and so we issue an instruction sequence that
	 * clears them more quickly, if a little obscurely.
	 */
	btw	$7, FXSAVE_STATE_FSW(%rdi)	/* Test saved ES bit */
	jnc	0f				/* jump if ES = 0 */
	fnclex		/* clear pending x87 exceptions */
0:	ffree	%st(7)	/* clear tag bit to remove possible stack overflow */
	fildl	.fpzero_const(%rip)
			/* dummy load changes all exception pointers */
	STTS(%rsi)	/* trap on next fpu touch */
1:	rep;	ret	/* use 2 byte return instruction when branch target */
			/* AMD Software Optimization Guide - Section 6.2 */
	SET_SIZE(fpxsave_ctxt)

#elif defined(__i386)

	ENTRY_NP(fpnsave_ctxt)
	movl	4(%esp), %eax		/* a struct fpu_ctx */
	cmpl	$FPU_EN, FPU_CTX_FPU_FLAGS(%eax)
	jne	1f

	movl	$_CONST(FPU_VALID|FPU_EN), FPU_CTX_FPU_FLAGS(%eax)
	fnsave	FPU_CTX_FPU_REGS(%eax)
			/* (fnsave also reinitializes x87 state) */
	STTS(%edx)	/* trap on next fpu touch */
1:	rep;	ret	/* use 2 byte return instruction when branch target */
			/* AMD Software Optimization Guide - Section 6.2 */
	SET_SIZE(fpnsave_ctxt)

	ENTRY_NP(fpxsave_ctxt)
	movl	4(%esp), %eax		/* a struct fpu_ctx */
	cmpl	$FPU_EN, FPU_CTX_FPU_FLAGS(%eax)
	jne	1f

	movl	$_CONST(FPU_VALID|FPU_EN), FPU_CTX_FPU_FLAGS(%eax)
	fxsave	FPU_CTX_FPU_REGS(%eax)
			/* (see notes above about "exception pointers") */
	btw	$7, FXSAVE_STATE_FSW(%eax)	/* Test saved ES bit */
	jnc	0f				/* jump if ES = 0 */
	fnclex		/* clear pending x87 exceptions */
0:	ffree	%st(7)	/* clear tag bit to remove possible stack overflow */
	fildl	.fpzero_const
			/* dummy load changes all exception pointers */
	STTS(%edx)	/* trap on next fpu touch */
1:	rep;	ret	/* use 2 byte return instruction when branch target */
			/* AMD Software Optimization Guide - Section 6.2 */
	SET_SIZE(fpxsave_ctxt)

#endif	/* __i386 */

	.align	8
.fpzero_const:
	.4byte	0x0
	.4byte	0x0

#endif	/* __lint */


#if defined(__lint)

/*ARGSUSED*/
void
fpsave(struct fnsave_state *f)
{}

/*ARGSUSED*/
void
fpxsave(struct fxsave_state *f)
{}

#else	/* __lint */

#if defined(__amd64)

	ENTRY_NP(fpxsave)
	CLTS
	FXSAVEQ	((%rdi))
	fninit				/* clear exceptions, init x87 tags */
	STTS(%rdi)			/* set TS bit in %cr0 (disable FPU) */
	ret
	SET_SIZE(fpxsave)

#elif defined(__i386)

	ENTRY_NP(fpsave)
	CLTS
	movl	4(%esp), %eax
	fnsave	(%eax)
	STTS(%eax)			/* set TS bit in %cr0 (disable FPU) */
	ret
	SET_SIZE(fpsave)

	ENTRY_NP(fpxsave)
	CLTS
	movl	4(%esp), %eax
	fxsave	(%eax)
	fninit				/* clear exceptions, init x87 tags */
	STTS(%eax)			/* set TS bit in %cr0 (disable FPU) */
	ret
	SET_SIZE(fpxsave)

#endif	/* __i386 */
#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
fprestore(struct fnsave_state *f)
{}

/*ARGSUSED*/
void
fpxrestore(struct fxsave_state *f)
{}

#else	/* __lint */

#if defined(__amd64)

	ENTRY_NP(fpxrestore)
	CLTS
	FXRSTORQ	((%rdi))
	ret
	SET_SIZE(fpxrestore)

#elif defined(__i386)

	ENTRY_NP(fprestore)
	CLTS
	movl	4(%esp), %eax
	frstor	(%eax)
	ret
	SET_SIZE(fprestore)

	ENTRY_NP(fpxrestore)
	CLTS
	movl	4(%esp), %eax
	fxrstor	(%eax)
	ret
	SET_SIZE(fpxrestore)

#endif	/* __i386 */
#endif	/* __lint */

/*
 * Disable the floating point unit.
 */

#if defined(__lint)

void
fpdisable(void)
{}

#else	/* __lint */

#if defined(__amd64)

	ENTRY_NP(fpdisable)
	STTS(%rdi)			/* set TS bit in %cr0 (disable FPU) */ 
	ret
	SET_SIZE(fpdisable)

#elif defined(__i386)

	ENTRY_NP(fpdisable)
	STTS(%eax)
	ret
	SET_SIZE(fpdisable)

#endif	/* __i386 */
#endif	/* __lint */

/*
 * Initialize the fpu hardware.
 */

#if defined(__lint)

void
fpinit(void)
{}

#else	/* __lint */

#if defined(__amd64)

	ENTRY_NP(fpinit)
	CLTS
	leaq	sse_initial(%rip), %rax
	FXRSTORQ	((%rax))		/* load clean initial state */
	ret
	SET_SIZE(fpinit)

#elif defined(__i386)

	ENTRY_NP(fpinit)
	CLTS
	cmpl	$__FP_SSE, fp_kind
	je	1f

	fninit
	movl	$x87_initial, %eax
	frstor	(%eax)			/* load clean initial state */
	ret
1:
	movl	$sse_initial, %eax
	fxrstor	(%eax)			/* load clean initial state */
	ret
	SET_SIZE(fpinit)

#endif	/* __i386 */
#endif	/* __lint */

/*
 * Clears FPU exception state.
 * Returns the FP status word.
 */

#if defined(__lint)

uint32_t
fperr_reset(void)
{ return (0); }

uint32_t
fpxerr_reset(void)
{ return (0); }

#else	/* __lint */

#if defined(__amd64)

	ENTRY_NP(fperr_reset)
	CLTS
	xorl	%eax, %eax
	fnstsw	%ax
	fnclex
	ret
	SET_SIZE(fperr_reset)

	ENTRY_NP(fpxerr_reset)
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$0x10, %rsp		/* make some temporary space */
	CLTS
	stmxcsr	(%rsp)
	movl	(%rsp), %eax
	andl	$_BITNOT(SSE_MXCSR_EFLAGS), (%rsp)
	ldmxcsr	(%rsp)			/* clear processor exceptions */
	leave
	ret
	SET_SIZE(fpxerr_reset)

#elif defined(__i386)

	ENTRY_NP(fperr_reset)
	CLTS
	xorl	%eax, %eax
	fnstsw	%ax
	fnclex
	ret
	SET_SIZE(fperr_reset)

	ENTRY_NP(fpxerr_reset)
	CLTS
	subl	$4, %esp		/* make some temporary space */
	stmxcsr	(%esp)
	movl	(%esp), %eax
	andl	$_BITNOT(SSE_MXCSR_EFLAGS), (%esp)
	ldmxcsr	(%esp)			/* clear processor exceptions */
	addl	$4, %esp
	ret
	SET_SIZE(fpxerr_reset)

#endif	/* __i386 */
#endif	/* __lint */

#if defined(__lint)

uint32_t
fpgetcwsw(void)
{
	return (0);
}

#else   /* __lint */

#if defined(__amd64)

	ENTRY_NP(fpgetcwsw)
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$0x10, %rsp		/* make some temporary space	*/
	CLTS
	fnstsw	(%rsp)			/* store the status word	*/
	fnstcw	2(%rsp)			/* store the control word	*/
	movl	(%rsp), %eax		/* put both in %eax		*/
	leave
	ret
	SET_SIZE(fpgetcwsw)

#elif defined(__i386)

	ENTRY_NP(fpgetcwsw)
	CLTS
	subl	$4, %esp		/* make some temporary space	*/
	fnstsw	(%esp)			/* store the status word	*/
	fnstcw	2(%esp)			/* store the control word	*/
	movl	(%esp), %eax		/* put both in %eax		*/
	addl	$4, %esp
	ret
	SET_SIZE(fpgetcwsw)

#endif	/* __i386 */
#endif  /* __lint */

/*
 * Returns the MXCSR register.
 */

#if defined(__lint)

uint32_t
fpgetmxcsr(void)
{
	return (0);
}

#else   /* __lint */

#if defined(__amd64)

	ENTRY_NP(fpgetmxcsr)
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$0x10, %rsp		/* make some temporary space */
	CLTS
	stmxcsr	(%rsp)
	movl	(%rsp), %eax
	leave
	ret
	SET_SIZE(fpgetmxcsr)

#elif defined(__i386)

	ENTRY_NP(fpgetmxcsr)
	CLTS
	subl	$4, %esp		/* make some temporary space */
	stmxcsr	(%esp)
	movl	(%esp), %eax
	addl	$4, %esp
	ret
	SET_SIZE(fpgetmxcsr)

#endif	/* __i386 */
#endif  /* __lint */
