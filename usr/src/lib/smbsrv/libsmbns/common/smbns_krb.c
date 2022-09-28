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
/*
 * Copyright 1990 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 *
 * Initialize a credentials cache.
 */
#include <kerberosv5/krb5.h>
#include <kerberosv5/com_err.h>
#include <assert.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>

#include <smbsrv/libsmbns.h>
#include <smbns_krb.h>

int
smb_kinit(char *principal_name, char *principal_passwd)
{
	krb5_context ctx = NULL;
	krb5_ccache cc = NULL;
	krb5_principal me = NULL;
	krb5_creds my_creds;
	krb5_error_code code;
	const char *errmsg = NULL;
	const char *doing = NULL;

	assert(principal_name != NULL);
	assert(principal_passwd != NULL);

	(void) memset(&my_creds, 0, sizeof (my_creds));

	/*
	 * From this point on, we can goto cleanup because the key variables
	 * are initialized.
	 */

	code = krb5_init_context(&ctx);
	if (code) {
		doing = "initializing context";
		goto cleanup;
	}

	code = krb5_cc_default(ctx, &cc);
	if (code != 0) {
		doing = "resolve default credentials cache";
		goto cleanup;
	}

	/* Use specified name */
	code = krb5_parse_name(ctx, principal_name, &me);
	if (code != 0) {
		doing = "parsing principal name";
		goto cleanup;
	}

	code = krb5_get_init_creds_password(ctx, &my_creds, me,
	    principal_passwd, NULL, 0, (krb5_deltat)0,
	    NULL, NULL);
	if (code != 0) {
		doing = "getting initial credentials";

		if (code == KRB5KRB_AP_ERR_BAD_INTEGRITY) {
			errmsg = "Password incorrect";
		}

		goto cleanup;
	}

	code = krb5_cc_initialize(ctx, cc, me);
	if (code != 0) {
		doing = "initializing cache";
		goto cleanup;
	}

	code = krb5_cc_store_cred(ctx, cc, &my_creds);
	if (code != 0) {
		doing = "storing credentials";
		goto cleanup;
	}

	/* SUCCESS! */

cleanup:
	if (code != 0) {
		if (errmsg == NULL)
			errmsg = error_message(code);
		syslog(LOG_ERR, "k5_kinit: %s (%s)", doing, errmsg);
	}

	if (my_creds.client == me) {
		my_creds.client = NULL;
	}
	krb5_free_cred_contents(ctx, &my_creds);

	if (me)
		krb5_free_principal(ctx, me);
	if (cc)
		(void) krb5_cc_close(ctx, cc);
	if (ctx)
		krb5_free_context(ctx);

	return (code == 0);
}

/*
 * smb_ccache_init
 *
 * Creates the directory where the Kerberos ccache file is located
 * and set KRB5CCNAME in the environment.
 *
 * Returns 0 upon succcess.  Otherwise, returns
 * -1 if it fails to create the specified directory fails.
 * -2 if it fails to set the KRB5CCNAME environment variable.
 */
int
smb_ccache_init(char *dir, char *filename)
{
	static char buf[MAXPATHLEN];

	if ((mkdir(dir, 0700) < 0) && (errno != EEXIST))
		return (-1);

	(void) snprintf(buf, MAXPATHLEN, "KRB5CCNAME=%s/%s", dir, filename);
	if (putenv(buf) != 0)
		return (-2);
	return (0);
}

void
smb_ccache_remove(char *path)
{
	if ((remove(path) < 0) && (errno != ENOENT))
		syslog(LOG_ERR, "failed to remove ccache (%s)", path);
}
