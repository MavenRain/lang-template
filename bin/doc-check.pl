#!/usr/bin/perl
# Check each Markdown file below a directory (default: the current one).
# Report an em-dash or an en-dash, and a placeholder other than {{LANG}} or
# {{HOST}}. Exit 1 when the check finds a problem.
use strict;
use warnings;
use File::Find;

my $root = shift // '.';
my %skip = map { $_ => 1 } qw(.git build node_modules scratch);
my @problems;

sub check_file {
    my ($file, $path) = @_;
    open my $fh, '<:encoding(UTF-8)', $file or die "doc-check: $path: $!\n";
    while (my $line = <$fh>) {
        push @problems, "$path:$.: em-dash or en-dash" if $line =~ /[\x{2013}\x{2014}]/;
        (my $rest = $line) =~ s/\{\{(?:LANG|HOST)\}\}//g;
        push @problems, "$path:$.: malformed placeholder"
            if $rest =~ /\{\{|\}\}|\{\s*(?:LANG|HOST)\s*\}/;
    }
    close $fh;
}

find(
    {
        wanted => sub {
            return $File::Find::prune = 1 if -d $_ && $skip{$_};
            check_file($_, $File::Find::name) if -f $_ && /\.md\z/;
        },
    },
    $root
);

my @report = map { s/\A\Q$root\E\/?//r } @problems;
print "$_\n" for @report;
printf "doc-check: %d problem(s)\n", scalar @report;
exit(@report ? 1 : 0);
