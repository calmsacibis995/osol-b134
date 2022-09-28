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

#include <assert.h>
#include <alloca.h>
#include <errno.h>
#include <fcntl.h>
#include <strings.h>
#include <macros.h>
#include <sys/brand.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/systeminfo.h>
#include <sys/types.h>
#include <sys/lx_types.h>
#include <sys/lx_debug.h>
#include <sys/lx_misc.h>
#include <sys/lx_stat.h>
#include <sys/lx_syscall.h>
#include <sys/lx_thunk_server.h>
#include <sys/lx_fcntl.h>
#include <unistd.h>
#include <libintl.h>
#include <zone.h>

extern int sethostname(char *, int);

/* ARGUSED */
int
lx_rename(uintptr_t p1, uintptr_t p2)
{
	int ret;

	ret = rename((const char *)p1, (const char *)p2);

	if (ret < 0) {
		/*
		 * If rename(2) failed and we're in install mode, return
		 * success if the the reason we failed was either because the
		 * source file didn't actually exist or if it was because we
		 * tried to rename it to be the name of a device currently in
		 * use (resulting in an EBUSY.)
		 *
		 * To help install along further, if the failure was due
		 * to an EBUSY, delete the original file so we don't leave
		 * extra files lying around.
		 */
		if (lx_install != 0) {
			if (errno == ENOENT)
				return (0);

			if (errno == EBUSY) {
				(void) unlink((const char *)p1);
				return (0);
			}
		}

		return (-errno);
	}

	return (0);
}

int
lx_renameat(uintptr_t ext1, uintptr_t p1, uintptr_t ext2, uintptr_t p2)
{
	int ret;
	int atfd1 = (int)ext1;
	int atfd2 = (int)ext2;

	if (atfd1 == LX_AT_FDCWD)
		atfd1 = AT_FDCWD;

	if (atfd2 == LX_AT_FDCWD)
		atfd2 = AT_FDCWD;

	ret = renameat(atfd1, (const char *)p1, atfd2, (const char *)p2);

	if (ret < 0) {
		/* see lx_rename() for why we check lx_install */
		if (lx_install != 0) {
			if (errno == ENOENT)
				return (0);

			if (errno == EBUSY) {
				(void) unlinkat(ext1, (const char *)p1, 0);
				return (0);
			}
		}

		return (-errno);
	}

	return (0);
}

/*ARGSUSED*/
int
lx_reboot(uintptr_t p1, uintptr_t p2, uintptr_t p3, uintptr_t p4)
{
	int magic = (int)p1;
	int magic2 = (int)p2;
	uint_t flag = (int)p3;
	int rc;

	if (magic != LINUX_REBOOT_MAGIC1)
		return (-EINVAL);
	if (magic2 != LINUX_REBOOT_MAGIC2 && magic2 != LINUX_REBOOT_MAGIC2A &&
	    magic2 != LINUX_REBOOT_MAGIC2B && magic2 != LINUX_REBOOT_MAGIC2C &&
	    magic2 != LINUX_REBOOT_MAGIC2D)
		return (-EINVAL);

	if (geteuid() != 0)
		return (-EPERM);

	switch (flag) {
	case LINUX_REBOOT_CMD_CAD_ON:
	case LINUX_REBOOT_CMD_CAD_OFF:
		/* ignored */
		rc = 0;
		break;
	case LINUX_REBOOT_CMD_POWER_OFF:
	case LINUX_REBOOT_CMD_HALT:
		rc = reboot(RB_HALT, NULL);
		break;
	case LINUX_REBOOT_CMD_RESTART:
	case LINUX_REBOOT_CMD_RESTART2:
		/* RESTART2 may need more work */
		lx_msg(gettext("Restarting system.\n"));
		rc = reboot(RB_AUTOBOOT, NULL);
		break;
	default:
		return (-EINVAL);
	}

	return ((rc == -1) ? -errno : rc);
}

/*
 * getcwd() - Linux syscall semantics are slightly different; we need to return
 * the length of the pathname copied (+ 1 for the terminating NULL byte.)
 */
int
lx_getcwd(uintptr_t p1, uintptr_t p2)
{
	char *buf;
	size_t buflen = (size_t)p2;
	size_t copylen, local_len;
	size_t len = 0;

	if ((getcwd((char *)p1, (size_t)p2)) == NULL)
		return (-errno);

	/*
	 * We need the length of the pathname getcwd() copied but we never want
	 * to dereference a Linux pointer for any reason.
	 *
	 * Thus, to get the string length we will uucopy() up to copylen bytes
	 * at a time into a local buffer and will walk each chunk looking for
	 * the string-terminating NULL byte.
	 *
	 * We can use strlen() to find the length of the string in the
	 * local buffer by delimiting the buffer with a NULL byte in the
	 * last element that will never be overwritten.
	 */
	copylen = min(buflen, MAXPATHLEN + 1);
	buf = SAFE_ALLOCA(copylen + 1);
	if (buf == NULL)
		return (-ENOMEM);
	buf[copylen] = '\0';

	for (;;) {
		if (uucopy((char *)p1 + len, buf, copylen) != 0)
			return (-errno);

		local_len = strlen(buf);
		len += local_len;

		/*
		 * If the strlen() is less than copylen, we found the
		 * real end of the string -- not the NULL byte used to
		 * delimit the end of our buffer.
		 */
		if (local_len != copylen)
			break;

		/* prepare to check the next chunk of the string */
		buflen -= copylen;
		copylen = min(buflen, copylen);
	}

	return (len + 1);
}

