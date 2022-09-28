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

#include <ctype.h>
#include <sys/types.h>
#include <time.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <sys/param.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <errno.h>
#include <strings.h>
#include <sys/vtoc.h>
#include <sys/efi_partition.h>
#include <uuid/uuid.h>
#include <sys/scsi/impl/uscsi.h>
#include <sys/scsi/generic/commands.h>
#include <sys/scsi/impl/commands.h>
#include <libzfs.h>
#include <syslog.h>
#include <priv.h>

#include <iscsitgt_impl.h>
#include "queue.h"
#include "target.h"
#include "iscsi_cmd.h"
#include "utility.h"
#include "errcode.h"
#include "t10_spc.h"
#include "isns_client.h"
#include "mgmt_scf.h"

extern char *getfullrawname();

static char *create_target(tgt_node_t *);
static char *create_initiator(tgt_node_t *);
static char *create_tpgt(tgt_node_t *);
static char *create_zfs(tgt_node_t *, ucred_t *);
static Boolean_t create_target_dir(char *targ_name, char *local_name);
static char *create_node_name(char *local_nick, char *alias);
static Boolean_t create_lun(char *targ_name, char *local_name, char *type,
    int lun, char *size_str, char *backing, err_code_t *code);
static Boolean_t create_lun_common(char *targ_name, char *local_name, int lun,
    uint64_t size, err_code_t *code);
static Boolean_t setup_disk_backing(err_code_t *code, char *path, char *backing,
    tgt_node_t *n, uint64_t *size);
static Boolean_t setup_raw_backing(err_code_t *code, char *path, char *backing,
    uint64_t *size);

/*
 * []----
 * | create_func -- Branch out to appropriate object create function
 * []----
 */
/*ARGSUSED*/
void
create_func(tgt_node_t *p, target_queue_t *reply, target_queue_t *mgmt,
    ucred_t *cred)
{
	tgt_node_t	*x;
	char		msgbuf[80];
	char		*reply_msg	= NULL;

	x = p->x_child;

	/*
	 * create_zfs() does not affect SMF data
	 * therefore it is not covered by auth check
	 */
	if (x == NULL) {
		xml_rtn_msg(&reply_msg, ERR_SYNTAX_MISSING_OBJECT);
	} else if (strcmp(x->x_name, XML_ELEMENT_ZFS) == 0) {
		reply_msg = create_zfs(x, cred);
	} else if (check_auth_addremove(cred) != True) {
		xml_rtn_msg(&reply_msg, ERR_NO_PERMISSION);
	} else {
		if (x->x_name == NULL) {
			xml_rtn_msg(&reply_msg, ERR_SYNTAX_MISSING_OBJECT);
		} else if (strcmp(x->x_name, XML_ELEMENT_TARG) == 0) {
			reply_msg = create_target(x);
		} else if (strcmp(x->x_name, XML_ELEMENT_INIT) == 0) {
			reply_msg = create_initiator(x);
		} else if (strcmp(x->x_name, XML_ELEMENT_TPGT) == 0) {
			reply_msg = create_tpgt(x);
		} else {
			(void) snprintf(msgbuf, sizeof (msgbuf),
			    "Unknown object '%s' for create element",
			    x->x_name);
			xml_rtn_msg(&reply_msg, ERR_INVALID_OBJECT);
		}
	}
	queue_message_set(reply, 0, msg_mgmt_rply, reply_msg);
}

/*
 * create_target -- an administrative request to create a target
 */
