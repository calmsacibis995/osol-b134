#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

LIBCBASE=.

LIBRARY= libc.a
LIB_PIC= libc_pic.a
VERS=	.1
CPP=	/usr/lib/cpp
TARGET_ARCH=	amd64

# objects are grouped by source directory

# local objects
STRETS=

CRTOBJS=			\
	cerror.o

DYNOBJS=

FPOBJS=				\
	_base_il.o		\
	fpgetmask.o		\
	fpgetround.o		\
	fpsetmask.o		\
	fpsetround.o		\
	fpstart.o

I386FPOBJS=			\
	_D_cplx_div.o		\
	_D_cplx_div_ix.o	\
	_D_cplx_div_rx.o	\
	_D_cplx_lr_div.o	\
	_D_cplx_lr_div_ix.o	\
	_D_cplx_lr_div_rx.o	\
	_D_cplx_mul.o		\
	_F_cplx_div.o		\
	_F_cplx_div_ix.o	\
	_F_cplx_div_rx.o	\
	_F_cplx_lr_div.o	\
	_F_cplx_lr_div_ix.o	\
	_F_cplx_lr_div_rx.o	\
	_F_cplx_mul.o		\
	_X_cplx_div.o		\
	_X_cplx_div_ix.o	\
	_X_cplx_div_rx.o	\
	_X_cplx_lr_div.o	\
	_X_cplx_lr_div_ix.o	\
	_X_cplx_lr_div_rx.o	\
	_X_cplx_mul.o

FPASMOBJS=			\
	__xgetRD.o		\
	_xtoll.o		\
	_xtoull.o		\
	fpcw.o			\
	fpgetsticky.o		\
	fpsetsticky.o

ATOMICOBJS=			\
	atomic.o

XATTROBJS=			\
	xattr_common.o
COMOBJS=			\
	bcmp.o			\
	bcopy.o			\
	bsearch.o		\
	bzero.o			\
	ffs.o			\
	qsort.o			\
	strtol.o		\
	strtoul.o		\
	strtoll.o		\
	strtoull.o

GENOBJS=			\
	_getsp.o		\
	abs.o			\
	alloca.o		\
	attrat.o		\
	byteorder.o		\
	cuexit.o		\
	ecvt.o			\
	errlst.o		\
	amd64_data.o		\
	ldivide.o		\
	lock.o			\
	makectxt.o		\
	memccpy.o		\
	memchr.o		\
	memcmp.o		\
	memcpy.o		\
	memset.o		\
	new_list.o		\
	proc64_id.o		\
	proc64_support.o	\
	setjmp.o		\
	siginfolst.o		\
	siglongjmp.o		\
	strcmp.o		\
	strcpy.o		\
	strlen.o		\
	strncmp.o		\
	strncpy.o		\
	strnlen.o		\
	sync_instruction_memory.o

# Preserved solely to ease maintenance of 32-bit and 64-bit library builds
# This macro should ALWAYS be empty; native APIs are already 'large file'.
COMSYSOBJS64=

SYSOBJS64=