int
lx_get_kern_version(void)
{
	/*
	 * Since this function is called quite often, and zone_getattr is slow,
	 * we cache the kernel version in kvers_cache. -1 signifies that no
	 * value has yet been cached.
	 */
	static int kvers_cache = -1;
	/* dummy variable for use in zone_getattr */
	int kvers;

	if (kvers_cache != -1)
		return (kvers_cache);
	if (zone_getattr(getzoneid(), LX_KERN_VERSION_NUM, &kvers, sizeof (int))
	    != sizeof (int))
		return (kvers_cache = LX_KERN_2_4);
	else
		return (kvers_cache = kvers);
}

int
lx_uname(uintptr_t p1)
{
	struct lx_utsname *un = (struct lx_utsname *)p1;
	char buf[LX_SYS_UTS_LN + 1];

	if (gethostname(un->nodename, sizeof (un->nodename)) == -1)
		return (-errno);

	(void) strlcpy(un->sysname, LX_UNAME_SYSNAME, LX_SYS_UTS_LN);
	(void) strlcpy(un->release, lx_release, LX_SYS_UTS_LN);
	(void) strlcpy(un->version, LX_UNAME_VERSION, LX_SYS_UTS_LN);
	(void) strlcpy(un->machine, LX_UNAME_MACHINE, LX_SYS_UTS_LN);
	if ((sysinfo(SI_SRPC_DOMAIN, buf, LX_SYS_UTS_LN) < 0))
		un->domainname[0] = '\0';
	else
		(void) strlcpy(un->domainname, buf, LX_SYS_UTS_LN);

	return (0);
}

/*
 * {get,set}groups16() - Handle the conversion between 16-bit Linux gids and
 * 32-bit Solaris gids.
 */
int
lx_getgroups16(uintptr_t p1, uintptr_t p2)
{
	int count = (int)p1;
	lx_gid16_t *grouplist = (lx_gid16_t *)p2;
	gid_t *grouplist32;
	int ret;
	int i;

	grouplist32 = SAFE_ALLOCA(count * sizeof (gid_t));
	if (grouplist32 == NULL)
		return (-ENOMEM);
	if ((ret = getgroups(count, grouplist32)) < 0)
		return (-errno);

	for (i = 0; i < ret; i++)
		grouplist[i] = LX_GID32_TO_GID16(grouplist32[i]);

	return (ret);
}

int
lx_setgroups16(uintptr_t p1, uintptr_t p2)
{
	int count = (int)p1;
	lx_gid16_t *grouplist = (lx_gid16_t *)p2;
	gid_t *grouplist32;
	int i;

	grouplist32 = SAFE_ALLOCA(count * sizeof (gid_t));
	if (grouplist32 == NULL)
		return (-ENOMEM);
	for (i = 0; i < count; i++)
		grouplist32[i] = LX_GID16_TO_GID32(grouplist[i]);

	return (setgroups(count, grouplist32) ? -errno : 0);
}

/*
 * personality() - Solaris doesn't support Linux personalities, but we have to
 * emulate enough to show that we support the basic personality.
 */
#define	LX_PER_LINUX	0x0

int
lx_personality(uintptr_t p1)
{
	int per = (int)p1;

	switch (per) {
	case -1:
		/* Request current personality */
		return (LX_PER_LINUX);
	case LX_PER_LINUX:
		return (0);
	default:
		return (-EINVAL);
	}
}

/*
 * mknod() - Since we don't have the SYS_CONFIG privilege within a zone, the
 * only mode we have to support is S_IFIFO.  We also have to distinguish between
 * an invalid type and insufficient privileges.
 */
#define	LX_S_IFMT	0170000
#define	LX_S_IFDIR	0040000
#define	LX_S_IFCHR	0020000
#define	LX_S_IFBLK	0060000
#define	LX_S_IFREG	0100000
#define	LX_S_IFIFO	0010000
#define	LX_S_IFLNK	0120000
#define	LX_S_IFSOCK	0140000

