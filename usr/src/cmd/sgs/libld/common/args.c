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
 * Publicly available flags are defined in ld(1).   The following flags are
 * private, and may be removed at any time.
 *
 *    OPTION			MEANING
 *
 *    -z dtrace=symbol		assigns symbol to PT_SUNWDTRACE segment,
 *    				providing scratch area for dtrace processing.
 *
 *    -z noreloc		suppress relocation processing.  This provides
 *				a mechanism for validating kernel module symbol
 *				resolution that would normally incur fatal
 *				relocation errors.
 *
 *    -z rtldinfo=symbol	assigns symbol to SUNW_RTLDINF dynamic tag,
 *				providing pre-initialization specific routines
 *				for TLS initialization.
 *
 *    -z nointerp		suppress the addition of an interpreter
 *				section.  This is used to generate the kernel,
 *				but makes no sense to be used by anyone else.
 *
 *    -z norelaxreloc		suppress the automatic addition of relaxed
 *				relocations to GNU linkonce/COMDAT sections.
 *
 *    -z nosighandler		suppress the registration of the signal handler
 *				used to manage SIGBUS.
 */

/*
 * The following flags are committed, and will not be removed, but are
 * not publically documented, either because they are obsolete, or because
 * they exist to work around defects in other software and are not of
 * sufficient interest otherwise.
 *
 *    OPTION			MEANING
 *
 *    -Wl,...			compiler drivers and configuration tools
 *				have been known to pass this compiler option
 *				to ld(1).  Strip off the "-Wl," prefix and
 *			        process the remainder (...) as a normal option.
 */

#include	<sys/link.h>
#include	<stdio.h>
#include	<fcntl.h>
#include	<string.h>
#include	<errno.h>
#include	<elf.h>
#include	<unistd.h>
#include	<debug.h>
#include	"msg.h"
#include	"_libld.h"

/*
 * Define a set of local argument flags, the settings of these will be
 * verified in check_flags() and lead to the appropriate output file flags
 * being initialized.
 */
typedef	enum {
	SET_UNKNOWN = -1,
	SET_FALSE = 0,
	SET_TRUE = 1
} Setstate;

static Setstate	dflag	= SET_UNKNOWN;
static Setstate	zdflag	= SET_UNKNOWN;
static Setstate	Qflag	= SET_UNKNOWN;
static Setstate	Bdflag	= SET_UNKNOWN;

static Boolean	aflag	= FALSE;
static Boolean	bflag	= FALSE;
static Boolean	rflag	= FALSE;
static Boolean	sflag	= FALSE;
static Boolean	zinflag = FALSE;
static Boolean	zlflag	= FALSE;
static Boolean	Bgflag	= FALSE;
static Boolean	Blflag	= FALSE;
static Boolean	Beflag	= FALSE;
static Boolean	Bsflag	= FALSE;
static Boolean	Dflag	= FALSE;
static Boolean	Gflag	= FALSE;
static Boolean	Vflag	= FALSE;

/*
 * ztflag's state is set by pointing it to the matching string:
 *	text | textoff | textwarn
 */
static const char	*ztflag = 0;

static uintptr_t process_files_com(Ofl_desc *, int, char **);
static uintptr_t process_flags_com(Ofl_desc *, int, char **, int *);

/*
 * Print usage message to stderr - 2 modes, summary message only,
 * and full usage message.
 */
static void
usage_mesg(Boolean detail)
{
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_USAGE),
	    MSG_ORIG(MSG_STR_OPTIONS));

	if (detail == FALSE)
		return;

	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_3));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_6));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_A));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_B));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CBDR));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CBDY));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CBE));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CBG));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CBL));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CBR));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CBS));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_C));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CC));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_D));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CD));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_E));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_F));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CF));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CG));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_H));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_I));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CI));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_L));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CL));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_M));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CM));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CN));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_O));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_P));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CP));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CQ));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_R));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CR));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_S));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CS));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_T));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_U));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CV));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_CY));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZA));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZAE));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZAL));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZC));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZDFS));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZDRS));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZE));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZFA));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZGP));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZH));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZIG));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZINA));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZINI));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZINT));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZLAZY));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZLD32));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZLD64));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZLO));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZM));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZNC));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZNDFS));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZNDEF));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZNDEL));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZNDLO));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZNDU));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZNLD));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZNOW));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZNPA));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZNV));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZO));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZPIA));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZRL));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZRREL));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZRS));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZRSN));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZRSGRP));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZTARG));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZT));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZTO));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZTW));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZWRAP));
	(void) fprintf(stderr, MSG_INTL(MSG_ARG_DETAIL_ZV));
}

/*
 * Rescan the archives seen on the command line in order
 * to handle circularly dependent archives, stopping when
 * no further member extraction occurs.
 *
 * entry:
 *	ofl - Output file descriptor
 *	isgrp - True if this is a an archive group search, False
 *		to search starting with argv[1] through end_arg_ndx
 *	end_arg_ndx - Index of final argv element to consider.
 */
static uintptr_t
ld_rescan_archives(Ofl_desc *ofl, int isgrp, int end_arg_ndx)
{
	ofl->ofl_flags1 |= FLG_OF1_EXTRACT;

	while (ofl->ofl_flags1 & FLG_OF1_EXTRACT) {
		Aliste		idx;
		Ar_desc		*adp;
		Word		start_ndx = isgrp ? ofl->ofl_ars_gsndx : 0;
		Word		ndx = 0;

		ofl->ofl_flags1 &= ~FLG_OF1_EXTRACT;

		DBG_CALL(Dbg_file_ar_rescan(ofl->ofl_lml,
		    isgrp ? ofl->ofl_ars_gsandx : 1, end_arg_ndx));

		for (APLIST_TRAVERSE(ofl->ofl_ars, idx, adp)) {
			/* If not to starting index yet, skip it */
			if (ndx++ < start_ndx)
				continue;

			/*
			 * If this archive was processed with -z allextract,
			 * then all members have already been extracted.
			 */
			if (adp->ad_elf == NULL)
				continue;

			/*
			 * Reestablish any archive specific command line flags.
			 */
			ofl->ofl_flags1 &= ~MSK_OF1_ARCHIVE;
			ofl->ofl_flags1 |= (adp->ad_flags & MSK_OF1_ARCHIVE);

			/*
			 * Re-process the archive.  Note that a file descriptor
			 * is unnecessary, as the file is already available in
			 * memory.
			 */
			if (ld_process_archive(adp->ad_name, -1,
			    adp, ofl) == S_ERROR)
				return (S_ERROR);
			if (ofl->ofl_flags & FLG_OF_FATAL)
				return (1);
		}
	}

	return (1);
}

/*
 * Checks the command line option flags for consistency.
 */
