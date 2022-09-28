
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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#if defined(__lint)

int silence_lint_warnings = 0;

#else /* __lint */

#include <sys/multiboot.h>
#include <sys/asm_linkage.h>
#include <sys/segments.h>
#include <sys/controlregs.h>

#include "dboot_xboot.h"

	.text
	.globl _start
_start:
	jmp	code_start

	/*
	 * The multiboot header has to be at the start of the file
	 *
	 * The 32 bit kernel is ELF32, so the MB header is mostly ignored.
	 *
	 * The 64 bit kernel is ELF64, so we get grub to load the entire
	 * ELF file into memory and trick it into jumping into this code.
	 * The trick is done by a binary utility run after unix is linked,
	 * that rewrites the mb_header.
	 */
	.align 4
	.globl	mb_header
mb_header:
	.long	MB_HEADER_MAGIC	/* magic number */
#if defined(_BOOT_TARGET_i386)
	.long	MB_HEADER_FLAGS_32	/* flags */
	.long	MB_HEADER_CHECKSUM_32	/* checksum */
#elif defined (_BOOT_TARGET_amd64)
	.long	MB_HEADER_FLAGS_64	/* flags */
	.long	MB_HEADER_CHECKSUM_64	/* checksum */
#else
#error No architecture defined
#endif
	.long	0x11111111	/* header_addr: patched by mbh_patch */
	.long	0x100000	/* load_addr: patched by mbh_patch */
	.long	0		/* load_end_addr - 0 means entire file */
	.long	0		/* bss_end_addr */
	.long	0x2222222	/* entry_addr: patched by mbh_patch */
	.long	0		/* video mode.. */
	.long	0		/* width 0 == don't care */
	.long	0		/* height 0 == don't care */
	.long	0		/* depth 0 == don't care */

	/*
	 * At entry we are in protected mode, 32 bit execution, paging and
	 * interrupts are disabled.
	 *
	 * EAX == MB_BOOTLOADER_MAGIC
	 * EBX points to multiboot information
	 * segment registers all have segments with base 0, limit == 0xffffffff
	 */
code_start:
	movl	%ebx, mb_info

	movl	$stack_space, %esp	/* load my stack pointer */
	addl	$STACK_SIZE, %esp

	pushl	$0x0			/* push a dead-end frame */
	pushl	$0x0
	movl	%esp, %ebp

	pushl	$0x0			/* clear all processor flags */
	popf

	/*
	 * setup a global descriptor table with known contents
	 */
	lgdt	gdt_info
	movw	$B32DATA_SEL, %ax
	movw    %ax, %ds
	movw    %ax, %es
	movw    %ax, %fs
	movw    %ax, %gs
	movw    %ax, %ss
	ljmp    $B32CODE_SEL, $newgdt
newgdt:
	nop

	/*
	 * go off and determine memory config, build page tables, etc.
	 */
	call	startup_kernel


	/*
	 * On amd64 we'll want the stack pointer to be 16 byte aligned.
	 */
	andl	$0xfffffff0, %esp

	/*
	 * Enable PGE, PAE and large pages
	 */
	movl	%cr4, %eax
	testl	$1, pge_support
	jz	1f
	orl	$CR4_PGE, %eax
1:
	testl	$1, pae_support
	jz	1f
	orl	$CR4_PAE, %eax
1:
	testl	$1, largepage_support
	jz	1f
	orl	$CR4_PSE, %eax
1:
	movl	%eax, %cr4

	/*
	 * enable NX protection if processor supports it
	 */
	testl   $1, NX_support
	jz      1f
	movl    $MSR_AMD_EFER, %ecx
	rdmsr
	orl     $AMD_EFER_NXE, %eax
	wrmsr
1:


	/*
	 * load the pagetable base address into cr3
	 */
	movl	top_page_table, %eax
	movl	%eax, %cr3

#if defined(_BOOT_TARGET_amd64)
	/*
	 * enable long mode
	 */
	movl	$MSR_AMD_EFER, %ecx
	rdmsr
	orl	$AMD_EFER_LME, %eax
	wrmsr
#endif

	/*
	 * enable paging, write protection, alignment masking, but disable
	 * the cache disable and write through only bits.
	 */
	movl	%cr0, %eax
	orl	$_CONST(CR0_PG | CR0_WP | CR0_AM), %eax
	andl	$_BITNOT(CR0_NW | CR0_CD), %eax
	movl	%eax, %cr0
	jmp	paging_on
paging_on:

	/*
	 * The xboot_info ptr gets passed to the kernel as its argument
	 */
	movl	bi, %edi
	movl	entry_addr_low, %esi

#if defined(_BOOT_TARGET_i386)

	pushl	%edi
	call	*%esi

#elif defined(_BOOT_TARGET_amd64)

	/*
	 * We're still in compatibility mode with 32 bit execution.
	 * Switch to 64 bit mode now by switching to a 64 bit code segment.
	 * then set up and do a lret to get into 64 bit execution.
	 */
	pushl	$B64CODE_SEL
	pushl	$longmode
	lret
longmode:
	.code64
	movq	$0xffffffff00000000,%rdx
	orq	%rdx, %rsi		/* set upper bits of entry addr */
	notq	%rdx
	andq	%rdx, %rdi		/* clean %rdi for passing arg */
	call	*%rsi

#else
#error	"undefined target"
#endif

	.code32

	/*
	 * if reset fails halt the system
	 */
	ENTRY_NP(dboot_halt)
	hlt
	SET_SIZE(dboot_halt)

	/*
	 * flush the TLB
	 */
	ENTRY_NP(reload_cr3)
	movl	%cr3, %eax
	movl	%eax, %cr3
	ret
	SET_SIZE(reload_cr3)

	/*
	 * Detect if we can do cpuid, see if we can change bit 21 of eflags.
	 * Note we don't do the bizarre tests for Cyrix CPUs in ml/locore.s.
	 * If you're on such a CPU, you're stuck with non-PAE 32 bit kernels.
	 */
	ENTRY_NP(have_cpuid)
	pushf
	pushf
	xorl	%eax, %eax
	popl	%ecx
	movl	%ecx, %edx
	xorl	$0x200000, %ecx
	pushl	%ecx
	popf
	pushf
	popl	%ecx
	cmpl	%ecx, %edx
	setne	%al
	popf
	ret
	SET_SIZE(have_cpuid)

	/*
	 * We want the GDT to be on its own page for better performance
	 * running under hypervisors.
	 */
	.skip 4096
#include "../boot/boot_gdt.s"
	.skip 4096
	.long	0

#endif /* __lint */
