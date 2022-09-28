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
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#define	ELF_TARGET_AMD64

#include	<stdio.h>
#include	<memory.h>
#include	<debug.h>
#include	"msg.h"
#include	"_libld.h"

/*
 * The loader uses a `segment descriptor' list to describe the output
 * segments it can potentially create. This list is initially seeded
 * using the templates contained in the sg_desc[] array below. Additional
 * segments may be added using a mapfile.
 *
 * The entries in sg_desc[] must be put in the order defined by the
 * Segment_id enum, such that a given LD_XXX value can serve as
 * an index into sg_desc[] for the corresponding descriptor.
 *
 * The entries in sg_desc[] are initialized using the SG_DESC_INIT macro
 * for two reasons:
 *
 *	1) The first field of the Sg_desc struct is a program header
 *		entry. ELF32_Phdr and ELF64_Phdr have the same fields,
 *		but their order is different. Use of a macro allows us
 *		to handle this transparently.
 *	2) Most of the fields in the Sg_desc entries are set to 0.
 *		Use of a macro allows us to hide the clutter.
 */
#ifdef _ELF64
#define	SG_DESC_INIT(id, p_type, p_flags, sg_name, sg_flags) \
	{ id, { p_type, p_flags, 0, 0, 0, 0, 0, 0}, \
	    sg_name, 0, 0, NULL, NULL, sg_flags, NULL, 0, NULL}
#else
#define	SG_DESC_INIT(id, p_type, p_flags, sg_name, sg_flags) \
	{ id, { p_type, 0, 0, 0, 0, 0, p_flags, 0}, \
	    sg_name, 0, 0, NULL, NULL, sg_flags, NULL, 0, NULL}
#endif

static const Sg_desc sg_desc[LD_NUM] = {
	SG_DESC_INIT(LD_PHDR, PT_PHDR, PF_R + PF_X, MSG_ORIG(MSG_ENT_PHDR),
	    (FLG_SG_TYPE | FLG_SG_FLAGS)),

	SG_DESC_INIT(LD_INTERP, PT_INTERP, PF_R, MSG_ORIG(MSG_ENT_INTERP),
	    (FLG_SG_TYPE | FLG_SG_FLAGS)),

	SG_DESC_INIT(LD_SUNWCAP, PT_SUNWCAP, PF_R, MSG_ORIG(MSG_ENT_SUNWCAP),
	    (FLG_SG_TYPE | FLG_SG_FLAGS)),

	SG_DESC_INIT(LD_TEXT, PT_LOAD, PF_R + PF_X, MSG_ORIG(MSG_ENT_TEXT),
	    (FLG_SG_TYPE | FLG_SG_FLAGS)),

	SG_DESC_INIT(LD_DATA, PT_LOAD, 0, MSG_ORIG(MSG_ENT_DATA),
	    (FLG_SG_TYPE | FLG_SG_FLAGS)),

	SG_DESC_INIT(LD_BSS, PT_LOAD, 0, MSG_ORIG(MSG_ENT_BSS),
	    (FLG_SG_TYPE | FLG_SG_FLAGS | FLG_SG_DISABLED)),

#if	defined(_ELF64)
	/* (amd64-only) */
	SG_DESC_INIT(LD_LRODATA, PT_LOAD, PF_R, MSG_ORIG(MSG_ENT_LRODATA),
	    (FLG_SG_TYPE | FLG_SG_FLAGS)),

	/* (amd64-only) */
	SG_DESC_INIT(LD_LDATA, PT_LOAD, 0, MSG_ORIG(MSG_ENT_LDATA),
	    (FLG_SG_TYPE | FLG_SG_FLAGS)),
#endif
	SG_DESC_INIT(LD_DYN, PT_DYNAMIC, 0, MSG_ORIG(MSG_ENT_DYNAMIC),
	    (FLG_SG_TYPE | FLG_SG_FLAGS)),

	SG_DESC_INIT(LD_DTRACE, PT_SUNWDTRACE, 0, MSG_ORIG(MSG_ENT_DTRACE),
	    (FLG_SG_TYPE | FLG_SG_FLAGS)),

	SG_DESC_INIT(LD_TLS, PT_TLS, PF_R, MSG_ORIG(MSG_ENT_TLS),
	    (FLG_SG_TYPE | FLG_SG_FLAGS)),

	SG_DESC_INIT(LD_UNWIND, PT_SUNW_UNWIND, PF_R, MSG_ORIG(MSG_ENT_UNWIND),
	    (FLG_SG_TYPE | FLG_SG_FLAGS)),

	SG_DESC_INIT(LD_NOTE, PT_NOTE, 0, MSG_ORIG(MSG_ENT_NOTE), FLG_SG_TYPE),

	SG_DESC_INIT(LD_EXTRA, PT_NULL, 0, MSG_ORIG(MSG_ENT_EXTRA), FLG_SG_TYPE)
};

