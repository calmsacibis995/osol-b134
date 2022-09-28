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
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_MSGS_H
#define	_MSGS_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	SYNTAX_ERR	gettext("ERROR: Ignoring rules syntax error: %s\n")
#define	SYNTAX_ABORT	gettext("ABORTING: Syntax error(s) in the rules file\n")
#define	INVALID_FILE	gettext("WARNING: Ignoring invalid file: %s\n")
#define	INVALID_SUBTREE	gettext("WARNING: Ignoring invalid subtree: %s\n")
#define	RULES_ERR	gettext("ERROR: -r option requires a filename\n")
#define	INPUT_ERR \
	gettext("ERROR: Cannot use -I and -r options together\n")
#define	MISSING_VER	\
	gettext("WARNING: %s has missing/invalid version string\n")
#define	MANIFEST_ERR	gettext("ERROR: Manifest corrupt, cannot continue\n")
#define	CONTENTS_WARN	gettext("WARNING: Checksum failed: %s\n")
#define	USAGE_MSG gettext("Usage:\n"\
	"\tbart create [-n] [-R root] [-r rules|-]\n"\
	"\tbart create [-n] [-R root] [-I | -I filelist]\n"\
	"\tbart compare [-r rules|-] [-i keywords] [-p] "\
		"control-manifest test-manifest\n")

#ifdef	__cplusplus
}
#endif

#endif	/* _MSGS_H */