static uintptr_t
check_flags(Ofl_desc * ofl, int argc)
{
	if (Plibpath && (Llibdir || Ulibdir)) {
		eprintf(ofl->ofl_lml, ERR_FATAL, MSG_INTL(MSG_ARG_YP),
		    Llibdir ? 'L' : 'U');
		ofl->ofl_flags |= FLG_OF_FATAL;
	}

	if (rflag) {
		if (dflag == SET_UNKNOWN)
			dflag = SET_FALSE;
		if (ofl->ofl_flags & FLG_OF_COMREL) {
			/*
			 * Combining relocations when building a relocatable
			 * object isn't allowed.  Warn the user, but proceed.
			 */
			eprintf(ofl->ofl_lml, ERR_WARNING,
			    MSG_INTL(MSG_MARG_INCOMP), MSG_INTL(MSG_MARG_REL),
			    MSG_ORIG(MSG_ARG_ZCOMBRELOC));
			ofl->ofl_flags &= ~FLG_OF_COMREL;
		}
		ofl->ofl_flags |= FLG_OF_RELOBJ;
	} else {
		/*
		 * If the user hasn't explicitly requested that relocations
		 * not be combined, combine them by default.
		 */
		if ((ofl->ofl_flags & FLG_OF_NOCOMREL) == 0)
			ofl->ofl_flags |= FLG_OF_COMREL;
	}

	if (zdflag == SET_TRUE)
		ofl->ofl_flags |= FLG_OF_NOUNDEF;

	if (zinflag)
		ofl->ofl_dtflags_1 |= DF_1_INTERPOSE;

	if (sflag)
		ofl->ofl_flags |= FLG_OF_STRIP;

	if (Qflag == SET_TRUE)
		ofl->ofl_flags |= FLG_OF_ADDVERS;

	if (Blflag)
		ofl->ofl_flags |= FLG_OF_AUTOLCL;

	if (Beflag)
		ofl->ofl_flags |= FLG_OF_AUTOELM;

	if (Blflag && Beflag) {
		eprintf(ofl->ofl_lml, ERR_FATAL, MSG_INTL(MSG_ARG_INCOMP),
		    MSG_ORIG(MSG_ARG_BELIMINATE), MSG_ORIG(MSG_ARG_BLOCAL));
		ofl->ofl_flags |= FLG_OF_FATAL;
	}

	if (ofl->ofl_interp && (ofl->ofl_flags1 & FLG_OF1_NOINTRP)) {
		eprintf(ofl->ofl_lml, ERR_FATAL, MSG_INTL(MSG_ARG_INCOMP),
		    MSG_ORIG(MSG_ARG_CI), MSG_ORIG(MSG_ARG_ZNOINTERP));
		ofl->ofl_flags |= FLG_OF_FATAL;
	}

	if ((ofl->ofl_flags1 & (FLG_OF1_NRLXREL | FLG_OF1_RLXREL)) ==
	    (FLG_OF1_NRLXREL | FLG_OF1_RLXREL)) {
		eprintf(ofl->ofl_lml, ERR_FATAL, MSG_INTL(MSG_ARG_INCOMP),
		    MSG_ORIG(MSG_ARG_ZRELAXRELOC),
		    MSG_ORIG(MSG_ARG_ZNORELAXRELOC));
		ofl->ofl_flags |= FLG_OF_FATAL;
	}

	if (ofl->ofl_filtees && !Gflag) {
		if (ofl->ofl_flags & FLG_OF_AUX) {
			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_MARG_ST_ONLYAVL),
			    MSG_INTL(MSG_MARG_FILTER_AUX));
		} else {
			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_MARG_ST_ONLYAVL),
			    MSG_INTL(MSG_MARG_FILTER));
		}
		ofl->ofl_flags |= FLG_OF_FATAL;
	}

	if (dflag != SET_FALSE) {
		/*
		 * Set -Bdynamic on by default, setting is rechecked as input
		 * files are processed.
		 */
		ofl->ofl_flags |=
		    (FLG_OF_DYNAMIC | FLG_OF_DYNLIBS | FLG_OF_PROCRED);

		if (aflag) {
			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_ARG_INCOMP), MSG_ORIG(MSG_ARG_DY),
			    MSG_ORIG(MSG_ARG_A));
			ofl->ofl_flags |= FLG_OF_FATAL;
		}

		if (bflag)
			ofl->ofl_flags |= FLG_OF_BFLAG;

		if (Bgflag == TRUE) {
			if (zdflag == SET_FALSE) {
				eprintf(ofl->ofl_lml, ERR_FATAL,
				    MSG_INTL(MSG_ARG_INCOMP),
				    MSG_ORIG(MSG_ARG_BGROUP),
				    MSG_ORIG(MSG_ARG_ZNODEF));
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
			ofl->ofl_dtflags_1 |= DF_1_GROUP;
			ofl->ofl_flags |= FLG_OF_NOUNDEF;
		}

		/*
		 * If the use of default library searching has been suppressed
		 * but no runpaths have been provided we're going to have a hard
		 * job running this object.
		 */
		if ((ofl->ofl_dtflags_1 & DF_1_NODEFLIB) && !ofl->ofl_rpath)
			eprintf(ofl->ofl_lml, ERR_WARNING,
			    MSG_INTL(MSG_ARG_NODEFLIB),
			    MSG_INTL(MSG_MARG_RPATH));

		/*
		 * By default, text relocation warnings are given when building
		 * an executable unless the -b flag is specified.  This option
		 * implies that unclean text can be created, so no warnings are
		 * generated unless specifically asked for.
		 */
		if ((ztflag == MSG_ORIG(MSG_ARG_ZTEXTOFF)) ||
		    ((ztflag == 0) && bflag))
			ofl->ofl_flags1 |= FLG_OF1_TEXTOFF;
		else if (ztflag == MSG_ORIG(MSG_ARG_ZTEXT))
			ofl->ofl_flags |= FLG_OF_PURETXT;

		if (Gflag || !rflag) {
			/*
			 * Create a dynamic object.  -Bdirect indicates that all
			 * references should be bound directly.  This also
			 * enables lazyloading.  Individual symbols can be
			 * bound directly (or not) using mapfiles and the
			 * DIRECT (NODIRECT) qualifier.  With this capability,
			 * each syminfo entry is tagged SYMINFO_FLG_DIRECTBIND.
			 * Prior to this per-symbol direct binding, runtime
			 * direct binding was controlled via the DF_1_DIRECT
			 * flag.  This flag affected all references from the
			 * object.  -Bdirect continues to set this flag, and
			 * thus provides a means of taking a newly built
			 * direct binding object back to older systems.
			 *
			 * NOTE, any use of per-symbol NODIRECT bindings, or
			 * -znodirect, will disable the creation of the
			 * DF_1_DIRECT flag.  Older runtime linkers do not
			 * have the capability to do per-symbol direct bindings.
			 */
			if (Bdflag == SET_TRUE) {
				ofl->ofl_dtflags_1 |= DF_1_DIRECT;
				ofl->ofl_flags1 |= FLG_OF1_LAZYLD;
				ofl->ofl_flags |= FLG_OF_SYMINFO;
			}

			/*
			 * -Bnodirect disables directly binding to any symbols
			 * exported from the object being created.  Individual
			 * references to external objects can still be affected
			 * by -zdirect or mapfile DIRECT directives.
			 */
			if (Bdflag == SET_FALSE) {
				ofl->ofl_flags1 |= (FLG_OF1_NDIRECT |
				    FLG_OF1_NGLBDIR | FLG_OF1_ALNODIR);
				ofl->ofl_flags |= FLG_OF_SYMINFO;
			}
		}

		if (!Gflag && !rflag) {
			/*
			 * Dynamically linked executable.
			 */
			ofl->ofl_flags |= FLG_OF_EXEC;

			if (zdflag != SET_FALSE)
				ofl->ofl_flags |= FLG_OF_NOUNDEF;

			if (Bsflag) {
				eprintf(ofl->ofl_lml, ERR_FATAL,
				    MSG_INTL(MSG_ARG_DY_INCOMP),
				    MSG_ORIG(MSG_ARG_BSYMBOLIC));
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
			if (ofl->ofl_soname) {
				eprintf(ofl->ofl_lml, ERR_FATAL,
				    MSG_INTL(MSG_MARG_DY_INCOMP),
				    MSG_INTL(MSG_MARG_SONAME));
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
		} else if (!rflag) {
			/*
			 * Shared library.
			 */
			ofl->ofl_flags |= FLG_OF_SHAROBJ;

			/*
			 * By default, print text relocation errors for
			 * executables but *not* for shared objects.
			 */
			if (ztflag == 0)
				ofl->ofl_flags1 |= FLG_OF1_TEXTOFF;

			if (Bsflag) {
				/*
				 * -Bsymbolic, and -Bnodirect make no sense.
				 */
				if (Bdflag == SET_FALSE) {
					eprintf(ofl->ofl_lml, ERR_FATAL,
					    MSG_INTL(MSG_ARG_INCOMP),
					    MSG_ORIG(MSG_ARG_BSYMBOLIC),
					    MSG_ORIG(MSG_ARG_BNODIRECT));
					ofl->ofl_flags |= FLG_OF_FATAL;
				}
				ofl->ofl_flags |= FLG_OF_SYMBOLIC;
				ofl->ofl_dtflags |= DF_SYMBOLIC;
			}
		} else {
			/*
			 * Dynamic relocatable object.
			 */
			if (ztflag == 0)
				ofl->ofl_flags1 |= FLG_OF1_TEXTOFF;

			if (ofl->ofl_interp) {
				eprintf(ofl->ofl_lml, ERR_FATAL,
				    MSG_INTL(MSG_MARG_INCOMP),
				    MSG_INTL(MSG_MARG_REL),
				    MSG_ORIG(MSG_ARG_CI));
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
		}
	} else {
		ofl->ofl_flags |= FLG_OF_STATIC;

		if (bflag) {
			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_ARG_ST_INCOMP), MSG_ORIG(MSG_ARG_B));
			ofl->ofl_flags |= FLG_OF_FATAL;
		}
		if (ofl->ofl_soname) {
			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_MARG_ST_INCOMP),
			    MSG_INTL(MSG_MARG_SONAME));
			ofl->ofl_flags |= FLG_OF_FATAL;
		}
		if (ofl->ofl_depaudit) {
			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_ARG_ST_INCOMP), MSG_ORIG(MSG_ARG_CP));
			ofl->ofl_flags |= FLG_OF_FATAL;
		}
		if (ofl->ofl_audit) {
			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_ARG_ST_INCOMP), MSG_ORIG(MSG_ARG_P));
			ofl->ofl_flags |= FLG_OF_FATAL;
		}
		if (ofl->ofl_config) {
			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_ARG_ST_INCOMP), MSG_ORIG(MSG_ARG_C));
			ofl->ofl_flags |= FLG_OF_FATAL;
		}
		if (ztflag) {
			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_ARG_ST_INCOMP),
			    MSG_ORIG(MSG_ARG_ZTEXTALL));
			ofl->ofl_flags |= FLG_OF_FATAL;
		}
		if (Gflag) {
			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_MARG_ST_INCOMP),
			    MSG_INTL(MSG_MARG_SO));
			ofl->ofl_flags |= FLG_OF_FATAL;
		}
		if (aflag && rflag) {
			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_MARG_INCOMP), MSG_ORIG(MSG_ARG_A),
			    MSG_INTL(MSG_MARG_REL));
			ofl->ofl_flags |= FLG_OF_FATAL;
		}

		if (rflag) {
			/*
			 * We can only strip the symbol table and string table
			 * if no output relocations will refer to them.
			 */
			if (sflag) {
				eprintf(ofl->ofl_lml, ERR_WARNING,
				    MSG_INTL(MSG_ARG_STRIP),
				    MSG_INTL(MSG_MARG_REL),
				    MSG_INTL(MSG_MARG_STRIP));
			}

			if (ztflag == 0)
				ofl->ofl_flags1 |= FLG_OF1_TEXTOFF;

			if (ofl->ofl_interp) {
				eprintf(ofl->ofl_lml, ERR_FATAL,
				    MSG_INTL(MSG_MARG_INCOMP),
				    MSG_INTL(MSG_MARG_REL),
				    MSG_ORIG(MSG_ARG_CI));
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
		} else {
			/*
			 * Static executable.
			 */
			ofl->ofl_flags |= FLG_OF_EXEC | FLG_OF_PROCRED;

			if (zdflag != SET_FALSE)
				ofl->ofl_flags |= FLG_OF_NOUNDEF;
		}
	}

	/*
	 * If the user didn't supply an output file name supply a default.
	 */
	if (ofl->ofl_name == NULL)
		ofl->ofl_name = MSG_ORIG(MSG_STR_AOUT);

	/*
	 * We set the entrance criteria after all input argument processing as
	 * it is only at this point we're sure what the output image will be
	 * (static or dynamic).
	 */
	if (ld_ent_setup(ofl, ld_targ.t_m.m_segm_align) == S_ERROR)
		return (S_ERROR);

	/*
	 * Does the host currently running the linker have the same
	 * byte order as the target for which the object is being produced?
	 * If not, set FLG_OF1_ENCDIFF so relocation code will know
	 * to check.
	 */
	if (_elf_sys_encoding() != ld_targ.t_m.m_data)
		ofl->ofl_flags1 |= FLG_OF1_ENCDIFF;

	/*
	 * If the target has special executable section filling requirements,
	 * register the fill function with libelf
	 */
	if (ld_targ.t_ff.ff_execfill != NULL)
		_elf_execfill(ld_targ.t_ff.ff_execfill);

	/*
	 * Initialize string tables.  Symbol definitions within mapfiles can
	 * result in the creation of input sections.
	 */
	if (ld_init_strings(ofl) == S_ERROR)
		return (S_ERROR);

	/*
	 * Process any mapfiles after establishing the entrance criteria as
	 * the user may be redefining or adding sections/segments.
	 */
	if (ofl->ofl_maps) {
		const char	*name;
		Aliste		idx;
		Is_desc		*isp;

		for (APLIST_TRAVERSE(ofl->ofl_maps, idx, name))
			if (ld_map_parse(name, ofl) == S_ERROR)
				return (S_ERROR);

		if (ofl->ofl_flags & FLG_OF_SEGSORT)
			if (ld_sort_seg_list(ofl) == S_ERROR)
				return (S_ERROR);

		/*
		 * Mapfiles may have been used to create symbol definitions
		 * with backing storage.  Although the backing storage is
		 * associated with an input section, the association of the
		 * section to an output section (and segment) is initially
		 * deferred.  Now that all mapfile processing is complete, any
		 * entrance criteria requirements have been processed, and
		 * these backing storage sections can be associated with the
		 * appropriate output section (and segment).
		 */
		if (ofl->ofl_maptext || ofl->ofl_mapdata)
			DBG_CALL(Dbg_sec_backing(ofl->ofl_lml));

		for (APLIST_TRAVERSE(ofl->ofl_maptext, idx, isp)) {
			if (ld_place_section(ofl, isp,
			    ld_targ.t_id.id_text, NULL) == (Os_desc *)S_ERROR)
				return (S_ERROR);
		}

		for (APLIST_TRAVERSE(ofl->ofl_mapdata, idx, isp)) {
			if (ld_place_section(ofl, isp,
			    ld_targ.t_id.id_data, NULL) == (Os_desc *)S_ERROR)
				return (S_ERROR);
		}
	}

	/*
	 * If a mapfile has been used to define a single symbolic scope of
	 * interfaces, -Bsymbolic is established.  This global setting goes
	 * beyond individual symbol protection, and ensures all relocations
	 * (even those that reference section symbols) are processed within
	 * the object being built.
	 */
	if (((ofl->ofl_flags &
	    (FLG_OF_MAPSYMB | FLG_OF_MAPGLOB)) == FLG_OF_MAPSYMB) &&
	    (ofl->ofl_flags & (FLG_OF_AUTOLCL | FLG_OF_AUTOELM))) {
		ofl->ofl_flags |= FLG_OF_SYMBOLIC;
		ofl->ofl_dtflags |= DF_SYMBOLIC;
	}

	/*
	 * If -zloadfltr is set, verify that filtering is in effect.  Filters
	 * are either established from the command line, and affect the whole
	 * object, or are set on a per-symbol basis from a mapfile.
	 */
	if (zlflag) {
		if ((ofl->ofl_filtees == NULL) && (ofl->ofl_dtsfltrs == NULL)) {
			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_ARG_NOFLTR),
			    MSG_ORIG(MSG_ARG_ZLOADFLTR));
			ofl->ofl_flags |= FLG_OF_FATAL;
		}
		ofl->ofl_dtflags_1 |= DF_1_LOADFLTR;
	}

	/*
	 * Check that we have something to work with. This check is carried out
	 * after mapfile processing as its possible a mapfile is being used to
	 * define symbols, in which case it would be sufficient to build the
	 * output file purely from the mapfile.
	 */
	if ((ofl->ofl_objscnt == 0) && (ofl->ofl_soscnt == 0)) {
		if ((Vflag ||
		    (Dflag && (dbg_desc->d_extra & DBG_E_HELP_EXIT))) &&
		    (argc == 2)) {
			ofl->ofl_flags1 |= FLG_OF1_DONE;
		} else {
			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_ARG_NOFILES));
			return (S_ERROR);
		}
	}
	return (1);
}

