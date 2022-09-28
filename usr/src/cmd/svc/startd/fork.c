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
 * fork.c - safe forking for svc.startd
 *
 * fork_configd() and fork_sulogin() are related, special cases that handle the
 * spawning of specific client processes for svc.startd.
 */

#include <sys/contract/process.h>
#include <sys/corectl.h>
#include <sys/ctfs.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libcontract.h>
#include <libcontract_priv.h>
#include <libscf_priv.h>
#include <limits.h>
#include <poll.h>
#include <port.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmpx.h>
#include <spawn.h>

#include "configd_exit.h"
#include "protocol.h"
#include "startd.h"

static	struct	utmpx	*utmpp;	/* pointer for getutxent() */

pid_t
startd_fork1(int *forkerr)
{
	pid_t p;

	/*
	 * prefork stack
	 */
	wait_prefork();

	p = fork1();

	if (p == -1 && forkerr != NULL)
		*forkerr = errno;

	/*
	 * postfork stack
	 */
	wait_postfork(p);

	return (p);
}

/*
 * void fork_mount(char *, char *)
 *   Run mount(1M) with the given options and mount point.  (mount(1M) has much
 *   hidden knowledge; it's much less correct to reimplement that logic here to
 *   save a fork(2)/exec(2) invocation.)
 */
int
fork_mount(char *path, char *opts)
{
	pid_t pid;
	uint_t tries = 0;
	int status;

	for (pid = fork1(); pid == -1; pid = fork1()) {
		if (++tries > MAX_MOUNT_RETRIES)
			return (-1);

		(void) sleep(tries);
	}

	if (pid != 0) {
		(void) waitpid(pid, &status, 0);

		/*
		 * If our mount(1M) invocation exited by peculiar means, or with
		 * a non-zero status, our mount likelihood is low.
		 */
		if (!WIFEXITED(status) ||
		    WEXITSTATUS(status) != 0)
			return (-1);

		return (0);
	}

	(void) execl("/sbin/mount", "mount", "-o", opts, path, NULL);

	return (-1);
}

/*
 * pid_t fork_common(...)
 *   Common routine used by fork_sulogin and fork_configd to fork a
 *   process in a contract with the provided terms.  Invokes
 *   fork_sulogin (with its no-fork argument set) on errors.
 */
static pid_t
fork_common(const char *name, const char *svc_fmri, int retries, ctid_t *ctidp,
    uint_t inf, uint_t crit, uint_t fatal, uint_t param, uint64_t cookie)
{
	uint_t tries = 0;
	int ctfd, err;
	pid_t pid;

	/*
	 * Establish process contract terms.
	 */
	if ((ctfd = open64(CTFS_ROOT "/process/template", O_RDWR)) == -1) {
		fork_sulogin(B_TRUE, "Could not open process contract template "
		    "for %s: %s\n", name, strerror(errno));
		/* NOTREACHED */
	}

	err = ct_tmpl_set_critical(ctfd, crit);
	err |= ct_pr_tmpl_set_fatal(ctfd, fatal);
	err |= ct_tmpl_set_informative(ctfd, inf);
	err |= ct_pr_tmpl_set_param(ctfd, param);
	err |= ct_tmpl_set_cookie(ctfd, cookie);
	err |= ct_pr_tmpl_set_svc_fmri(ctfd, svc_fmri);
	err |= ct_pr_tmpl_set_svc_aux(ctfd, name);
	if (err) {
		(void) close(ctfd);
		fork_sulogin(B_TRUE, "Could not set %s process contract "
		    "terms\n", name);
		/* NOTREACHED */
	}

	if (err = ct_tmpl_activate(ctfd)) {
		(void) close(ctfd);
		fork_sulogin(B_TRUE, "Could not activate %s process contract "
		    "template: %s\n", name, strerror(err));
		/* NOTREACHED */
	}

	/*
	 * Attempt to fork "retries" times.
	 */
	for (pid = fork1(); pid == -1; pid = fork1()) {
		if (++tries > retries) {
			/*
			 * When we exit the sulogin session, init(1M)
			 * will restart svc.startd(1M).
			 */
			err = errno;
			(void) ct_tmpl_clear(ctfd);
			(void) close(ctfd);
			fork_sulogin(B_TRUE, "Could not fork to start %s: %s\n",
			    name, strerror(err));
			/* NOTREACHED */
		}
		(void) sleep(tries);
	}

	/*
	 * Clean up, return pid and ctid.
	 */
	if (pid != 0 && (errno = contract_latest(ctidp)) != 0)
		uu_die("Could not get new contract id for %s\n", name);
	(void) ct_tmpl_clear(ctfd);
	(void) close(ctfd);

	return (pid);
}

