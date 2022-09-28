#! /bin/ksh -p
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Make SUNWonbld package tarball for OpenSolaris.  Besides the
# package, we include licensing files.
#

fail() {
	echo $*
	exit 1
}

#
# Directory that we assemble everything in.  Everything goes into
# $subdir so that it unpacks cleanly.
#
stagedir=$(mktemp -dt onblddropXXXXX)
subdir=onbld

[ -n "$SRC" ] || fail "Please set SRC."
[ -n "$CODEMGR_WS" ] || fail "Please set CODEMGR_WS."
[ -n "$PKGARCHIVE" ] || fail "Please set PKGARCHIVE."

[ -n "$MAKE" ] || export MAKE=make

isa=`uname -p`
tarfile=$CODEMGR_WS/SUNWonbld.$isa.tar

#
# Generate the README from boilerplate and the contents of the
# SUNWonbld tree.
# usage: mkreadme targetdir
#
mkreadme() {
	targetdir=$1
	readme=README.ON-BUILD-TOOLS.$isa
	sed -e s/@ISA@/$isa/ -e s/@DELIVERY@/ON-BUILD-TOOLS/ \
	    $SRC/tools/opensolaris/README.binaries.tmpl > $targetdir/$readme
	(cd $targetdir; find SUNWonbld -type f -print | \
	    sort >> $targetdir/$readme)
}

[ -n "$stagedir" ] || fail "Can't create staging directory."
mkdir -p $stagedir/$subdir || fail "Can't create $stagedir/$subdir."

cd $CODEMGR_WS
# $MAKE -e to make sure PKGARCHIVE is used.
(cd usr/src/tools/SUNWonbld; $MAKE -e install) || fail "Can't make package."

[ -d $PKGARCHIVE/SUNWonbld ] || \
    fail "$PKGARCHIVE/SUNWonbld is missing."
(cd $PKGARCHIVE; tar cf - SUNWonbld) | (cd $stagedir/$subdir; tar xf -)

mkreadme $stagedir/$subdir
cp -p $CODEMGR_WS/THIRDPARTYLICENSE.ON-BUILD-TOOLS $stagedir/$subdir || \
    fail "Can't add THIRDPARTYLICENSE.ON-BUILD-TOOLS."

(cd $stagedir; tar cf $tarfile $subdir) || fail "Can't create $tarfile."
bzip2 -f $tarfile || fail "Can't compress $tarfile".

rm -rf $stagedir