static char *
create_target(tgt_node_t *x)
{
	char		*msg		= NULL;
	char		*name		= NULL;
	char		*alias		= NULL;
	char		*size		= NULL;
	char		*type		= NULL;
	char		*backing	= NULL;
	char		*node_name	= NULL;
	char		path[MAXPATHLEN];
	int		lun		= 0; /* default to LUN 0 */
	int		i;
	tgt_node_t	*n, *c, *l;
	err_code_t	code;

	(void) pthread_rwlock_wrlock(&targ_config_mutex);
	(void) tgt_find_value_str(x, XML_ELEMENT_BACK, &backing);
	(void) tgt_find_value_str(x, XML_ELEMENT_ALIAS, &alias);
	if (tgt_find_value_intchk(x, XML_ELEMENT_LUN, &lun) == False) {
		xml_rtn_msg(&msg, ERR_LUN_INVALID_RANGE);
		goto error;
	}

	/*
	 * We've got to have a name element or all bets are off.
	 */
	if (tgt_find_value_str(x, XML_ELEMENT_NAME, &name) == False) {
		xml_rtn_msg(&msg, ERR_SYNTAX_MISSING_NAME);
		goto error;
	}

	/*
	 * RFC3722 states that names must be one of:
	 * (1) a..z
	 * (2) A..Z
	 * (3) 0-9
	 * or
	 * (4) ':', '.', '-'
	 * If it's an upper case character is must be made lower
	 * case.
	 */
	for (i = 0; i < strlen(name); i++) {
		if (!isalnum(name[i]) &&
		    (name[i] != ':') && (name[i] != '.') && (name[i] != '-')) {
			xml_rtn_msg(&msg, ERR_SYNTAX_INVALID_NAME);
			goto error;
		} else if (isupper(name[i]))
			name[i] = tolower(name[i]);
	}

	if (tgt_find_value_str(x, XML_ELEMENT_TYPE, &type) == False) {
		/*
		 * If a type hasn't been specified default to disk emulation.
		 * We use strdup() since at the end of this routine the code
		 * is expecting to free 'type' along with other strings.
		 */
		type = strdup(TGT_TYPE_DISK);
	}

	if ((strcmp(type, "raw") == 0) && (backing == NULL)) {
		xml_rtn_msg(&msg, ERR_SYNTAX_MISSING_BACKING_STORE);
		goto error;
	}

	if (tgt_find_value_str(x, XML_ELEMENT_SIZE, &size) == False) {
		if (backing != NULL) {

			/*
			 * If a backing store has been provided we don't
			 * need the size since we can determine that from
			 * a READ_CAPACITY command which everyone issues.
			 *
			 * NOTE: strdup is used here, since at the end
			 * of this routine any of the string pointers which
			 * are non-NULL get freed.
			 */
			size = strdup("0");
		} else {
			xml_rtn_msg(&msg, ERR_SYNTAX_MISSING_SIZE);
			goto error;
		}
	}
	if ((lun < 0) || (lun > T10_MAX_LUNS)) {
		xml_rtn_msg(&msg, ERR_LUN_INVALID_RANGE);
		goto error;
	}

	/*
	 * See if we already have a local target name created. If so,
	 * the user is most likely wanting to create another LUN for this
	 * target. Checking to see if there's a duplicate LUN will be
	 * done later.
	 */
	for (n = main_config->x_child; n; n = n->x_sibling) {
		if (strcmp(n->x_value, name) == 0)
			break;
	}

	if (n == NULL) {
		if (lun != 0) {
			xml_rtn_msg(&msg, ERR_LUN_ZERO_NOT_FIRST);
			goto error;
		}
		if ((node_name = create_node_name(name, alias)) == NULL) {
			xml_rtn_msg(&msg, ERR_CREATE_NAME_TOO_LONG);
			goto error;
		}
		if (create_target_dir(node_name, name) == False) {
			xml_rtn_msg(&msg, ERR_CREATE_TARGET_DIR_FAILED);
			goto error;
		}
		n = tgt_node_alloc(XML_ELEMENT_TARG, String, name);
		c = tgt_node_alloc(XML_ELEMENT_INAME, String, node_name);
		tgt_node_add(n, c);
		c = tgt_node_alloc(XML_ELEMENT_LUNLIST, String, "");
		l = tgt_node_alloc(XML_ELEMENT_LUN, Int, &lun);
		tgt_node_add(c, l);
		tgt_node_add(n, c);
		if (alias != NULL) {
			c = tgt_node_alloc(XML_ELEMENT_ALIAS, String, alias);
			tgt_node_add(n, c);
		}
		tgt_node_add(targets_config, n);

	} else {
		if (tgt_find_value_str(n, XML_ELEMENT_INAME,
		    &node_name) == False) {
			xml_rtn_msg(&msg, ERR_SYNTAX_MISSING_INAME);
			goto error;
		}
		if ((c = tgt_node_next(n, XML_ELEMENT_LUNLIST, NULL)) == NULL) {
			xml_rtn_msg(&msg, ERR_INTERNAL_ERROR);
			goto error;
		}
		l = tgt_node_alloc(XML_ELEMENT_LUN, Int, &lun);
		tgt_node_add(c, l);
	}

	if (create_lun(node_name, name, type, lun, size, backing, &code)
	    == True) {
		if (mgmt_config_save2scf() == False) {
			xml_rtn_msg(&msg, ERR_INTERNAL_ERROR);
			goto error;
		}

		/* Only isns register on the 1st creation of the target */
		if (lun == 0 && isns_enabled() == True) {
			if (isns_reg(node_name) != 0) {
				xml_rtn_msg(&msg, ERR_ISNS_ERROR);
				goto error;
			}
		}

	} else if ((lun == 0) && (code != ERR_LUN_EXISTS)) {

		/*
		 * The first LU will have created the directory and
		 * symbolic link. Remove those on error.
		 */
		(void) snprintf(path, sizeof (path), "%s/%s",
		    target_basedir, node_name);
		(void) rmdir(path);
		(void) snprintf(path, sizeof (path), "%s/%s",
		    target_basedir, name);
		(void) unlink(path);
		(void) tgt_node_remove(targets_config, n, MatchBoth);
	} else
		(void) tgt_node_remove(c, l, MatchBoth);

	xml_rtn_msg(&msg, code);

error:
	if (name != NULL)
		free(name);
	if (size != NULL)
		free(size);
	if (alias != NULL)
		free(alias);
	if (backing != NULL)
		free(backing);
	if (node_name != NULL)
		free(node_name);
	if (type != NULL)
		free(type);

	(void) pthread_rwlock_unlock(&targ_config_mutex);
	return (msg);
}

static char *
create_initiator(tgt_node_t *x)
{
	char		*msg		= NULL;
	char		*name		= NULL;
	char		*iscsi_name	= NULL;
	tgt_node_t	*inode		= NULL;
	tgt_node_t	*n, *c;

	(void) pthread_rwlock_wrlock(&targ_config_mutex);
	if (tgt_find_value_str(x, XML_ELEMENT_NAME, &name) == False) {
		xml_rtn_msg(&msg, ERR_SYNTAX_MISSING_NAME);
		goto error;
	}
	if (tgt_find_value_str(x, XML_ELEMENT_INAME, &iscsi_name) == False) {
		xml_rtn_msg(&msg, ERR_SYNTAX_MISSING_INAME);
		goto error;
	}
	if (strlen(iscsi_name) >= ISCSI_MAX_NAME_LEN) {
		xml_rtn_msg(&msg, ERR_NAME_TOO_LONG);
		goto error;
	}

	while ((inode = tgt_node_next_child(main_config, XML_ELEMENT_INIT,
	    inode)) != NULL) {
		if (strcmp(inode->x_value, name) == 0) {
			xml_rtn_msg(&msg, ERR_INIT_EXISTS);
			goto error;
		}
	}

	n = tgt_node_alloc(XML_ELEMENT_INIT, String, name);
	c = tgt_node_alloc(XML_ELEMENT_INAME, String, iscsi_name);
	tgt_node_add(n, c);
	tgt_node_add(main_config, n);

	if (mgmt_config_save2scf() == True)
		xml_rtn_msg(&msg, ERR_SUCCESS);
	else
		xml_rtn_msg(&msg, ERR_INTERNAL_ERROR);

error:
	if (name)
		free(name);
	if (iscsi_name)
		free(iscsi_name);

	(void) pthread_rwlock_unlock(&targ_config_mutex);
	return (msg);
}

