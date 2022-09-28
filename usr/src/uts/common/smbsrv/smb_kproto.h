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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Function prototypes for the SMB module.
 */

#ifndef _SMB_KPROTO_H_
#define	_SMB_KPROTO_H_

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/strsubr.h>
#include <sys/socketvar.h>
#include <sys/ksocket.h>
#include <sys/cred.h>
#include <sys/sunddi.h>
#include <smbsrv/smb.h>
#include <smbsrv/string.h>
#include <smbsrv/smb_vops.h>
#include <smbsrv/smb_xdr.h>
#include <smbsrv/smb_token.h>
#include <smbsrv/smb_ktypes.h>
#include <smbsrv/smb_ioctl.h>

extern	int smb_maxbufsize;
extern	int smb_flush_required;
extern	int smb_dirsymlink_enable;
extern	int smb_oplock_timeout;
extern	int smb_sign_debug;
extern	uint_t smb_audit_flags;

int		fd_dealloc(int);

off_t		lseek(int fildes, off_t offset, int whence);

int		arpioctl(int cmd, void *data);
int		microtime(timestruc_t *tvp);
int		clock_get_uptime(void);

/*
 * SMB request handers called from the dispatcher.  Each SMB request
 * is handled in three phases: pre, com (command) and post.
 *
 * The pre-handler is primarily to set things up for the DTrace start
 * probe.  Typically, the SMB request is unmarshalled so that request
 * specific context can be traced.  This is also a useful place to
 * allocate memory that will be used throughout the processing of the
 * command.
 *
 * The com-handler performs the requested operation: request validation,
 * bulk (write) incoming data decode, implementation of the appropriate
 * algorithm and transmission of a response (as appropriate).
 *
 * The post-handler is always called, regardless of success or failure
 * of the pre or com functions, to trigger the DTrace done probe and
 * deallocate memory allocated in the pre-handler.
 */
#define	SMB_SDT_OPS(NAME)	\
	smb_pre_##NAME,		\
	smb_com_##NAME,		\
	smb_post_##NAME

#define	SMB_COM_DECL(NAME)				\
	smb_sdrc_t smb_pre_##NAME(smb_request_t *);	\
	smb_sdrc_t smb_com_##NAME(smb_request_t *);	\
	void smb_post_##NAME(smb_request_t *)

SMB_COM_DECL(check_directory);
SMB_COM_DECL(close);
SMB_COM_DECL(close_and_tree_disconnect);
SMB_COM_DECL(close_print_file);
SMB_COM_DECL(create);
SMB_COM_DECL(create_directory);
SMB_COM_DECL(create_new);
SMB_COM_DECL(create_temporary);
SMB_COM_DECL(delete);
SMB_COM_DECL(delete_directory);
SMB_COM_DECL(echo);
SMB_COM_DECL(find);
SMB_COM_DECL(find_close);
SMB_COM_DECL(find_close2);
SMB_COM_DECL(find_unique);
SMB_COM_DECL(flush);
SMB_COM_DECL(get_print_queue);
SMB_COM_DECL(invalid);
SMB_COM_DECL(ioctl);
SMB_COM_DECL(lock_and_read);
SMB_COM_DECL(lock_byte_range);
SMB_COM_DECL(locking_andx);
SMB_COM_DECL(logoff_andx);
SMB_COM_DECL(negotiate);
SMB_COM_DECL(nt_cancel);
SMB_COM_DECL(nt_create_andx);
SMB_COM_DECL(nt_rename);
SMB_COM_DECL(nt_transact);
SMB_COM_DECL(nt_transact_secondary);
SMB_COM_DECL(open);
SMB_COM_DECL(open_andx);
SMB_COM_DECL(open_print_file);
SMB_COM_DECL(process_exit);
SMB_COM_DECL(query_information);
SMB_COM_DECL(query_information2);
SMB_COM_DECL(query_information_disk);
SMB_COM_DECL(read);
SMB_COM_DECL(read_andx);
SMB_COM_DECL(read_raw);
SMB_COM_DECL(rename);
SMB_COM_DECL(search);
SMB_COM_DECL(seek);
SMB_COM_DECL(session_setup_andx);
SMB_COM_DECL(set_information);
SMB_COM_DECL(set_information2);
SMB_COM_DECL(transaction);
SMB_COM_DECL(transaction2);
SMB_COM_DECL(transaction2_secondary);
SMB_COM_DECL(transaction_secondary);
SMB_COM_DECL(tree_connect);
SMB_COM_DECL(tree_connect_andx);
SMB_COM_DECL(tree_disconnect);
SMB_COM_DECL(unlock_byte_range);
SMB_COM_DECL(write);
SMB_COM_DECL(write_and_close);
SMB_COM_DECL(write_and_unlock);
SMB_COM_DECL(write_andx);
SMB_COM_DECL(write_print_file);
SMB_COM_DECL(write_raw);