/*
 * Decompose the string pointed by optarg into argv[][] so that argv[][] can be
 * used as an argument to getopt().
 *
 * If the second argument 'error' is not 0, then this is called from the first
 * pass. Else this is called from the second pass.
 */
static uintptr_t
createargv(Ofl_desc *ofl, int *error)
{
	int		argc = 0, idx = 0, ooptind;
	uintptr_t	ret;
	char		**argv, *p0;

	/*
	 * The argument being examined is either:
	 *	ld32= 	or
	 *	ld64=
	 */
#if	defined(_LP64)
	if (optarg[2] == '3')
		return (0);
#else
	if (optarg[2] == '6')
		return (0);
#endif

	p0 = &optarg[5];

	/*
	 * Count the number of arguments.
	 */
	while (*p0) {
		/*
		 * Pointing at non-separator character.
		 */
		if (*p0 != ',') {
			argc++;
			while (*p0 && (*p0 != ','))
				p0++;
			continue;
		}

		/*
		 * Pointing at a separator character.
		 */
		if (*p0 == ',') {
			while (*p0 == ',')
				p0++;
			continue;
		}
	}

	if (argc == 0)
		return (0);

	/*
	 * Allocate argument vector.
	 */
	if ((p0 = (char *)strdup(&optarg[5])) == NULL)
		return (S_ERROR);
	if ((argv = libld_malloc((sizeof (char *)) * (argc + 1))) == NULL)
		return (S_ERROR);

	while (*p0) {
		char *p;

		/*
		 * Pointing at the beginning of non-separator character string.
		 */
		if (*p0 != ',') {
			p = p0;
			while (*p0 && (*p0 != ','))
				p0++;
			argv[idx++] = p;
			if (*p0) {
				*p0 = '\0';
				p0++;
			}
			continue;
		}

		/*
		 * Pointing at the beginining of separator character string.
		 */
		if (*p0 == ',') {
			while (*p0 == ',')
				p0++;
			continue;
		}
	}
	argv[idx] = 0;
	ooptind = optind;
	optind = 0;

	/*
	 * Dispatch to pass1 or pass2
	 */
	if (error)
		ret = process_flags_com(ofl, argc, argv, error);
	else
		ret = process_files_com(ofl, argc, argv);

	optind = ooptind;
	return (ret);
}

