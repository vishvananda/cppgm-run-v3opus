#!/usr/bin/perl
# A differential probe: run one synthesized translation unit through our
# cppgm++ and through the reference binary and report whether the relaxed
# comparison the harness runs would call them equal.
use strict;
use warnings;
use FindBin;
use lib "$FindBin::Bin/../../scripts";
use File::Basename;

# `compare_results_common.pl` is a program as well as a library, so only the
# part above its own argument handling is loaded here.
{
	my $path = "$FindBin::Bin/../../scripts/compare_results_common.pl";
	open(my $fh, '<', $path) or die "$path: $!";
	local $/;
	my $text = <$fh>;
	close($fh);
	$text =~ s/\nif \(scalar\(\@ARGV\) != 4\).*\z/\n1;\n/s;
	eval $text;
	die $@ if $@;
}

my $root = "$FindBin::Bin/../..";
# `MYCC` names a cppgm++ built somewhere else - a worktree at the commit before
# the checkpoint, say - so the same probe answers whether a disagreement with
# the reference is one the checkpoint introduced or one that was already there.
# That is the question every audit asks first, and a `git worktree add` plus one
# build is the whole of it.
my $mine = $ENV{'MYCC'} || "$root/dev/cppgm++";
my $ref = "$root/pa17/cppgm++-ref";
my $show = 0;
my @files;
foreach my $arg (@ARGV)
{
	if ($arg eq '-v') { $show = 1; next; }
	push @files, $arg;
}

foreach my $file (@files)
{
	my $base = $file;
	$base =~ s/\.t$//;
	my $my_out = "$base.my.lowir";
	my $ref_out = "$base.ref.lowir";
	my $my_status = system("$mine --emit-lowir -O0 -o $my_out $file > $base.my.err 2>&1") == 0
		? 'EXIT_SUCCESS' : 'EXIT_FAILURE';
	my $ref_status = system("$ref --emit-lowir -O0 -o $ref_out $file > $base.ref.err 2>&1") == 0
		? 'EXIT_SUCCESS' : 'EXIT_FAILURE';
	if ($my_status ne $ref_status)
	{
		print "STATUS-DIFF $file: mine=$my_status ref=$ref_status\n";
		if ($show)
		{
			print "  mine: ", `head -c 400 $base.my.err`, "\n";
			print "  ref:  ", `head -c 400 $base.ref.err`, "\n";
		}
		next;
	}
	if ($my_status ne 'EXIT_SUCCESS')
	{
		print "BOTH-REFUSE $file\n";
		next;
	}
	my $my_data = do { local $/; open(my $fh, '<', $my_out) or die $!; <$fh> };
	my $ref_data = do { local $/; open(my $fh, '<', $ref_out) or die $!; <$fh> };
	my ($ref_c, $my_c) = canonicalize_lowir_pair_for_compare($ref_data, $my_data);
	if ($ref_c eq $my_c)
	{
		print "SAME $file\n";
		next;
	}
	print "DIFF $file\n";
	open(my $a, '>', "$base.ref.canon"); print $a $ref_c; close($a);
	open(my $b, '>', "$base.my.canon"); print $b $my_c; close($b);
	if ($show)
	{
		print `diff -u $base.ref.canon $base.my.canon`;
	}
}