#define	SMB_NT_TRANSACT_DECL(NAME)				\
	smb_sdrc_t smb_pre_##NAME(smb_request_t *, smb_xa_t *);	\
	smb_sdrc_t smb_##NAME(smb_request_t *, smb_xa_t *);	\
	void smb_post_##NAME(smb_request_t *, smb_xa_t *)

SMB_NT_TRANSACT_DECL(nt_transact_create);

int smb_notify_init(void);
void smb_notify_fini(void);

smb_sdrc_t smb_nt_transact_notify_change(smb_request_t *, smb_xa_t *);
smb_sdrc_t smb_nt_transact_query_security_info(smb_request_t *, smb_xa_t *);
smb_sdrc_t smb_nt_transact_set_security_info(smb_request_t *, smb_xa_t *);
smb_sdrc_t smb_nt_transact_ioctl(smb_request_t *, smb_xa_t *);
smb_sdrc_t smb_nt_transact_rename(smb_request_t *, smb_xa_t *);

smb_sdrc_t smb_com_trans2_open2(smb_request_t *, smb_xa_t *);
smb_sdrc_t smb_com_trans2_create_directory(smb_request_t *, smb_xa_t *);
smb_sdrc_t smb_com_trans2_find_first2(smb_request_t *, smb_xa_t *);
smb_sdrc_t smb_com_trans2_find_next2(smb_request_t *, smb_xa_t *);
smb_sdrc_t smb_com_trans2_query_fs_information(smb_request_t *, smb_xa_t *);
smb_sdrc_t smb_com_trans2_query_path_information(smb_request_t *, smb_xa_t *);
smb_sdrc_t smb_com_trans2_query_file_information(smb_request_t *, smb_xa_t *);
smb_sdrc_t smb_com_trans2_set_path_information(smb_request_t *, smb_xa_t *);
smb_sdrc_t smb_com_trans2_set_file_information(smb_request_t *, smb_xa_t *);
int smb_trans2_rename(smb_request_t *, smb_node_t *, char *, int);

/*
 * Logging functions
 */
void smb_log_flush(void);
void smb_correct_keep_alive_values(uint32_t new_keep_alive);
void smb_close_all_connections(void);

int smb_net_id(uint32_t);

void smb_process_file_notify_change_queue(smb_ofile_t *of);

void smb_oplock_acquire(smb_node_t *, smb_ofile_t *, open_param_t	*);
boolean_t smb_oplock_break(smb_node_t *, smb_session_t *, boolean_t);
void smb_oplock_release(smb_node_t *, smb_ofile_t *);
boolean_t smb_oplock_conflict(smb_node_t *, smb_session_t *, open_param_t *);
boolean_t smb_oplock_broadcast(smb_node_t *);

/*
 * macros used in oplock processing
 *
 * SMB_ATTR_ONLY_OPEN: Checks to see if this is
 * an attribute-only open with no contravening
 * dispositions.  Such an open cannot effect an
 * oplock break.  However, a contravening disposition
 * of FILE_SUPERSEDE or FILE_OVERWRITE can allow
 * an oplock break.
 */

#define	SMB_ATTR_ONLY_OPEN(op)					\
	((op) && (op)->desired_access &&			\
	(((op)->desired_access & ~(FILE_READ_ATTRIBUTES |	\
	FILE_WRITE_ATTRIBUTES | SYNCHRONIZE)) == 0) &&		\
	((op)->create_disposition != FILE_SUPERSEDE) &&		\
	((op)->create_disposition != FILE_OVERWRITE))		\

uint32_t smb_lock_get_lock_count(smb_node_t *);
uint32_t smb_unlock_range(smb_request_t *, smb_node_t *,
    uint64_t, uint64_t);
uint32_t smb_lock_range(smb_request_t *, uint64_t, uint64_t, uint32_t,
    uint32_t locktype);
void smb_lock_range_error(smb_request_t *, uint32_t);

