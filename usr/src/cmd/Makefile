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

include ../Makefile.master
include ../Makefile.ksh93switch

#
#	Note that the commands 'agents', 'lp', 'perl', and 'man' are first in
#	the list, violating alphabetical order.  This is because they are very
#	long-running and should be given the most wall-clock time for a
#	parallel build.
#
#	Commands in the FIRST_SUBDIRS list are built before starting the build
#	of other commands.  Currently this includes only 'isaexec' and
#	'platexec'.  This is necessary because $(ROOT)/usr/lib/isaexec or
#	$(ROOT)/usr/lib/platexec must exist when some other commands are built
#	because their 'make install' creates a hard link to one of them.
#
#	Commands are listed one per line so that TeamWare can auto-merge most
#	changes.
#

FIRST_SUBDIRS=		\
	isaexec		\
	platexec

COMMON_SUBDIRS=		\
	agents		\
	availdevs	\
	lp		\
	perl		\
	man		\
	Adm		\
	abi		\
	adbgen		\
	acct		\
	acctadm		\
	arch		\
	asa		\
	ast		\
	audio		\
	auths		\
	autopush	\
	avs		\
	awk		\
	awk_xpg4	\
	backup		\
	banner		\
	bart		\
	basename	\
	bc		\
	bdiff		\
	bfs		\
	bnu		\
	boot		\
	busstat		\
	cal		\
	calendar	\
	captoinfo	\
	cat		\
	cdrw		\
	cfgadm		\
	checkeq		\
	checknr		\
	chgrp		\
	chmod		\
	chown		\
	chroot		\
	clear		\
	clinfo		\
	cmd-crypto	\
	cmd-inet	\
	col		\
	compress	\
	consadm		\
	coreadm		\
	cpio		\
	cpc		\
	cron		\
	crypt		\
	csh		\
	csplit		\
	ctrun		\
	ctstat		\
	ctwatch		\
	datadm		\
	date		\
	dc		\
	dd		\
	deroff		\
	des		\
	devfsadm	\
	syseventd	\
	devctl		\
	devinfo		\
	devmgmt		\
	devprop		\
	dfs.cmds	\
	diff		\
	diff3		\
	diffmk		\
	dircmp		\
	dirname		\
	dis		\
	diskmgtd	\
	dispadmin	\
	dladm		\
	dmesg		\
	dodatadm	\
	dtrace		\
	du		\
	dumpadm		\
	dumpcs		\
	echo		\
	ed		\
	eeprom		\
	egrep		\
	eject		\
	emul64ioctl	\
	enhance		\
	env		\
	eqn		\
	expand		\
	expr		\
	exstr		\
	factor		\
	false		\
	fcinfo		\
	fcoesvc		\
	fdetach		\
	fdformat	\
	fdisk		\
	filesync	\
	fgrep		\
	file		\
	filebench	\
	find		\
	flowadm		\
	fm		\
	fmli		\
	fmt		\
	fmthard		\
	fmtmsg		\
	fold		\
	format		\
	fs.d		\
	fstyp		\
	fuser		\
	fwflash		\
	gcore		\
	gencat		\
	geniconvtbl	\
	genmsg		\
	getconf		\
	getdevpolicy	\
	getent		\
	getfacl		\
	getmajor	\
	getopt		\
	gettext		\
	gettxt		\
	graph		\
	grep		\
	grep_xpg4	\
	groups		\
	grpck		\
	gss		\
	hal		\
	halt		\
	head		\
	hostid		\
	hostname	\
	hotplug		\
	hotplugd	\
	hwdata		\
	id		\
	idmap		\
	infocmp		\
	init		\
	initpkg		\
	install.d	\
	intrd		\
	intrstat	\
	ipcrm		\
	ipcs		\
	ipf		\
	isainfo		\
	isalist		\
	iscsi		\
	itutools	\
	iscsiadm	\
	iscsid		\
	iscsitsvc	\
	isns		\
	itadm		\
	java		\
	kbd		\
	keyserv		\
	killall		\
	krb5		\
	ksh		\
	kstat		\
	last		\
	lastcomm	\
	latencytop	\
	ldap		\
	ldapcachemgr	\
	lgrpinfo	\
	line		\
	link		\
	dlmgmtd		\
	listen		\
	loadkeys	\
	locale		\
	lockstat	\
	locator		\
	lofiadm		\
	logadm		\
	logger		\
	login		\
	logins		\
	look		\
	ls		\
	luxadm		\
	lvm		\
	mach		\
	machid		\
	mail		\
	mailx		\
	makekey		\
	mdb		\
	mesg		\
	mkdir		\
	mkfifo		\
	mkfile		\
	mkmsgs		\
	mknod		\
	mkpwdict	\
	mktemp		\
	mms		\
	modload		\
	more		\
	mpathadm	\
	msgfmt		\
	msgid		\
	mt		\
	mv		\
	mvdir		\
	ndmpadm		\
	ndmpd		\
	ndmpstat	\
	netadm		\
	netfiles	\
	newform		\
	newgrp		\
	news		\
	newtask		\
	nice		\
	nl		\
	nlsadmin	\
	nohup		\
	nsadmin		\
	nscd		\
	oamuser		\
	oawk		\
	pack		\
	pagesize	\
	passmgmt	\
	passwd		\
	pathchk		\
	pbind		\
	pcidr		\
	pcitool		\
	pfexec		\
	pgrep		\
	picl		\
	plimit		\
	policykit	\
	pools		\
	power		\
	powertop	\
	ppgsz		\
	pg		\
	plockstat	\
	pr		\
	prctl		\
	print		\
	priocntl	\
	profiles	\
	projadd		\
	projects	\
	prstat		\
	prtconf		\
	prtdiag		\
	prtvtoc		\
	ps		\
	psradm		\
	psrinfo		\
	psrset		\
	ptools		\
	pwck		\
	pwconv		\
	pwd		\
	pyzfs		\
	raidctl		\
	ramdiskadm	\
	rcap		\
	rcm_daemon	\
	rctladm		\
	refer		\
	regcmp		\
	renice		\
	rexd		\
	rm		\
	rmdir		\
	rmformat	\
	rmmount		\
	rmt		\
	rmvolmgr	\
	roles		\
	rpcbind		\
	rpcgen		\
	rpcinfo		\
	rpcsvc		\
	runat		\
	sa		\
	saf		\
	sasinfo		\
	savecore	\
	sbdadm		\
	script		\
	scsi		\
	sdiff		\
	sdpadm		\
	sendmail	\
	setfacl		\
	setmnt		\
	setpgrp		\
	setuname	\
	sgs		\
	sh		\
	shcomp		\
	smbios		\
	smbsrv		\
	smserverd	\
	soelim		\
	sort		\
	spell		\
	spline		\
	split		\
	sqlite		\
	srchtxt		\
	srptsvc		\
	ssh		\
	stat		\
	stmfadm		\
	stmfproxy	\
	stmfsvc		\
	stmsboot	\
	streams		\
	strings		\
	su		\
	sulogin		\
	sunpc		\
	svc		\
	svr4pkg		\
	swap		\
	sync		\
	sysdef		\
	syseventadm	\
	syslogd		\
	tabs		\
	tar		\
	tbl		\
	tcopy		\
	tcpd		\
	terminfo	\
	th_tools	\
	tic		\
	time		\
	tip		\
	tnf		\
	touch		\
	tplot		\
	tput		\
	trapstat	\
	troff		\
	true		\
	truss		\
	tsol		\
	tty		\
	ttymon		\
	tzreload	\
	uadmin		\
	ul		\
	uname		\
	units		\
	unlink		\
	unpack		\
	users		\
	utmp_update	\
	utmpd		\
	valtools	\
	vgrind		\
	vi		\
	volcheck	\
	volrmmount	\
	vrrpadm		\
	vscan		\
	vt		\
	w		\
	wall		\
	wbem		\
	which		\
	who		\
	whodo		\
	wracct		\
	write		\
	wusbadm		\
	xargs		\
	xstr		\
	yes		\
	ypcmd		\
	yppasswd	\
	zdb		\
	zdump		\
	zfs		\
	zic		\
	zinject		\
	zlogin		\
	zoneadm		\
	zoneadmd	\
	zonecfg		\
	zonename	\
	zpool		\
	zlook		\
	zstreamdump	\
	ztest