COMSYSOBJS=			\
	__clock_timer.o		\
	__getloadavg.o		\
	__rusagesys.o		\
	__signotify.o		\
	__sigrt.o		\
	__time.o		\
	_lgrp_home_fast.o	\
	_lgrpsys.o		\
	_nfssys.o		\
	_portfs.o		\
	_pset.o			\
	_rpcsys.o		\
	_sigaction.o		\
	_so_accept.o		\
	_so_bind.o		\
	_so_connect.o		\
	_so_getpeername.o	\
	_so_getsockname.o	\
	_so_getsockopt.o	\
	_so_listen.o		\
	_so_recv.o		\
	_so_recvfrom.o		\
	_so_recvmsg.o		\
	_so_send.o		\
	_so_sendmsg.o		\
	_so_sendto.o		\
	_so_setsockopt.o	\
	_so_shutdown.o		\
	_so_socket.o		\
	_so_socketpair.o	\
	_sockconfig.o		\
	access.o		\
	acct.o			\
	acl.o			\
	adjtime.o		\
	alarm.o			\
	brk.o			\
	chdir.o			\
	chmod.o			\
	chown.o			\
	chroot.o		\
	cladm.o			\
	close.o			\
	creat.o			\
	dup.o			\
	execve.o		\
	exit.o			\
	facl.o			\
	fchdir.o		\
	fchmod.o		\
	fchown.o		\
	fchroot.o		\
	fcntl.o			\
	fdsync.o		\
	fpathconf.o		\
	fstat.o			\
	fstatfs.o		\
	fstatvfs.o		\
	getcpuid.o		\
	getdents.o		\
	getegid.o		\
	geteuid.o		\
	getgid.o		\
	getgroups.o		\
	gethrtime.o		\
	getitimer.o		\
	getmsg.o		\
	getpid.o		\
	getpmsg.o		\
	getppid.o		\
	getrlimit.o		\
	getuid.o		\
	gtty.o			\
	install_utrap.o		\
	ioctl.o			\
	kaio.o			\
	kill.o			\
	lchown.o		\
	link.o			\
	llseek.o		\
	lseek.o			\
	lstat.o			\
	mmapobjsys.o		\
	memcntl.o		\
	mincore.o		\
	mkdir.o			\
	mknod.o			\
	mmap.o			\
	modctl.o		\
	mount.o			\
	mprotect.o		\
	munmap.o		\
	nice.o			\
	ntp_adjtime.o		\
	ntp_gettime.o		\
	open.o			\
	p_online.o		\
	pathconf.o		\
	pause.o			\
	pcsample.o		\
	pollsys.o		\
	pread.o			\
	priocntlset.o		\
	processor_bind.o	\
	processor_info.o	\
	profil.o		\
	putmsg.o		\
	putpmsg.o		\
	pwrite.o		\
	read.o			\
	readlink.o		\
	readv.o			\
	rename.o		\
	resolvepath.o		\
	rmdir.o			\
	seteguid.o		\
	setgid.o		\
	setgroups.o		\
	setitimer.o		\
	setreid.o		\
	setrlimit.o		\
	setuid.o		\
	sigaltstk.o		\
	sigprocmsk.o		\
	sigsendset.o		\
	sigsuspend.o		\
	stat.o			\
	statfs.o		\
	statvfs.o		\
	stty.o			\
	symlink.o		\
	sync.o			\
	sysconfig.o		\
	sysfs.o			\
	sysinfo.o		\
	syslwp.o		\
	times.o			\
	ulimit.o		\
	umask.o			\
	umount2.o		\
	unlink.o		\
	utssys.o		\
	uucopy.o		\
	vhangup.o		\
	waitid.o		\
	write.o			\
	writev.o		\
	yield.o

SYSOBJS=			\
	__clock_gettime.o	\
	__getcontext.o		\
	__uadmin.o		\
	_lwp_mutex_unlock.o	\
	_stack_grow.o		\
	door.o			\
	forkx.o			\
	forkallx.o		\
	fxstat.o		\
	getcontext.o		\
	gettimeofday.o		\
	lwp_private.o		\
	lxstat.o		\
	nuname.o		\
	pipe.o			\
	syscall.o		\
	sysi86.o		\
	tls_get_addr.o		\
	uadmin.o		\
	umount.o		\
	uname.o			\
	vforkx.o		\
	xmknod.o		\
	xstat.o

# Preserved solely to ease maintenance of 32-bit and 64-bit library builds
# This macro should ALWAYS be empty; native APIs are already 'large file'.
PORTGEN64=

# objects from source under ../port
PORTFP=				\
	__flt_decim.o		\
	__flt_rounds.o		\
	__tbl_10_b.o		\
	__tbl_10_h.o		\
	__tbl_10_s.o		\
	__tbl_2_b.o		\
	__tbl_2_h.o		\
	__tbl_2_s.o		\
	__tbl_fdq.o		\
	__tbl_tens.o		\
	__x_power.o		\
	_base_sup.o		\
	aconvert.o		\
	decimal_bin.o		\
	double_decim.o		\
	econvert.o		\
	fconvert.o		\
	file_decim.o		\
	finite.o		\
	fp_data.o		\
	func_decim.o		\
	gconvert.o		\
	hex_bin.o		\
	ieee_globals.o		\
	pack_float.o		\
	sigfpe.o		\
	string_decim.o

