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

#include <syslog.h>
#include <synch.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/errno.h>

#include <smbsrv/libsmb.h>
#include <smbsrv/libsmbns.h>
#include <smbsrv/libmlsvc.h>
#include <smbsrv/smbinfo.h>
#include <smbsrv/ntstatus.h>
#include "smbd.h"


/*
 * This is a short-lived thread that triggers the initial DC discovery
 * at startup.
 */
static pthread_t smb_locate_dc_thr;

static void *smbd_locate_dc_thread(void *);
static int smbd_get_kpasswd_srv(char *, size_t);
static uint32_t smbd_join_workgroup(smb_joininfo_t *);
static uint32_t smbd_join_domain(smb_joininfo_t *);

/*
 * smbd_join
 *
 * Joins the specified domain/workgroup.
 *
 * If the security mode or domain name is being changed,
 * the caller must restart the service.
 */
uint32_t
smbd_join(smb_joininfo_t *info)
{
	uint32_t status;

	dssetup_clear_domain_info();
	if (info->mode == SMB_SECMODE_WORKGRP)
		status = smbd_join_workgroup(info);
	else
		status = smbd_join_domain(info);

	return (status);
}

/*
 * smbd_set_netlogon_cred
 *
 * If the system is joined to an AD domain via kclient, SMB daemon will need
 * to establish the NETLOGON credential chain.
 *
 * Since the kclient has updated the machine password stored in SMF
 * repository, the cached ipc_info must be updated accordingly by calling
 * smb_ipc_commit.
 *
 * Due to potential replication delays in a multiple DC environment, the
 * NETLOGON rpc request must be sent to the DC, to which the KPASSWD request
 * is sent. If the DC discovered by the SMB daemon is different than the
 * kpasswd server, the current connection with the DC will be torn down
 * and a DC discovery process will be triggered to locate the kpasswd
 * server.
 *
 * If joining a new domain, the domain_name property must be set after a
 * successful credential chain setup.
 */
boolean_t
smbd_set_netlogon_cred(void)
{
	char kpasswd_srv[MAXHOSTNAMELEN];
	char kpasswd_domain[MAXHOSTNAMELEN];
	char sam_acct[SMB_SAMACCT_MAXLEN];
	char ipc_usr[SMB_USERNAME_MAXLEN];
	char *dom;
	boolean_t new_domain = B_FALSE;
	smb_domainex_t dxi;
	smb_domain_t *di;

	if (smb_config_get_secmode() != SMB_SECMODE_DOMAIN)
		return (B_FALSE);

	if (smb_match_netlogon_seqnum())
		return (B_FALSE);

	(void) smb_config_getstr(SMB_CI_KPASSWD_SRV, kpasswd_srv,
	    sizeof (kpasswd_srv));

	if (*kpasswd_srv == '\0')
		return (B_FALSE);

	/*
	 * If the domain join initiated by smbadm join CLI is in
	 * progress, don't do anything.
	 */
	(void) smb_getsamaccount(sam_acct, sizeof (sam_acct));
	smb_ipc_get_user(ipc_usr, SMB_USERNAME_MAXLEN);
	if (smb_strcasecmp(ipc_usr, sam_acct, 0))
		return (B_FALSE);

	di = &dxi.d_primary;
	if (!smb_domain_getinfo(&dxi))
		(void) smb_getfqdomainname(di->di_fqname, MAXHOSTNAMELEN);

	(void) smb_config_getstr(SMB_CI_KPASSWD_DOMAIN, kpasswd_domain,
	    sizeof (kpasswd_domain));

	if (*kpasswd_domain != '\0' &&
	    smb_strcasecmp(kpasswd_domain, di->di_fqname, 0)) {
		dom = kpasswd_domain;
		new_domain = B_TRUE;
	} else {
		dom = di->di_fqname;
	}

	/*
	 * DC discovery will be triggered if the domain info is not
	 * currently cached or the SMB daemon has previously discovered a DC
	 * that is different than the kpasswd server.
	 */
	if (new_domain || smb_strcasecmp(dxi.d_dc, kpasswd_srv, 0) != 0) {
		if (*dxi.d_dc != '\0')
			mlsvc_disconnect(dxi.d_dc);

		if (!smb_locate_dc(dom, kpasswd_srv, &dxi)) {
			if (!smb_locate_dc(di->di_fqname, "", &dxi)) {
				smb_ipc_commit();
				return (B_FALSE);
			}
		}
	}

	smb_ipc_commit();
	if (mlsvc_netlogon(dxi.d_dc, di->di_nbname)) {
		syslog(LOG_ERR,
		    "failed to establish NETLOGON credential chain");
		return (B_TRUE);
	} else {
		if (new_domain) {
			smb_config_setdomaininfo(di->di_nbname, di->di_fqname,
			    di->di_sid,
			    di->di_u.di_dns.ddi_forest,
			    di->di_u.di_dns.ddi_guid);
			(void) smb_config_setstr(SMB_CI_KPASSWD_DOMAIN, "");
		}
	}

	return (new_domain);
}

/*
 * smbd_locate_dc_start()
 *
 * Initialization of the thread that triggers the initial DC discovery
 * when SMB daemon starts up.
 * Returns 0 on success, an error number if thread creation fails.
 */