DWORD smb_range_check(smb_request_t *, smb_node_t *, uint64_t, uint64_t,
    boolean_t);

int smb_mangle_name(ino64_t, char *, char *, char *, int);
int smb_unmangle_name(smb_node_t *, char *, char *, int, uint32_t);
int smb_maybe_mangled_name(char *);
int smb_maybe_mangled_path(const char *, size_t);
int smb_needs_mangle(char *, char **);
boolean_t smb_is_invalid_filename(const char *);

void smbsr_cleanup(smb_request_t *sr);

int smbsr_connect_tree(smb_request_t *);

int smb_common_create_directory(smb_request_t *);

void	smb_convert_wildcards(char *);
boolean_t smb_contains_wildcards(const char *);
int	smb_ascii_or_unicode_strlen(smb_request_t *, char *);
int	smb_ascii_or_unicode_strlen_null(smb_request_t *, char *);
int	smb_ascii_or_unicode_null_len(smb_request_t *);

int	smb_search(smb_request_t *);

uint32_t smb_common_create(smb_request_t *);
uint32_t smb_common_open(smb_request_t *);
int smb_common_write(smb_request_t *, smb_rw_param_t *);

void smb_pathname_init(smb_request_t *, smb_pathname_t *, char *);
boolean_t smb_pathname_validate(smb_request_t *, smb_pathname_t *);
boolean_t smb_validate_dirname(smb_request_t *, smb_pathname_t *);
boolean_t smb_validate_object_name(smb_request_t *, smb_pathname_t *);
boolean_t smb_validate_stream_name(smb_request_t *, smb_pathname_t *);
boolean_t smb_is_stream_name(char *);
void smb_stream_parse_name(char *, char *, char *);


uint32_t smb_omode_to_amask(uint32_t desired_access);

void	sshow_distribution_info(char *);

boolean_t smb_dispatch_request(smb_request_t *);
int	smbsr_encode_empty_result(smb_request_t *);
void	smbsr_lookup_file(smb_request_t *);
void	smbsr_release_file(smb_request_t *);

int	smbsr_decode_vwv(smb_request_t *sr, char *fmt, ...);
int	smbsr_decode_data(smb_request_t *sr, char *fmt, ...);
int	smbsr_encode_result(smb_request_t *, int, int, char *, ...);
smb_xa_t *smbsr_lookup_xa(smb_request_t *sr);
void	smbsr_send_reply(smb_request_t *);

void	smbsr_map_errno(int, smb_error_t *);
void	smbsr_set_error(smb_request_t *, smb_error_t *);
void	smbsr_errno(smb_request_t *, int);
void	smbsr_warn(smb_request_t *, DWORD, uint16_t, uint16_t);
void	smbsr_error(smb_request_t *, DWORD, uint16_t, uint16_t);

int	clock_get_milli_uptime(void);

int	smb_mbc_vencodef(mbuf_chain_t *, char *, va_list);
int	smb_mbc_vdecodef(mbuf_chain_t *, char *, va_list);
int	smb_mbc_decodef(mbuf_chain_t *, char *, ...);
int	smb_mbc_encodef(mbuf_chain_t *, char *, ...);
int	smb_mbc_peek(mbuf_chain_t *, int, char *, ...);
int	smb_mbc_poke(mbuf_chain_t *, int, char *, ...);

void	smbsr_encode_header(smb_request_t *sr, int wct,
		    int bcc, char *fmt, ...);

int	smb_xlate_dialect_str_to_cd(char *);
char	*smb_xlate_com_cd_to_str(int);
char	*smb_xlate_dialect_cd_to_str(int);

int smb_lock_range_access(smb_request_t *, smb_node_t *,
    uint64_t, uint64_t, boolean_t);

void smb_encode_sid(smb_xa_t *, smb_sid_t *);
uint32_t smb_decode_sd(smb_xa_t *, smb_sd_t *);

/*
 * Socket functions
 */
ksocket_t smb_socreate(int domain, int type, int protocol);
void smb_soshutdown(ksocket_t so);
void smb_sodestroy(ksocket_t so);
int smb_sorecv(ksocket_t so, void *msg, size_t len);
int smb_net_init(void);
void smb_net_fini(void);
void smb_net_txl_constructor(smb_txlst_t *);
void smb_net_txl_destructor(smb_txlst_t *);
smb_txreq_t *smb_net_txr_alloc(void);
void smb_net_txr_free(smb_txreq_t *);
int smb_net_txr_send(ksocket_t, smb_txlst_t *, smb_txreq_t *);

