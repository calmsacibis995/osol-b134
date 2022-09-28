#!/usr/bin/perl -w
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
# Check versioning information.
#
# This script descends a directory hierarchy inspecting ELF shared objects for
# version definitions.  The general theme is to verify that common versioning
# rules have been used to build these objects.
#
# As always, a number of components don't follow the rules, or require
# special handling. An exceptions file is used to specify these cases.
#
# By default any file that has conditions that should be reported is first
# listed and then each condition follows.  The -o (one-line) option produces a
# more terse output which is better for sorting/diffing with "nightly".
#
# Besides the default operation of checking the files within a directory
# hierarchy, a detailed analysis of each files versions can be created with the
# -d option.  The database created is useful for auditing the difference between
# different builds, and for thus monitoring that versioning changes are made in
# a compatible manner.


# Define all global variables (required for strict)
use vars  qw($Prog $Intfdir);
use vars  qw(%opt @SaveArgv $ErrFH $ObjCnt);


# An exception file is used to specify regular expressions to match
# objects. These directives specify special attributes of the object.
# The regular expressions are read from the file and compiled into the
# regular expression variables.
#
# The name of each regular expression variable is of the form
#
#	$EXRE_xxx
#
# where xxx is the name of the exception in lower case. For example,
# the regular expression variable for PLUGINS is $EXRE_plugins.
#
# onbld_elfmod::LoadExceptionsToEXRE() depends on this naming convention
# to initialize the regular expression variables, and to detect invalid
# exception names.
#
# If a given exception is not used in the exception file, its regular
# expression variable will be undefined. Users of these variables must
# test the variable with defined() prior to use:
#
#	defined($EXRE_plugins) && ($foo =~ $EXRE_plugins)
#
# ----
#
# The exceptions are:
#
# NONSTD_VERNAME
#	Objects are expected to use standard names for versions.
#	This directive is used to relax that requirement.
#
# NOVERDEF
#	Objects that are not required to have a versioned name. Note that
#	PLUGINS objects are implicitly NOVERDEF, so this directive is
#	for use with non-plugin objects.
#
# PLUGINS
#	Plugin objects are not required to have a versioned name, and are
#	not required to be internally versioned.
#
use vars  qw($EXRE_nonstd_vername $EXRE_noverdef $EXRE_plugin);

use strict;

use POSIX qw(getenv);
use Getopt::Std;
use File::Basename;