static char *
create_tpgt(tgt_node_t *x)
{
	char		*msg	= NULL;
	char		*tpgt	= NULL;
	char		*extra	 = NULL;
	tgt_node_t	*tnode	= NULL;
	tgt_node_t	*n;
	int		tpgt_val;

	(void) pthread_rwlock_wrlock(&targ_config_mutex);
	if (tgt_find_value_str(x, XML_ELEMENT_NAME, &tpgt) == False) {
		xml_rtn_msg(&msg, ERR_SYNTAX_MISSING_NAME);
		goto error;
	}

	/* ---- Validation checks ---- */
	tpgt_val = strtol(tpgt, &extra, 0);
	if ((extra && (*extra != '\0')) ||
	    (tpgt_val < TPGT_MIN) || (tpgt_val > TPGT_MAX)) {
		xml_rtn_msg(&msg, ERR_INVALID_TPGT);
		goto error;
	}

	while ((tnode = tgt_node_next_child(main_config, XML_ELEMENT_TPGT,
	    tnode)) != NULL) {
		if (strcmp(tnode->x_value, tpgt) == 0) {
			xml_rtn_msg(&msg, ERR_TPGT_EXISTS);
			goto error;
		}
	}

	n = tgt_node_alloc(XML_ELEMENT_TPGT, String, tpgt);
	tgt_node_add(main_config, n);

	if (mgmt_config_save2scf() == True)
		xml_rtn_msg(&msg, ERR_SUCCESS);
	else
		xml_rtn_msg(&msg, ERR_INTERNAL_ERROR);

error:
	if (tpgt)
		free(tpgt);

	(void) pthread_rwlock_unlock(&targ_config_mutex);
	return (msg);
}

/*
 * create_zfs -- given a dataset, export it through the iSCSI protocol
 *
 * This function is called when someone uses the libiscsitgt function
 * iscsitgt_zfs_share(char *dataset)
 */
static char *
create_zfs(tgt_node_t *x, ucred_t *cred)
{
	char		*msg		= NULL;
	char		*dataset	= NULL;
	char		*cptr		= NULL;
	char		path[MAXPATHLEN];
	tgt_node_t	*n = NULL;
	tgt_node_t	*c;
	tgt_node_t	*l;
	uint64_t	size;
	int		status;

	(void) pthread_rwlock_wrlock(&targ_config_mutex);
	/*
	 * Extract the dataset name from the arguments passed in
	 */
	if (tgt_find_value_str(x, XML_ELEMENT_NAME, &dataset) == False) {
		xml_rtn_msg(&msg, ERR_SYNTAX_MISSING_NAME);
		goto error;
	}

	/*
	 * Since this is a create, assure that an existing dataset with the
	 * same name does not exists
	 */
	c = NULL;
	while ((c = tgt_node_next_child(targets_config, XML_ELEMENT_TARG, c))) {
		if (strcmp(c->x_value, dataset) == 0) {
			xml_rtn_msg(&msg, ERR_LUN_EXISTS);
			goto error;
		}
	}

	/*
	 * See if this is a re-create of a previously create ZVOL target
	 * If no shareiscsi properties exists, create a new set of properties
	 */
	status = get_zfs_shareiscsi(dataset, &n, &size, cred);
	if ((status != ERR_SUCCESS) && (status != ERR_NULL_XML_MESSAGE)) {
		xml_rtn_msg(&msg, status);
		goto error;
	} else if (status == ERR_NULL_XML_MESSAGE) {

		char	*name;
		int	lun = 0;
		int	guid = 0;
		int	rpm = DEFAULT_RPM;
		int	heads = DEFAULT_HEADS;
		int	cylinders = DEFAULT_CYLINDERS;
		int	spt = DEFAULT_SPT;
		int	bytes_sect = DEFAULT_BYTES_PER;
		int	interleave = DEFAULT_INTERLEAVE;

		n = tgt_node_alloc(XML_ELEMENT_TARG, String, NULL);
		c = tgt_node_alloc(XML_ELEMENT_INCORE, String, XML_VALUE_TRUE);
		tgt_node_add_attr(n, c);

		if (name = create_node_name(NULL, NULL)) {
			c = tgt_node_alloc(XML_ELEMENT_INAME, String, name);
			tgt_node_add(n, c);
			free(name);
		} else {
			xml_rtn_msg(&msg, ERR_CREATE_NAME_TOO_LONG);
			goto error;
		}

		c = tgt_node_alloc(XML_ELEMENT_LUNLIST, String, "");
		tgt_node_add(n, c);

		l = tgt_node_alloc(XML_ELEMENT_LUN, Int, &lun);
		tgt_node_add(c, l);

		c = tgt_node_alloc(XML_ELEMENT_VERS, String, "1.0");
		tgt_node_add_attr(l, c);

		c = tgt_node_alloc(XML_ELEMENT_GUID, Int, &guid);
		tgt_node_add(l, c);

		c = tgt_node_alloc(XML_ELEMENT_PID, String, DEFAULT_PID);
		tgt_node_add(l, c);

		c = tgt_node_alloc(XML_ELEMENT_VID, String, DEFAULT_VID);
		tgt_node_add(l, c);

		c = tgt_node_alloc(XML_ELEMENT_DTYPE, String, TGT_TYPE_DISK);
		tgt_node_add(l, c);

		create_geom(size, &cylinders, &heads, &spt);

		c = tgt_node_alloc(XML_ELEMENT_RPM, Int, &rpm);
		tgt_node_add(l, c);

		c = tgt_node_alloc(XML_ELEMENT_HEADS, Int, &heads);
		tgt_node_add(l, c);

		c = tgt_node_alloc(XML_ELEMENT_CYLINDERS, Int, &cylinders);
		tgt_node_add(l, c);

		c = tgt_node_alloc(XML_ELEMENT_SPT, Int, &spt);
		tgt_node_add(l, c);

		c = tgt_node_alloc(XML_ELEMENT_BPS, Int, &bytes_sect);
		tgt_node_add(l, c);

		c = tgt_node_alloc(XML_ELEMENT_INTERLEAVE, Int, &interleave);
		tgt_node_add(l, c);

		c = tgt_node_alloc(XML_ELEMENT_STATUS, String,
		    TGT_STATUS_ONLINE);
		tgt_node_add(l, c);

		/*
		 * Set the ZFS persisted shareiscsi options
		 */
		if ((status = put_zfs_shareiscsi(dataset, n)) != ERR_SUCCESS) {
			xml_rtn_msg(&msg, status);
			goto error;
		}
	} else {
		/*
		 * If the was a recreate of a ZVOL iSCSI Target, 'n' is expected
		 * to contain the properties for this iSCSI target node
		 *
		 * Make sure these properties have the "in-core" attribute
		 */
		if (tgt_find_attr_str(n, XML_ELEMENT_INCORE, &cptr) == True) {
			if (strcmp(cptr, "true") != 0) {
				free(cptr);
				xml_rtn_msg(&msg, ERR_INTERNAL_ERROR);
				goto error;
			}
			free(cptr);
		}
	}

	/*
	 * Pick up the LU node from the LU List which hangs off of the target
	 * node. If either is NULL there's an internal error someplace.
	 */
	if (((l = tgt_node_next(n, XML_ELEMENT_LUNLIST, NULL)) == NULL) ||
	    ((l = tgt_node_next(l, XML_ELEMENT_LUN, NULL)) == NULL)) {
		xml_rtn_msg(&msg, ERR_INTERNAL_ERROR);
			goto error;
	}

	/*
	 * With ZVOLS, some elements can change everytime we share the dataset.
	 * The TargetAlias and backing store can change since these are based on
	 * the dataset and the size of the volume. Therefore, these pieces of
	 * information are not stored as part of the iscsioptions properity, but
	 * are retained in the in-core targets_config
	 */
	(void) tgt_update_value_str(n, XML_ELEMENT_TARG, dataset);
	c = tgt_node_alloc(XML_ELEMENT_ALIAS, String, dataset);
	tgt_node_add(n, c);

	(void) snprintf(path, sizeof (path), "%s%s", ZVOL_PATH, dataset);
	c = tgt_node_alloc(XML_ELEMENT_BACK, String, path);
	tgt_node_add(l, c);

	size /= 512LL;
	c = tgt_node_alloc(XML_ELEMENT_SIZE, Uint64, &size);
	tgt_node_add(l, c);

	cptr = NULL;
	if (tgt_find_value_str(n, XML_ELEMENT_INAME, &cptr) != True) {
		xml_rtn_msg(&msg, ERR_SYNTAX_MISSING_INAME);
		goto error; /* xml node contruction has an issue. */
	}

	/*
	 * Add ZVOL target to config of all targets
	 */
	tgt_node_add(targets_config, n);
	n = NULL; /* don't remove the node from the targets_config. */

	/* register with iSNS */
	if (isns_enabled() == True) {
		if (isns_reg(cptr) != 0) {
			xml_rtn_msg(&msg, ERR_ISNS_ERROR);
			goto error;
		}
	}

	xml_rtn_msg(&msg, ERR_SUCCESS);

error:
	if (cptr)
		free(cptr);
	if (dataset)
		free(dataset);
	if (n)
		tgt_node_free(n);

	(void) pthread_rwlock_unlock(&targ_config_mutex);
	return (msg);
}

