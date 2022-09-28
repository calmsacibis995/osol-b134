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

/*
 * Map file parsing and input section to output segment mapping.
 */
#include	<stdio.h>
#include	<string.h>
#include	<debug.h>
#include	"msg.h"
#include	"_libld.h"

/*
 * Each time a section is placed, the function set_addralign()
 * is called.  This function performs:
 *
 *  .	if the section is from an external file, check if this is empty or not.
 *	If not, we know the segment this section will belong needs a program
 *	header. (Of course, the program is needed only if this section falls
 *	into a loadable segment.)
 *  .	compute the Least Common Multiplier for setting the segment alignment.
 */
static void
set_addralign(Ofl_desc *ofl, Os_desc *osp, Is_desc *isp)
{
	Shdr	*shdr = isp->is_shdr;

	/* A discarded section has no influence on the output */
	if (isp->is_flags & FLG_IS_DISCARD)
		return;

	/*
	 * If this section has data or will be assigned data
	 * later, mark this segment not-empty.
	 */
	if ((shdr->sh_size != 0) ||
	    ((isp->is_flags & FLG_IS_EXTERNAL) == 0))
		osp->os_sgdesc->sg_flags |= FLG_SG_PHREQ;

	if ((ofl->ofl_dtflags_1 & DF_1_NOHDR) &&
	    (osp->os_sgdesc->sg_phdr).p_type != PT_LOAD)
		return;

	osp->os_sgdesc->sg_addralign =
	    ld_lcm(osp->os_sgdesc->sg_addralign, shdr->sh_addralign);
}

/*
 * Return the first input descriptor for a given output descriptor,
 * or NULL if there are none.
 */

Is_desc *
ld_os_first_isdesc(Os_desc *osp)
{
	int i;

	for (i = 0; i < OS_ISD_NUM; i++) {
		APlist *ap_isdesc = osp->os_isdescs[i];

		if (aplist_nitems(ap_isdesc) > 0)
			return ((Is_desc *)ap_isdesc->apl_data[0]);
	}

	return (NULL);
}

/*
 * Attach an input section to an output section
 *
 * entry:
 *	ofl - File descriptor
 *	osp - Output section descriptor
 *	isp - Input section descriptor
 *	mapfile_sort - True (1) if segment supports mapfile specified ordering
 *		of otherwise unordered input sections, and False (0) otherwise.
 *
 * exit:
 *	- The input section has been attached to the output section
 *	- If the input section is a candidate for string table merging,
 *		then it is appended to the output section's list of merge
 *		candidates (os_mstridescs).
 *
 *	On success, returns True (1). On failure, False (0).
 */