static int	optitle = 0;
/*
 * Parsing options pass1 for process_flags().
 */
static uintptr_t
parseopt_pass1(Ofl_desc *ofl, int argc, char **argv, int *error)
{
	int	c, ndx = optind;

	/*
	 * The -32, -64 and -ztarget options are special, in that we validate
	 * them, but otherwise ignore them. libld.so (this code) is called
	 * from the ld front end program. ld has already examined the
	 * arguments to determine the output class and machine type of the
	 * output object, as reflected in the version (32/64) of ld_main()
	 * that was called and the value of the 'mach' argument passed.
	 * By time execution reaches this point, these options have already
	 * been seen and acted on.
	 */
	while ((c = ld_getopt(ofl->ofl_lml, ndx, argc, argv)) != -1) {

		switch (c) {
		case '3':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));

			/*
			 * -32 is processed by ld to determine the output class.
			 * Here we sanity check the option incase some other
			 * -3* option is mistakenly passed to us.
			 */
			if (optarg[0] != '2') {
				eprintf(ofl->ofl_lml, ERR_FATAL,
				    MSG_INTL(MSG_ARG_ILLEGAL),
				    MSG_ORIG(MSG_ARG_3), optarg);
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
			continue;

		case '6':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));

			/*
			 * -64 is processed by ld to determine the output class.
			 * Here we sanity check the option incase some other
			 * -6* option is mistakenly passed to us.
			 */
			if (optarg[0] != '4') {
				eprintf(ofl->ofl_lml, ERR_FATAL,
				    MSG_INTL(MSG_ARG_ILLEGAL),
				    MSG_ORIG(MSG_ARG_6), optarg);
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
			continue;

		case 'a':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, NULL));
			aflag = TRUE;
			break;

		case 'b':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, NULL));
			bflag = TRUE;

			/*
			 * This is a hack, and may be undone later.
			 * The -b option is only used to build the Unix
			 * kernel and its related kernel-mode modules.
			 * We do not want those files to get a .SUNW_ldynsym
			 * section. At least for now, the kernel makes no
			 * use of .SUNW_ldynsym, and we do not want to use
			 * the space to hold it. Therefore, we overload
			 * the use of -b to also imply -znoldynsym.
			 */
			ofl->ofl_flags |= FLG_OF_NOLDYNSYM;
			break;

		case 'c':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			if (ofl->ofl_config)
				eprintf(ofl->ofl_lml, ERR_WARNING,
				    MSG_INTL(MSG_ARG_MTONCE),
				    MSG_ORIG(MSG_ARG_C));
			else
				ofl->ofl_config = optarg;
			break;

		case 'C':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, NULL));
			demangle_flag = 1;
			break;

		case 'd':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			if ((optarg[0] == 'n') && (optarg[1] == '\0')) {
				if (dflag != SET_UNKNOWN)
					eprintf(ofl->ofl_lml, ERR_WARNING,
					    MSG_INTL(MSG_ARG_MTONCE),
					    MSG_ORIG(MSG_ARG_D));
				else
					dflag = SET_FALSE;
			} else if ((optarg[0] == 'y') && (optarg[1] == '\0')) {
				if (dflag != SET_UNKNOWN)
					eprintf(ofl->ofl_lml, ERR_WARNING,
					    MSG_INTL(MSG_ARG_MTONCE),
					    MSG_ORIG(MSG_ARG_D));
				else
					dflag = SET_TRUE;
			} else {
				eprintf(ofl->ofl_lml, ERR_FATAL,
				    MSG_INTL(MSG_ARG_ILLEGAL),
				    MSG_ORIG(MSG_ARG_D), optarg);
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
			break;

		case 'e':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			if (ofl->ofl_entry)
				eprintf(ofl->ofl_lml, ERR_WARNING,
				    MSG_INTL(MSG_MARG_MTONCE),
				    MSG_INTL(MSG_MARG_ENTRY));
			else
				ofl->ofl_entry = (void *)optarg;
			break;

		case 'f':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			if (ofl->ofl_filtees &&
			    (!(ofl->ofl_flags & FLG_OF_AUX))) {
				eprintf(ofl->ofl_lml, ERR_FATAL,
				    MSG_INTL(MSG_MARG_INCOMP),
				    MSG_INTL(MSG_MARG_FILTER_AUX),
				    MSG_INTL(MSG_MARG_FILTER));
				ofl->ofl_flags |= FLG_OF_FATAL;
			} else {
				if ((ofl->ofl_filtees =
				    add_string(ofl->ofl_filtees, optarg)) ==
				    (const char *)S_ERROR)
					return (S_ERROR);
				ofl->ofl_flags |= FLG_OF_AUX;
			}
			break;

		case 'F':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			if (ofl->ofl_filtees &&
			    (ofl->ofl_flags & FLG_OF_AUX)) {
				eprintf(ofl->ofl_lml, ERR_FATAL,
				    MSG_INTL(MSG_MARG_INCOMP),
				    MSG_INTL(MSG_MARG_FILTER),
				    MSG_INTL(MSG_MARG_FILTER_AUX));
				ofl->ofl_flags |= FLG_OF_FATAL;
			} else {
				if ((ofl->ofl_filtees =
				    add_string(ofl->ofl_filtees, optarg)) ==
				    (const char *)S_ERROR)
					return (S_ERROR);
			}
			break;

		case 'h':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			if (ofl->ofl_soname)
				eprintf(ofl->ofl_lml, ERR_WARNING,
				    MSG_INTL(MSG_MARG_MTONCE),
				    MSG_INTL(MSG_MARG_SONAME));
			else
				ofl->ofl_soname = (const char *)optarg;
			break;

		case 'i':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, NULL));
			ofl->ofl_flags |= FLG_OF_IGNENV;
			break;

		case 'I':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			if (ofl->ofl_interp)
				eprintf(ofl->ofl_lml, ERR_WARNING,
				    MSG_INTL(MSG_ARG_MTONCE),
				    MSG_ORIG(MSG_ARG_CI));
			else
				ofl->ofl_interp = (const char *)optarg;
			break;

		case 'l':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			/*
			 * For now, count any library as a shared object.  This
			 * is used to size the internal symbol cache.  This
			 * value is recalculated later on actual file processing
			 * to get an accurate shared object count.
			 */
			ofl->ofl_soscnt++;
			break;

		case 'm':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, NULL));
			ofl->ofl_flags |= FLG_OF_GENMAP;
			break;

		case 'o':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			if (ofl->ofl_name)
				eprintf(ofl->ofl_lml, ERR_WARNING,
				    MSG_INTL(MSG_MARG_MTONCE),
				    MSG_INTL(MSG_MARG_OUTFILE));
			else
				ofl->ofl_name = (const char *)optarg;
			break;

		case 'p':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));

			/*
			 * Multiple instances of this option may occur.  Each
			 * additional instance is effectively concatenated to
			 * the previous separated by a colon.
			 */
			if (*optarg != '\0') {
				if ((ofl->ofl_audit =
				    add_string(ofl->ofl_audit,
				    optarg)) == (const char *)S_ERROR)
					return (S_ERROR);
			}
			break;

		case 'P':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));

			/*
			 * Multiple instances of this option may occur.  Each
			 * additional instance is effectively concatenated to
			 * the previous separated by a colon.
			 */
			if (*optarg != '\0') {
				if ((ofl->ofl_depaudit =
				    add_string(ofl->ofl_depaudit,
				    optarg)) == (const char *)S_ERROR)
					return (S_ERROR);
			}
			break;

		case 'r':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, NULL));
			rflag = TRUE;
			break;

		case 'R':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));

			/*
			 * Multiple instances of this option may occur.  Each
			 * additional instance is effectively concatenated to
			 * the previous separated by a colon.
			 */
			if (*optarg != '\0') {
				if ((ofl->ofl_rpath =
				    add_string(ofl->ofl_rpath,
				    optarg)) == (const char *)S_ERROR)
					return (S_ERROR);
			}
			break;

		case 's':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, NULL));
			sflag = TRUE;
			break;

		case 't':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, NULL));
			ofl->ofl_flags |= FLG_OF_NOWARN;
			break;

		case 'u':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			break;

		case 'z':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));

			/*
			 * For specific help, print our usage message and exit
			 * immediately to ensure a 0 return code.
			 */
			if (strncmp(optarg, MSG_ORIG(MSG_ARG_HELP),
			    MSG_ARG_HELP_SIZE) == 0) {
				usage_mesg(1);
				exit(0);
			}

			/*
			 * For some options set a flag - further consistancy
			 * checks will be carried out in check_flags().
			 */
			if ((strncmp(optarg, MSG_ORIG(MSG_ARG_LD32),
			    MSG_ARG_LD32_SIZE) == 0) ||
			    (strncmp(optarg, MSG_ORIG(MSG_ARG_LD64),
			    MSG_ARG_LD64_SIZE) == 0)) {
				if (createargv(ofl, error) == S_ERROR)
					return (S_ERROR);

			} else if (
			    strcmp(optarg, MSG_ORIG(MSG_ARG_DEFS)) == 0) {
				if (zdflag != SET_UNKNOWN)
					eprintf(ofl->ofl_lml, ERR_WARNING,
					    MSG_INTL(MSG_ARG_MTONCE),
					    MSG_ORIG(MSG_ARG_ZDEFNODEF));
				else
					zdflag = SET_TRUE;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NODEFS)) == 0) {
				if (zdflag != SET_UNKNOWN)
					eprintf(ofl->ofl_lml, ERR_WARNING,
					    MSG_INTL(MSG_ARG_MTONCE),
					    MSG_ORIG(MSG_ARG_ZDEFNODEF));
				else
					zdflag = SET_FALSE;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_TEXT)) == 0) {
				if (ztflag &&
				    (ztflag != MSG_ORIG(MSG_ARG_ZTEXT))) {
					eprintf(ofl->ofl_lml, ERR_FATAL,
					    MSG_INTL(MSG_ARG_INCOMP),
					    MSG_ORIG(MSG_ARG_ZTEXT),
					    ztflag);
					ofl->ofl_flags |= FLG_OF_FATAL;
				}
				ztflag = MSG_ORIG(MSG_ARG_ZTEXT);
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_TEXTOFF)) == 0) {
				if (ztflag &&
				    (ztflag != MSG_ORIG(MSG_ARG_ZTEXTOFF))) {
					eprintf(ofl->ofl_lml, ERR_FATAL,
					    MSG_INTL(MSG_ARG_INCOMP),
					    MSG_ORIG(MSG_ARG_ZTEXTOFF),
					    ztflag);
					ofl->ofl_flags |= FLG_OF_FATAL;
				}
				ztflag = MSG_ORIG(MSG_ARG_ZTEXTOFF);
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_TEXTWARN)) == 0) {
				if (ztflag &&
				    (ztflag != MSG_ORIG(MSG_ARG_ZTEXTWARN))) {
					eprintf(ofl->ofl_lml, ERR_FATAL,
					    MSG_INTL(MSG_ARG_INCOMP),
					    MSG_ORIG(MSG_ARG_ZTEXTWARN),
					    ztflag);
					ofl->ofl_flags |= FLG_OF_FATAL;
				}
				ztflag = MSG_ORIG(MSG_ARG_ZTEXTWARN);

			/*
			 * For other options simply set the ofl flags directly.
			 */
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_RESCAN)) == 0) {
				ofl->ofl_flags1 |= FLG_OF1_RESCAN;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_ABSEXEC)) == 0) {
				ofl->ofl_flags1 |= FLG_OF1_ABSEXEC;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_LOADFLTR)) == 0) {
				zlflag = TRUE;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NORELOC)) == 0) {
				ofl->ofl_dtflags_1 |= DF_1_NORELOC;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NOVERSION)) == 0) {
				ofl->ofl_flags |= FLG_OF_NOVERSEC;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_MULDEFS)) == 0) {
				ofl->ofl_flags |= FLG_OF_MULDEFS;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_REDLOCSYM)) == 0) {
				ofl->ofl_flags |= FLG_OF_REDLSYM;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_INITFIRST)) == 0) {
				ofl->ofl_dtflags_1 |= DF_1_INITFIRST;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NODELETE)) == 0) {
				ofl->ofl_dtflags_1 |= DF_1_NODELETE;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NOPARTIAL)) == 0) {
				ofl->ofl_flags1 |= FLG_OF1_NOPARTI;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NOOPEN)) == 0) {
				ofl->ofl_dtflags_1 |= DF_1_NOOPEN;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NOW)) == 0) {
				ofl->ofl_dtflags_1 |= DF_1_NOW;
				ofl->ofl_dtflags |= DF_BIND_NOW;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_ORIGIN)) == 0) {
				ofl->ofl_dtflags_1 |= DF_1_ORIGIN;
				ofl->ofl_dtflags |= DF_ORIGIN;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NODEFAULTLIB)) == 0) {
				ofl->ofl_dtflags_1 |= DF_1_NODEFLIB;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NODUMP)) == 0) {
				ofl->ofl_dtflags_1 |= DF_1_NODUMP;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_ENDFILTEE)) == 0) {
				ofl->ofl_dtflags_1 |= DF_1_ENDFILTEE;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_VERBOSE)) == 0) {
				ofl->ofl_flags |= FLG_OF_VERBOSE;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_COMBRELOC)) == 0) {
				ofl->ofl_flags |= FLG_OF_COMREL;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NOCOMBRELOC)) == 0) {
				ofl->ofl_flags |= FLG_OF_NOCOMREL;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NOCOMPSTRTAB)) == 0) {
				ofl->ofl_flags1 |= FLG_OF1_NCSTTAB;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NOINTERP)) == 0) {
				ofl->ofl_flags1 |= FLG_OF1_NOINTRP;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_INTERPOSE)) == 0) {
				zinflag = TRUE;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_IGNORE)) == 0) {
				ofl->ofl_flags1 |= FLG_OF1_IGNPRC;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_RELAXRELOC)) == 0) {
				ofl->ofl_flags1 |= FLG_OF1_RLXREL;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NORELAXRELOC)) == 0) {
				ofl->ofl_flags1 |= FLG_OF1_NRLXREL;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NOLDYNSYM)) == 0) {
				ofl->ofl_flags |= FLG_OF_NOLDYNSYM;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_GLOBAUDIT)) == 0) {
				ofl->ofl_dtflags_1 |= DF_1_GLOBAUDIT;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NOSIGHANDLER)) == 0) {
				ofl->ofl_flags1 |= FLG_OF1_NOSGHND;

			/*
			 * Check archive group usage
			 *	-z rescan-start ... -z rescan-end
			 * to ensure they don't overlap and are well formed.
			 */
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_RESCAN_START)) == 0) {
				if (ofl->ofl_ars_gsandx == 0) {
					ofl->ofl_ars_gsandx = ndx;
				} else if (ofl->ofl_ars_gsandx > 0) {
					/* Another group is still open */
					eprintf(ofl->ofl_lml, ERR_FATAL,
					    MSG_INTL(MSG_ARG_AR_GRP_OLAP),
					    MSG_INTL(MSG_MARG_AR_GRPS));
					ofl->ofl_flags |= FLG_OF_FATAL;
					/* Don't report cascading errors */
					ofl->ofl_ars_gsandx = -1;
				}
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_RESCAN_END)) == 0) {
				if (ofl->ofl_ars_gsandx > 0) {
					ofl->ofl_ars_gsandx = 0;
				} else if (ofl->ofl_ars_gsandx == 0) {
					/* There was no matching begin */
					eprintf(ofl->ofl_lml, ERR_FATAL,
					    MSG_INTL(MSG_ARG_AR_GRP_BAD),
					    MSG_INTL(MSG_MARG_AR_GRP_END),
					    MSG_INTL(MSG_MARG_AR_GRP_START));
					ofl->ofl_flags |= FLG_OF_FATAL;
					/* Don't report cascading errors */
					ofl->ofl_ars_gsandx = -1;
				}

			/*
			 * If -z wrap is seen, enter the symbol to be wrapped
			 * into the wrap AVL tree.
			 */
			} else if (strncmp(optarg, MSG_ORIG(MSG_ARG_WRAP),
			    MSG_ARG_WRAP_SIZE) == 0) {
				if (ld_wrap_enter(ofl,
				    optarg + MSG_ARG_WRAP_SIZE) == NULL)
					return (S_ERROR);

			/*
			 * The following options just need validation as they
			 * are interpreted on the second pass through the
			 * command line arguments.
			 */
			} else if (
			    strncmp(optarg, MSG_ORIG(MSG_ARG_INITARRAY),
			    MSG_ARG_INITARRAY_SIZE) &&
			    strncmp(optarg, MSG_ORIG(MSG_ARG_FINIARRAY),
			    MSG_ARG_FINIARRAY_SIZE) &&
			    strncmp(optarg, MSG_ORIG(MSG_ARG_PREINITARRAY),
			    MSG_ARG_PREINITARRAY_SIZE) &&
			    strncmp(optarg, MSG_ORIG(MSG_ARG_RTLDINFO),
			    MSG_ARG_RTLDINFO_SIZE) &&
			    strncmp(optarg, MSG_ORIG(MSG_ARG_DTRACE),
			    MSG_ARG_DTRACE_SIZE) &&
			    strcmp(optarg, MSG_ORIG(MSG_ARG_ALLEXTRT)) &&
			    strcmp(optarg, MSG_ORIG(MSG_ARG_DFLEXTRT)) &&
			    strcmp(optarg, MSG_ORIG(MSG_ARG_DIRECT)) &&
			    strcmp(optarg, MSG_ORIG(MSG_ARG_NODIRECT)) &&
			    strcmp(optarg, MSG_ORIG(MSG_ARG_GROUPPERM)) &&
			    strcmp(optarg, MSG_ORIG(MSG_ARG_LAZYLOAD)) &&
			    strcmp(optarg, MSG_ORIG(MSG_ARG_NOGROUPPERM)) &&
			    strcmp(optarg, MSG_ORIG(MSG_ARG_NOLAZYLOAD)) &&
			    strcmp(optarg, MSG_ORIG(MSG_ARG_RECORD)) &&
			    strcmp(optarg, MSG_ORIG(MSG_ARG_ALTEXEC64)) &&
			    strcmp(optarg, MSG_ORIG(MSG_ARG_WEAKEXT)) &&
			    strncmp(optarg, MSG_ORIG(MSG_ARG_TARGET),
			    MSG_ARG_TARGET_SIZE) &&
			    strcmp(optarg, MSG_ORIG(MSG_ARG_RESCAN_NOW))) {
				eprintf(ofl->ofl_lml, ERR_FATAL,
				    MSG_INTL(MSG_ARG_ILLEGAL),
				    MSG_ORIG(MSG_ARG_Z), optarg);
				ofl->ofl_flags |= FLG_OF_FATAL;
			}

			break;

		case 'D':
			/*
			 * If we have not yet read any input files go ahead
			 * and process any debugging options (this allows any
			 * argument processing, entrance criteria and library
			 * initialization to be displayed).  Otherwise, if an
			 * input file has been seen, skip interpretation until
			 * process_files (this allows debugging to be turned
			 * on and off around individual groups of files).
			 */
			Dflag = 1;
			if (ofl->ofl_objscnt == 0) {
				if (dbg_setup(ofl, optarg, 2) == 0)
					return (S_ERROR);
			}

			/*
			 * A diagnostic can only be provided after dbg_setup().
			 * As this is the first diagnostic that can be produced
			 * by ld(1), issue a title for timing and basic output.
			 */
			if ((optitle == 0) && DBG_ENABLED) {
				optitle++;
				DBG_CALL(Dbg_basic_options(ofl->ofl_lml));
			}
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			break;

		case 'B':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			if (strcmp(optarg, MSG_ORIG(MSG_ARG_DIRECT)) == 0) {
				if (Bdflag == SET_FALSE) {
					eprintf(ofl->ofl_lml, ERR_FATAL,
					    MSG_INTL(MSG_ARG_INCOMP),
					    MSG_ORIG(MSG_ARG_BNODIRECT),
					    MSG_ORIG(MSG_ARG_BDIRECT));
					ofl->ofl_flags |= FLG_OF_FATAL;
				} else
					Bdflag = SET_TRUE;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_NODIRECT)) == 0) {
				if (Bdflag == SET_TRUE) {
					eprintf(ofl->ofl_lml, ERR_FATAL,
					    MSG_INTL(MSG_ARG_INCOMP),
					    MSG_ORIG(MSG_ARG_BDIRECT),
					    MSG_ORIG(MSG_ARG_BNODIRECT));
					ofl->ofl_flags |= FLG_OF_FATAL;
				} else
					Bdflag = SET_FALSE;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_STR_SYMBOLIC)) == 0)
				Bsflag = TRUE;
			else if (strcmp(optarg, MSG_ORIG(MSG_ARG_REDUCE)) == 0)
				ofl->ofl_flags |= FLG_OF_PROCRED;
			else if (strcmp(optarg, MSG_ORIG(MSG_STR_LOCAL)) == 0)
				Blflag = TRUE;
			else if (strcmp(optarg, MSG_ORIG(MSG_ARG_GROUP)) == 0)
				Bgflag = TRUE;
			else if (strcmp(optarg,
			    MSG_ORIG(MSG_STR_ELIMINATE)) == 0)
				Beflag = TRUE;
			else if (strcmp(optarg,
			    MSG_ORIG(MSG_ARG_TRANSLATOR)) == 0) {
				eprintf(ofl->ofl_lml, ERR_WARNING,
				    MSG_INTL(MSG_ARG_UNSUPPORTED),
				    MSG_ORIG(MSG_ARG_BTRANSLATOR));
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_STR_LD_DYNAMIC)) &&
			    strcmp(optarg, MSG_ORIG(MSG_ARG_STATIC))) {
				eprintf(ofl->ofl_lml, ERR_FATAL,
				    MSG_INTL(MSG_ARG_ILLEGAL),
				    MSG_ORIG(MSG_ARG_CB), optarg);
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
			break;

		case 'G':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, NULL));
			Gflag = TRUE;
			break;

		case 'L':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			break;

		case 'M':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			if (aplist_append(&(ofl->ofl_maps), optarg,
			    AL_CNT_OFL_MAPFILES) == NULL)
				return (S_ERROR);
			break;

		case 'N':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			break;

		case 'Q':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			if ((optarg[0] == 'n') && (optarg[1] == '\0')) {
				if (Qflag != SET_UNKNOWN)
					eprintf(ofl->ofl_lml, ERR_WARNING,
					    MSG_INTL(MSG_ARG_MTONCE),
					    MSG_ORIG(MSG_ARG_CQ));
				else
					Qflag = SET_FALSE;
			} else if ((optarg[0] == 'y') && (optarg[1] == '\0')) {
				if (Qflag != SET_UNKNOWN)
					eprintf(ofl->ofl_lml, ERR_WARNING,
					    MSG_INTL(MSG_ARG_MTONCE),
					    MSG_ORIG(MSG_ARG_CQ));
				else
					Qflag = SET_TRUE;
			} else {
				eprintf(ofl->ofl_lml, ERR_FATAL,
				    MSG_INTL(MSG_ARG_ILLEGAL),
				    MSG_ORIG(MSG_ARG_CQ), optarg);
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
			break;

		case 'S':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			if (aplist_append(&lib_support, optarg,
			    AL_CNT_SUPPORT) == NULL)
				return (S_ERROR);
			break;

		case 'V':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, NULL));
			if (!Vflag)
				(void) fprintf(stderr, MSG_ORIG(MSG_STR_STRNL),
				    ofl->ofl_sgsid);
			Vflag = TRUE;
			break;

		case 'Y':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, optarg));
			if (strncmp(optarg, MSG_ORIG(MSG_ARG_LCOM), 2) == 0) {
				if (Llibdir)
					eprintf(ofl->ofl_lml, ERR_WARNING,
					    MSG_INTL(MSG_ARG_MTONCE),
					    MSG_ORIG(MSG_ARG_CYL));
				else
					Llibdir = optarg + 2;
			} else if (strncmp(optarg,
			    MSG_ORIG(MSG_ARG_UCOM), 2) == 0) {
				if (Ulibdir)
					eprintf(ofl->ofl_lml, ERR_WARNING,
					    MSG_INTL(MSG_ARG_MTONCE),
					    MSG_ORIG(MSG_ARG_CYU));
				else
					Ulibdir = optarg + 2;
			} else if (strncmp(optarg,
			    MSG_ORIG(MSG_ARG_PCOM), 2) == 0) {
				if (Plibpath)
					eprintf(ofl->ofl_lml, ERR_WARNING,
					    MSG_INTL(MSG_ARG_MTONCE),
					    MSG_ORIG(MSG_ARG_CYP));
				else
					Plibpath = optarg + 2;
			} else {
				eprintf(ofl->ofl_lml, ERR_FATAL,
				    MSG_INTL(MSG_ARG_ILLEGAL),
				    MSG_ORIG(MSG_ARG_CY), optarg);
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
			break;

		case '?':
			DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c, NULL));
			(*error)++;
			break;

		default:
			break;
		}

		/*
		 * Update the argument index for the next getopt() iteration.
		 */
		ndx = optind;
	}
	return (1);
}