/*
 * []------------------------------------------------------------------[]
 * | Utility functions used by the routines above.			|
 * []------------------------------------------------------------------[]
 */

/*
 * []----
 * | create_node_name -- Creates the IQN that adhears to RFC3270
 * []----
 */
static char *
create_node_name(char *local_nick, char *alias)
{
	uuid_t	id;
	char	id_str[37];
	char	*p;
	char	*anp;		/* alias or nick pointer */

	if ((p = (char *)malloc(ISCSI_MAX_NAME_LEN)) == NULL)
		return (NULL);

	/*
	 * Originally we we going to use the machines MAC address and
	 * timestamp in hex format. This would be consistent with the
	 * Solaris iSCSI initiator and NAS5310. Unfortunately, someone
	 * pointed out that there's no requirement that the network
	 * interfaces be plumbed before someone attempts to create
	 * targets. If the networks aren't plumbed there are no MAC
	 * addresses available and we can't use 0 for the MAC address
	 * since that would introduce the probability of non-unique
	 * IQN names.
	 */
	uuid_generate(id);
	uuid_unparse(id, id_str);
	if (snprintf(p, ISCSI_MAX_NAME_LEN, "iqn.1986-03.com.sun:%02d:%s",
	    TARGET_NAME_VERS, id_str) > ISCSI_MAX_NAME_LEN) {
		free(p);
		return (NULL);
	}
	if (local_nick || alias) {
		anp = (alias != NULL) ? alias : local_nick;

		/*
		 * Make sure we still have room to add the alias or
		 * local nickname, a '.', and NULL character to the
		 * buffer.
		 */
		if ((strlen(p) + strlen(anp) + 2) > ISCSI_MAX_NAME_LEN) {
			free(p);
			return (NULL);
		}
		(void) strcat(p, ".");
		(void) strcat(p, anp);
	}

	return (p);
}

/*
 * []----
 * | create_target_dir -- create the target directory
 * []----
 */
static Boolean_t
create_target_dir(char *targ_name, char *local_name)
{
	char	path[MAXPATHLEN];
	char	sympath[MAXPATHLEN];

	if ((mkdir(target_basedir, 0777) == -1) && (errno != EEXIST))
		return (False);

	(void) snprintf(path, sizeof (path), "%s/%s", target_basedir,
	    targ_name);
	(void) snprintf(sympath, sizeof (sympath), "%s/%s", target_basedir,
	    local_name);

	if ((mkdir(path, 0777) == -1) && (errno != EEXIST))
		return (False);

	/*
	 * This symbolic link is here for convenience and nothing more, so if
	 * if fails. Oh well.
	 */
	(void) symlink(path, sympath);
	return (True);
}

/*
 * []----
 * | create_lun -- given type, lun, size, backing create LU and params
 * []----
 */