static int
os_attach_isp(Ofl_desc *ofl, Os_desc *osp, Is_desc *isp, int mapfile_sort)
{
	Aliste	init_arritems;
	int	os_isdescs_idx, do_append = 1;

	if ((isp->is_flags & FLG_IS_ORDERED) == 0) {
		init_arritems = AL_CNT_OS_ISDESCS;
		os_isdescs_idx = OS_ISD_DEFAULT;

		/*
		 * If section ordering was specified for an unordered section
		 * via the mapfile, then search in the OS_ISD_DEFAULT list
		 * and insert it in the specified position. Ordered sections
		 * are placed in ascending order before unordered sections
		 * (sections with an is_ordndx value of zero).
		 *
		 * If no mapfile ordering was specified, we append it in
		 * the usual way below.
		 */
		if (mapfile_sort && (isp->is_ordndx > 0)) {
			APlist *ap_isdesc = osp->os_isdescs[OS_ISD_DEFAULT];
			Aliste	idx2;
			Is_desc	*isp2;

			for (APLIST_TRAVERSE(ap_isdesc, idx2, isp2)) {
				if (isp2->is_ordndx &&
				    (isp2->is_ordndx <= isp->is_ordndx))
						continue;

				if (aplist_insert(
				    &osp->os_isdescs[OS_ISD_DEFAULT],
				    isp, init_arritems, idx2) == NULL)
					return (0);
				do_append = 0;
				break;
			}
		}
	} else {		/* Ordered section (via shdr flags) */
		Word shndx;

		/* SHF_ORDERED uses sh_info, SHF_LINK_ORDERED uses sh_link */
		shndx = (isp->is_shdr->sh_flags & SHF_ORDERED) ?
		    isp->is_shdr->sh_info : isp->is_shdr->sh_link;

		if (shndx == SHN_BEFORE) {
			init_arritems = AL_CNT_OS_ISDESCS_BA;
			os_isdescs_idx = OS_ISD_BEFORE;
		} else if (shndx == SHN_AFTER) {
			init_arritems = AL_CNT_OS_ISDESCS_BA;
			os_isdescs_idx = OS_ISD_AFTER;
		} else {
			init_arritems = AL_CNT_OS_ISDESCS;
			os_isdescs_idx = OS_ISD_ORDERED;
		}
	}

	/*
	 * If we didn't insert a section into the default list using
	 * mapfile specified ordering above, then append the input
	 * section to the appropriate list.
	 */
	if (do_append && aplist_append(&(osp->os_isdescs[os_isdescs_idx]),
	    isp, init_arritems) == NULL)
		return (0);
	isp->is_osdesc = osp;

	/*
	 * A section can be merged if the following are true:
	 * -	The SHF_MERGE|SHF_STRINGS flags must be set
	 * -	String table compression must not be disabled (-znocompstrtab)
	 * -	Mapfile ordering must not have been used.
	 * -	The section must not be ordered via section header flags.
	 * -	It must not be the generated section being built to
	 *	replace the sections on this list.
	 */
	if (((isp->is_shdr->sh_flags & (SHF_MERGE | SHF_STRINGS)) !=
	    (SHF_MERGE | SHF_STRINGS)) ||
	    ((ofl->ofl_flags1 & FLG_OF1_NCSTTAB) != 0) ||
	    !do_append ||
	    ((isp->is_flags & (FLG_IS_ORDERED | FLG_IS_GNSTRMRG)) != 0))
		return (1);

	/*
	 * Skip sections with (sh_entsize > 1) or (sh_addralign > 1).
	 *
	 * sh_entsize:
	 *	We are currently only able to merge string tables containing
	 *	strings with 1-byte (char) characters. Support for wide
	 *	characters will require our string table compression code
	 *	to be extended to handle larger character sizes.
	 *
	 * sh_addralign:
	 *	Alignments greater than 1 would require our string table
	 *	compression code to insert null bytes to move each
	 *	string to the required alignment.
	 */
	if ((isp->is_shdr->sh_entsize > 1) ||
	    (isp->is_shdr->sh_addralign > 1)) {
		DBG_CALL(Dbg_sec_unsup_strmerge(ofl->ofl_lml, isp));
		return (1);
	}

	if (aplist_append(&osp->os_mstrisdescs, isp,
	    AL_CNT_OS_MSTRISDESCS) == NULL)
		return (0);

	/*
	 * The SHF_MERGE|SHF_STRINGS flags tell us that the program that
	 * created the section intended it to be mergeable. The
	 * FLG_IS_INSTRMRG flag says that we have done validity testing
	 * and decided that it is safe to act on that hint.
	 */
	isp->is_flags |= FLG_IS_INSTRMRG;

	return (1);
}

/*
 * Determine whether this input COMDAT section already exists for the associated
 * output section.  If so, then discard this input section.  Otherwise, this
 * must be the first COMDAT section, thus it is kept for future comparisons.
 */
static uintptr_t
add_comdat(Ofl_desc *ofl, Os_desc *osp, Is_desc *isp)
{
	Isd_node	isd, *isdp;
	avl_tree_t	*avlt;
	avl_index_t	where;

	/*
	 * Create a COMDAT avl tree for this output section if required.
	 */
	if ((avlt = osp->os_comdats) == NULL) {
		if ((avlt = libld_calloc(sizeof (avl_tree_t), 1)) == NULL)
			return (S_ERROR);
		avl_create(avlt, isdavl_compare, sizeof (Isd_node),
		    SGSOFFSETOF(Isd_node, isd_avl));
		osp->os_comdats = avlt;
	}

	/*
	 * A standard COMDAT section uses the section name as search key.
	 */
	isd.isd_name = isp->is_name;
	isd.isd_hash = sgs_str_hash(isd.isd_name);

	if ((isdp = avl_find(avlt, &isd, &where)) != NULL) {
		isp->is_osdesc = osp;

		/*
		 * If this section hasn't already been identified as discarded,
		 * generate a suitable diagnostic.
		 */
		if ((isp->is_flags & FLG_IS_DISCARD) == 0) {
			isp->is_flags |= FLG_IS_DISCARD;
			isp->is_comdatkeep = isdp->isd_isp;
			DBG_CALL(Dbg_sec_discarded(ofl->ofl_lml, isp,
			    isdp->isd_isp));
		}

		/*
		 * A discarded section does not require assignment to an output
		 * section.  However, if relaxed relocations have been enabled
		 * (either from -z relaxreloc, or asserted with .gnu.linkonce
		 * processing), then this section must still be assigned to an
		 * output section so that the sloppy relocation logic will have
		 * the information necessary to do its work.
		 */
		return (0);
	}

	/*
	 * This is a new COMDAT section - so keep it.
	 */
	if ((isdp = libld_calloc(sizeof (Isd_node), 1)) == NULL)
		return (S_ERROR);

	isdp->isd_name = isd.isd_name;
	isdp->isd_hash = isd.isd_hash;
	isdp->isd_isp = isp;

	avl_insert(avlt, isdp, where);
	return (1);
}