/*
 * SMB RPC interface
 */
int smb_opipe_open(smb_request_t *);
void smb_opipe_close(smb_ofile_t *);
smb_sdrc_t smb_opipe_transact(smb_request_t *, struct uio *);
int smb_opipe_read(smb_request_t *, struct uio *);
int smb_opipe_write(smb_request_t *, struct uio *);

void smb_opipe_door_init(void);
void smb_opipe_door_fini(void);
int smb_opipe_door_open(int);
void smb_opipe_door_close(void);

/*
 * SMB server functions (file smb_server.c)
 */
int smb_server_svc_init(void);
int smb_server_svc_fini(void);
int smb_server_create(void);
int smb_server_delete(void);
int smb_server_configure(smb_ioc_cfg_t *);
int smb_server_start(smb_ioc_start_t *);
int smb_server_nbt_listen(smb_ioc_listen_t *);
int smb_server_tcp_listen(smb_ioc_listen_t *);
int smb_server_nbt_receive(void);
int smb_server_tcp_receive(void);
uint32_t smb_server_get_session_count(void);
int smb_server_share_export(smb_ioc_share_t *);
int smb_server_share_unexport(smb_ioc_share_t *);
int smb_server_set_gmtoff(smb_ioc_gmt_t *);
int smb_server_numopen(smb_ioc_opennum_t *);
int smb_server_enum(smb_ioc_svcenum_t *);
int smb_server_session_close(smb_ioc_session_t *);
int smb_server_file_close(smb_ioc_fileid_t *);

void smb_server_reconnection_check(smb_server_t *, smb_session_t *);
void smb_server_get_cfg(smb_server_t *, smb_kmod_cfg_t *);


/*
 * SMB node functions (file smb_node.c)
 */
int smb_node_init(void);
void smb_node_fini(void);
smb_node_t *smb_node_lookup(smb_request_t *, open_param_t *,
    cred_t *, vnode_t *, char *, smb_node_t *, smb_node_t *);
smb_node_t *smb_stream_node_lookup(smb_request_t *, cred_t *,
    smb_node_t *, vnode_t *, vnode_t *, char *);

void smb_node_ref(smb_node_t *);
void smb_node_release(smb_node_t *);
void smb_node_rename(smb_node_t *, smb_node_t *, smb_node_t *, char *);
int smb_node_root_init(vnode_t *, smb_server_t *, smb_node_t **);
void smb_node_add_lock(smb_node_t *, smb_lock_t *);
void smb_node_destroy_lock(smb_node_t *, smb_lock_t *);
void smb_node_destroy_lock_by_ofile(smb_node_t *, smb_ofile_t *);
void smb_node_start_crit(smb_node_t *, krw_t);
void smb_node_end_crit(smb_node_t *);
int smb_node_in_crit(smb_node_t *);
void smb_node_rdlock(smb_node_t *);
void smb_node_wrlock(smb_node_t *);
void smb_node_unlock(smb_node_t *);
uint32_t smb_node_get_ofile_count(smb_node_t *);
void smb_node_add_ofile(smb_node_t *, smb_ofile_t *);
void smb_node_rem_ofile(smb_node_t *, smb_ofile_t *);
void smb_node_inc_open_ofiles(smb_node_t *);
void smb_node_dec_open_ofiles(smb_node_t *);
boolean_t smb_node_is_dir(smb_node_t *);
boolean_t smb_node_is_link(smb_node_t *);

uint32_t smb_node_open_check(smb_node_t *, cred_t *,
    uint32_t, uint32_t);
DWORD smb_node_rename_check(smb_node_t *);
DWORD smb_node_delete_check(smb_node_t *);
void smb_node_notify_change(smb_node_t *);

int smb_node_getattr(smb_request_t *, smb_node_t *, smb_attr_t *);
int smb_node_setattr(smb_request_t *, smb_node_t *, cred_t *,
    smb_ofile_t *, smb_attr_t *);

int smb_node_set_delete_on_close(smb_node_t *, cred_t *, uint32_t);
void smb_node_reset_delete_on_close(smb_node_t *);

boolean_t smb_node_file_is_readonly(smb_node_t *);

/*
 * Pathname functions
 */

int smb_pathname_reduce(smb_request_t *, cred_t *,
    const char *, smb_node_t *, smb_node_t *, smb_node_t **, char *);