/*
 * Parsing options pass2 for
 */
static uintptr_t
parseopt_pass2(Ofl_desc *ofl, int argc, char **argv)
{
	int	c, ndx = optind;

	while ((c = ld_getopt(ofl->ofl_lml, ndx, argc, argv)) != -1) {
		Ifl_desc	*ifl;
		Sym_desc	*sdp;

		switch (c) {
			case 'l':
				DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c,
				    optarg));
				if (ld_find_library(optarg, ofl) == S_ERROR)
					return (S_ERROR);
				break;
			case 'B':
				DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c,
				    optarg));
				if (strcmp(optarg,
				    MSG_ORIG(MSG_STR_LD_DYNAMIC)) == 0) {
					if (ofl->ofl_flags & FLG_OF_DYNAMIC)
						ofl->ofl_flags |=
						    FLG_OF_DYNLIBS;
					else {
						eprintf(ofl->ofl_lml, ERR_FATAL,
						    MSG_INTL(MSG_ARG_ST_INCOMP),
						    MSG_ORIG(MSG_ARG_BDYNAMIC));
						ofl->ofl_flags |= FLG_OF_FATAL;
					}
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_STATIC)) == 0)
					ofl->ofl_flags &= ~FLG_OF_DYNLIBS;
				break;
			case 'L':
				DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c,
				    optarg));
				if (ld_add_libdir(ofl, optarg) == S_ERROR)
					return (S_ERROR);
				break;
			case 'N':
				DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c,
				    optarg));
				/*
				 * Record DT_NEEDED string
				 */
				if (!(ofl->ofl_flags & FLG_OF_DYNAMIC)) {
					eprintf(ofl->ofl_lml, ERR_FATAL,
					    MSG_INTL(MSG_ARG_ST_INCOMP),
					    MSG_ORIG(MSG_ARG_CN));
					ofl->ofl_flags |= FLG_OF_FATAL;
				}
				if (((ifl = libld_calloc(1,
				    sizeof (Ifl_desc))) == NULL) ||
				    (aplist_append(&ofl->ofl_sos, ifl,
				    AL_CNT_OFL_LIBS) == NULL))
					return (S_ERROR);

				ifl->ifl_name = MSG_INTL(MSG_STR_COMMAND);
				ifl->ifl_soname = optarg;
				ifl->ifl_flags = (FLG_IF_NEEDSTR |
				    FLG_IF_FILEREF | FLG_IF_DEPREQD);

				break;
			case 'D':
				DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c,
				    optarg));
				(void) dbg_setup(ofl, optarg, 3);
				break;
			case 'u':
				DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c,
				    optarg));
				if (ld_sym_add_u(optarg, ofl,
				    MSG_STR_COMMAND) == (Sym_desc *)S_ERROR)
					return (S_ERROR);
				break;
			case 'z':
				DBG_CALL(Dbg_args_option(ofl->ofl_lml, ndx, c,
				    optarg));
				if ((strncmp(optarg, MSG_ORIG(MSG_ARG_LD32),
				    MSG_ARG_LD32_SIZE) == 0) ||
				    (strncmp(optarg, MSG_ORIG(MSG_ARG_LD64),
				    MSG_ARG_LD64_SIZE) == 0)) {
					if (createargv(ofl, 0) == S_ERROR)
						return (S_ERROR);
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_ALLEXTRT)) == 0) {
					ofl->ofl_flags1 |= FLG_OF1_ALLEXRT;
					ofl->ofl_flags1 &= ~FLG_OF1_WEAKEXT;
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_WEAKEXT)) == 0) {
					ofl->ofl_flags1 |= FLG_OF1_WEAKEXT;
					ofl->ofl_flags1 &= ~FLG_OF1_ALLEXRT;
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_DFLEXTRT)) == 0) {
					ofl->ofl_flags1 &=
					    ~(FLG_OF1_ALLEXRT |
					    FLG_OF1_WEAKEXT);
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_DIRECT)) == 0) {
					ofl->ofl_flags1 |= FLG_OF1_ZDIRECT;
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_NODIRECT)) == 0) {
					ofl->ofl_flags1 &= ~FLG_OF1_ZDIRECT;
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_IGNORE)) == 0) {
					ofl->ofl_flags1 |= FLG_OF1_IGNORE;
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_RECORD)) == 0) {
					ofl->ofl_flags1 &= ~FLG_OF1_IGNORE;
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_LAZYLOAD)) == 0) {
					ofl->ofl_flags1 |= FLG_OF1_LAZYLD;
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_NOLAZYLOAD)) == 0) {
					ofl->ofl_flags1 &= ~ FLG_OF1_LAZYLD;
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_GROUPPERM)) == 0) {
					ofl->ofl_flags1 |= FLG_OF1_GRPPRM;
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_NOGROUPPERM)) == 0) {
					ofl->ofl_flags1 &= ~FLG_OF1_GRPPRM;
				} else if (strncmp(optarg,
				    MSG_ORIG(MSG_ARG_INITARRAY),
				    MSG_ARG_INITARRAY_SIZE) == 0) {
					if (((sdp = ld_sym_add_u(optarg +
					    MSG_ARG_INITARRAY_SIZE, ofl,
					    MSG_STR_COMMAND)) ==
					    (Sym_desc *)S_ERROR) ||
					    (aplist_append(&ofl->ofl_initarray,
					    sdp, AL_CNT_OFL_ARRAYS) == NULL))
						return (S_ERROR);
				} else if (strncmp(optarg,
				    MSG_ORIG(MSG_ARG_FINIARRAY),
				    MSG_ARG_FINIARRAY_SIZE) == 0) {
					if (((sdp = ld_sym_add_u(optarg +
					    MSG_ARG_FINIARRAY_SIZE, ofl,
					    MSG_STR_COMMAND)) ==
					    (Sym_desc *)S_ERROR) ||
					    (aplist_append(&ofl->ofl_finiarray,
					    sdp, AL_CNT_OFL_ARRAYS) == NULL))
						return (S_ERROR);
				} else if (strncmp(optarg,
				    MSG_ORIG(MSG_ARG_PREINITARRAY),
				    MSG_ARG_PREINITARRAY_SIZE) == 0) {
					if (((sdp = ld_sym_add_u(optarg +
					    MSG_ARG_PREINITARRAY_SIZE, ofl,
					    MSG_STR_COMMAND)) ==
					    (Sym_desc *)S_ERROR) ||
					    (aplist_append(&ofl->ofl_preiarray,
					    sdp, AL_CNT_OFL_ARRAYS) == NULL))
						return (S_ERROR);
				} else if (strncmp(optarg,
				    MSG_ORIG(MSG_ARG_RTLDINFO),
				    MSG_ARG_RTLDINFO_SIZE) == 0) {
					if (((sdp = ld_sym_add_u(optarg +
					    MSG_ARG_RTLDINFO_SIZE, ofl,
					    MSG_STR_COMMAND)) ==
					    (Sym_desc *)S_ERROR) ||
					    (aplist_append(&ofl->ofl_rtldinfo,
					    sdp, AL_CNT_OFL_ARRAYS) == NULL))
						return (S_ERROR);
				} else if (strncmp(optarg,
				    MSG_ORIG(MSG_ARG_DTRACE),
				    MSG_ARG_DTRACE_SIZE) == 0) {
					if ((sdp = ld_sym_add_u(optarg +
					    MSG_ARG_DTRACE_SIZE, ofl,
					    MSG_STR_COMMAND)) ==
					    (Sym_desc *)S_ERROR)
						return (S_ERROR);
					ofl->ofl_dtracesym = sdp;
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_RESCAN_NOW)) == 0) {
					if (ld_rescan_archives(ofl, 0, ndx) ==
					    S_ERROR)
						return (S_ERROR);
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_RESCAN_START)) == 0) {
					ofl->ofl_ars_gsndx = ofl->ofl_arscnt;
					ofl->ofl_ars_gsandx = ndx;
				} else if (strcmp(optarg,
				    MSG_ORIG(MSG_ARG_RESCAN_END)) == 0) {
					if (ld_rescan_archives(ofl, 1, ndx) ==
					    S_ERROR)
						return (S_ERROR);
				}
			default:
				break;
		}

		/*
		 * Update the argument index for the next getopt() iteration.
		 */
		ndx = optind;
	}
	return (1);
}