/*
 * Determine whether a GNU group COMDAT section name follows the convention
 *
 *	section-name.symbol-name
 *
 * Each section within the input file is compared to see if the full section
 * name matches the beginning of the COMDAT section, with a following '.'.
 * A pointer to the symbol name, starting with the '.' is returned so that the
 * caller can strip off the required section name.
 */
static char *
gnu_comdat_sym(Ifl_desc *ifl, Is_desc *gisp)
{
	size_t	ndx;

	for (ndx = 1; ndx < ifl->ifl_shnum; ndx++) {
		Is_desc	*isp;
		size_t	ssize;

		if (((isp = ifl->ifl_isdesc[ndx]) == NULL) ||
		    (isp == gisp) || (isp->is_name == NULL))
			continue;

		/*
		 * It's questionable whether this size should be cached in the
		 * Is_desc.  However, this seems an infrequent operation and
		 * adding Is_desc members can escalate memory usage for large
		 * link-edits.  For now, size the section name dynamically.
		 */
		ssize = strlen(isp->is_name);
		if ((strncmp(isp->is_name, gisp->is_name, ssize) != 0) &&
		    (gisp->is_name[ssize] == '.'))
			return ((char *)&gisp->is_name[ssize]);
	}
	return (NULL);
}

/*
 * GNU .gnu.linkonce sections follow a naming convention that indicates the
 * required association with an output section.  Determine whether this input
 * section follows the convention, and if so return the appropriate output
 * section name.
 *
 *	.gnu.linkonce.b.*    ->	.bss
 *	.gnu.linkonce.d.*    ->	.data
 *	.gnu.linkonce.l.*    ->	.ldata
 *	.gnu.linkonce.lb.*   ->	.lbss
 *	.gnu.linkonce.lr.*   ->	.lrodata
 *	.gnu.linkonce.r.*    ->	.rodata
 *	.gnu.linkonce.s.*    ->	.sdata
 *	.gnu.linkonce.s2.*   ->	.sdata2
 *	.gnu.linkonce.sb.*   ->	.sbss
 *	.gnu.linkonce.sb2.*  ->	.sbss2
 *	.gnu.linkonce.t.*    ->	.text
 *	.gnu.linkonce.tb.*   ->	.tbss
 *	.gnu.linkonce.td.*   ->	.tdata
 *	.gnu.linkonce.wi.*   ->	.debug_info
 */
#define	NSTR_CH1(ch) (*(nstr + 1) == (ch))
#define	NSTR_CH2(ch) (*(nstr + 2) == (ch))
#define	NSTR_CH3(ch) (*(nstr + 3) == (ch))

static const char *
gnu_linkonce_sec(const char *ostr)
{
	const char	*nstr = &ostr[MSG_SCN_GNU_LINKONCE_SIZE];

	switch (*nstr) {
	case 'b':
		if (NSTR_CH1('.'))
			return (MSG_ORIG(MSG_SCN_BSS));
		break;
	case 'd':
		if (NSTR_CH1('.'))
			return (MSG_ORIG(MSG_SCN_DATA));
		break;
	case 'l':
		if (NSTR_CH1('.'))
			return (MSG_ORIG(MSG_SCN_LDATA));
		else if (NSTR_CH1('b') && NSTR_CH2('.'))
			return (MSG_ORIG(MSG_SCN_LBSS));
		else if (NSTR_CH1('r') && NSTR_CH2('.'))
			return (MSG_ORIG(MSG_SCN_LRODATA));
		break;
	case 'r':
		if (NSTR_CH1('.'))
			return (MSG_ORIG(MSG_SCN_RODATA));
		break;
	case 's':
		if (NSTR_CH1('.'))
			return (MSG_ORIG(MSG_SCN_SDATA));
		else if (NSTR_CH1('2') && NSTR_CH2('.'))
			return (MSG_ORIG(MSG_SCN_SDATA2));
		else if (NSTR_CH1('b') && NSTR_CH2('.'))
			return (MSG_ORIG(MSG_SCN_SBSS));
		else if (NSTR_CH1('b') && NSTR_CH2('2') && NSTR_CH3('.'))
			return (MSG_ORIG(MSG_SCN_SBSS2));
		break;
	case 't':
		if (NSTR_CH1('.'))
			return (MSG_ORIG(MSG_SCN_TEXT));
		else if (NSTR_CH1('b') && NSTR_CH2('.'))
			return (MSG_ORIG(MSG_SCN_TBSS));
		else if (NSTR_CH1('d') && NSTR_CH2('.'))
			return (MSG_ORIG(MSG_SCN_TDATA));
		break;
	case 'w':
		if (NSTR_CH1('i') && NSTR_CH2('.'))
			return (MSG_ORIG(MSG_SCN_DEBUG_INFO));
		break;
	default:
		break;
	}

	/*
	 * No special name match found.
	 */
	return (ostr);
}
#undef	NSTR_CH1
#undef	NSTR_CH2
#undef	NSTR_CH3

