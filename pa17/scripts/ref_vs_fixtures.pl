#!/usr/bin/perl
# Run the reference binary over every checked-in PA17 test and report where its
# output disagrees with the checked-in `.ref` under the harness's own relaxed
# comparison.  What this answers is how far the binary may be trusted as a
# second oracle: a test it reproduces is one a probe of it settles.
use strict;
use warnings;
use FindBin;
use lib "$FindBin::Bin/../../scripts";

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
my $ref = "$root/pa17/cppgm++-ref";
my @files = @ARGV ? @ARGV : glob("$root/pa17/tests/*/*.t");
my ($same, $diff, $status) = (0, 0, 0);
foreach my $file (@files)
{
	my $base = $file;
	$base =~ s/\.t$//;
	my $out = "/tmp/refprobe.lowir";
	my $code = system("$ref --emit-lowir -O0 -o $out $file > /tmp/refprobe.err 2>&1") == 0
		? 'EXIT_SUCCESS' : 'EXIT_FAILURE';
	my $want = 'EXIT_SUCCESS';
	if (-e "$base.ref.exit_status")
	{
		$want = do { local $/; open(my $fh, '<', "$base.ref.exit_status") or die $!; <$fh> };
		$want =~ s/\s+$//;
	}
	if ($code ne $want)
	{
		print "STATUS-DIFF $file: ref=$code want=$want\n";
		++$status;
		next;
	}
	next if $code ne 'EXIT_SUCCESS';
	my $ref_data = do { local $/; open(my $fh, '<', "$base.ref") or die $!; <$fh> };
	my $got = do { local $/; open(my $fh, '<', $out) or die $!; <$fh> };
	my ($a, $b) = canonicalize_lowir_pair_for_compare($ref_data, $got);
	if ($a eq $b) { ++$same; next; }
	print "DIFF $file\n";
	++$diff;
}
print "reference binary vs fixtures: same=$same diff=$diff status-diff=$status\n";
