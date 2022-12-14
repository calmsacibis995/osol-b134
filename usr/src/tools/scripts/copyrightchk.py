#! /usr/bin/python2.4
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
# Check for valid SMI copyright notices in source files.
#

import sys, os

sys.path.append(os.path.join(os.path.dirname(__file__), '../lib/python'))
sys.path.append(os.path.join(os.path.dirname(__file__), '..'))

from onbld.Checks.Copyright import copyright

ret = 0
for filename in sys.argv[1:]:
	try:
		fin = open(filename, 'r')
	except IOError, e:
		sys.stderr.write("failed to open '%s': %s\n" %
				 (e.filename, e.strerror))
		continue

	ret |= copyright(fin, output=sys.stdout)
	fin.close()

sys.exit(ret)
