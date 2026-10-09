#!/bin/sh
# Table tests of langc, run by make test after make (SPEC section 10,
# chunk 4a): the table of each example program gives its fate at every
# tally, each mutant keeps its code under table, and TABLE_LIMIT bounds the
# table. Each table case test/table-*.lang gives its result on its expect
# lines (M7). Files go to build/test. Each run stays far under 4 GB: the
# arena of one run takes at most LANG_ARENA_MAX (src/syntax.h).
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
langc=$root/build/langc
programs=$root/examples/programs
mutants=$root/examples/mutants
out=$root/build/test
mkdir -p "$out"
failures=0

pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; failures=$((failures + 1)); }

open='mkPolicy allow nonZero blockTime 0 1 (inj 1 of 2 (tuple ()))'

# table_is NAME PROG: exit 0 and stdout equals $out/want.txt.
table_is() {
  "$langc" table "$2" > "$out/table.out" 2> "$out/table.err"
  status=$?
  if [ "$status" -eq 0 ] && cmp -s "$out/table.out" "$out/want.txt"; then
    pass "$1"
  else
    fail "$1: exit $status, stderr: $(cat "$out/table.err")"
    diff "$out/want.txt" "$out/table.out"
  fi
}

# arrow-impossibility: none at every tally.
cat > "$out/want.txt" <<EOF
members 2
candidates 2
policy 0 $open
policy 1 mkPolicy deny nonZero blockTime 0 1 (inj 1 of 2 (tuple ()))
tally 2 0 : none
tally 1 1 : none
tally 0 2 : none
EOF
table_is "arrow-impossibility is none at every tally" "$programs/arrow-impossibility.lang"

# arrow-debreu: one p at every tally; openLog while it has more ballots.
cat > "$out/want.txt" <<EOF
members 3
candidates 2
policy 0 $open
policy 1 mkPolicy deny nonZero blockTime 0 1 (inj 1 of 2 (tuple ()))
tally 3 0 : one 0
tally 2 1 : one 0
tally 1 2 : one 1
tally 0 3 : one 1
EOF
table_is "arrow-debreu is one p at every tally" "$programs/arrow-debreu.lang"

# schelling-ising: two p q at every tally, the policy with more ballots
# first; both sides are frozen.
cat > "$out/want.txt" <<EOF
members 2
candidates 2
policy 0 $open
policy 1 mkPolicy allow nonZero blockTime 0 2 (inj 1 of 2 (tuple ()))
tally 2 0 : two 0 1
tally 1 1 : two 0 1
tally 0 2 : two 1 0
EOF
table_is "schelling-ising is two p q at every tally" "$programs/schelling-ising.lang"

# arrow-debreu-amend (SPEC section 10, O3): the amendTo mask of each policy,
# digit k for constitution k, then the tally rows of each constitution.
cat > "$out/want.txt" <<EOF
members 3
candidates 2
constitutions 2
policy 0 $open
policy 1 mkPolicy deny nonZero blockTime 0 1 (inj 1 of 2 (tuple ()))
amendTo 0 11
amendTo 1 10
constitution 0
tally 3 0 : one 0
tally 2 1 : one 0
tally 1 2 : one 1
tally 0 3 : one 1
constitution 1
tally 3 0 : one 0
tally 2 1 : one 1
tally 1 2 : one 1
tally 0 3 : one 1
EOF
table_is "arrow-debreu-amend gives the rows of each constitution" "$programs/arrow-debreu-amend.lang"