PORTGEN=			\
	_env_data.o		\
	_xftw.o			\
	a64l.o			\
	abort.o			\
	addsev.o		\
	assert.o		\
	atof.o			\
	atoi.o			\
	atol.o			\
	atoll.o			\
	attropen.o		\
	atexit.o		\
	atfork.o		\
	basename.o		\
	calloc.o		\
	catgets.o		\
	catopen.o		\
	cfgetispeed.o		\
	cfgetospeed.o		\
	cfree.o			\
	cfsetispeed.o		\
	cfsetospeed.o		\
	cftime.o		\
	clock.o			\
	closedir.o		\
	closefrom.o		\
	confstr.o		\
	crypt.o			\
	csetlen.o		\
	ctime.o			\
	ctime_r.o		\
	daemon.o		\
	deflt.o			\
	directio.o		\
	dirname.o		\
	div.o			\
	drand48.o		\
	dup2.o			\
	env_data.o		\
	err.o			\
	errno.o			\
	euclen.o		\
	event_port.o		\
	execvp.o		\
	fattach.o		\
	fdetach.o		\
	fdopendir.o		\
	fmtmsg.o		\
	ftime.o			\
	ftok.o			\
	ftw.o			\
	gcvt.o			\
	getauxv.o		\
	getcwd.o		\
	getdate_err.o		\
	getdtblsize.o		\
	getenv.o		\
	getexecname.o		\
	getgrnam.o		\
	getgrnam_r.o		\
	gethostid.o		\
	gethostname.o		\
	gethz.o			\
	getisax.o		\
	getloadavg.o		\
	getlogin.o		\
	getmntent.o		\
	getnetgrent.o		\
	getopt.o		\
	getopt_long.o		\
	getpagesize.o		\
	getpw.o			\
	getpwnam.o		\
	getpwnam_r.o		\
	getrusage.o		\
	getspent.o		\
	getspent_r.o		\
	getsubopt.o		\
	gettxt.o		\
	getusershell.o		\
	getut.o			\
	getutx.o		\
	getvfsent.o		\
	getwd.o			\
	getwidth.o		\
	getxby_door.o		\
	gtxt.o			\
	hsearch.o		\
	iconv.o			\
	imaxabs.o		\
	imaxdiv.o		\
	index.o			\
	initgroups.o		\
	insque.o		\
	isaexec.o		\
	isastream.o		\
	isatty.o		\
	killpg.o		\
	klpdlib.o		\
	l64a.o			\
	lckpwdf.o		\
	lconstants.o		\
	lexp10.o		\
	lfind.o			\
	lfmt.o			\
	lfmt_log.o		\
	lldiv.o			\
	llog10.o		\
	lltostr.o		\
	lmath.o			\
	localtime.o		\
	lsearch.o		\
	madvise.o		\
	malloc.o		\
	memalign.o		\
	mkdev.o			\
	mkdtemp.o		\
	mkfifo.o		\
	mkstemp.o		\
	mktemp.o		\
	mlock.o			\
	mlockall.o		\
	mon.o			\
	msync.o			\
	munlock.o		\
	munlockall.o		\
	ndbm.o			\
	nftw.o			\
	nlspath_checks.o	\
	nsparse.o		\
	nss_common.o		\
	nss_dbdefs.o		\
	nss_deffinder.o		\
	opendir.o		\
	opt_data.o		\
	perror.o		\
	pfmt.o			\
	pfmt_data.o		\
	pfmt_print.o		\
	plock.o			\
	poll.o			\
	posix_fadvise.o		\
	posix_fallocate.o	\
	posix_madvise.o		\
	posix_memalign.o	\
	priocntl.o		\
	privlib.o		\
	priv_str_xlate.o	\
	psiginfo.o		\
	psignal.o		\
	pt.o			\
	putpwent.o		\
	putspent.o		\
	raise.o			\
	rand.o			\
	random.o		\
	rctlops.o		\
	readdir.o		\
	readdir_r.o		\
	realpath.o		\
	reboot.o		\
	regexpr.o		\
	remove.o		\
	rewinddir.o		\
	rindex.o		\
	scandir.o		\
	seekdir.o		\
	select.o		\
	setlabel.o		\
	setpriority.o		\
	settimeofday.o		\
	sh_locks.o		\
	sigflag.o		\
	siglist.o		\
	sigsend.o		\
	sigsetops.o		\
	ssignal.o		\
	stack.o			\
	str2sig.o		\
	strcase_charmap.o	\
	strcasecmp.o		\
	strcat.o		\
	strchr.o		\
	strcspn.o		\
	strdup.o		\
	strerror.o		\
	strlcat.o		\
	strlcpy.o		\
	strncasecmp.o		\
	strncat.o		\
	strpbrk.o		\
	strrchr.o		\
	strsep.o		\
	strsignal.o		\
	strspn.o		\
	strstr.o		\
	strtod.o		\
	strtoimax.o		\
	strtok.o		\
	strtok_r.o		\
	strtoumax.o		\
	swab.o			\
	swapctl.o		\
	sysconf.o		\
	syslog.o		\
	tcdrain.o		\
	tcflow.o		\
	tcflush.o		\
	tcgetattr.o		\
	tcgetpgrp.o		\
	tcgetsid.o		\
	tcsendbreak.o		\
	tcsetattr.o		\
	tcsetpgrp.o		\
	tell.o			\
	telldir.o		\
	tfind.o			\
	time_data.o		\
	time_gdata.o		\
	truncate.o		\
	tsdalloc.o		\
	tsearch.o		\
	ttyname.o		\
	ttyslot.o		\
	ualarm.o		\
	ucred.o			\
	valloc.o		\
	vlfmt.o			\
	vpfmt.o			\
	waitpid.o		\
	walkstack.o		\
	wdata.o			\
	xgetwidth.o		\
	xpg4.o			\
	xpg6.o