static Boolean_t
create_lun(char *targ_name, char *local_name, char *type, int lun,
    char *size_str, char *backing, err_code_t *code)
{
	uint64_t	size, ssize;
	int		fd		= -1;
	int		rpm		= DEFAULT_RPM;
	int		heads		= DEFAULT_HEADS;
	int		cylinders	= DEFAULT_CYLINDERS;
	int		spt		= DEFAULT_SPT;
	int		bytes_sect	= DEFAULT_BYTES_PER;
	int		interleave	= DEFAULT_INTERLEAVE;
	char		*vid		= DEFAULT_VID;
	char		*pid		= DEFAULT_PID;
	char		path[MAXPATHLEN];
	tgt_node_t	*n		= NULL;
	tgt_node_t	*pn		= NULL;

	/*
	 * after calling stroll_multipler it's an error for size to be
	 * 0, if and only if, the size_str doesn't equal "0". The administrator
	 * may want the code to determine the size. This would be the case
	 * when the administrator has provide a backing store which exists.
	 */
	if ((strtoll_multiplier(size_str, &size) == False) ||
	    ((size == 0) && (size_str != NULL) && strcmp(size_str, "0"))) {
		*code = ERR_INVALID_SIZE;
		return (False);
	}

	if ((size % 512) != 0) {
		*code = ERR_SIZE_MOD_BLOCK;
		return (False);
	}

	/*
	 * Make sure we're not trying to recreate an existing LU.
	 */
	(void) snprintf(path, sizeof (path), "%s/%s/%s%d", target_basedir,
	    targ_name, LUNBASE, lun);
	if (access(path, F_OK) == 0) {
		*code = ERR_LUN_EXISTS;
		return (False);
	}

	n = tgt_node_alloc(XML_ELEMENT_PARAMS, String, NULL);

	pn = tgt_node_alloc(XML_ELEMENT_VERS, String, "1.0");
	tgt_node_add_attr(n, pn);

	pn = tgt_node_alloc(XML_ELEMENT_GUID, String, "0");
	tgt_node_add(n, pn);
	pn = tgt_node_alloc(XML_ELEMENT_PID, String, pid);
	tgt_node_add(n, pn);
	pn = tgt_node_alloc(XML_ELEMENT_VID, String, vid);
	tgt_node_add(n, pn);
	pn = tgt_node_alloc(XML_ELEMENT_DTYPE, String, type);
	tgt_node_add(n, pn);

	if (strcmp(type, TGT_TYPE_DISK) == 0) {

		(void) snprintf(path, sizeof (path), "%s/%s/lun.%d",
		    target_basedir, targ_name, lun);
		if (setup_disk_backing(code, path, backing, n, &size) == False)
			goto error;

		create_geom(size, &cylinders, &heads, &spt);

		pn = tgt_node_alloc(XML_ELEMENT_RPM, Int, &rpm);
		tgt_node_add(n, pn);
		pn = tgt_node_alloc(XML_ELEMENT_HEADS, Int, &heads);
		tgt_node_add(n, pn);
		pn = tgt_node_alloc(XML_ELEMENT_CYLINDERS, Int, &cylinders);
		tgt_node_add(n, pn);
		pn = tgt_node_alloc(XML_ELEMENT_SPT, Int, &spt);
		tgt_node_add(n, pn);
		pn = tgt_node_alloc(XML_ELEMENT_BPS, Int, &bytes_sect);
		tgt_node_add(n, pn);
		pn = tgt_node_alloc(XML_ELEMENT_INTERLEAVE, Int, &interleave);
		tgt_node_add(n, pn);
		pn = tgt_node_alloc(XML_ELEMENT_STATUS, String,
		    TGT_STATUS_OFFLINE);
		tgt_node_add(n, pn);

	} else if (strcmp(type, TGT_TYPE_TAPE) == 0) {
#ifndef	_LP64
		*code = ERR_TAPE_NOT_SUPPORTED_IN_32BIT;
		goto error;
#else
		pn = tgt_node_alloc(XML_ELEMENT_STATUS, String,
		    TGT_STATUS_OFFLINE);
		tgt_node_add(n, pn);

		(void) snprintf(path, sizeof (path), "%s/%s/lun.%d",
		    target_basedir, targ_name, lun);
		if (setup_disk_backing(code, path, backing, n, &size) == False)
			goto error;
#endif

	} else if (strcmp(type, TGT_TYPE_RAW) == 0) {

		pn = tgt_node_alloc(XML_ELEMENT_STATUS, String,
		    TGT_STATUS_ONLINE);
		tgt_node_add(n, pn);

		backing = getfullrawname(backing);
		if (setup_raw_backing(code, path, backing, &size) == False)
			goto error;

		pn = tgt_node_alloc(XML_ELEMENT_MMAP_LUN, String, "false");
		tgt_node_add(n, pn);

		(void) snprintf(path, sizeof (path), "%s/%s/lun.%d",
		    target_basedir, targ_name, lun);
		if (symlink(backing, path)) {
			*code = ERR_CREATE_SYMLINK_FAILED;
			goto error;
		}

		iscsi_inventory_change(targ_name);

	} else if (strcmp(type, TGT_TYPE_OSD) == 0) {

		(void) snprintf(path, sizeof (path), "%s/%s/%s%d",
		    target_basedir, targ_name, OSDBASE, lun);
		if (mkdir(path, 0700) != 0)
			goto error;
	}


	/*
	 * Wait to set the size until here because it may be unknown until
	 * the possible backing store has been setup.
	 */
	ssize = size / 512LL;
	pn = tgt_node_alloc(XML_ELEMENT_SIZE, Uint64, &ssize);
	tgt_node_add(n, pn);

	if (backing != NULL) {
		pn = tgt_node_alloc(XML_ELEMENT_BACK, String, backing);
		tgt_node_add(n, pn);
	}

	(void) mgmt_param_save2scf(n, local_name, lun);

	if ((strcmp(type, TGT_TYPE_DISK) == 0) ||
	    (strcmp(type, TGT_TYPE_TAPE) == 0)) {
		if (create_lun_common(targ_name, local_name, lun, size,
		    code) == False)
			goto error;
	}

	tgt_node_free(n);

	*code = ERR_SUCCESS;
	return (True);

error:
	/* Free node n */
	tgt_node_free(n);

	(void) snprintf(path, sizeof (path), "%s/%s/%s%d", target_basedir,
	    targ_name, PARAMBASE, lun);
	(void) unlink(path);
	if (fd == -1)
		(void) close(fd);
	return (False);
}

/*
 * []----
 * | create_lun_common -- create LU and start provisioning if needed
 * |
 * | This function is common to both the tape and disk emulation
 * | code.
 * []----
 */