$(CLOSED_BUILD)COMMON_SUBDIRS += \
	$(CLOSED)/cmd/iconv	\
	$(CLOSED)/cmd/ksh	\
	$(CLOSED)/cmd/llc2	\
	$(CLOSED)/cmd/localedef	\
	$(CLOSED)/cmd/more_xpg4	\
	$(CLOSED)/cmd/mtst	\
	$(CLOSED)/cmd/od	\
	$(CLOSED)/cmd/patch	\
	$(CLOSED)/cmd/pax	\
	$(CLOSED)/cmd/printf	\
	$(CLOSED)/cmd/sed	\
	$(CLOSED)/cmd/sed_xpg4	\
ODE	$(CLOSED)/cmd/tail	\
	$(CLOSED)/cmd/tr_xpg4

i386_SUBDIRS=		\
	addbadsec	\
	biosdev		\
	diskscan	\
	lms		\
	ntfsprogs	\
	parted		\
	rtc		\
	ucodeadm	\
	xvm

sparc_SUBDIRS=		\
	cvcd		\
	dcs		\
	device_remap	\
	drd		\
	fps		\
	fruadm		\
	ldmad		\
	oplhpd		\
	prtdscp		\
	prtfru		\
	scadm		\
	sckmd		\
	sf880drd	\
	vntsd