/*
 * void fork_sulogin(boolean_t, const char *, ...)
 *   When we are invoked with the -s flag from boot (or run into an unfixable
 *   situation), we run a private copy of sulogin.  When the sulogin session
 *   is ended, we continue.  This is the last fallback action for system
 *   maintenance.
 *
 *   If immediate is true, fork_sulogin() executes sulogin(1M) directly, without
 *   forking.
 *
 *   Because fork_sulogin() is needed potentially before we daemonize, we leave
 *   it outside the wait_register() framework.
 */
/*PRINTFLIKE2*/
void
fork_sulogin(boolean_t immediate, const char *format, ...)
{
	va_list args;
	int fd_console;

	(void) printf("Requesting System Maintenance Mode\n");

	if (!booting_to_single_user)
		(void) printf("(See /lib/svc/share/README for more "
		    "information.)\n");

	va_start(args, format);
	(void) vprintf(format, args);
	va_end(args);

	if (!immediate) {
		ctid_t	ctid;
		pid_t	pid;

		pid = fork_common("sulogin", SVC_SULOGIN_FMRI,
		    MAX_SULOGIN_RETRIES, &ctid, CT_PR_EV_HWERR, 0,
		    CT_PR_EV_HWERR, CT_PR_PGRPONLY, SULOGIN_COOKIE);

		if (pid != 0) {
			(void) waitpid(pid, NULL, 0);
			contract_abandon(ctid);
			return;
		}
		/* close all inherited fds */
		closefrom(0);
	} else {
		(void) printf("Directly executing sulogin.\n");
		/*
		 * Can't call closefrom() in this MT section
		 * so safely close a minimum set of fds.
		 */
		(void) close(STDIN_FILENO);
		(void) close(STDOUT_FILENO);
		(void) close(STDERR_FILENO);
	}

	(void) setpgrp();

	/* open the console for sulogin */
	if ((fd_console = open("/dev/console", O_RDWR)) >= 0) {
		if (fd_console != STDIN_FILENO)
			while (dup2(fd_console, STDIN_FILENO) < 0 &&
			    errno == EINTR)
				;
		if (fd_console != STDOUT_FILENO)
			while (dup2(fd_console, STDOUT_FILENO) < 0 &&
			    errno == EINTR)
				;
		if (fd_console != STDERR_FILENO)
			while (dup2(fd_console, STDERR_FILENO) < 0 &&
			    errno == EINTR)
				;
		if (fd_console > STDERR_FILENO)
			(void) close(fd_console);
	}

	setutxent();
	while ((utmpp = getutxent()) != NULL) {
		if (strcmp(utmpp->ut_user, "LOGIN") != 0) {
			if (strcmp(utmpp->ut_line, "console") == 0) {
				(void) kill(utmpp->ut_pid, 9);
				break;
			}
		}
	}

	(void) execl("/sbin/sulogin", "sulogin", NULL);

	uu_warn("Could not exec() sulogin");

	exit(1);
}

#define	CONFIGD_PATH	"/lib/svc/bin/svc.configd"

/*
 * void fork_configd(int status)
 *   We are interested in exit events (since the parent's exiting means configd
 *   is ready to run and since the child's exiting indicates an error case) and
 *   in empty events.  This means we have a unique template for initiating
 *   configd.
 */