PORTPRINT_W=			\
	doprnt_w.o

PORTPRINT=			\
	asprintf.o		\
	doprnt.o		\
	fprintf.o		\
	printf.o		\
	snprintf.o		\
	sprintf.o		\
	vfprintf.o		\
	vprintf.o		\
	vsnprintf.o		\
	vsprintf.o		\
	vwprintf.o		\
	wprintf.o

# Preserved solely to ease maintenance of 32-bit and 64-bit library builds
# This macro should ALWAYS be empty; native APIs are already 'large file'.
PORTSTDIO64=

PORTSTDIO_W=			\
	doscan_w.o

PORTSTDIO=			\
	__extensions.o		\
	_endopen.o		\
	_filbuf.o		\
	_findbuf.o		\
	_flsbuf.o		\
	_wrtchk.o		\
	clearerr.o		\
	ctermid.o		\
	ctermid_r.o		\
	cuserid.o		\
	data.o			\
	doscan.o		\
	fdopen.o		\
	feof.o			\
	ferror.o		\
	fgetc.o			\
	fgets.o			\
	fileno.o		\
	flockf.o		\
	flush.o			\
	fopen.o			\
	fpos.o			\
	fputc.o			\
	fputs.o			\
	fread.o			\
	fseek.o			\
	fseeko.o		\
	ftell.o			\
	ftello.o		\
	fwrite.o		\
	getc.o			\
	getchar.o		\
	getpass.o		\
	gets.o			\
	getw.o			\
	mse.o			\
	popen.o			\
	putc.o			\
	putchar.o		\
	puts.o			\
	putw.o			\
	rewind.o		\
	scanf.o			\
	setbuf.o		\
	setbuffer.o		\
	setvbuf.o		\
	system.o		\
	tempnam.o		\
	tmpfile.o		\
	tmpnam_r.o		\
	ungetc.o		\
	vscanf.o		\
	vwscanf.o		\
	wscanf.o