static Boolean_t
create_lun_common(char *targ_name, char *local_name, int lun, uint64_t size,
    err_code_t *code)
{
	struct stat		s;
	int			fd			= -1;
	char			path[MAXPATHLEN];
	char			buf[512];
	struct statvfs		fs;
	tgt_node_t		*node			= NULL;
	tgt_node_t		*c;

	/*
	 * Touch the last block of the file which will cause file systems
	 * to understand the intent of the file to be a certain size. The
	 * space isn't allocated, but the daemon can then mmap in this file
	 * and start writing to it.
	 */
	(void) snprintf(path, sizeof (path), "%s/%s/%s%d",
	    target_basedir, targ_name, LUNBASE, lun);
	if ((fd = open(path, O_RDWR|O_CREAT|O_LARGEFILE, 0600)) < 0)
		goto error;

	(void) lseek(fd, size - 512LL, 0);
	bzero(buf, sizeof (buf));
	if (write(fd, buf, sizeof (buf)) != sizeof (buf)) {
		(void) unlink(path);
		if (errno == EFBIG)
			*code = ERR_FILE_TOO_BIG;
		else
			*code = ERR_FAILED_TO_CREATE_LU;
		goto error;
	}
	(void) close(fd);

	/*
	 * Set the fd back to -1 so that if an error occurs we don't
	 * attempt to close this device twice. This could be an issue
	 * if another thread opened a file right after we closed this
	 * one and the system reused the file descriptor. During an
	 * error we would then close another threads file which would
	 * be ugly, not to mention difficult to track down.
	 */
	fd = -1;

	if (stat(path, &s) != 0) {
		*code = ERR_FAILED_TO_CREATE_LU;
		goto error;
	}

	/*
	 * If the backing store is a regular file and the default is
	 * used which initializes the file instead of sparse allocation
	 * go ahead a set things up.
	 */
	if ((thin_provisioning == False) && ((s.st_mode & S_IFMT) == S_IFREG)) {
		thick_provo_t	*tp;
		pthread_t	junk;

		/*
		 * Attempt to see if there is enough space currently
		 * for the LU. The initialization might still fail
		 * with "out of space" because someone else is
		 * consuming space while the initialization is occuring.
		 * Nothing we can do about that.
		 */
		if (statvfs(path, &fs) != 0) {
			queue_prt(mgmtq, Q_GEN_ERRS,
			    "GEN  statvfs failed for %s", path);
			*code = ERR_FAILED_TO_CREATE_LU;
			goto error;
		} else if ((fs.f_frsize * fs.f_bfree) < size) {
			queue_prt(mgmtq, Q_STE_ERRS,
			    "GEN  Not enough space for LU");
			*code = ERR_FILE_TOO_BIG;
			goto error;
		}

		/*
		 * Run the initialization thread in the background so that
		 * the administrator doesn't have to wait which for UFS could
		 * be a long time on a large LU.
		 */
		if ((tp = calloc(1, sizeof (*tp))) != NULL) {
			tp->targ_name	= strdup(targ_name);
			tp->lun		= lun;
			tp->q		= queue_alloc();
			(void) pthread_create(&junk, NULL,
			    thick_provo_start, tp);

			/*
			 * As soon as the thread starts it will send a simple
			 * ACK to it's own queue that we can look for. When
			 * we see this message we know that the thread has
			 * started and it's been added to the provisioning
			 * list. If this were not done it's possible for someone
			 * to create and delete a target within a script and
			 * have the delete run and fail to find the provision
			 * thread in the list.
			 */
			queue_message_free(queue_message_get(tp->q));
		}
	} else {
		(void) mgmt_get_param(&node, local_name, lun);

		c = tgt_node_alloc(XML_ELEMENT_STATUS, String,
		    TGT_STATUS_ONLINE);
		tgt_node_replace(node, c, MatchName);
		tgt_node_free(c);

		if (mgmt_param_save2scf(node, local_name, lun) == False) {
			queue_prt(mgmtq, Q_STE_ERRS,
			    "GEN%d  failed to dump out params", lun);
			goto error;
		}
		iscsi_inventory_change(targ_name);
		tgt_node_free(node);
	}

	return (True);

error:
	if (fd != -1)
		(void) close(fd);
	if (node)
		tgt_node_free(node);
	return (False);
}

static Boolean_t
readvtoc(int fd, struct extvtoc *v, int *slice)
{
	if ((*slice = read_extvtoc(fd, v)) >= 0)
		return (True);
	else
		return (False);
}

static Boolean_t
readefi(int fd, struct dk_gpt **efi, int *slice)
{
	if ((*slice = efi_alloc_and_read(fd, efi)) >= 0)
		return (True);
	else
		return (False);
}

/*
 * []----
 * | setup_alt_backing -- use backing store link for regular file lun
 * |
 * | If the size is zero, then the administrator MUST have
 * | specified a backing store to use.
 * | If the size is non-zero and the backing store doesn't exist it will
 * | be created. Also a tag will be added indicating that during removal
 * | the backing store should be deleted as well.
 * []----
 */
static Boolean_t
setup_disk_backing(err_code_t *code, char *path, char *backing, tgt_node_t *n,
    uint64_t *size)
{
	struct stat	s;
	char		*raw_name, buf[512];
	struct extvtoc	extvtoc;
	struct dk_gpt	*efi;
	int		slice, fd;
	tgt_node_t	*pn;

	/*
	 * Error checking regarding size and backing store has already
	 * been done. If the backing store is null at this point everything
	 * is okay so just return True.
	 */
	if (backing == NULL)
		return (True);

	if (stat(backing, &s) == -1) {
		if (*size == 0) {
			*code = ERR_STAT_BACKING_FAILED;
			return (False);
		} else {
			pn = tgt_node_alloc(XML_ELEMENT_DELETE_BACK, String,
			    "true");
			tgt_node_add(n, pn);
			if ((fd = open(backing, O_RDWR|O_CREAT|O_LARGEFILE,
			    0600)) < 0) {
				*code = ERR_FAILED_TO_CREATE_LU;
				return (False);
			}
			(void) lseek(fd, *size - 512LL, 0);
			bzero(buf, sizeof (buf));
			(void) write(fd, buf, sizeof (buf));
			(void) close(fd);
		}
	} else if (*size != 0) {
		*code = ERR_DISK_BACKING_SIZE_OR_FILE;
		return (False);
	} else if (((s.st_mode & S_IFMT) == S_IFCHR) ||
	    ((s.st_mode & S_IFMT) == S_IFBLK)) {
		raw_name = getfullrawname(backing);
		if ((raw_name == NULL) ||
		    ((fd = open(raw_name, O_NONBLOCK|O_RDONLY)) < 0)) {
			*code = ERR_DISK_BACKING_NOT_VALID_RAW;
			(void) close(fd);
			if (raw_name)
				free(raw_name);
			return (False);
		}
		free(raw_name);
		if (readvtoc(fd, &extvtoc, &slice) == True) {
			*size = extvtoc.v_part[slice].p_size * 512;

		} else if (readefi(fd, &efi, &slice) == True) {
			*size = efi->efi_parts[slice].p_size * 512;
			efi_free(efi);
		} else {
			*code = ERR_DISK_BACKING_NOT_VALID_RAW;
			(void) close(fd);
			return (False);
		}
		(void) close(fd);

	} else if ((s.st_mode & S_IFMT) == S_IFREG) {
		*size = s.st_size;
	} else {
		*code = ERR_DISK_BACKING_MUST_BE_REGULAR_FILE;
		return (False);
	}

	if (symlink(backing, path)) {
		*code = ERR_CREATE_SYMLINK_FAILED;
		return (False);
	}

	return (True);
}