void
fork_configd(int exitstatus)
{
	pid_t pid;
	ctid_t ctid = -1;
	int err;
	char path[PATH_MAX];

	/*
	 * Checking the existatus for the potential failure of the
	 * daemonized svc.configd.  If this is not the first time
	 * through, but a call from the svc.configd monitoring thread
	 * after a failure this is the status that is expected.  Other
	 * failures are exposed during initialization or are fixed
	 * by a restart (e.g door closings).
	 *
	 * If this is on-disk database corruption it will also be
	 * caught by a restart but could be cleared before the restart.
	 *
	 * Or this could be internal database corruption due to a
	 * rogue service that needs to be cleared before restart.
	 */
	if (WEXITSTATUS(exitstatus) == CONFIGD_EXIT_DATABASE_BAD) {
		fork_sulogin(B_FALSE, "svc.configd exited with database "
		    "corrupt error after initialization of the repository\n");
	}

retry:
	log_framework(LOG_DEBUG, "fork_configd trying to start svc.configd\n");

	/*
	 * If we're retrying, we will have an old contract lying around
	 * from the failure.  Since we're going to be creating a new
	 * contract shortly, we abandon the old one now.
	 */
	if (ctid != -1)
		contract_abandon(ctid);
	ctid = -1;

	pid = fork_common("svc.configd", SCF_SERVICE_CONFIGD,
	    MAX_CONFIGD_RETRIES, &ctid, 0, CT_PR_EV_EXIT, 0,
	    CT_PR_INHERIT | CT_PR_REGENT, CONFIGD_COOKIE);

	if (pid != 0) {
		int exitstatus;

		st->st_configd_pid = pid;

		if (waitpid(pid, &exitstatus, 0) == -1) {
			fork_sulogin(B_FALSE, "waitpid on svc.configd "
			    "failed: %s\n", strerror(errno));
		} else if (WIFEXITED(exitstatus)) {
			char *errstr;

			/*
			 * Examine exitstatus.  This will eventually get more
			 * complicated, as we will want to teach startd how to
			 * invoke configd with alternate repositories, etc.
			 *
			 * Note that exec(2) failure results in an exit status
			 * of 1, resulting in the default clause below.
			 */

			/*
			 * Assign readable strings to cases we don't handle, or
			 * have error outcomes that cannot be eliminated.
			 */
			switch (WEXITSTATUS(exitstatus)) {
			case CONFIGD_EXIT_BAD_ARGS:
				errstr = "bad arguments";
				break;

			case CONFIGD_EXIT_DATABASE_BAD:
				errstr = "database corrupt";
				break;

			case CONFIGD_EXIT_DATABASE_LOCKED:
				errstr = "database locked";
				break;
			case CONFIGD_EXIT_INIT_FAILED:
				errstr = "initialization failure";
				break;
			case CONFIGD_EXIT_DOOR_INIT_FAILED:
				errstr = "door initialization failure";
				break;
			case CONFIGD_EXIT_DATABASE_INIT_FAILED:
				errstr = "database initialization failure";
				break;
			case CONFIGD_EXIT_NO_THREADS:
				errstr = "no threads available";
				break;
			case CONFIGD_EXIT_LOST_MAIN_DOOR:
				errstr = "lost door server attachment";
				break;
			case 1:
				errstr = "execution failure";
				break;
			default:
				errstr = "unknown error";
				break;
			}

			/*
			 * Remedial actions for various configd failures.
			 */
			switch (WEXITSTATUS(exitstatus)) {
			case CONFIGD_EXIT_OKAY:
				break;

			case CONFIGD_EXIT_DATABASE_LOCKED:
				/* attempt remount of / read-write */
				if (fs_is_read_only("/", NULL) == 1) {
					if (fs_remount("/") == -1)
						fork_sulogin(B_FALSE,
						    "remount of root "
						    "filesystem failed\n");

					goto retry;
				}
				break;

			default:
				fork_sulogin(B_FALSE, "svc.configd exited "
				    "with status %d (%s)\n",
				    WEXITSTATUS(exitstatus), errstr);
				goto retry;
			}
		} else if (WIFSIGNALED(exitstatus)) {
			char signame[SIG2STR_MAX];

			if (sig2str(WTERMSIG(exitstatus), signame))
				(void) snprintf(signame, SIG2STR_MAX,
				    "signum %d", WTERMSIG(exitstatus));

			fork_sulogin(B_FALSE, "svc.configd signalled:"
			    " %s\n", signame);

			goto retry;
		} else {
			fork_sulogin(B_FALSE, "svc.configd non-exit "
			    "condition: 0x%x\n", exitstatus);

			goto retry;
		}

		/*
		 * Announce that we have a valid svc.configd status.
		 */
		MUTEX_LOCK(&st->st_configd_live_lock);
		st->st_configd_lives = 1;
		err = pthread_cond_broadcast(&st->st_configd_live_cv);
		assert(err == 0);
		MUTEX_UNLOCK(&st->st_configd_live_lock);

		log_framework(LOG_DEBUG, "fork_configd broadcasts configd is "
		    "live\n");
		return;
	}

	/*
	 * Set our per-process core file path to leave core files in
	 * /etc/svc/volatile directory, named after the PID to aid in debugging.
	 */
	(void) snprintf(path, sizeof (path),
	    "/etc/svc/volatile/core.configd.%%p");

	(void) core_set_process_path(path, strlen(path) + 1, getpid());

	log_framework(LOG_DEBUG, "executing svc.configd\n");

	(void) execl(CONFIGD_PATH, CONFIGD_PATH, NULL);

	/*
	 * Status code is used above to identify configd exec failure.
	 */
	exit(1);
}