int smb_pathname(smb_request_t *, char *, int, smb_node_t *,
    smb_node_t *, smb_node_t **, smb_node_t **, cred_t *);

/*
 * smb_vfs functions
 */

boolean_t smb_vfs_hold(smb_server_t *, vfs_t *);
void smb_vfs_rele(smb_server_t *, vfs_t *);
void smb_vfs_rele_all(smb_server_t *);
boolean_t smb_vfs_cmp(vfs_t *, vfs_t *);
boolean_t smb_vfs_is_readonly(vfs_t *);

/* NOTIFY CHANGE */
void smb_process_session_notify_change_queue(smb_session_t *, smb_tree_t *);
void smb_process_node_notify_change_queue(smb_node_t *);
void smb_reply_specific_cancel_request(smb_request_t *);

void smb_fem_fcn_install(smb_node_t *);
void smb_fem_fcn_uninstall(smb_node_t *);

/* FEM */

int smb_fem_init(void);
void smb_fem_fini(void);

int smb_try_grow(smb_request_t *sr, int64_t new_size);

void smb_srm_init(smb_request_t *sr);
void smb_srm_fini(smb_request_t *sr);
void *smb_srm_alloc(smb_request_t *, size_t);
void *smb_srm_realloc(smb_request_t *, void *, size_t);

unsigned short smb_worker_getnum();

/* SMB signing routines smb_signing.c */
void smb_sign_init(smb_request_t *, smb_session_key_t *, char *, int);
int smb_sign_check_request(smb_request_t *);
int smb_sign_check_secondary(smb_request_t *, unsigned int);
void smb_sign_reply(smb_request_t *, mbuf_chain_t *);

boolean_t smb_sattr_check(uint16_t, uint16_t);

void smb_request_cancel(smb_request_t *);
void smb_request_wait(smb_request_t *);

/*
 * session functions (file smb_session.c)
 */
smb_session_t *smb_session_create(ksocket_t, uint16_t, smb_server_t *, int);
int smb_session_daemon(smb_session_list_t *);
void smb_session_reconnection_check(smb_session_list_t *, smb_session_t *);
void smb_session_timers(smb_session_list_t *);
void smb_session_delete(smb_session_t *session);
void smb_session_cancel_requests(smb_session_t *, smb_tree_t *,
    smb_request_t *);
void smb_session_config(smb_session_t *session);
void smb_session_disconnect_share(smb_session_list_t *, char *);
void smb_session_list_constructor(smb_session_list_t *);
void smb_session_list_destructor(smb_session_list_t *);
void smb_session_list_append(smb_session_list_t *, smb_session_t *);
void smb_session_list_delete_tail(smb_session_list_t *);
smb_session_t *smb_session_list_activate_head(smb_session_list_t *);
void smb_session_list_terminate(smb_session_list_t *, smb_session_t *);
void smb_session_list_signal(smb_session_list_t *);
smb_user_t *smb_session_dup_user(smb_session_t *, char *, char *);
void smb_session_getclient(smb_session_t *, char *, size_t);
boolean_t smb_session_isclient(smb_session_t *, const char *);
void smb_session_correct_keep_alive_values(smb_session_list_t *, uint32_t);
void smb_session_oplock_break(smb_session_t *, smb_ofile_t *);
int smb_session_send(smb_session_t *, uint8_t type, mbuf_chain_t *);
int smb_session_xprt_gethdr(smb_session_t *, smb_xprt_t *);
boolean_t smb_session_oplocks_enable(smb_session_t *);

#define	SMB_SESSION_GET_ID(s)	((s)->s_kid)

smb_request_t *smb_request_alloc(smb_session_t *, int);
void smb_request_free(smb_request_t *);

/*
 * ofile functions (file smb_ofile.c)
 */
smb_ofile_t *smb_ofile_lookup_by_fid(smb_tree_t *, uint16_t);
smb_ofile_t *smb_ofile_lookup_by_uniqid(smb_tree_t *, uint32_t);
boolean_t smb_ofile_disallow_fclose(smb_ofile_t *);
smb_ofile_t *smb_ofile_open(smb_tree_t *, smb_node_t *, uint16_t,
    open_param_t *, uint16_t, uint32_t, smb_error_t *);