/*
 * []----
 * | validate_raw_backing -- check that device is full partition
 * |
 * | The size of the device will be returned in rtn_size in bytes.
 * |
 * | Need to guarantee that the backing store for a raw device is:
 * | (a) character device
 * | (b) Not buffered
 * |     Don't want this host to have data which is not flushed
 * |     out during a write since a multiple path access to
 * |     the backing store would be possible meaning we'd have
 * |     cache issue.
 * | (c) read/write will access entire device.
 * |     To speed things up we use asynchronous I/O which means
 * |     the path has to have access to the entire device through
 * |     the partition table. If not, some client will issue a
 * |     READ_CAPACITY command, but not be able to access all of
 * |     the data.
 * []----
 */
static Boolean_t
setup_raw_backing(err_code_t *code, char *path, char *backing,
    uint64_t *rtn_size)
{
	struct stat		s;
	char			buf[512];
	int			fd;
	uint64_t		size;
	struct uscsi_cmd	u;
	struct scsi_extended_sense	sense;
	union scsi_cdb		cdb;
	struct scsi_capacity	cap;
	struct scsi_capacity_16	cap16;
	int			cap_len = sizeof (cap16);
	size_t			cc;
	Boolean_t		rval		= False;

	if (stat(backing, &s) == -1) {
		*code = ERR_ACCESS_RAW_DEVICE_FAILED;
		return (False);
	} else if (((s.st_mode & S_IFMT) != S_IFCHR) &&
	    ((s.st_mode & S_IFMT) != S_IFBLK)) {
		*code = ERR_DISK_BACKING_NOT_VALID_RAW;
		return (False);
	}

	if ((backing == NULL) ||
	    ((fd = open(backing, O_NDELAY|O_RDONLY|O_LARGEFILE)) < 0)) {
		*code = ERR_DISK_BACKING_NOT_VALID_RAW;
		(void) close(fd);
		return (False);
	}

	bzero(&u, sizeof (u));
	bzero(&cdb, sizeof (cdb));
	bzero(&cap, sizeof (cap));
	bzero(&sense, sizeof (sense));

	cdb.scc_cmd	= 0x25;	/* ---- READ_CAPACITY(10) ---- */

	u.uscsi_cdb	= (caddr_t)&cdb;
	u.uscsi_cdblen	= CDB_GROUP1;
	u.uscsi_bufaddr	= (caddr_t)&cap;
	u.uscsi_buflen	= sizeof (cap);
	u.uscsi_flags	= USCSI_READ | USCSI_RQENABLE;
	u.uscsi_rqbuf	= (char *)&sense;
	u.uscsi_rqlen	= sizeof (sense);

	if ((ioctl(fd, USCSICMD, &u) != 0) || (u.uscsi_status != 0)) {
		queue_prt(mgmtq, Q_GEN_DETAILS, "GEN0  uscsi(READ_CAP) failed");
		*code = ERR_DISK_BACKING_NOT_VALID_RAW;
		rval = False;
		goto error;
	}

	if (cap.capacity == 0xffffffff) {

		bzero(&u, sizeof (u));
		bzero(&cdb, sizeof (cdb));
		bzero(&sense, sizeof (sense));
		/*
		 * The device is to large for the 10byte CDB.
		 * Using the larger 16byte read capacity command
		 */
		cdb.scc_cmd	= 0x9E;
		cdb.g4_reladdr	= 0x10;
		cdb.g4_count3	= hibyte(hiword(cap_len));
		cdb.g4_count2	= lobyte(hiword(cap_len));
		cdb.g4_count1	= hibyte(loword(cap_len));
		cdb.g4_count0	= lobyte(loword(cap_len));

		u.uscsi_cdb	= (caddr_t)&cdb;
		u.uscsi_cdblen	= CDB_GROUP4;
		u.uscsi_bufaddr	= (caddr_t)&cap16;
		u.uscsi_buflen	= sizeof (cap16);
		u.uscsi_flags	= USCSI_READ | USCSI_RQENABLE;
		u.uscsi_rqbuf	= (char *)&sense;
		u.uscsi_rqlen	= sizeof (sense);

		if ((ioctl(fd, USCSICMD, &u) != 0) || (u.uscsi_status != 0)) {
			queue_prt(mgmtq, Q_GEN_DETAILS,
			    "GEN0  uscsi(READ_CAP16) failed");
			*code = ERR_DISK_BACKING_NOT_VALID_RAW;
			rval = False;
			goto error;
		}

		size = ntohll(cap16.sc_capacity) - 1;
	} else
		size = (uint64_t)ntohl(cap.capacity) - 1;

	if ((cc = pread(fd, buf, sizeof (buf), size * 512LL)) != sizeof (buf)) {
		queue_prt(mgmtq, Q_GEN_DETAILS,
		    "GEN0  Partition size != capacity(0x%llx), cc=%d, errno=%d",
		    size, cc, errno);
		*code = ERR_RAW_PART_NOT_CAP;
		rval = False;
		goto error;
	} else {
		*rtn_size = size * 512LL;
		rval = True;
	}

error:
	(void) close(fd);
	return (rval);
}