PORTI18N=			\
	__fgetwc_xpg5.o		\
	__fgetws_xpg5.o		\
	__fputwc_xpg5.o		\
	__fputws_xpg5.o		\
	__ungetwc_xpg5.o	\
	getwchar.o		\
	putwchar.o		\
	putws.o			\
	strtows.o		\
	wcsstr.o		\
	wcstoimax.o		\
	wcstol.o		\
	wcstoul.o		\
	wcswcs.o		\
	wmemchr.o		\
	wmemcmp.o		\
	wmemcpy.o		\
	wmemmove.o		\
	wmemset.o		\
	wscasecmp.o		\
	wscat.o			\
	wschr.o			\
	wscmp.o			\
	wscpy.o			\
	wscspn.o		\
	wsdup.o			\
	wslen.o			\
	wsncasecmp.o		\
	wsncat.o		\
	wsncmp.o		\
	wsncpy.o		\
	wspbrk.o		\
	wsprintf.o		\
	wsrchr.o		\
	wsscanf.o		\
	wsspn.o			\
	wstod.o			\
	wstok.o			\
	wstol.o			\
	wstoll.o		\
	wsxfrm.o		\
	gettext.o		\
	gettext_gnu.o		\
	gettext_real.o		\
	gettext_util.o		\
	plural_parser.o		\
	wdresolve.o		\
	_ctype.o		\
	isascii.o		\
	toascii.o

PORTI18N_COND=			\
	wcstol_longlong.o	\
	wcstoul_longlong.o

AIOOBJS=			\
	aio.o			\
	aio_alloc.o		\
	posix_aio.o

RTOBJS=				\
	clock_timer.o		\
	mqueue.o		\
	pos4obj.o		\
	sched.o			\
	sem.o			\
	shm.o			\
	sigev_thread.o

TPOOLOBJS=			\
	thread_pool.o

THREADSOBJS=			\
	alloc.o			\
	assfail.o		\
	cancel.o		\
	door_calls.o		\
	pthr_attr.o		\
	pthr_barrier.o		\
	pthr_cond.o		\
	pthr_mutex.o		\
	pthr_rwlock.o		\
	pthread.o		\
	rwlock.o		\
	scalls.o		\
	sema.o			\
	sigaction.o		\
	spawn.o			\
	synch.o			\
	tdb_agent.o		\
	thr.o			\
	thread_interface.o	\
	tls.o			\
	tsd.o

THREADSMACHOBJS=		\
	machdep.o

THREADSASMOBJS=			\
	asm_subr.o

UNICODEOBJS=			\
	u8_textprep.o		\
	uconv.o

UNWINDMACHOBJS=			\
	call_frame_inst.o	\
	eh_frame.o		\
	thrp_unwind.o		\
	unwind.o

pics/unwind.o:= COPTFLAG64 =

UNWINDASMOBJS=			\
	unwind_frame.o

# Preserved solely to ease maintenance of 32-bit and 64-bit library builds
# This macro should ALWAYS be empty; native APIs are already 'large file'.
PORTSYS64=

PORTSYS=			\
	_autofssys.o		\
	acctctl.o		\
	bsd_signal.o		\
	corectl.o		\
	exacctsys.o		\
	execl.o			\
	execle.o		\
	execv.o			\
	fsmisc.o		\
	fstatat.o		\
	getpagesizes.o		\
	getpeerucred.o		\
	inst_sync.o		\
	issetugid.o		\
	label.o			\
	libc_fcntl.o		\
	libc_link.o		\
	libc_open.o		\
	lockf.o			\
	lwp.o			\
	lwp_cond.o		\
	lwp_rwlock.o		\
	lwp_sigmask.o		\
	meminfosys.o		\
	msgsys.o		\
	nfssys.o		\
	openat.o		\
	pgrpsys.o		\
	posix_sigwait.o		\
	ppriv.o			\
	psetsys.o		\
	rctlsys.o		\
	sbrk.o			\
	semsys.o		\
	set_errno.o		\
	sharefs.o		\
	shmsys.o		\
	sidsys.o		\
	siginterrupt.o		\
	signal.o		\
	sigpending.o		\
	sigstack.o		\
	tasksys.o		\
	time.o			\
	time_util.o		\
	ucontext.o		\
	ustat.o			\
	utimesys.o		\
	zone.o

PORTREGEX=			\
	glob.o			\
	regcmp.o		\
	regex.o			\
	wordexp.o

VALUES=				\
	values-Xa.o