/*
 * The processing of input files by the link-editor involves matching the
 * files sections to an `entrance descriptor definition'.  The entrance
 * criteria can be modified further using a mapfile.  Each entrance criteria
 * is associated with a segment descriptor, thus a mapping of input sections
 * to output segments is maintained.
 *
 * Note the trick used for the ec_segment field, which is supposed to be a
 * pointer to a segment descriptor.  We initialize this with the index of the
 * descriptor, and then turn it into an actual pointer at runtime, once memory
 * has been allocated and the templates copied.
 */
static const Ent_desc	ent_desc[] = {
	{NULL, MSG_ORIG(MSG_ENT_NOTE), SHT_NOTE, 0, 0,
		(Sg_desc *)LD_NOTE, 0, FLG_EC_BUILTIN},

#if	defined(_ELF64)		/* (amd64-only) */
	{NULL, MSG_ORIG(MSG_ENT_LRODATA), NULL,
		SHF_ALLOC + SHF_WRITE + SHF_AMD64_LARGE,
		SHF_ALLOC + SHF_AMD64_LARGE,
		(Sg_desc *)LD_LRODATA, 0, FLG_EC_BUILTIN},
#endif
	{NULL, MSG_ORIG(MSG_ENT_TEXT), NULL,
		SHF_ALLOC + SHF_WRITE, SHF_ALLOC,
		(Sg_desc *)LD_TEXT, 0, FLG_EC_BUILTIN},

	{NULL, MSG_ORIG(MSG_ENT_BSS), SHT_NOBITS,
		SHF_ALLOC + SHF_WRITE, SHF_ALLOC + SHF_WRITE,
		(Sg_desc *)LD_BSS, 0, FLG_EC_BUILTIN},

#if	defined(_ELF64)		/* (amd64-only) */
	{NULL, MSG_ORIG(MSG_ENT_LBSS), SHT_NOBITS,
		SHF_ALLOC + SHF_WRITE + SHF_AMD64_LARGE,
		SHF_ALLOC + SHF_WRITE + SHF_AMD64_LARGE,
		(Sg_desc *)LD_DATA, 0, FLG_EC_BUILTIN},

	{NULL, MSG_ORIG(MSG_ENT_LDATA), NULL,
		SHF_ALLOC + SHF_WRITE + SHF_AMD64_LARGE,
		SHF_ALLOC + SHF_WRITE + SHF_AMD64_LARGE,
		(Sg_desc *)LD_LDATA, 0, FLG_EC_BUILTIN},
#endif
	{NULL, MSG_ORIG(MSG_ENT_DATA), NULL,
		SHF_ALLOC + SHF_WRITE, SHF_ALLOC + SHF_WRITE,
		(Sg_desc *)LD_DATA, 0, FLG_EC_BUILTIN},

	{NULL, MSG_ORIG(MSG_ENT_EXTRA), 0, 0, 0,
		(Sg_desc *)LD_EXTRA, 0, FLG_EC_BUILTIN}
};

/*
 * Initialize new entrance and segment descriptors and add them as lists to
 * the output file descriptor.
 */