/*
 *
 * Pass 1 -- process_flags: collects all options and sets flags
 */
static uintptr_t
process_flags_com(Ofl_desc *ofl, int argc, char **argv, int *e)
{
	for (; optind < argc; optind++) {
		/*
		 * If we detect some more options return to getopt().
		 * Checking argv[optind][1] against null prevents a forever
		 * loop if an unadorned `-' argument is passed to us.
		 */
		while ((optind < argc) && (argv[optind][0] == '-')) {
			if (argv[optind][1] != '\0') {
				if (parseopt_pass1(ofl, argc, argv, e) ==
				    S_ERROR)
					return (S_ERROR);
			} else if (++optind < argc)
				continue;
		}
		if (optind >= argc)
			break;
		ofl->ofl_objscnt++;
	}

	/* Did an unterminated archive group run off the end? */
	if (ofl->ofl_ars_gsandx > 0) {
		eprintf(ofl->ofl_lml, ERR_FATAL, MSG_INTL(MSG_ARG_AR_GRP_BAD),
		    MSG_INTL(MSG_MARG_AR_GRP_START),
		    MSG_INTL(MSG_MARG_AR_GRP_END));
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (S_ERROR);
	}

	return (1);
}

uintptr_t
ld_process_flags(Ofl_desc *ofl, int argc, char **argv)
{
	int	error = 0;	/* Collect all argument errors before exit */

	if (argc < 2) {
		usage_mesg(FALSE);
		return (S_ERROR);
	}

	/*
	 * Option handling
	 */
	opterr = 0;
	optind = 1;
	if (process_flags_com(ofl, argc, argv, &error) == S_ERROR)
		return (S_ERROR);

	/*
	 * Having parsed everything, did we have any errors.
	 */
	if (error) {
		usage_mesg(TRUE);
		return (S_ERROR);
	}

	return (check_flags(ofl, argc));
}

