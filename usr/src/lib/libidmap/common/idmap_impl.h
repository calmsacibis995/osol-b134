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
 * Private Header for Identity Mapping
 */

#ifndef _IDMAP_IMPL_H
#define	_IDMAP_IMPL_H


#include <rpc/xdr.h>
#include <libscf.h>
#include <resolv.h>

#include <rpcsvc/idmap_prot.h>
#include "idmap_priv.h"



#ifdef __cplusplus
extern "C" {
#endif

#define	_IDMAP_HANDLE_RPC_DOORS		1

#define	_IDMAP_GET_CLIENT_HANDLE(h, clnt) \
		if (h == NULL) \
			return (IDMAP_ERR_CLIENT_HANDLE);\
		if (h->type != _IDMAP_HANDLE_RPC_DOORS) \
			return (IDMAP_ERR_NOTSUPPORTED);\
		clnt = (CLIENT *)h->privhandle;\
		if (clnt == NULL)\
			return (IDMAP_ERR_RPC_HANDLE);

struct idmap_handle {
	int	type;
	void	*privhandle;
	/* locks */
};

struct idmap_udt_handle {
	struct idmap_handle	*ih;
	idmap_update_batch	batch;
	uint64_t		next;
	int64_t			error_index;
	idmap_stat		commit_stat;
	idmap_namerule		error_rule;
	idmap_namerule		conflict_rule;
};

#define	_IDMAP_RESET_UDT_HANDLE(uh) \
	(void) xdr_free(xdr_idmap_update_batch, (caddr_t)&uh->batch);\
	uh->next = 0;\
	uh->error_index = -1;\
	(void) xdr_free(xdr_idmap_namerule, (caddr_t)&uh->error_rule);\
	(void) xdr_free(xdr_idmap_namerule, (caddr_t)&uh->conflict_rule);

typedef struct idmap_get_res {
	idmap_id_type	idtype;
	uid_t		*uid;
	gid_t		*gid;
	int		*is_user;
	char		**sidprefix;
	idmap_rid_t	*rid;
	idmap_stat	*stat;
	idmap_info	*info;
	int		cache_res;
} idmap_get_res_t;

struct idmap_get_handle {
	struct idmap_handle	*ih;
	idmap_mapping_batch	batch;
	idmap_get_res_t		*retlist;
	uint64_t		next;
};

#define	_IDMAP_RESET_GET_HANDLE(gh) \
	(void) xdr_free(xdr_idmap_mapping_batch, (caddr_t)&gh->batch);\
	if (gh->retlist) \
		free(gh->retlist);\
	gh->retlist = NULL;\
	gh->next = 0;

struct idmap_iter {
	struct idmap_handle	*ih;
	int			type;
	uint64_t		limit;
	void			*arg;
	idmap_retcode		retcode;
	uint64_t		lastrowid;
	uint64_t		next;
	void			*retlist;
};

typedef struct stat_table {
	idmap_retcode	retcode;
	const char	*msg;
	int		errnum;
} stat_table_t;

typedef idmap_retcode	_idmap_stat;

extern idmap_retcode	_udt_extend_batch(idmap_udt_handle_t *);
extern idmap_retcode	_get_ids_extend_batch(idmap_get_handle_t *);
extern idmap_stat	_iter_get_next_list(int, idmap_iter_t *, void *,
				uchar_t **, size_t, xdrproc_t, xdrproc_t);
extern idmap_stat	_idmap_rpc2stat(CLIENT *);

extern idmap_stat idmap_get_prop_ds(idmap_handle_t *, idmap_prop_type,
    idmap_ad_disc_ds_t *);
extern idmap_stat idmap_get_prop_str(idmap_handle_t *, idmap_prop_type,
    char **);

extern idmap_logger logger;

#ifdef __cplusplus
}
#endif

#endif /* _IDMAP_IMPL_H */
