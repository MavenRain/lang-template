#!/usr/bin/env bash
# usage: bash refuse.sh [--domain DIR] [--header FILE] PROGRAM
# Applies the refusal list to PROGRAM.  It reads tokens outside `--` line
# comments and outside double-quoted strings.  A program may not use
# `mu`, `nu`, `axiom`, `def rec`, `contract`, `storage`, `entry`,
# `payable`, `constructor`, `fallback`, `error`, `invariant`,
# `predicate`, `proof`, `guard`, `sload` or `sstore`, and a top-level
# `def` may not use a name of the header, the prelude, a carrier kit or
# the domain (assemble.sh --stop-after domain gives those names).  The
# kernel accepts an axiom witness (host CAPABILITY.md, Contracts), so this
# script is the only stop for an axiom.  Exit 0 when there is no hit.
# Exit 1 with one line for each hit.  Exit 2 on a usage error.
set -euo pipefail

kit=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
pass_args=()
program=

usage() {
  printf 'usage: bash refuse.sh [--domain DIR] [--header FILE] PROGRAM\n' >&2
  exit 2
}

while [ $# -gt 0 ]; do
  case $1 in
    --domain | --header)
      [ $# -ge 2 ] || usage
      pass_args+=("$1" "$2")
      shift 2
      ;;
    -*)
      usage
      ;;
    *)
      [ -z "$program" ] || usage
      program=$1
      shift
      ;;
  esac
done
[ -n "$program" ] || usage
[ -f "$program" ] || {
  printf 'refuse: no file %s\n' "$program" >&2
  exit 2
}

prelude=$(bash "$kit/assemble.sh" ${pass_args[@]+"${pass_args[@]}"} --stop-after domain)
printf "%s\n" "$prelude" | perl -e '
  use strict;
  use warnings;
  # Keep declaration tokens across lines and use the host identifier alphabet.
  # Comments precede strings so a quote in a comment cannot hide later code.
  my $ident = qr/[A-Za-z_][A-Za-z0-9_\x27]*/;
  sub tokens {
    my ($source) = @_;
    my (@tokens, $line);
    $line = 1;
    while ($source =~ /\G(--[^\n]*|"(?:[^"\\]|\\.)*"|\s+|$ident|.)/gcs) {
      my $token = $1;
      push @tokens, [$token, $line] unless $token =~ /\A(?:--|"|\s)/;
      $line += ($token =~ tr/\n/\n/);
    }
    return @tokens;
  }
  local $/;
  my @prefix = tokens(<STDIN> // "");
  my %prot;
  for (my $i = 0; $i < @prefix; $i++) {
    next unless $prefix[$i][0] =~ /\A(?:def|mu|axiom|\|)\z/;
    my $j = $i + 1;
    $j++ if $j < @prefix && $prefix[$j][0] eq "rec";
    $prot{$prefix[$j][0]} = 1 if $j < @prefix && $prefix[$j][0] =~ /\A$ident\z/;
  }
  my %bad = map { ($_ => 1) } qw(mu nu axiom contract storage entry payable
    constructor fallback error invariant predicate proof guard sload sstore);
  my $file = $ARGV[0];
  open(my $fh, "<", $file) or die "refuse: cannot read $file\n";
  my @tokens = tokens(<$fh> // "");
  my $hits = 0;
  for (my $i = 0; $i < @tokens; $i++) {
    my ($tok, $n) = @{$tokens[$i]};
    if ($tok eq "def" && $i + 1 < @tokens) {
      my $j = $i + 1;
      if ($tokens[$j][0] eq "rec") {
        print "$file:$n: refused form def rec\n";
        $hits++;
        $j++;
      }
      if ($j < @tokens && $prot{$tokens[$j][0]}) {
        print "$file:$n: the def $tokens[$j][0] redefines a header, prelude, carrier or domain name\n";
        $hits++;
      }
    }
    if ($bad{$tok}) {
      print "$file:$n: refused form $tok\n";
      $hits++;
    }
  }
  exit($hits ? 1 : 0);
' "$program"