/*ARGSUSED*/
int
lx_mknod(uintptr_t p1, uintptr_t p2, uintptr_t p3)
{
	char *path = (char *)p1;
	lx_dev_t lx_dev = (lx_dev_t)p3;
	struct sockaddr_un sockaddr;
	struct stat statbuf;
	mode_t mode, type;
	dev_t dev;
	int fd;

	type = ((mode_t)p2 & LX_S_IFMT);
	mode = ((mode_t)p2 & 07777);

	switch (type) {
	case 0:
	case LX_S_IFREG:
		/* create a regular file */
		if (stat(path, &statbuf) == 0)
			return (-EEXIST);

		if (errno != ENOENT)
			return (-errno);

		if ((fd = creat(path, mode)) < 0)
			return (-errno);

		(void) close(fd);
		return (0);

	case LX_S_IFSOCK:
		/*
		 * Create a UNIX domain socket.
		 *
		 * Most programmers aren't even aware you can do this.
		 *
		 * Note you can also do this via Solaris' mknod(2), but
		 * Linux allows anyone who can create a UNIX domain
		 * socket via bind(2) to create one via mknod(2);
		 * Solaris requires the caller to be privileged.
		 */
		if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
			return (-errno);

		if (stat(path, &statbuf) == 0)
			return (-EEXIST);

		if (errno != ENOENT)
			return (-errno);

		if (uucopy(path, &sockaddr.sun_path,
		    sizeof (sockaddr.sun_path)) < 0)
			return (-errno);

		/* assure NULL termination of sockaddr.sun_path */
		sockaddr.sun_path[sizeof (sockaddr.sun_path) - 1] = '\0';
		sockaddr.sun_family = AF_UNIX;

		if (bind(fd, (struct sockaddr *)&sockaddr,
		    strlen(sockaddr.sun_path) +
		    sizeof (sockaddr.sun_family)) < 0)
			return (-errno);

		(void) close(fd);
		return (0);

	case LX_S_IFIFO:
		dev = 0;
		break;

	case LX_S_IFCHR:
	case LX_S_IFBLK:
		/*
		 * The "dev" RPM package wants to create all possible Linux
		 * device nodes, so just report its mknod()s as having
		 * succeeded if we're in install mode.
		 */
		if (lx_install != 0) {
			lx_debug("lx_mknod: install mode spoofed creation of "
			    "Linux device [%lld, %lld]\n",
			    LX_GETMAJOR(lx_dev), LX_GETMINOR(lx_dev));

			return (0);
		}

		dev = makedevice(LX_GETMAJOR(lx_dev), LX_GETMINOR(lx_dev));
		break;

	default:
		return (-EINVAL);
	}

	return (mknod(path, mode | type, dev) ? -errno : 0);
}

int
lx_sethostname(uintptr_t p1, uintptr_t p2)
{
	char *name = (char *)p1;
	int len = (size_t)p2;

	return (sethostname(name, len) ? -errno : 0);
}

int
lx_setdomainname(uintptr_t p1, uintptr_t p2)
{
	char *name = (char *)p1;
	int len = (size_t)p2;
	long rval;

	if (len < 0 || len >= LX_SYS_UTS_LN)
		return (-EINVAL);

	rval = sysinfo(SI_SET_SRPC_DOMAIN, name, len);

	return ((rval < 0) ? -errno : 0);
}

int
lx_getpid(void)
{
	int pid;

	/* First call the thunk server hook. */
	if (lxt_server_pid(&pid) != 0)
		return (pid);

	pid = syscall(SYS_brand, B_EMULATE_SYSCALL + 20);
	return ((pid == -1) ? -errno : pid);
}

int
lx_execve(uintptr_t p1, uintptr_t p2, uintptr_t p3)
{
	char *filename = (char *)p1;
	char **argv = (char **)p2;
	char **envp = (char **)p3;
	char *nullist[] = { NULL };
	char path[64];

	/* First call the thunk server hook. */
	lxt_server_exec_check();

	/* Get a copy of the executable we're trying to run */
	path[0] = '\0';
	(void) uucopystr(filename, path, sizeof (path));

	/* Check if we're trying to run a native binary */
	if (strncmp(path, "/native/usr/lib/brand/lx/lx_native",
	    sizeof (path)) == 0) {
		/* Skip the first element in the argv array */
		argv++;

		/*
		 * The name of the new program to execute was the first
		 * parameter passed to lx_native.
		 */
		if (uucopy(argv, &filename, sizeof (char *)) != 0)
			return (-errno);

		(void) syscall(SYS_brand, B_EXEC_NATIVE, filename, argv, envp,
		    NULL, NULL, NULL);
		return (-errno);
	}

	if (argv == NULL)
		argv = nullist;

	/* This is a normal exec call. */
	(void) execve(filename, argv, envp);

	return (-errno);
}

int
lx_setgroups(uintptr_t p1, uintptr_t p2)
{
	int ng = (int)p1;
	gid_t *glist;
	int i, r;

	lx_debug("\tlx_setgroups(%d, 0x%p", ng, p2);

	if (ng > 0) {
		if ((glist = (gid_t *)SAFE_ALLOCA(ng * sizeof (gid_t))) == NULL)
			return (-ENOMEM);

		if (uucopy((void *)p2, glist, ng * sizeof (gid_t)) != 0)
			return (-errno);

		/*
		 * Linux doesn't check the validity of the group IDs, but
		 * Solaris does. Change any invalid group IDs to a known, valid
		 * value (yuck).
		 */
		for (i = 0; i < ng; i++) {
			if (glist[i] > MAXUID)
				glist[i] = MAXUID;
		}
	}

	r = syscall(SYS_brand, B_EMULATE_SYSCALL + LX_SYS_setgroups32,
	    ng, glist);

	return ((r == -1) ? -errno : r);
}