uintptr_t
ld_ent_setup(Ofl_desc *ofl, Xword segalign)
{
	const Ent_desc	*oenp;
	Sg_desc		*sgp;
	size_t		idx;

	/*
	 * Initialize the elf library.
	 */
	if (elf_version(EV_CURRENT) == EV_NONE) {
		eprintf(ofl->ofl_lml, ERR_FATAL, MSG_INTL(MSG_ELF_LIBELF),
		    EV_CURRENT);
		return (S_ERROR);
	}

	/*
	 * Initialize internal Global Symbol Table AVL tree
	 */
	avl_create(&ofl->ofl_symavl, &ld_sym_avl_comp, sizeof (Sym_avlnode),
	    SGSOFFSETOF(Sym_avlnode, sav_node));

	/*
	 * Allocate and initialize writable copies of both the entrance and
	 * segment descriptors.
	 *
	 * Note that on non-amd64 targets, this allocates a few more
	 * elements than are needed. For now, we are willing to overallocate
	 * a small amount to simplify the code.
	 */
	if ((sgp = libld_malloc(sizeof (sg_desc))) == NULL)
		return (S_ERROR);
	(void) memcpy(sgp, sg_desc, sizeof (sg_desc));

	/*
	 * The data segment permissions can differ:
	 *
	 *	- Architecural/ABI per-platform differences
	 *	- Whether the object is built statically or dynamically
	 *
	 * Those segments so affected have their program header flags
	 * set here at runtime, rather than in the sg_desc templates above.
	 */
	sgp[LD_DATA].sg_phdr.p_flags = ld_targ.t_m.m_dataseg_perm;
	sgp[LD_BSS].sg_phdr.p_flags = ld_targ.t_m.m_dataseg_perm;
	sgp[LD_DYN].sg_phdr.p_flags = ld_targ.t_m.m_dataseg_perm;
	sgp[LD_DTRACE].sg_phdr.p_flags = ld_targ.t_m.m_dataseg_perm;
#if	defined(_ELF64)
	sgp[LD_LDATA].sg_phdr.p_flags = ld_targ.t_m.m_dataseg_perm;
	sgp[LD_DTRACE].sg_phdr.p_flags |= PF_X;
#endif
	if ((ofl->ofl_flags & FLG_OF_DYNAMIC) == 0)
		sgp[LD_DATA].sg_phdr.p_flags |= PF_X;

	/*
	 * Traverse the new entrance descriptor list converting the segment
	 * pointer entries to the absolute address within the new segment
	 * descriptor list.  Add each entrance descriptor to the output file
	 * list.
	 */
	for (idx = 0, oenp = ent_desc;
	    idx < (sizeof (ent_desc) / sizeof (ent_desc[0])); idx++, oenp++) {
		Ent_desc	*nenp;

#if	defined(_ELF64)
		/* Don't use the amd64 entry conditions for non-amd64 targets */
		if ((oenp->ec_attrmask & SHF_AMD64_LARGE) &&
		    (ld_targ.t_m.m_mach != EM_AMD64))
			continue;
#endif
		if ((nenp = alist_append(&(ofl->ofl_ents), oenp,
		    sizeof (Ent_desc), AL_CNT_OFL_ENTRANCE)) == NULL)
			return (S_ERROR);

		nenp->ec_segment = &sgp[(long)oenp->ec_segment];
	}

	/*
	 * Traverse the new segment descriptor list adding each entry to the
	 * segment descriptor list.  For each loadable segment initialize
	 * a default alignment (ld(1) and ld.so.1 initialize this differently).
	 */
	for (idx = 0; idx < LD_NUM; idx++, sgp++) {
		Phdr	*phdr = &(sgp->sg_phdr);

#if	defined(_ELF64)
		/* Ignore amd64 segment templates for non-amd64 targets */
		switch (idx) {
		case LD_LRODATA:
		case LD_LDATA:
			if ((ld_targ.t_m.m_mach != EM_AMD64))
				continue;
		}
#endif
		if ((aplist_append(&ofl->ofl_segs, sgp,
		    AL_CNT_SEGMENTS)) == NULL)
			return (S_ERROR);
		if (phdr->p_type == PT_LOAD)
			phdr->p_align = segalign;
	}

	return (1);
}