MOSTOBJS=			\
	$(STRETS)		\
	$(CRTOBJS)		\
	$(DYNOBJS)		\
	$(FPOBJS)		\
	$(I386FPOBJS)		\
	$(FPASMOBJS)		\
	$(ATOMICOBJS)		\
	$(XATTROBJS)		\
	$(COMOBJS)		\
	$(GENOBJS)		\
	$(PORTFP)		\
	$(PORTGEN)		\
	$(PORTGEN64)		\
	$(PORTI18N)		\
	$(PORTI18N_COND)	\
	$(PORTPRINT)		\
	$(PORTPRINT_W)		\
	$(PORTREGEX)		\
	$(PORTSTDIO)		\
	$(PORTSTDIO64)		\
	$(PORTSTDIO_W)		\
	$(PORTSYS)		\
	$(PORTSYS64)		\
	$(AIOOBJS)		\
	$(RTOBJS)		\
	$(TPOOLOBJS)		\
	$(THREADSOBJS)		\
	$(THREADSMACHOBJS)	\
	$(THREADSASMOBJS)	\
	$(UNICODEOBJS)		\
	$(UNWINDMACHOBJS)	\
	$(UNWINDASMOBJS)	\
	$(COMSYSOBJS)		\
	$(SYSOBJS)		\
	$(COMSYSOBJS64)		\
	$(SYSOBJS64)		\
	$(VALUES)

TRACEOBJS=			\
	plockstat.o

# NOTE:	libc.so.1 must be linked with the minimal crti.o and crtn.o
# modules whose source is provided in the $(SRC)/lib/common directory.
# This must be done because otherwise the Sun C compiler would insert
# its own versions of these modules and those versions contain code
# to call out to C++ initialization functions.  Such C++ initialization
# functions can call back into libc before thread initialization is
# complete and this leads to segmentation violations and other problems.
# Since libc contains no C++ code, linking with the minimal crti.o and
# crtn.o modules is safe and avoids the problems described above.
OBJECTS= $(CRTI) $(MOSTOBJS) $(CRTN)
CRTSRCS= ../../common/amd64

# include common library definitions
include ../../Makefile.lib
include ../../Makefile.lib.64

# NOTE: libc_i18n.a will be part of libc.so.1.  Therefore, the compilation
# conditions such as the settings of CFLAGS and CPPFLAGS for the libc_i18n stuff
# need to be compatible with the ones for the libc stuff.  Whenever the changes
# that affect the compilation conditions of libc happened, those for libc_i18n
# also need to be updated.

CFLAGS64 += $(CTF_FLAGS)

# This is necessary to avoid problems with calling _ex_unwind().
# We probably don't want any inlining anyway.
CFLAGS64 += -xinline=

# Setting THREAD_DEBUG = -DTHREAD_DEBUG (make THREAD_DEBUG=-DTHREAD_DEBUG ...)
# enables ASSERT() checking in the threads portion of the library.
# This is automatically enabled for DEBUG builds, not for non-debug builds.
THREAD_DEBUG =
$(NOT_RELEASE_BUILD)THREAD_DEBUG = -DTHREAD_DEBUG

# Make string literals read-only to save memory
CFLAGS64 += $(XSTRCONST)

ALTPICS= $(TRACEOBJS:%=pics/%)

$(DYNLIB) := PICS += $(ROOTFS_LIBDIR64)/libc_i18n.a
$(DYNLIB) := BUILD.SO = $(LD) -o $@ -G $(DYNFLAGS) $(PICS) $(ALTPICS)

MAPFILES =	../port/mapfile-vers mapfile-vers

CPPFLAGS=	-D_REENTRANT -D$(MACH64) -D__$(MACH64) $(THREAD_DEBUG) \
		-I. -I$(LIBCBASE)/inc -I../inc $(CPPFLAGS.master) -I/usr/include
ASFLAGS=	$(AS_PICFLAGS) -P -D__STDC__ -D_ASM $(CPPFLAGS) \
		$(amd64_AS_XARCH)

# Conditionally add support for making |wordexp()| check whether
# /usr/bin/ksh is ksh93 or not
include ../../../Makefile.ksh93switch
CPPFLAGS += -DWORDEXP_KSH93=$(ON_BUILD_KSH93_AS_BINKSH)