void smb_ofile_close(smb_ofile_t *, uint32_t);
uint32_t smb_ofile_access(smb_ofile_t *, cred_t *, uint32_t);
int smb_ofile_seek(smb_ofile_t *, ushort_t, int32_t, uint32_t *);
boolean_t smb_ofile_hold(smb_ofile_t *);
void smb_ofile_release(smb_ofile_t *);
void smb_ofile_close_all(smb_tree_t *);
void smb_ofile_close_all_by_pid(smb_tree_t *, uint16_t);
void smb_ofile_set_flags(smb_ofile_t *, uint32_t);
boolean_t smb_ofile_is_open(smb_ofile_t *);
int smb_ofile_enum(smb_ofile_t *, smb_svcenum_t *);
uint32_t smb_ofile_open_check(smb_ofile_t *, cred_t *, uint32_t, uint32_t);
uint32_t smb_ofile_rename_check(smb_ofile_t *);
uint32_t smb_ofile_delete_check(smb_ofile_t *);
cred_t *smb_ofile_getcred(smb_ofile_t *);
void smb_ofile_set_oplock_granted(smb_ofile_t *);
void smb_ofile_set_delete_on_close(smb_ofile_t *);

void smb_ofile_set_write_time_pending(smb_ofile_t *);
boolean_t smb_ofile_write_time_pending(smb_ofile_t *);
void smb_ofile_set_explicit_times(smb_ofile_t *, uint32_t);
uint32_t smb_ofile_explicit_times(smb_ofile_t *);
void smb_delayed_write_timer(smb_llist_t *);

#define	SMB_OFILE_GET_SESSION(of)	((of)->f_session)
#define	SMB_OFILE_GET_TREE(of)		((of)->f_tree)
#define	SMB_OFILE_GET_FID(of)		((of)->f_fid)
#define	SMB_OFILE_GET_NODE(of)		((of)->f_node)

#define	smb_ofile_granted_access(_of_)	((_of_)->f_granted_access)

/*
 * odir functions (file smb_odir.c)
 */
uint16_t smb_odir_open(smb_request_t *, char *, uint16_t, uint32_t);
uint16_t smb_odir_openat(smb_request_t *, smb_node_t *);
void smb_odir_close(smb_odir_t *);
boolean_t smb_odir_hold(smb_odir_t *);
void smb_odir_release(smb_odir_t *);

int smb_odir_read(smb_request_t *, smb_odir_t *,
    smb_odirent_t *, boolean_t *);
int smb_odir_read_fileinfo(smb_request_t *, smb_odir_t *,
    smb_fileinfo_t *, boolean_t *);
int smb_odir_read_streaminfo(smb_request_t *, smb_odir_t *,
    smb_streaminfo_t *, boolean_t *);

void smb_odir_save_cookie(smb_odir_t *, int, uint32_t cookie);
void smb_odir_resume_at(smb_odir_t *, smb_odir_resume_t *);

/*
 * SMB user functions (file smb_user.c)
 */
int smb_user_init(void);
void smb_user_fini(void);
smb_user_t *smb_user_login(smb_session_t *, cred_t *,
    char *, char *, uint32_t, uint32_t, uint32_t);
smb_user_t *smb_user_dup(smb_user_t *);
void smb_user_logoff(smb_user_t *);
void smb_user_logoff_all(smb_session_t *);
smb_user_t *smb_user_lookup_by_uid(smb_session_t *, uint16_t);
smb_user_t *smb_user_lookup_by_state(smb_session_t *, smb_user_t *);
smb_tree_t *smb_user_lookup_tree(smb_user_t *, uint16_t);
smb_tree_t *smb_user_lookup_share(smb_user_t *, const char *, smb_tree_t *);
smb_tree_t *smb_user_lookup_volume(smb_user_t *, const char *, smb_tree_t *);
boolean_t smb_user_is_admin(smb_user_t *);
boolean_t smb_user_namecmp(smb_user_t *, const char *);
int smb_user_enum(smb_user_t *, smb_svcenum_t *);
void smb_user_close_pid(smb_user_t *, uint16_t);
void smb_user_disconnect_trees(smb_user_t *user);
void smb_user_disconnect_share(smb_user_t *, const char *);
int smb_user_fclose(smb_user_t *, uint32_t);
boolean_t smb_user_hold(smb_user_t *);
void smb_user_release(smb_user_t *);
cred_t *smb_user_getcred(smb_user_t *);
cred_t *smb_user_getprivcred(smb_user_t *);
void smb_user_netinfo_init(smb_user_t *, smb_netuserinfo_t *);
void smb_user_netinfo_fini(smb_netuserinfo_t *);
int smb_user_netinfo_encode(smb_user_t *, uint8_t *, size_t, uint32_t *);