/*
 * get_zfs_shareiscsi -- given a dataset, get the ZFS properties
 *
 * This function is called when "set shareiscsi=on" calles into libiscsitgt,
 * such that the iSCSI Target can get the ZFS_PROP_ISCSIOPTIONS. This is in
 * lieu of properties being stored in SCF.
 */
int
get_zfs_shareiscsi(char *dataset, tgt_node_t **n, uint64_t *size, ucred_t *cred)
{
	libzfs_handle_t		*zh;
	zfs_handle_t		*zfsh;
	const priv_set_t	*eset;
	tgt_node_t		*c;
	char			*prop = NULL;
	char			*cp;	/* current pair */
	char			*np;	/* next pair */
	char			*vp;	/* value pointer */
	int			status  = ERR_SUCCESS;

	if (((zh = libzfs_init()) == NULL) ||
	    ((zfsh = zfs_open(zh, dataset, ZFS_TYPE_DATASET)) == NULL)) {
		status = ERR_INTERNAL_ERROR;
		goto error;
	}

	if (((eset = ucred_getprivset(cred, PRIV_EFFECTIVE)) != NULL)
	    ? !priv_ismember(eset, PRIV_SYS_CONFIG)
	    : ucred_geteuid(cred) != 0) {
		/*
		 * See if user has ZFS dataset permissions to do operation
		 */
		if (zfs_iscsi_perm_check(zh, dataset, cred) != 0) {
			status = ERR_NO_PERMISSION;
			goto error;
		}
	}

	/*
	 * Get the current size of the volume, return to caller
	 */
	*size = zfs_prop_get_int(zfsh, ZFS_PROP_VOLSIZE);

	/*
	 * Allocate a local buffer to read the ZFS properties into
	 */
	if ((prop = malloc(ZFS_PROP_SIZE)) == NULL) {
		status = ERR_INTERNAL_ERROR;
		goto error;
	}

	/*
	 * Get the shareiscsi property
	 */
	*prop = '\0';
	if (zfs_prop_get(zfsh, ZFS_PROP_SHAREISCSI, prop, ZFS_PROP_SIZE, NULL,
	    NULL, 0, B_TRUE)) {
		status = ERR_INTERNAL_ERROR;
		goto error;
	}

	/*
	 * The options property is a string with name/value pairs separated
	 * by comma characters. Stand alone values of 'on' and 'off' are
	 * also permitted, but having the property set to off when share()
	 * is called is an error.
	 * Currently we only look for 'type=<value>' and ignore others.
	 */

	for (cp = prop; cp; cp = np) {
		if (np = strchr(cp, ','))
			*np++ = '\0';
		if (strcmp(cp, "on") == 0) {
			cp = np;
			continue;
		}
		if (strcmp(cp, "off") == 0) {
			status = ERR_ZFS_ISCSISHARE_OFF;
			goto error;
		}
		if (vp = strchr(cp, '='))
			*vp++ = '\0';
		/*
		 * Only support 'disk' emulation at this point.
		 */
		if ((strcmp(cp, "type") == 0) && (strcmp(vp, "disk") != 0)) {
			status = ERR_INTERNAL_ERROR;
			goto error;
		}
	}

	/*
	 * Now get the ZFS persisted shareiscsi options
	 */
	*prop = '\0';
	if (zfs_prop_get(zfsh, ZFS_PROP_ISCSIOPTIONS, prop, ZFS_PROP_SIZE, NULL,
	    NULL, 0, B_TRUE) && (status != ERR_ZFS_ISCSISHARE_OFF)) {
		status = ERR_INTERNAL_ERROR;
		goto error;
	}

	/*
	 * Now move the ZFS persisted shareiscsi options into XML, then into
	 * iSCSI Target properties
	 */
	if (strlen(prop)) {
		xmlTextReaderPtr xml_ptr = (xmlTextReaderPtr)xmlReaderForMemory(
		    prop, strlen(prop), NULL, NULL, 0);

		if (xml_ptr != NULL) {
			*n = NULL;
			while (xmlTextReaderRead(xml_ptr)) {
				if (tgt_node_process(xml_ptr, n) == False) {
					break;
				}
			}

			/* Cleanup XML data */
			(void) xmlTextReaderClose(xml_ptr);
			xmlFreeTextReader(xml_ptr);
			xmlCleanupParser();

			/* Assure these XML elements are tagged as in-core */
			if (tgt_find_attr_str(*n, XML_ELEMENT_INCORE, &cp)
			    == False) {
				c = tgt_node_alloc(XML_ELEMENT_INCORE, String,
				    XML_VALUE_TRUE);
				tgt_node_add_attr(*n, c);
			} else {
				free(cp);
			}

		} else {
			status = ERR_NULL_XML_MESSAGE;
		}
	} else {
		status = ERR_NULL_XML_MESSAGE;
	}

error:
	if (prop)
		free(prop);
	if (zh) {
		if (zfsh)
			zfs_close(zfsh);
		libzfs_fini(zh);
	}
	return (status);
}

/*
 * put_zfs_shareiscsi -- given a dataset, put the ZFS properties
 *
 * This function is called whenever persistence is needed to the set of
 * iSCSI Target properties stored in ZFS_PROP_ISCSIOPTIONS. This is in lieu
 * of properties being stored in SCF.
 */
int
put_zfs_shareiscsi(char *dataset, tgt_node_t *n)
{
	libzfs_handle_t		*zh;
	zfs_handle_t		*zfsh;
	char			*prop = NULL;
	int			status;

	if (((zh = libzfs_init()) == NULL) ||
	    ((zfsh = zfs_open(zh, dataset, ZFS_TYPE_DATASET)) == NULL)) {
		status = ERR_INTERNAL_ERROR;
		goto error;
	}

	/*
	 * Now store this information on the ZVOL property so that
	 * next time we get a shareiscsi request the same data will be
	 * used.
	 */
	tgt_dump2buf(n, &prop);
	if (zfs_prop_set(zfsh, zfs_prop_to_name(ZFS_PROP_ISCSIOPTIONS), prop)) {
		status = ERR_INTERNAL_ERROR;
	} else {
		status = ERR_SUCCESS;
	}

error:
	if (prop)
		free(prop);
	if (zh) {
		if (zfsh)
			zfs_close(zfsh);
		libzfs_fini(zh);
	}
	return (status);
}