# proc64_id.o is built with defines in $(SRC)/uts/intel/sys/x86_archext.h
pics/proc64_id.o	:= CFLAGS64 += -I$(SRC)/uts/intel

# Inform the run-time linker about libc specialized initialization
RTLDINFO =	-z rtldinfo=tls_rtldinfo
DYNFLAGS +=	$(RTLDINFO)

# Force libc's internal references to be resolved immediately upon loading
# in order to avoid critical region problems.  Since almost all libc symbols
# are marked 'protected' in the mapfiles, this is a minimal set (15 to 20).
DYNFLAGS +=	-znow

BUILD.s=	$(AS) $(ASFLAGS) $< -o $@

# Override this top level flag so the compiler builds in its native
# C99 mode.  This has been enabled to support the complex arithmetic
# added to libc.
C99MODE=	$(C99_ENABLE)

# libc method of building an archive
BUILD.AR= $(RM) $@ ; \
	$(AR) q $@ `$(LORDER) $(MOSTOBJS:%=$(DIR)/%)| $(TSORT)`

# extra files for the clean target
CLEANFILES=			\
	../port/gen/errlst.c	\
	../port/gen/new_list.c	\
	assym.h			\
	genassym		\
	crt/_rtld.s		\
	pics/crti.o		\
	pics/crtn.o		\
	$(ALTPICS)

CLOBBERFILES +=	$(LIB_PIC)

# list of C source for lint
SRCS=							\
	$(ATOMICOBJS:%.o=$(SRC)/common/atomic/%.c)	\
	$(XATTROBJS:%.o=$(SRC)/common/xattr/%.c)	\
	$(COMOBJS:%.o=$(SRC)/common/util/%.c)		\
	$(PORTFP:%.o=../port/fp/%.c)			\
	$(PORTGEN:%.o=../port/gen/%.c)			\
	$(PORTI18N:%.o=../port/i18n/%.c)		\
	$(PORTPRINT:%.o=../port/print/%.c)		\
	$(PORTREGEX:%.o=../port/regex/%.c)		\
	$(PORTSTDIO:%.o=../port/stdio/%.c)		\
	$(PORTSYS:%.o=../port/sys/%.c)			\
	$(AIOOBJS:%.o=../port/aio/%.c)			\
	$(RTOBJS:%.o=../port/rt/%.c)			\
	$(TPOOLOBJS:%.o=../port/tpool/%.c)		\
	$(THREADSOBJS:%.o=../port/threads/%.c)		\
	$(THREADSMACHOBJS:%.o=threads/%.c)		\
	$(UNICODEOBJS:%.o=$(SRC)/common/unicode/%.c)	\
	$(UNWINDMACHOBJS:%.o=unwind/%.c)		\
	$(FPOBJS:%.o=fp/%.c)				\
	$(I386FPOBJS:%.o=../i386/fp/%.c)		\
	$(LIBCBASE)/gen/ecvt.c				\
	$(LIBCBASE)/gen/makectxt.c			\
	$(LIBCBASE)/gen/siginfolst.c			\
	$(LIBCBASE)/gen/siglongjmp.c			\
	$(LIBCBASE)/gen/sync_instruction_memory.c	\
	$(LIBCBASE)/sys/uadmin.c

# conditional assignments
# $(DYNLIB) $(LIB_PIC) := DYNOBJS = _rtbootld.o
$(DYNLIB) := CRTI = crti.o
$(DYNLIB) := CRTN = crtn.o

# Files which need the threads .il inline template
TIL=				\
	aio.o			\
	alloc.o			\
	assfail.o		\
	atexit.o		\
	atfork.o		\
	cancel.o		\
	door_calls.o		\
	errno.o			\
	lwp.o			\
	ma.o			\
	machdep.o		\
	posix_aio.o		\
	pthr_attr.o		\
	pthr_barrier.o		\
	pthr_cond.o		\
	pthr_mutex.o		\
	pthr_rwlock.o		\
	pthread.o		\
	rand.o			\
	rwlock.o		\
	scalls.o		\
	sched.o			\
	sema.o			\
	sigaction.o		\
	sigev_thread.o		\
	spawn.o			\
	stack.o			\
	synch.o			\
	tdb_agent.o		\
	thr.o			\
	thread_interface.o	\
	thread_pool.o		\
	thrp_unwind.o		\
	tls.o			\
	tsd.o