/*
 * SMB tree functions (file smb_tree.c)
 */
smb_tree_t *smb_tree_connect(smb_request_t *);
void smb_tree_disconnect(smb_tree_t *, boolean_t);
void smb_tree_close_pid(smb_tree_t *, uint16_t);
boolean_t smb_tree_has_feature(smb_tree_t *, uint_t);
int smb_tree_enum(smb_tree_t *, smb_svcenum_t *);
int smb_tree_fclose(smb_tree_t *, uint32_t);
boolean_t smb_tree_hold(smb_tree_t *);
void smb_tree_release(smb_tree_t *);
smb_odir_t *smb_tree_lookup_odir(smb_tree_t *, uint16_t);
boolean_t smb_tree_is_connected(smb_tree_t *);
#define	SMB_TREE_GET_TID(tree)		((tree)->t_tid)

/*
 * SMB user's credential functions
 */
cred_t *smb_cred_create(smb_token_t *, uint32_t *);
void smb_cred_rele(cred_t *cr);
int smb_cred_is_member(cred_t *cr, smb_sid_t *sid);
cred_t *smb_cred_create_privs(cred_t *, uint32_t);

smb_xa_t *smb_xa_create(smb_session_t *session, smb_request_t *sr,
    uint32_t total_parameter_count, uint32_t total_data_count,
    uint32_t max_parameter_count, uint32_t max_data_count,
    uint32_t max_setup_count, uint32_t setup_word_count);
void smb_xa_delete(smb_xa_t *xa);
smb_xa_t *smb_xa_hold(smb_xa_t *xa);
void smb_xa_rele(smb_session_t *session, smb_xa_t *xa);
int smb_xa_open(smb_xa_t *xa);
void smb_xa_close(smb_xa_t *xa);
int smb_xa_complete(smb_xa_t *xa);
smb_xa_t *smb_xa_find(smb_session_t *session, uint16_t pid, uint16_t mid);

struct mbuf *smb_mbuf_get(uchar_t *buf, int nbytes);
struct mbuf *smb_mbuf_allocate(struct uio *uio);
void smb_mbuf_trim(struct mbuf *mhead, int nbytes);

void smb_check_status(void);
int smb_handle_write_raw(smb_session_t *session, smb_request_t *sr);

void smb_reconnection_check(smb_session_t *);

int32_t smb_time_gmt_to_local(smb_request_t *, int32_t);
int32_t smb_time_local_to_gmt(smb_request_t *, int32_t);
int32_t	smb_time_dos_to_unix(int16_t, int16_t);
void smb_time_unix_to_dos(int32_t, int16_t *, int16_t *);
void smb_time_nt_to_unix(uint64_t nt_time, timestruc_t *unix_time);
uint64_t smb_time_unix_to_nt(timestruc_t *);

int netbios_name_isvalid(char *in, char *out);

int uioxfer(struct uio *src_uio, struct uio *dst_uio, int n);

int smb_match_name(ino64_t, char *, char *, boolean_t);
int token2buf(smb_token_t *token, char *buf);

/*
 * Pool ID function prototypes
 */
int	smb_idpool_constructor(smb_idpool_t *pool);
void	smb_idpool_destructor(smb_idpool_t  *pool);
int	smb_idpool_alloc(smb_idpool_t *pool, uint16_t *id);
void	smb_idpool_free(smb_idpool_t *pool, uint16_t id);

/*
 * SMB thread function prototypes
 */
void	smb_session_worker(void *arg);

/*
 * SMB locked list function prototypes
 */
void	smb_llist_constructor(smb_llist_t *, size_t, size_t);
void	smb_llist_destructor(smb_llist_t *);
void	smb_llist_insert_head(smb_llist_t *ll, void *obj);
void	smb_llist_insert_tail(smb_llist_t *ll, void *obj);
void	smb_llist_remove(smb_llist_t *ll, void *obj);
int	smb_llist_upgrade(smb_llist_t *ll);
uint32_t smb_llist_get_count(smb_llist_t *ll);
#define	smb_llist_enter(ll, mode)	rw_enter(&(ll)->ll_lock, mode)
#define	smb_llist_exit(ll)		rw_exit(&(ll)->ll_lock)
#define	smb_llist_head(ll)		list_head(&(ll)->ll_list)
#define	smb_llist_next(ll, obj)		list_next(&(ll)->ll_list, obj)
int	smb_account_connected(smb_user_t *user);