int
smbd_locate_dc_start(void)
{
	pthread_attr_t tattr;
	int rc;

	(void) pthread_attr_init(&tattr);
	(void) pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
	rc = pthread_create(&smb_locate_dc_thr, &tattr, smbd_locate_dc_thread,
	    NULL);
	(void) pthread_attr_destroy(&tattr);
	return (rc);
}

/*
 * smbd_locate_dc_thread()
 *
 * If necessary, set up Netlogon credential chain and locate a
 * domain controller in the given resource domain.
 *
 * The domain configuration will be updated upon a successful DC discovery.
 */
/*ARGSUSED*/
static void *
smbd_locate_dc_thread(void *arg)
{
	char domain[MAXHOSTNAMELEN];
	smb_domainex_t new_domain;
	smb_domain_t *di;

	if (!smb_match_netlogon_seqnum()) {
		(void) smbd_set_netlogon_cred();
	} else {
		if (smb_getfqdomainname(domain, MAXHOSTNAMELEN) != 0) {
			(void) smb_getdomainname(domain, MAXHOSTNAMELEN);
			(void) smb_strupr(domain);
		}

		if (smb_locate_dc(domain, "", &new_domain)) {
			di = &new_domain.d_primary;
			smb_config_setdomaininfo(di->di_nbname, di->di_fqname,
			    di->di_sid,
			    di->di_u.di_dns.ddi_forest,
			    di->di_u.di_dns.ddi_guid);
		}
	}

	return (NULL);
}


/*
 * Retrieve the kpasswd server from krb5.conf.
 *
 * Initialization of the locate dc thread.
 * Returns 0 on success, an error number if thread creation fails.
 */
static int
smbd_get_kpasswd_srv(char *srv, size_t len)
{
	FILE *fp;
	static char buf[512];
	char *p;

	*srv = '\0';
	p = getenv("KRB5_CONFIG");
	if (p == NULL || *p == '\0')
		p = "/etc/krb5/krb5.conf";

	if ((fp = fopen(p, "r")) == NULL)
		return (-1);

	while (fgets(buf, sizeof (buf), fp)) {

		/* Weed out any comment text */
		(void) trim_whitespace(buf);
		if (*buf == '#')
			continue;

		if ((p = strstr(buf, "kpasswd_server")) != NULL) {
			if ((p = strchr(p, '=')) != NULL) {
				(void) trim_whitespace(++p);
				(void) strlcpy(srv, p, len);
			}
			break;
		}
	}


	(void) fclose(fp);
	return ((*srv == '\0') ? -1 : 0);
}

static uint32_t
smbd_join_workgroup(smb_joininfo_t *info)
{
	char nb_domain[SMB_PI_MAX_DOMAIN];

	(void) smb_config_getstr(SMB_CI_DOMAIN_NAME, nb_domain,
	    sizeof (nb_domain));

	smbd_set_secmode(SMB_SECMODE_WORKGRP);
	smb_config_setdomaininfo(info->domain_name, "", "", "", "");

	if (strcasecmp(nb_domain, info->domain_name))
		smb_browser_reconfig();

	return (NT_STATUS_SUCCESS);
}

static uint32_t
smbd_join_domain(smb_joininfo_t *info)
{
	uint32_t status;
	unsigned char passwd_hash[SMBAUTH_HASH_SZ];
	char dc[MAXHOSTNAMELEN];
	smb_domainex_t dxi;
	smb_domain_t *di;

	/*
	 * Ensure that any previous membership of this domain has
	 * been cleared from the environment before we start. This
	 * will ensure that we don't attempt a NETLOGON_SAMLOGON
	 * when attempting to find the PDC.
	 */

	(void) smb_config_setbool(SMB_CI_DOMAIN_MEMB, B_FALSE);

	if (smb_auth_ntlm_hash(info->domain_passwd, passwd_hash)
	    != SMBAUTH_SUCCESS) {
		syslog(LOG_ERR, "smbd: could not compute ntlm hash for '%s'",
		    info->domain_username);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	smb_ipc_set(info->domain_username, passwd_hash);

	(void) smbd_get_kpasswd_srv(dc, sizeof (dc));
	/* info->domain_name could either be NetBIOS domain name or FQDN */
	if (smb_locate_dc(info->domain_name, dc, &dxi)) {
		status = mlsvc_join(&dxi, info->domain_username,
		    info->domain_passwd);

		if (status == NT_STATUS_SUCCESS) {
			di = &dxi.d_primary;
			smbd_set_secmode(SMB_SECMODE_DOMAIN);
			smb_config_setdomaininfo(di->di_nbname, di->di_fqname,
			    di->di_sid,
			    di->di_u.di_dns.ddi_forest,
			    di->di_u.di_dns.ddi_guid);
			smb_ipc_commit();
			return (status);
		}

		smb_ipc_rollback();
		syslog(LOG_ERR, "smbd: failed joining %s (%s)",
		    info->domain_name, xlate_nt_status(status));
		return (status);
	}

	smb_ipc_rollback();
	syslog(LOG_ERR, "smbd: failed locating domain controller for %s",
	    info->domain_name);
	return (NT_STATUS_DOMAIN_CONTROLLER_NOT_FOUND);
}