$(TIL:%=pics/%) := CFLAGS64 += $(LIBCBASE)/threads/amd64.il

# pics/mul64.o := CFLAGS64 += crt/mul64.il

# large-file-aware components that should be built large

#$(COMSYSOBJS64:%=pics/%) := \
#	CPPFLAGS += -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64

#$(SYSOBJS64:%=pics/%) := \
#	CPPFLAGS += -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64

#$(PORTGEN64:%=pics/%) := \
#	CPPFLAGS += -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64

#$(PORTSTDIO64:%=pics/%) := \
#	CPPFLAGS += -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64

#$(PORTSYS64:%=pics/%) := \
#	CPPFLAGS += -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64

$(PORTSTDIO_W:%=pics/%) := \
	CPPFLAGS += -D_WIDE

$(PORTPRINT_W:%=pics/%) := \
	CPPFLAGS += -D_WIDE

$(PORTPRINT_C89:%=pics/%) := \
	CPPFLAGS += -D_C89_INTMAX32

$(PORTSTDIO_C89:%=pics/%) := \
	CPPFLAGS += -D_C89_INTMAX32

$(PORTI18N_COND:%=pics/%) := \
	CPPFLAGS += -D_WCS_LONGLONG

.KEEP_STATE:

all: $(LIBS) $(LIB_PIC)

lint	:=	CPPFLAGS += -I../$(MACH)/fp
lint	:=	CPPFLAGS += -D_MSE_INT_H -D_LCONV_C99
lint	:=	LINTFLAGS64 += -mn -erroff=E_SUPPRESSION_DIRECTIVE_UNUSED

lint:
	@echo $(LINT.c) ... $(LDLIBS)
	@$(LINT.c) $(SRCS) $(LDLIBS)

$(LINTLIB):= SRCS=../port/llib-lc
$(LINTLIB):= CPPFLAGS += -D_MSE_INT_H
$(LINTLIB):= LINTFLAGS64=-nvx -m64

# object files that depend on inline template
$(TIL:%=pics/%): $(LIBCBASE)/threads/amd64.il
# pics/mul64.o: crt/mul64.il

# include common libc targets
include ../Makefile.targ

# We need to strip out all CTF data from the static library
$(LIB_PIC) := DIR = pics
$(LIB_PIC): pics $$(PICS)
	$(BUILD.AR)
	$(MCS) -d -n .SUNW_ctf $@ > /dev/null 2>&1
	$(AR) -ts $@ > /dev/null
	$(POST_PROCESS_A)

ASSYMDEP_OBJS=			\
	_lwp_mutex_unlock.o	\
	_stack_grow.o		\
	asm_subr.o		\
	getcontext.o		\
	setjmp.o		\
	tls_get_addr.o		\
	vforkx.o

$(ASSYMDEP_OBJS:%=pics/%): assym.h

# assym.h build rules

GENASSYM_C = genassym.c

# XXX	A hack.  Perhaps this should be 'CPPFLAGS.native' and
#	live in Makefile.master

CPPFLAGS.genassym = \
	$(ENVCPPFLAGS1) $(ENVCPPFLAGS2) $(ENVCPPFLAGS3) $(ENVCPPFLAGS4)

genassym: $(GENASSYM_C)
	$(NATIVECC) -Iinc -I../inc $(CPPFLAGS.genassym) -o $@ $(GENASSYM_C)

OFFSETS = ../$(MACH)/offsets.in

assym.h: $(OFFSETS) genassym
	$(OFFSETS_CREATE) <$(OFFSETS) >$@
	./genassym >>$@

# derived C source and related explicit dependencies
../port/gen/errlst.c + \
../port/gen/new_list.c: ../port/gen/errlist ../port/gen/errlist.awk
	cd ../port/gen; pwd; $(AWK) -f errlist.awk < errlist

pics/errlst.o: ../port/gen/errlst.c

pics/new_list.o: ../port/gen/new_list.c