/*
 * SMB Synchronized list function prototypes
 */
void	smb_slist_constructor(smb_slist_t *, size_t, size_t);
void	smb_slist_destructor(smb_slist_t *);
void	smb_slist_insert_head(smb_slist_t *sl, void *obj);
void	smb_slist_insert_tail(smb_slist_t *sl, void *obj);
void	smb_slist_remove(smb_slist_t *sl, void *obj);
void	smb_slist_wait_for_empty(smb_slist_t *sl);
void	smb_slist_exit(smb_slist_t *sl);
uint32_t smb_slist_move_tail(list_t *lst, smb_slist_t *sl);
void    smb_slist_obj_move(smb_slist_t *dst, smb_slist_t *src, void *obj);
#define	smb_slist_enter(sl)		mutex_enter(&(sl)->sl_mutex)
#define	smb_slist_head(sl)		list_head(&(sl)->sl_list)
#define	smb_slist_next(sl, obj)		list_next(&(sl)->sl_list, obj)

void    smb_rwx_init(smb_rwx_t *rwx);
void    smb_rwx_destroy(smb_rwx_t *rwx);
#define	smb_rwx_rwenter(rwx, mode)	rw_enter(&(rwx)->rwx_lock, mode)
void    smb_rwx_rwexit(smb_rwx_t *rwx);
int	smb_rwx_rwwait(smb_rwx_t *rwx, clock_t timeout);
#define	smb_rwx_xenter(rwx)		mutex_enter(&(rwx)->rwx_mutex)
#define	smb_rwx_xexit(rwx)		mutex_exit(&(rwx)->rwx_mutex)
krw_t   smb_rwx_rwupgrade(smb_rwx_t *rwx);
void    smb_rwx_rwdowngrade(smb_rwx_t *rwx, krw_t mode);

void	smb_thread_init(smb_thread_t *, char *, smb_thread_ep_t, void *,
    smb_thread_aw_t, void *);
void	smb_thread_destroy(smb_thread_t *);
int	smb_thread_start(smb_thread_t *);
void	smb_thread_stop(smb_thread_t *);
void    smb_thread_signal(smb_thread_t *);
boolean_t smb_thread_continue(smb_thread_t *);
boolean_t smb_thread_continue_nowait(smb_thread_t *);
boolean_t smb_thread_continue_timedwait(smb_thread_t *, int /* seconds */);
void smb_thread_set_awaken(smb_thread_t *, smb_thread_aw_t, void *);

uint32_t smb_denymode_to_sharemode(uint32_t desired_access, char *fname);
uint32_t smb_ofun_to_crdisposition(uint16_t ofun);

/* 100's of ns between 1/1/1970 and 1/1/1601 */
#define	NT_TIME_BIAS	(134774LL * 24LL * 60LL * 60LL * 10000000LL)

uint32_t smb_sd_read(smb_request_t *, smb_sd_t *, uint32_t);
uint32_t smb_sd_write(smb_request_t *, smb_sd_t *, uint32_t);

acl_t *smb_fsacl_inherit(acl_t *, int, int, uid_t);
acl_t *smb_fsacl_merge(acl_t *, acl_t *);
void smb_fsacl_split(acl_t *, acl_t **, acl_t **, int);
acl_t *smb_fsacl_from_vsa(vsecattr_t *, acl_type_t);
int smb_fsacl_to_vsa(acl_t *, vsecattr_t *, int *);

boolean_t smb_ace_is_generic(int);
boolean_t smb_ace_is_access(int);
boolean_t smb_ace_is_audit(int);

uint32_t smb_vss_ioctl_enumerate_snaps(smb_request_t *, smb_xa_t *);
int smb_vss_lookup_nodes(smb_request_t *, smb_node_t *, smb_node_t *,
    char *, smb_node_t **, smb_node_t **);
vnode_t *smb_lookuppathvptovp(smb_request_t *, char *, vnode_t *, vnode_t *);

void smb_panic(char *, const char *, int);
#pragma	does_not_return(smb_panic)
#define	SMB_PANIC()	smb_panic(__FILE__, __func__, __LINE__)

#ifdef	__cplusplus
}
#endif

#endif /* _SMB_KPROTO_H_ */