## ProcFile(BasePath, RelPath, Class, Type, Verdef, Alias)
#
# Investigate runtime attributes of a sharable object
#
# entry:
#	BasePath - Base path from which relative paths are taken
#	RelPath - Path of object taken relative to BasePath
#	Class - ELFCLASS of object
#	Type - ELF type of object
#	Verdef - VERDEF if object defines versions, NOVERDEF otherwise
#	Alias - Alias lines corresponding to the object, or an empty ('')
#		string if there are no aliases.
#
sub ProcFile {
	my($BasePath, $RelPath, $Class, $Type, $Verdef, $Alias) = @_;

	my($File, $FullPath, %Vers, $VersCnt, %TopVer);
	my($Val, $Ttl, $NotPlugin);

	$FullPath = "$BasePath/$RelPath";
	@_ = split /\//, $RelPath;
	$File = $_[$#_];

	$Ttl = 0;

	# If this object does not follow the runtime versioned name convention,
	# and it does not reside underneath a directory identified as
	# containing plugin objects intended for use with dlopen() only,
	# issue a warning.
	$NotPlugin = !defined($EXRE_plugin) || ($RelPath !~ $EXRE_plugin);
	if (($File !~ /\.so\./) && $NotPlugin) {
		onbld_elfmod::OutMsg($ErrFH, \$Ttl, $RelPath,
		    "does not have a versioned name");
	}

	# If there are no versions in the file we're done.
	if ($Verdef eq 'NOVERDEF') {
	        # Report the lack of versioning, unless the object is
	    	# a known plugin, or is explicitly exempt.
		if ($NotPlugin &&
		    (!defined($EXRE_noverdef) || ($RelPath !~ $EXRE_noverdef))) {
			onbld_elfmod::OutMsg($ErrFH, \$Ttl, $RelPath,
			    "no versions found");
		}
		return;
	}

	# Get a hash of the top versions in the inheritance chains.
	%TopVer = ();
	foreach my $Line (split(/\n/, `pvs -don $FullPath 2>&1`)) {
		$Line =~ s/^.*-\s*(.*);/$1/;
		$TopVer{$Line} = 1;
	}

	# First determine what versions exist that offer interfaces.  pvs -dos
	# will list these.  Note that other versions may exist, ones that
	# don't offer interfaces ... we'll get to those next.
	%Vers = ();
	$VersCnt = 0;
	my %TopSUNWVers = ();
	foreach my $Line (split(/\n/, `pvs -dos $FullPath 2>&1`)) {
		my($Ver) = $Line;
		
		$Ver =~ s/^.*-\t(.*): .*/$1/; 		# isolate version

		# See if we've already caught this version name. We only look
		# at each version once.
		next if ($Vers{$Ver}) ;

		# Note that the non-empty version has been seen
		$Vers{$Ver} = 1;
		$VersCnt++;

		# We expect the public SUNW_major.minor.micro versions to use
		# inheritance, so there should only be one top version for
		# each major number. It is possible, though rare, to have
		# more than one top version if the major numbers differ.
		#
		# %TopSUNWVers uses the major name as the key, with each
		# value yielding an array reference to the top versions for
		# that major number.
		if ($Ver =~ /^(SUNW_[0-9]+)[0-9.]+$/) {
			push @{$TopSUNWVers{$1}}, $Ver if $TopVer{$Ver};
			next;
		}

		# Having already handled SUNW_ public versions above, is it
		# a different version name that we recognise?
		#
		# Along with the standard version names, each object exports
		# a "base" version which contains the linker generated symbols
		# _etext, _edata, etc., and is named using the objects SONAME.
		# This name should typically match the file name.
		next if (($Ver =~ /^SYSVABI_1.[23]$/) ||
		    ($Ver =~ /^SISCD_2.3[ab]*$/) ||
		    ($Ver =~ /^SUNWprivate(_[0-9.]+)?$/) ||
		    ($Ver =~ /$File/));

		# If we get here, it's a non-standard version.
		if (!defined($EXRE_nonstd_vername) ||
		    ($RelPath !~ $EXRE_nonstd_vername)) {
			onbld_elfmod::OutMsg($ErrFH, \$Ttl, $RelPath,
			   "non-standard version name: $Ver");
		}
		next;
	}

	# If this file has been scoped, but not versioned (i.e., a mapfile was
	# used to demote symbols but no version name was applied to the
	# global interfaces) then it's another non-standard case.
	if ($VersCnt eq 0) {
		onbld_elfmod::OutMsg($ErrFH, \$Ttl, $RelPath,
		    "scoped object contains no versions");
		return;
	}

	# If this file has multiple inheritance chains with the public
	# SUNW_ name, that's wrong.
	foreach my $Ver (sort keys %TopSUNWVers) {
		if (scalar(@{$TopSUNWVers{$Ver}}) > 1) {
			onbld_elfmod::OutMsg($ErrFH, \$Ttl, $RelPath,
			    "multiple $Ver inheritance chains (missing " .
			    "inheritance?): " .
			    join(', ', @{$TopSUNWVers{$Ver}}));
		}
	}


	# Produce an interface description for the object.
	# For each version, generate a VERSION declaration of the form:
	#
	#	[TOP_]VERSION  version  direct-count  total-count
	#		symname1
	#		symname2
	#		...
	#
	# There are two types of version that we suppress from this
	# output:
	#
	# 	BASE
	#	The "base" version is used to hold symbols that must be
	#	public, but which are not part of the versioning interface
	#	(_end, _GLOBAL_OFFSET_TABLE_, _PROCEDURE_LINKAGE_TABLE_, etc).
	#
	#	Private
	#	Any version with "private" in its name is skipped. We
	#	expect these to be SUNWprivate, but are extra lenient in
	#	what we accept.
	#
	# If an object only has base or private versions, we do not produce
	# an interface description for that object.
	#
	if ($opt{i}) {
		my $header_done = 0;

		# The use of 'pvs -v' is to identify the BASE version
		foreach my $Line (split(/\n/, `pvs -dv $FullPath 2>&1`)) {
			# Skip base version
			next if ($Line =~ /\[BASE\]/);

			# Skip private versions
			next if ($Line =~ /private/i);

			# Directly inherited versions follow the version name
			# in a comma separated list within {} brackets. Capture
			# that information, for use with our VERSION line.
			my $InheritVers = ($Line =~ /(\{.*\});$/) ? "\t$1" : '';

			$Line =~ s/^\s*([^;: ]*).*/$1/; 

			# Older versions of pvs have a bug that prevents
			# them from printing [BASE] on the base version.
			# Work around this by excluding versions that end
			# with a '.so.*' suffix.
			# SONAME of the object.
			next if $Line =~ /\.so\.\d+$/;

			# We want to output the symbols in sorted order, so
			# we gather them first, and then sort the results.
			# An array would suffice, but we have observed objects
			# with odd inheritance chains in which the same
			# sub-version gets inherited more than once, leading
			# to the same symbol showing up more than once. Using
			# a hash instead of an array thins out the duplicates.
			my %Syms = ();
			my $symitem = $opt{I} ? 'NEW' : 'SYMBOL';
			my $version_cnt = 0;
			foreach my $Sym
			    (split(/\n/, `pvs -ds -N $Line $FullPath 2>&1`)) {
				if ($Sym =~ /:$/) {
					$version_cnt++;
					# If this is an inherited sub-version,
					# we don't need to continue unless
					# generating output in -I mode.
					if ($version_cnt >= 2) {
						last if !$opt{I};
						$symitem = 'INHERIT';
					}
					next;
				}
				$Sym =~ s/[ \t]*(.*);$/$1/;
				$Sym =~ s/ .*$//;	# remove any data size
				$Syms{$Sym} = $symitem;
			}

			if (!$header_done) {
				print INTFILE "\n" if !$opt{h} && ($ObjCnt != 0);
				$ObjCnt++;
				print INTFILE "OBJECT\t$RelPath\n";
				print INTFILE "CLASS\tELFCLASS$Class\n";
				print INTFILE "TYPE\tET_$Type\n";
				print INTFILE $Alias if ($Alias ne '');
				$header_done = 1;
			}

			my $item = $TopVer{$Line} ? 'TOP_VERSION' : 'VERSION';
			print INTFILE "$item\t$Line$InheritVers\n";

			# Output symbols in sorted order
			foreach my $Sym (sort keys %Syms) {
				print INTFILE "\t$Syms{$Sym}\t$Sym\n";
			}
		}
	}
}

## ProcFindElf(file)
#
# Open the specified file, which must be produced by "find_elf -r",
# and process the files it describes.
sub ProcFindElf {
	my $file = $_[0];
	my $line;
	my $LineNum = 0;
	my $prefix;
	my @ObjList = ();
	my %ObjToAlias = ();

	open(FIND_ELF, $file) || die "$Prog: Unable to open $file";

	# This script requires relative paths, created by the 'find_elf -r'
	# option. When this is done, the first non-comment line will always
	# be PREFIX. Obtain that line, or issue a fatal error.
	while ($line = onbld_elfmod::GetLine(\*FIND_ELF, \$LineNum)) {
		if ($line =~ /^PREFIX\s+(.*)$/) {
			$prefix = $1;
			last;
		}

		die "$file: PREFIX expected on line $LineNum\n";
	}


	# Process the remainder of the file.
	while ($line = onbld_elfmod::GetLine(\*FIND_ELF, \$LineNum)) {
		if ($line =~ /^OBJECT\s/i) {
			push @ObjList, $line;
			next;
		}

		if ($line =~ /^ALIAS\s/i) {
			my ($item, $obj, $alias) = split(/\s+/, $line, 3);
			my $str = "ALIAS\t$alias\n";

			if (defined($ObjToAlias{$obj})) {
				$ObjToAlias{$obj} .= $str;
			} else {
				$ObjToAlias{$obj} = $str;
			}
		}
	}

	foreach $line (@ObjList) {
		my ($item, $class, $type, $verdef, $obj) =
		    split(/\s+/, $line, 5);

		my $alias = defined($ObjToAlias{$obj}) ? $ObjToAlias{$obj} : '';

		# We are only interested in sharable objects. We may see
		# other file types if processing a list of objects
		# supplied via the -f option.
		next if ($type ne 'DYN');

		ProcFile($prefix, $obj, $class, $type, $verdef, $alias);
	}

	close FIND_ELF;
}


# -----------------------------------------------------------------------------

# Establish a program name for any error diagnostics.
chomp($Prog = `basename $0`);

# The onbld_elfmod package is maintained in the same directory as this
# script, and is installed in ../lib/perl. Use the local one if present,
# and the installed one otherwise.
my $moddir = dirname($0);
$moddir = "$moddir/../lib/perl" if ! -f "$moddir/onbld_elfmod.pm";
require "$moddir/onbld_elfmod.pm";

# Check that we have arguments.
@SaveArgv = @ARGV;
if ((getopts('E:e:f:hIi:ow:', \%opt) == 0) || (!$opt{f} && ($#ARGV == -1))) {
	print "usage: $Prog [-hIo] [-E errfile] [-e exfile] [-f listfile]\n";
	print "\t\t[-i intffile] [-w outdir] file | dir, ...\n";
	print "\n";
	print "\t[-E errfile]\tdirect error output to file\n";
	print "\t[-e exfile]\texceptions file\n";
	print "\t[-f listfile]\tuse file list produced by find_elf -r\n";
	print "\t[-h]\tdo not produce a CDDL/Copyright header comment\n";
	print "\t[-I]\tExpand inheritance in -i output (debugging)\n";
	print "\t[-i intffile]\tcreate interface description output file\n";
	print "\t[-o]\t\tproduce one-liner output (prefixed with pathname)\n";
	print "\t[-w outdir]\tinterpret all files relative to given directory\n";
	exit 1;
}

# If -w, change working directory to given location
!$opt{w} || chdir($opt{w}) || die "$Prog: can't cd to $opt{w}";


# Error messages go to stdout unless -E is specified. $ErrFH is a
# file handle reference that points at the file handle where error messages
# are sent.
if ($opt{E}) {
	open(ERROR, ">$opt{E}") || die "$Prog: open failed: $opt{E}";
	$ErrFH = \*ERROR;
} else {
	$ErrFH = \*STDOUT;
}

# Locate and process the exceptions file
onbld_elfmod::LoadExceptionsToEXRE('interface_check');

# If creating an interface description output file, prepare it for use
if ($opt{i}) {
	open (INTFILE, ">$opt{i}") ||
	    die "$Prog: Unable to create file: $opt{i}";

	# Generate the output header
	onbld_elfmod::Header(\*INTFILE, $0, \@SaveArgv) if !$opt{h};;
}

# Number of OBJECTs output to INTFILE
$ObjCnt = 0;

# If we were passed a file previously produced by 'find_elf -r', use it.
ProcFindElf($opt{f}) if $opt{f};

# Process each argument
foreach my $Arg (@ARGV) {
	# Run find_elf to find the files given by $Arg and process them
	ProcFindElf("find_elf -frs $Arg|");
}

# Close any working output files.
close INTFILE if $opt{i};
close ERROR if $opt{E};

exit 0;