void *
fork_configd_thread(void *vctid)
{
	int fd, err;
	ctid_t configd_ctid = (ctid_t)vctid;

	if (configd_ctid == -1) {
		log_framework(LOG_DEBUG,
		    "fork_configd_thread starting svc.configd\n");
		fork_configd(0);
	} else {
		/*
		 * configd_ctid is known:  we broadcast and continue.
		 * test contract for appropriate state by verifying that
		 * there is one or more processes within it?
		 */
		log_framework(LOG_DEBUG,
		    "fork_configd_thread accepting svc.configd with CTID %ld\n",
		    configd_ctid);
		MUTEX_LOCK(&st->st_configd_live_lock);
		st->st_configd_lives = 1;
		(void) pthread_cond_broadcast(&st->st_configd_live_cv);
		MUTEX_UNLOCK(&st->st_configd_live_lock);
	}

	fd = open64(CTFS_ROOT "/process/pbundle", O_RDONLY);
	if (fd == -1)
		uu_die("process bundle open failed");

	/*
	 * Make sure we get all events (including those generated by configd
	 * before this thread was started).
	 */
	err = ct_event_reset(fd);
	assert(err == 0);

	for (;;) {
		int efd, sfd;
		ct_evthdl_t ev;
		uint32_t type;
		ctevid_t evid;
		ct_stathdl_t status;
		ctid_t ctid;
		uint64_t cookie;
		pid_t pid;

		if (err = ct_event_read_critical(fd, &ev)) {
			assert(err != EINVAL && err != EAGAIN);
			log_error(LOG_WARNING,
			    "Error reading next contract event: %s",
			    strerror(err));
			continue;
		}

		evid = ct_event_get_evid(ev);
		ctid = ct_event_get_ctid(ev);
		type = ct_event_get_type(ev);

		/* Fetch cookie. */
		sfd = contract_open(ctid, "process", "status", O_RDONLY);
		if (sfd < 0) {
			ct_event_free(ev);
			continue;
		}

		if (err = ct_status_read(sfd, CTD_COMMON, &status)) {
			log_framework(LOG_WARNING, "Could not get status for "
			    "contract %ld: %s\n", ctid, strerror(err));

			ct_event_free(ev);
			startd_close(sfd);
			continue;
		}

		cookie = ct_status_get_cookie(status);

		ct_status_free(status);

		startd_close(sfd);

		/*
		 * Don't process events from contracts we aren't interested in.
		 */
		if (cookie != CONFIGD_COOKIE) {
			ct_event_free(ev);
			continue;
		}

		if (type == CT_PR_EV_EXIT) {
			int exitstatus;

			(void) ct_pr_event_get_pid(ev, &pid);
			(void) ct_pr_event_get_exitstatus(ev,
			    &exitstatus);

			if (st->st_configd_pid != pid) {
				/*
				 * This is the child exiting, so we
				 * abandon the contract and restart
				 * configd.
				 */
				contract_abandon(ctid);
				fork_configd(exitstatus);
			}
		}

		efd = contract_open(ctid, "process", "ctl", O_WRONLY);
		if (efd != -1) {
			(void) ct_ctl_ack(efd, evid);
			startd_close(efd);
		}

		ct_event_free(ev);

	}

	/*NOTREACHED*/
	return (NULL);
}

void
fork_rc_script(char rl, const char *arg, boolean_t wait)
{
	pid_t pid;
	int tmpl, err, stat;
	char path[20] = "/sbin/rc.", log[20] = "rc..log", timebuf[20];
	time_t now;
	struct tm ltime;
	size_t sz;
	char *pathenv;
	char **nenv;

	path[8] = rl;

	tmpl = open64(CTFS_ROOT "/process/template", O_RDWR);
	if (tmpl >= 0) {
		err = ct_tmpl_set_critical(tmpl, 0);
		assert(err == 0);

		err = ct_tmpl_set_informative(tmpl, 0);
		assert(err == 0);

		err = ct_pr_tmpl_set_fatal(tmpl, 0);
		assert(err == 0);

		err = ct_tmpl_activate(tmpl);
		assert(err == 0);

		err = close(tmpl);
		assert(err == 0);
	} else {
		uu_warn("Could not create contract template for %s.\n", path);
	}

	pid = startd_fork1(NULL);
	if (pid < 0) {
		return;
	} else if (pid != 0) {
		/* parent */
		if (wait) {
			do
				err = waitpid(pid, &stat, 0);
			while (err != 0 && errno == EINTR)
				;

			if (!WIFEXITED(stat)) {
				log_framework(LOG_INFO,
				    "%s terminated with waitpid() status %d.\n",
				    path, stat);
			} else if (WEXITSTATUS(stat) != 0) {
				log_framework(LOG_INFO,
				    "%s failed with status %d.\n", path,
				    WEXITSTATUS(stat));
			}
		}

		return;
	}

	/* child */

	log[2] = rl;

	setlog(log);

	now = time(NULL);
	sz = strftime(timebuf, sizeof (timebuf), "%b %e %T",
	    localtime_r(&now, &ltime));
	assert(sz != 0);

	(void) fprintf(stderr, "%s Executing %s %s\n", timebuf, path, arg);

	if (rl == 'S')
		pathenv = "PATH=/sbin:/usr/sbin:/usr/bin";
	else
		pathenv = "PATH=/usr/sbin:/usr/bin";

	nenv = set_smf_env(NULL, 0, pathenv, NULL, NULL);

	(void) execle(path, path, arg, 0, nenv);

	perror("exec");
	exit(0);
}