/*
 * Place a section into the appropriate segment and output section.
 *
 * entry:
 *	ofl - File descriptor
 *	isp - Input section descriptor of section to be placed.
 *	ident - Section identifier, used to order sections relative to
 *		others within the output segment.
 *	alt_os_name - If non-NULL, the name of the output section to place
 *		isp into. If NULL, input sections go to an output section
 *		with the same name as the input section.
 */
Os_desc *
ld_place_section(Ofl_desc *ofl, Is_desc *isp, int ident,
    const char *alt_os_name)
{
	Ent_desc	*enp;
	Sg_desc		*sgp;
	Os_desc		*osp;
	Aliste		idx1, iidx;
	int		os_ndx;
	Shdr		*shdr = isp->is_shdr;
	Xword		shflagmask, shflags = shdr->sh_flags;
	Ifl_desc	*ifl = isp->is_file;
	char		*oname, *sname;
	uint_t		onamehash;

	/*
	 * Define any sections that must be thought of as referenced.  These
	 * sections may not be referenced externaly in a manner ld(1) can
	 * discover, but they must be retained (ie. not removed by -zignore).
	 */
	static const Msg RefSecs[] = {
		MSG_SCN_INIT,		/* MSG_ORIG(MSG_SCN_INIT) */
		MSG_SCN_FINI,		/* MSG_ORIG(MSG_SCN_FINI) */
		MSG_SCN_EX_RANGES,	/* MSG_ORIG(MSG_SCN_EX_RANGES) */
		MSG_SCN_EX_SHARED,	/* MSG_ORIG(MSG_SCN_EX_SHARED) */
		MSG_SCN_CTORS,		/* MSG_ORIG(MSG_SCN_CTORS) */
		MSG_SCN_DTORS,		/* MSG_ORIG(MSG_SCN_DTORS) */
		MSG_SCN_EHFRAME,	/* MSG_ORIG(MSG_SCN_EHFRAME) */
		MSG_SCN_EHFRAME_HDR,	/* MSG_ORIG(MSG_SCN_EHFRAME_HDR) */
		MSG_SCN_JCR,		/* MSG_ORIG(MSG_SCN_JCR) */
		0
	};

	DBG_CALL(Dbg_sec_in(ofl->ofl_lml, isp));

	/*
	 * If this section identfies group members, or this section indicates
	 * that it is a member of a group, determine whether the section is
	 * still required.
	 */
	if ((shflags & SHF_GROUP) || (shdr->sh_type == SHT_GROUP)) {
		Group_desc	*gdesc;

		if ((gdesc = ld_get_group(ofl, isp)) != NULL) {
			DBG_CALL(Dbg_sec_group(ofl->ofl_lml, isp, gdesc));

			/*
			 * If this group has been replaced by another group,
			 * then this section needs to be discarded.
			 */
			if (gdesc->gd_oisc) {
				isp->is_flags |= FLG_IS_DISCARD;

				/*
				 * Since we're discarding the section, we
				 * can skip assigning it to an output section.
				 * The exception is that if the user
				 * specifies -z relaxreloc, then
				 * we need to assign the output section so
				 * that the sloppy relocation logic will have
				 * the information necessary to do its work.
				 */
				if (!(ofl->ofl_flags1 & FLG_OF1_RLXREL))
					return (NULL);
			}
		}

		/*
		 * SHT_GROUP sections can only be included into relocatable
		 * objects.
		 */
		if (shdr->sh_type == SHT_GROUP) {
			if ((ofl->ofl_flags & FLG_OF_RELOBJ) == 0) {
				isp->is_flags |= FLG_IS_DISCARD;
				return (NULL);
			}
		}
	}

	/*
	 * Always assign SHF_TLS sections to the DATA segment (and then the
	 * PT_TLS embedded inside of there).
	 */
	if (shflags & SHF_TLS)
		shflags |= SHF_WRITE;

	/*
	 * Traverse the entrance criteria list searching for a segment that
	 * matches the input section we have.  If an entrance criterion is set
	 * then there must be an exact match.  If we complete the loop without
	 * finding a segment, then sgp will be NULL.
	 */
	sgp = NULL;
	for (ALIST_TRAVERSE(ofl->ofl_ents, idx1, enp)) {
		if (enp->ec_segment &&
		    (enp->ec_segment->sg_flags & FLG_SG_DISABLED))
			continue;
		if (enp->ec_type && (enp->ec_type != shdr->sh_type))
			continue;
		if (enp->ec_attrmask &&
		    /* LINTED */
		    (enp->ec_attrmask & enp->ec_attrbits) !=
		    (enp->ec_attrmask & shflags))
			continue;
		if (((enp->ec_flags & FLG_EC_BUILTIN) == 0) &&
		    enp->ec_name && (strcmp(enp->ec_name, isp->is_name) != 0))
			continue;
		if (enp->ec_files) {
			Aliste	idx2;
			char	*file;
			int	found = 0;

			if (isp->is_file == NULL)
				continue;

			for (APLIST_TRAVERSE(enp->ec_files, idx2, file)) {
				const char	*name = isp->is_file->ifl_name;

				if (file[0] == '*') {
					const char	*basename;

					basename = strrchr(name, '/');
					if (basename == NULL)
						basename = name;
					else if (basename[1] != '\0')
						basename++;

					if (strcmp(&file[1], basename) == 0) {
						found++;
						break;
					}
				} else {
					if (strcmp(file, name) == 0) {
						found++;
						break;
					}
				}
			}
			if (!found)
				continue;
		}
		sgp = enp->ec_segment;
		break;
	}

	if (sgp == NULL) {
		enp = alist_item(ofl->ofl_ents,
		    alist_nitems(ofl->ofl_ents) - 1);
		sgp = enp->ec_segment;
	}

	/*
	 * If our caller has supplied an alternative name for the output
	 * section, then we defer to their request. Otherwise, the default
	 * is to use the same name as that of the input section being placed.
	 *
	 * The COMDAT, SHT_GROUP and GNU name translations that follow have
	 * the potential to alter this initial name.
	 */
	oname = (char *)((alt_os_name == NULL) ? isp->is_name : alt_os_name);

	/*
	 * Solaris section names may follow the convention:
	 *
	 *	section-name%symbol-name
	 *
	 * This convention has been used to order the layout of sections within
	 * segments for objects built with the compilers -xF option.  However,
	 * the final object should not contain individual section headers for
	 * all such input sections, instead the symbol name is stripped from the
	 * name to establish the final output section name.
	 *
	 * This convention has also been followed for COMDAT and sections
	 * identified though SHT_GROUP data.
	 *
	 * Strip out the % from the section name in all cases except:
	 *
	 *    i.	when '-r' is used without '-M', and
	 *    ii.	when '-r' is used with '-M' but without the ?O flag.
	 */
	if (((ofl->ofl_flags & FLG_OF_RELOBJ) == 0) ||
	    (sgp->sg_flags & FLG_SG_ORDER)) {
		if ((sname = strchr(isp->is_name, '%')) != NULL) {
			size_t	size = sname - isp->is_name;

			if ((oname = libld_malloc(size + 1)) == NULL)
				return ((Os_desc *)S_ERROR);
			(void) strncpy(oname, isp->is_name, size);
			oname[size] = '\0';
			DBG_CALL(Dbg_sec_redirected(ofl->ofl_lml, isp, oname));
		}
		isp->is_ordndx = enp->ec_ordndx;
	}

	/*
	 * GNU section names may follow the convention:
	 *
	 *	.gnu.linkonce.*
	 *
	 * The .gnu.linkonce is a section naming convention that indicates a
	 * COMDAT requirement.  Determine whether this section follows the GNU
	 * pattern, and if so, determine whether this section should be
	 * discarded or retained.  The comparison of is_name[1] with 'g'
	 * is an optimization to skip using strncmp() too much. This is safe,
	 * because we know the name is not NULL, and therefore must have
	 * at least one character plus a NULL termination.
	 */
	if (((ofl->ofl_flags & FLG_OF_RELOBJ) == 0) &&
	    (isp->is_name == oname) && (isp->is_name[1] == 'g') &&
	    (strncmp(MSG_ORIG(MSG_SCN_GNU_LINKONCE), isp->is_name,
	    MSG_SCN_GNU_LINKONCE_SIZE) == 0)) {
		if ((oname =
		    (char *)gnu_linkonce_sec(isp->is_name)) != isp->is_name) {
			DBG_CALL(Dbg_sec_redirected(ofl->ofl_lml, isp, oname));
		}

		/*
		 * Explicitly identify this section type as COMDAT.  Also,
		 * enable relaxed relocation processing, as this is typically
		 * a requirement with .gnu.linkonce sections.
		 */
		isp->is_flags |= FLG_IS_COMDAT;
		if ((ofl->ofl_flags1 & FLG_OF1_NRLXREL) == 0)
			ofl->ofl_flags1 |= FLG_OF1_RLXREL;
		Dbg_sec_gnu_comdat(ofl->ofl_lml, isp, TRUE,
		    (ofl->ofl_flags1 & FLG_OF1_RLXREL) != 0);
	}

	/*
	 * GNU section names may also follow the convention:
	 *
	 *	section-name.symbol-name
	 *
	 * This convention is used when defining SHT_GROUP sections of type
	 * COMDAT.  Thus, any group processing will have discovered any group
	 * sections, and this identification can be triggered by a pattern
	 * match section names.
	 */
	if (((ofl->ofl_flags & FLG_OF_RELOBJ) == 0) &&
	    (isp->is_name == oname) && (isp->is_flags & FLG_IS_COMDAT) &&
	    ((sname = gnu_comdat_sym(ifl, isp)) != NULL)) {
		size_t	size = sname - isp->is_name;

		if ((oname = libld_malloc(size + 1)) == NULL)
			return ((Os_desc *)S_ERROR);
		(void) strncpy(oname, isp->is_name, size);
		oname[size] = '\0';
		DBG_CALL(Dbg_sec_redirected(ofl->ofl_lml, isp, oname));

		/*
		 * Enable relaxed relocation processing, as this is
		 * typically a requirement with GNU COMDAT sections.
		 */
		if ((ofl->ofl_flags1 & FLG_OF1_NRLXREL) == 0) {
			ofl->ofl_flags1 |= FLG_OF1_RLXREL;
			Dbg_sec_gnu_comdat(ofl->ofl_lml, isp, FALSE, TRUE);
		}
	}

	/*
	 * Assign a hash value now that the output section name has been
	 * finalized.
	 */
	onamehash = sgs_str_hash(oname);

	if (sgp->sg_flags & FLG_SG_ORDER)
		enp->ec_flags |= FLG_EC_USED;

	/*
	 * Determine if section ordering is turned on. If so, return the
	 * appropriate ordering index for the section. This information
	 * is derived from the Sg_desc->sg_segorder list that was built
	 * up from the Mapfile.
	 */
	os_ndx = 0;
	if (sgp->sg_secorder) {
		Sec_order	*scop;

		for (APLIST_TRAVERSE(sgp->sg_secorder, idx1, scop)) {
			if (strcmp(scop->sco_secname, oname) == 0) {
				scop->sco_flags |= FLG_SGO_USED;
				os_ndx = scop->sco_index;
				break;
			}
		}
	}

	/*
	 * Mask of section header flags to ignore when matching sections. We
	 * are more strict with relocatable objects, ignoring only the order
	 * flags, and keeping sections apart if they differ otherwise. This
	 * follows the policy that sections in a relative object should only
	 * be merged if their flags are the same, and avoids destroying
	 * information prematurely. For final products however, we ignore all
	 * flags that do not prevent a merge.
	 */
	shflagmask =
	    (ofl->ofl_flags & FLG_OF_RELOBJ) ? ALL_SHF_ORDER : ALL_SHF_IGNORE;

	/*
	 * Traverse the input section list for the output section we have been
	 * assigned. If we find a matching section simply add this new section.
	 */
	iidx = 0;
	for (APLIST_TRAVERSE(sgp->sg_osdescs, idx1, osp)) {
		Shdr	*_shdr = osp->os_shdr;

		if ((ident == osp->os_identndx) &&
		    (ident != ld_targ.t_id.id_rel) &&
		    (onamehash == osp->os_namehash) &&
		    (shdr->sh_type != SHT_GROUP) &&
		    (shdr->sh_type != SHT_SUNW_dof) &&
		    ((shdr->sh_type == _shdr->sh_type) ||
		    ((shdr->sh_type == SHT_SUNW_COMDAT) &&
		    (_shdr->sh_type == SHT_PROGBITS))) &&
		    ((shflags & ~shflagmask) ==
		    (_shdr->sh_flags & ~shflagmask)) &&
		    (strcmp(oname, osp->os_name) == 0)) {
			uintptr_t	err;

			/*
			 * Process any COMDAT section, keeping the first and
			 * discarding all others.
			 */
			if ((isp->is_flags & FLG_IS_COMDAT) &&
			    ((err = add_comdat(ofl, osp, isp)) != 1))
				return ((Os_desc *)err);

			/*
			 * Set alignment
			 */
			set_addralign(ofl, osp, isp);

			/*
			 * If this section is a non-empty TLS section indicate
			 * that a PT_TLS program header is required.
			 */
			if ((shflags & SHF_TLS) && shdr->sh_size &&
			    ((ofl->ofl_flags & FLG_OF_RELOBJ) == 0))
				ofl->ofl_flags |= FLG_OF_TLSPHDR;

			/*
			 * Insert the input section descriptor on the proper
			 * output section descriptor list.
			 *
			 * If this segment requires input section ordering,
			 * honor any mapfile specified ordering for otherwise
			 * unordered sections by setting the mapfile_sort
			 * argument of os_attach_isp() to True.
			 */

			if (os_attach_isp(ofl, osp, isp,
			    (sgp->sg_flags & FLG_SG_ORDER) != 0) == 0)
				return ((Os_desc *)S_ERROR);

			/*
			 * If this input section and file is associated to an
			 * artificially referenced output section, make sure
			 * they are marked as referenced also. This ensures
			 * that this input section and file isn't eliminated
			 * when -zignore is in effect.
			 *
			 * See -zignore comments when creating a new output
			 * section below.
			 */
			if (((ifl &&
			    (ifl->ifl_flags & FLG_IF_IGNORE)) || DBG_ENABLED) &&
			    (osp->os_flags & FLG_OS_SECTREF)) {
				isp->is_flags |= FLG_IS_SECTREF;
				if (ifl)
					ifl->ifl_flags |= FLG_IF_FILEREF;
			}

			DBG_CALL(Dbg_sec_added(ofl->ofl_lml, osp, sgp));
			return (osp);
		}

		/*
		 * Do we need to worry about section ordering?
		 */
		if (os_ndx) {
			if (osp->os_ordndx) {
				if (os_ndx < osp->os_ordndx)
					/* insert section here. */
					break;
				else {
					iidx = idx1 + 1;
					continue;
				}
			} else {
				/* insert section here. */
				break;
			}
		} else if (osp->os_ordndx) {
			iidx = idx1 + 1;
			continue;
		}

		/*
		 * If the new sections identifier is less than that of the
		 * present input section we need to insert the new section
		 * at this point.
		 */
		if (ident < osp->os_identndx)
			break;

		iidx = idx1 + 1;
	}

	/*
	 * We are adding a new output section.  Update the section header
	 * count and associated string size.
	 *
	 * If the input section triggering this output section has been marked
	 * for discard, and if no other non-discarded input section comes along
	 * to join it, then we will over count. We cannot know if this will
	 * happen or not until all input is seen. Set FLG_OF_AJDOSCNT to
	 * trigger a final count readjustment.
	 */
	if (isp->is_flags & FLG_IS_DISCARD)
		ofl->ofl_flags |= FLG_OF_ADJOSCNT;
	ofl->ofl_shdrcnt++;
	if (st_insert(ofl->ofl_shdrsttab, oname) == -1)
		return ((Os_desc *)S_ERROR);

	/*
	 * Create a new output section descriptor.
	 */
	if ((osp = libld_calloc(sizeof (Os_desc), 1)) == NULL)
		return ((Os_desc *)S_ERROR);
	if ((osp->os_shdr = libld_calloc(sizeof (Shdr), 1)) == NULL)
		return ((Os_desc *)S_ERROR);

	/*
	 * Convert COMDAT section to PROGBITS as this the first section of the
	 * output section.  Save any COMDAT section for later processing, as
	 * additional COMDAT sections that match this section need discarding.
	 */
	if (shdr->sh_type == SHT_SUNW_COMDAT) {
		Shdr	*tshdr;

		if ((tshdr = libld_malloc(sizeof (Shdr))) == NULL)
			return ((Os_desc *)S_ERROR);
		*tshdr = *shdr;
		isp->is_shdr = shdr = tshdr;
		shdr->sh_type = SHT_PROGBITS;
	}
	if ((isp->is_flags & FLG_IS_COMDAT) &&
	    (add_comdat(ofl, osp, isp) == S_ERROR))
		return ((Os_desc *)S_ERROR);

	osp->os_shdr->sh_type = shdr->sh_type;
	osp->os_shdr->sh_flags = shdr->sh_flags;
	osp->os_shdr->sh_entsize = shdr->sh_entsize;
	osp->os_name = oname;
	osp->os_namehash = onamehash;
	osp->os_ordndx = os_ndx;
	osp->os_sgdesc = sgp;

	if (ifl && (shdr->sh_type == SHT_PROGBITS)) {
		/*
		 * Try to preserve the intended meaning of sh_link/sh_info.
		 * See the translate_link() in update.c.
		 */
		osp->os_shdr->sh_link = shdr->sh_link;
		if (shdr->sh_flags & SHF_INFO_LINK)
			osp->os_shdr->sh_info = shdr->sh_info;
	}

	/*
	 * When -zignore is in effect, user supplied sections and files that are
	 * not referenced from other sections, are eliminated from the object
	 * being produced.  Some sections, although unreferenced, are special,
	 * and must not be eliminated.  Determine if this new output section is
	 * one of those special sections, and if so mark it artificially as
	 * referenced.  Any input section and file associated to this output
	 * section is also be marked as referenced, and thus won't be eliminated
	 * from the final output.
	 */
	if (ifl && ((ofl->ofl_flags1 & FLG_OF1_IGNPRC) || DBG_ENABLED)) {
		const Msg	*refsec;

		for (refsec = RefSecs; *refsec; refsec++) {
			if (strcmp(osp->os_name, MSG_ORIG(*refsec)) == 0) {
				osp->os_flags |= FLG_OS_SECTREF;

				if ((ifl->ifl_flags & FLG_IF_IGNORE) ||
				    DBG_ENABLED) {
					isp->is_flags |= FLG_IS_SECTREF;
					ifl->ifl_flags |= FLG_IF_FILEREF;
				}
				break;
			}
		}
	}

	/*
	 * Sections of type SHT_GROUP are added to the ofl->ofl_osgroups list,
	 * so that they can be updated as a group later.
	 */
	if ((shdr->sh_type == SHT_GROUP) &&
	    (aplist_append(&ofl->ofl_osgroups, osp,
	    AL_CNT_OFL_OSGROUPS) == NULL))
		return ((Os_desc *)S_ERROR);

	/*
	 * If this section is a non-empty TLS section indicate that a PT_TLS
	 * program header is required.
	 */
	if ((shflags & SHF_TLS) && shdr->sh_size &&
	    ((ofl->ofl_flags & FLG_OF_RELOBJ) == 0))
		ofl->ofl_flags |= FLG_OF_TLSPHDR;

	/*
	 * If a non-allocatable section is going to be put into a loadable
	 * segment then turn on the allocate bit for this section and warn the
	 * user that we have done so.  This could only happen through the use
	 * of a mapfile.
	 */
	if ((sgp->sg_phdr.p_type == PT_LOAD) &&
	    ((osp->os_shdr->sh_flags & SHF_ALLOC) == 0)) {
		eprintf(ofl->ofl_lml, ERR_WARNING, MSG_INTL(MSG_SCN_NONALLOC),
		    ofl->ofl_name, osp->os_name);
		osp->os_shdr->sh_flags |= SHF_ALLOC;
	}

	/*
	 * Retain this sections identifier for future comparisons when placing
	 * a section (after all sections have been processed this variable will
	 * be used to hold the sections symbol index as we don't need to retain
	 * the identifier any more).
	 */
	osp->os_identndx = ident;

	/*
	 * Set alignment.
	 */
	set_addralign(ofl, osp, isp);

	if (os_attach_isp(ofl, osp, isp, 0) == 0)
		return ((Os_desc *)S_ERROR);

	DBG_CALL(Dbg_sec_created(ofl->ofl_lml, osp, sgp));

	/*
	 * Insert the new section at the offset given by iidx.  If no position
	 * for it was identified above, this will be index 0, causing the new
	 * section to be prepended to the beginning of the section list.
	 * Otherwise, it is the index following the section that was identified.
	 */
	if (aplist_insert(&sgp->sg_osdescs, osp, AL_CNT_SG_OSDESC,
	    iidx) == NULL)
		return ((Os_desc *)S_ERROR);
	return (osp);
}