# Each mutant gives the same code under table as under check.
for f in "$mutants"/*.lang; do
  "$langc" check "$f" > /dev/null 2> "$out/check.err"
  "$langc" table "$f" > /dev/null 2> "$out/table.err"
  status=$?
  want=$(cut -d: -f2 "$out/check.err")
  got=$(cut -d: -f2 "$out/table.err")
  same=0
  if [ -n "$want" ] && [ "$want" = "$got" ]; then same=1; fi
  if [ "$status" -eq 1 ] && [ "$same" -eq 1 ]; then
    pass "mutant $(basename "$f") keeps$got"
  else
    fail "mutant $(basename "$f"): exit $status, check$want, table$got"
  fi
done

# TABLE_LIMIT: 2 candidates and N members give N + 1 tallies, at most 4096.
members_is() {
  awk -v n="$1" '/^def members/ { print "def members : Nat := " n; next } { print }' \
    "$programs/arrow-debreu.lang" > "$out/members-$1.lang"
}
members_is 4095
"$langc" table "$out/members-4095.lang" > "$out/table.out" 2> "$out/table.err"
status=$?
rows=$(awk '/^tally/' "$out/table.out" | wc -l | tr -d ' ')
if [ "$status" -eq 0 ] && [ "$rows" -eq 4096 ]; then
  pass "4095 members give 4096 tallies"
else
  fail "4095 members: exit $status, $rows tallies, stderr: $(cat "$out/table.err")"
fi
members_is 4096
"$langc" table "$out/members-4096.lang" > /dev/null 2> "$out/table.err"
status=$?
err=$(cat "$out/table.err")
case $err in
  "langc: TABLE_LIMIT: candidates: "*"4096 members and 2 candidates give more than 4096 tallies") shape=1 ;;
  *) shape=0 ;;
esac
if [ "$status" -eq 1 ] && [ "$shape" -eq 1 ]; then
  pass "4096 members are TABLE_LIMIT"
else
  fail "4096 members: exit $status, stderr: $err"
fi

# With C constitutions and R tallies, the table has C R rows, at most 4096.
# 2048 members give R = 2049 and C R = 4098.
awk '/^def members/ { print "def members : Nat := 2048"; next } { print }' \
  "$programs/arrow-debreu-amend.lang" > "$out/amend-members-2048.lang"
"$langc" table "$out/amend-members-2048.lang" > /dev/null 2> "$out/table.err"
status=$?
err=$(cat "$out/table.err")
case $err in
  "langc: TABLE_LIMIT: candidates: "*"2048 members, 2 candidates and 2 constitutions give more than 4096 rows") shape=1 ;;
  *) shape=0 ;;
esac
if [ "$status" -eq 1 ] && [ "$shape" -eq 1 ]; then
  pass "2048 members and 2 constitutions are TABLE_LIMIT"
else
  fail "2048 members and 2 constitutions: exit $status, stderr: $err"
fi

# The table cases (M7): each test/table-*.lang program gives its expected
# result on its "-- expect" lines. "exit N": check and table exit N. "rows
# N": table prints N tally rows. "same FILE": table prints the same bytes as
# the table of test/FILE. "code CODE": the first stderr line of check and
# of table starts with "langc: CODE: ". This loop is the same for each
# case.
expect_line() { awk -v key="$1" '$1 == "--" && $2 == "expect" && $3 == key { print $4; exit }' "$2"; }
has_code() { awk -v c="langc: $1: " 'NR == 1 { ok = index($0, c) == 1 } END { exit !ok }' "$2"; }
for f in "$root"/test/table-*.lang; do
  case_name=$(basename "$f")
  want_exit=$(expect_line exit "$f")
  want_rows=$(expect_line rows "$f")
  want_code=$(expect_line code "$f")
  same=$(expect_line same "$f")
  "$langc" check "$f" > /dev/null 2> "$out/case-check.err"
  check_status=$?
  "$langc" table "$f" > "$out/case.out" 2> "$out/case.err"
  status=$?
  rows=$(awk '/^tally/' "$out/case.out" | wc -l | tr -d ' ')
  ok=1
  [ "$check_status" = "$want_exit" ] || ok=0
  [ "$status" = "$want_exit" ] || ok=0
  [ -z "$want_rows" ] || [ "$rows" = "$want_rows" ] || ok=0
  [ -z "$want_code" ] || { has_code "$want_code" "$out/case-check.err" && has_code "$want_code" "$out/case.err"; } || ok=0
  if [ -n "$same" ]; then
    "$langc" table "$root/test/$same" > "$out/case-same.out" 2>&1
    cmp -s "$out/case.out" "$out/case-same.out" || ok=0
  fi
  if [ "$ok" -eq 1 ]; then
    pass "$case_name gives the result of its expect lines"
  else
    fail "$case_name: check exit $check_status, table exit $status, $rows tally rows, stderr: $(cat "$out/case.err")"
  fi
done

if [ "$failures" -ne 0 ]; then
  printf '%s table test(s) failed\n' "$failures"
  exit 1
fi
printf 'table.sh: all passed\n'