#
# Commands that are messaged.  Note that 'lp' and 'man' come first
# (see previous comment about 'lp' and 'man').
#
MSGSUBDIRS=		\
	lp		\
	man		\
	abi		\
	acctadm		\
	allocate	\
	asa		\
	audio		\
	audit		\
	auditd		\
	auditrecord	\
	auths		\
	autopush	\
	avs		\
	awk		\
	awk_xpg4	\
	backup		\
	banner		\
	bart		\
	basename	\
	bnu		\
	bsmconv		\
	bsmunconv	\
	busstat		\
	cal		\
	cat		\
	cdrw		\
	cfgadm		\
	checkeq		\
	checknr		\
	chgrp		\
	chmod		\
	chown		\
	cmd-crypto	\
	cmd-inet	\
	col		\
	compress	\
	consadm		\
	coreadm		\
	cpio		\
	cpc		\
	cron		\
	csh		\
	csplit		\
	ctrun		\
	ctstat		\
	ctwatch		\
	datadm		\
	date		\
	dc		\
	dcs		\
	dd		\
	deroff		\
	devfsadm	\
	dfs.cmds	\
	diff		\
	diffmk		\
	dladm		\
	du		\
	dumpcs		\
	ed		\
	eject		\
	env		\
	eqn		\
	expand		\
	expr		\
	fcinfo		\
	fgrep		\
	file		\
	filesync	\
	find		\
	flowadm		\
	fm		\
	fold		\
	fs.d		\
	fwflash		\
	geniconvtbl	\
	genmsg		\
	getconf		\
	getent		\
	gettext		\
	gettxt		\
	grep		\
	grep_xpg4	\
	grpck		\
	gss		\
	halt		\
	head		\
	hostname	\
	hotplug		\
	id		\
	idmap		\
	isaexec		\
	iscsi		\
	iscsiadm	\
	iscsid		\
	isns		\
	itadm		\
	kbd		\
	krb5		\
	ksh		\
	kstat		\
	last		\
	ldap		\
	ldapcachemgr	\
	lgrpinfo	\
	locale		\
	lofiadm		\
	logadm		\
	logger		\
	logins		\
	ls		\
	luxadm		\
	lvm		\
	mailx		\
	mesg		\
	mkdir		\
	mkpwdict	\
	mktemp		\
	more		\
	mpathadm	\
	msgfmt		\
	mv		\
	ndmpadm		\
	ndmpstat	\
	newgrp		\
	newtask		\
	nice		\
	nohup		\
	oawk		\
	pack		\
	passwd		\
	passmgmt	\
	pathchk		\
	pfexec		\
	pg		\
	pgrep		\
	picl            \
	pools		\
	power		\
	pr		\
	praudit		\
	print		\
	profiles	\
	projadd		\
	projects	\
	prstat		\
	prtdiag		\
	ps		\
	psrinfo		\
	ptools		\
	pwconv		\
	pwd		\
	pyzfs		\
	raidctl		\
	ramdiskadm	\
	rcap		\
	rcm_daemon	\
	refer		\
	regcmp		\
	renice		\
	roles		\
	rm		\
	rmdir		\
	rmformat	\
	rmmount		\
	rmvolmgr	\
	sasinfo		\
	sbdadm		\
	scadm		\
	script		\
	scsi		\
	sdiff		\
	sdpadm		\
	sgs		\
	sh		\
	shcomp		\
	smbsrv		\
	sort		\
	split		\
	ssh		\
	stat		\
	stmfadm		\
	stmsboot	\
	strings		\
	su		\
	svc		\
	svr4pkg		\
	swap		\
	syseventadm	\
	syseventd	\
	tabs		\
	tar		\
	tbl		\
	time		\
	tnf		\
	touch		\
	tput		\
	troff		\
	tsol		\
	tty		\
	ttymon		\
	tzreload	\
	ul		\
	uname		\
	units		\
	unlink		\
	unpack		\
	valtools	\
	vgrind		\
	vi		\
	volcheck	\
	volrmmount	\
	vrrpadm		\
	vscan		\
	w		\
	wbem		\
	who		\
	whodo		\
	wracct		\
	write		\
	wusbadm		\
	xargs		\
	yppasswd	\
	zdump		\
	zfs		\
	zic		\
	zlogin		\
	zoneadm		\
	zoneadmd	\
	zonecfg		\
	zonename	\
	zpool