/*
 * Pass 2 -- process_files: skips the flags collected in pass 1 and processes
 * files.
 */
static uintptr_t
process_files_com(Ofl_desc *ofl, int argc, char **argv)
{
	for (; optind < argc; optind++) {
		int		fd;
		Ifl_desc	*ifl;
		char		*path;
		Rej_desc	rej = { 0 };

		/*
		 * If we detect some more options return to getopt().
		 * Checking argv[optind][1] against null prevents a forever
		 * loop if an unadorned `-' argument is passed to us.
		 */
		while ((optind < argc) && (argv[optind][0] == '-')) {
			if (argv[optind][1] != '\0') {
				if (parseopt_pass2(ofl, argc, argv) == S_ERROR)
					return (S_ERROR);
			} else if (++optind < argc)
				continue;
		}
		if (optind >= argc)
			break;

		path = argv[optind];
		if ((fd = open(path, O_RDONLY)) == -1) {
			int err = errno;

			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(MSG_SYS_OPEN), path, strerror(err));
			ofl->ofl_flags |= FLG_OF_FATAL;
			continue;
		}

		DBG_CALL(Dbg_args_file(ofl->ofl_lml, optind, path));

		ifl = ld_process_open(path, path, &fd, ofl,
		    (FLG_IF_CMDLINE | FLG_IF_NEEDED), &rej);
		if (fd != -1)
			(void) close(fd);
		if (ifl == (Ifl_desc *)S_ERROR)
			return (S_ERROR);

		/*
		 * Check for mismatched input.
		 */
		if (rej.rej_type) {
			Conv_reject_desc_buf_t rej_buf;

			eprintf(ofl->ofl_lml, ERR_FATAL,
			    MSG_INTL(reject[rej.rej_type]),
			    rej.rej_name ? rej.rej_name :
			    MSG_INTL(MSG_STR_UNKNOWN),
			    conv_reject_desc(&rej, &rej_buf,
			    ld_targ.t_m.m_mach));
			ofl->ofl_flags |= FLG_OF_FATAL;
			return (1);
		}
	}
	return (1);
}

uintptr_t
ld_process_files(Ofl_desc *ofl, int argc, char **argv)
{
	DBG_CALL(Dbg_basic_files(ofl->ofl_lml));

	/*
	 * Process command line files (taking into account any applicable
	 * preceding flags).  Return if any fatal errors have occurred.
	 */
	opterr = 0;
	optind = 1;
	if (process_files_com(ofl, argc, argv) == S_ERROR)
		return (S_ERROR);
	if (ofl->ofl_flags & FLG_OF_FATAL)
		return (1);

	/*
	 * Now that all command line files have been processed see if there are
	 * any additional `needed' shared object dependencies.
	 */
	if (ofl->ofl_soneed)
		if (ld_finish_libs(ofl) == S_ERROR)
			return (S_ERROR);

	/*
	 * If rescanning archives is enabled, do so now to determine whether
	 * there might still be members extracted to satisfy references from any
	 * explicit objects.  Continue until no new objects are extracted.  Note
	 * that this pass is carried out *after* processing any implicit objects
	 * (above) as they may already have resolved any undefined references
	 * from any explicit dependencies.
	 */
	if (ofl->ofl_flags1 & FLG_OF1_RESCAN) {
		if (ld_rescan_archives(ofl, 0, argc) == S_ERROR)
			return (S_ERROR);
		if (ofl->ofl_flags & FLG_OF_FATAL)
			return (1);
	}

	/*
	 * If debugging, provide statistics on each archives extraction, or flag
	 * any archive that has provided no members.  Note that this could be a
	 * nice place to free up much of the archive infrastructure, as we've
	 * extracted any members we need.  However, as we presently don't free
	 * anything under ld(1) there's not much point in proceeding further.
	 */
	DBG_CALL(Dbg_statistics_ar(ofl));

	/*
	 * If any version definitions have been established, either via input
	 * from a mapfile or from the input relocatable objects, make sure any
	 * version dependencies are satisfied, and version symbols created.
	 */
	if (ofl->ofl_verdesc)
		if (ld_vers_check_defs(ofl) == S_ERROR)
			return (S_ERROR);

	/*
	 * If segment ordering was specified (using mapfile) verify things
	 * are ok.
	 */
	if (ofl->ofl_flags & FLG_OF_SEGORDER)
		ld_ent_check(ofl);

	return (1);
}

uintptr_t
ld_init_strings(Ofl_desc *ofl)
{
	uint_t	stflags;

	if (ofl->ofl_flags1 & FLG_OF1_NCSTTAB)
		stflags = 0;
	else
		stflags = FLG_STNEW_COMPRESS;

	if (((ofl->ofl_shdrsttab = st_new(stflags)) == NULL) ||
	    ((ofl->ofl_strtab = st_new(stflags)) == NULL) ||
	    ((ofl->ofl_dynstrtab = st_new(stflags)) == NULL))
		return (S_ERROR);

	return (0);
}