extern char **environ;

/*
 * A local variation on system(3c) which accepts a timeout argument.  This
 * allows us to better ensure that the system will actually shut down.
 *
 * gracetime specifies an amount of time in seconds which the routine must wait
 * after the command exits, to allow for asynchronous effects (like sent
 * signals) to take effect.  This can be zero.
 */
void
fork_with_timeout(const char *cmd, uint_t gracetime, uint_t timeout)
{
	int err = 0;
	pid_t pid;
	char *argv[4];
	posix_spawnattr_t attr;
	posix_spawn_file_actions_t factions;

	sigset_t mask, savemask;
	uint_t msec_timeout;
	uint_t msec_spent = 0;
	uint_t msec_gracetime;
	int status;

	msec_timeout = timeout * 1000;
	msec_gracetime = gracetime * 1000;

	/*
	 * See also system(3c) in libc.  This is very similar, except
	 * that we avoid some unneeded complexity.
	 */
	err = posix_spawnattr_init(&attr);
	if (err == 0)
		err = posix_spawnattr_setflags(&attr,
		    POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF |
		    POSIX_SPAWN_NOSIGCHLD_NP | POSIX_SPAWN_WAITPID_NP |
		    POSIX_SPAWN_NOEXECERR_NP);

	/*
	 * We choose to close fd's above 2, a deviation from system.
	 */
	if (err == 0)
		err = posix_spawn_file_actions_init(&factions);
	if (err == 0)
		err = posix_spawn_file_actions_addclosefrom_np(&factions,
		    STDERR_FILENO + 1);

	(void) sigemptyset(&mask);
	(void) sigaddset(&mask, SIGCHLD);
	(void) thr_sigsetmask(SIG_BLOCK, &mask, &savemask);

	argv[0] = "/bin/sh";
	argv[1] = "-c";
	argv[2] = (char *)cmd;
	argv[3] = NULL;

	if (err == 0)
		err = posix_spawn(&pid, "/bin/sh", &factions, &attr,
		    (char *const *)argv, (char *const *)environ);

	(void) posix_spawnattr_destroy(&attr);
	(void) posix_spawn_file_actions_destroy(&factions);

	if (err) {
		uu_warn("Failed to spawn %s: %s\n", cmd, strerror(err));
	} else {
		for (;;) {
			int w;
			w = waitpid(pid, &status, WNOHANG);
			if (w == -1 && errno != EINTR)
				break;
			if (w > 0) {
				/*
				 * Command succeeded, so give it gracetime
				 * seconds for it to have an effect.
				 */
				if (status == 0 && msec_gracetime != 0)
					(void) poll(NULL, 0, msec_gracetime);
				break;
			}

			(void) poll(NULL, 0, 100);
			msec_spent += 100;
			/*
			 * If we timed out, kill off the process, then try to
			 * wait for it-- it's possible that we could accumulate
			 * a zombie here since we don't allow waitpid to hang,
			 * but it's better to let that happen and continue to
			 * make progress.
			 */
			if (msec_spent >= msec_timeout) {
				uu_warn("'%s' timed out after %d "
				    "seconds.  Killing.\n", cmd,
				    timeout);
				(void) kill(pid, SIGTERM);
				(void) poll(NULL, 0, 100);
				(void) kill(pid, SIGKILL);
				(void) poll(NULL, 0, 100);
				(void) waitpid(pid, &status, WNOHANG);
				break;
			}
		}
	}
	(void) thr_sigsetmask(SIG_BLOCK, &savemask, NULL);
}