$(CLOSED_BUILD)MSGSUBDIRS += \
	$(CLOSED)/cmd/iconv	\
	$(CLOSED)/cmd/ksh	\
	$(CLOSED)/cmd/llc2	\
	$(CLOSED)/cmd/localedef	\
	$(CLOSED)/cmd/more_xpg4	\
	$(CLOSED)/cmd/od	\
	$(CLOSED)/cmd/patch	\
	$(CLOSED)/cmd/pax	\
	$(CLOSED)/cmd/printf	\
	$(CLOSED)/cmd/sed	\
	$(CLOSED)/cmd/sed_xpg4	\
	$(CLOSED)/cmd/tail	\
	$(CLOSED)/cmd/tr_xpg4

sparc_MSGSUBDIRS=	\
	fruadm		\
	prtdscp		\
	prtfru		\
	vntsd

i386_MSGSUBDIRS=	\
	ucodeadm

#
# commands that use dcgettext for localized time, LC_TIME
#
DCSUBDIRS=		\
	cal		\
	cfgadm		\
	diff		\
	ls		\
	pr		\
	ps		\
	tar		\
	w		\
	who		\
	whodo		\
	write

$(CLOSED_BUILD)DCSUBDIRS += \
	$(CLOSED)/cmd/pax

#
# commands that belong only to audit and device allocation
#
AUDITSUBDIRS=		\
	allocate	\
	amt		\
	audit		\
	audit_warn	\
	auditconfig	\
	auditd		\
	auditrecord	\
	auditreduce	\
	auditstat	\
	praudit		\
	bsmconv		\
	bsmunconv

#
# commands not owned by the systems group
#
BWOSDIRS=


all :=		TARGET = all
install :=	TARGET = install
clean :=	TARGET = clean
clobber :=	TARGET = clobber
lint :=		TARGET = lint
_msg :=		TARGET = _msg
_dc :=		TARGET = _dc

.KEEP_STATE:

SUBDIRS = $(COMMON_SUBDIRS) $($(MACH)_SUBDIRS)

.PARALLEL:	$(BWOSDIRS) $(SUBDIRS) $(MSGSUBDIRS) $(AUDITSUBDIRS)

all install clean clobber lint: $(FIRST_SUBDIRS) .WAIT $(SUBDIRS) \
	$(AUDITSUBDIRS)

#
# Manifests cannot be checked in parallel, because we are using
# the global repository that is in $(SRC)/cmd/svc/seed/global.db.
# For this reason, to avoid .PARALLEL and .NO_PARALLEL conflicts,
# we spawn off a sub-make to perform the non-parallel 'make check'
#
check:
	$(MAKE) -f Makefile.check check

#
# The .WAIT directive works around an apparent bug in parallel make.
# Evidently make was getting the target _msg vs. _dc confused under
# some level of parallelization, causing some of the _dc objects
# not to be built.
#
_msg: $(MSGSUBDIRS) $($(MACH)_MSGSUBDIRS) .WAIT _dc

_dc: $(DCSUBDIRS)

#
# Dependencies
#
fs.d: fstyp
ksh:	shcomp isaexec
print:	lp

$(FIRST_SUBDIRS) $(BWOSDIRS) $(SUBDIRS) $(AUDITSUBDIRS): FRC
	@if [ -f $@/Makefile  ]; then \
		cd $@; pwd; $(MAKE) $(TARGET); \
	else \
		true; \
	fi

FRC:
