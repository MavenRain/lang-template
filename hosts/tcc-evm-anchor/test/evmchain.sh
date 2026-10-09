# Helpers of the chain tests on geth evm (SPEC section 10, chunk 6).
# test/deploy.sh, test/diff.sh and test/laws.sh source this file after they
# set root (the repo) and out (their directory under build/test/chain). This
# file has no cases.
#
# Names: the receiver aa, the members b1, b2 and b3 (member position I has
# the address b(I + 1)), the stranger cc and the deployer ee. A deploy runs
# the creation code and the member words with evm run --create. A step runs
# one call on the receiver, on the storage after the last step. keccak256
# comes from a small contract on evm, not from src/keccak.c.

set -e
LC_ALL=C
export LC_ALL
langc=$root/build/langc
programs=$root/examples/programs
mkdir -p "$out"
failures=0

receiver=00000000000000000000000000000000000000aa
stranger=00000000000000000000000000000000000000cc
deployer=00000000000000000000000000000000000000ee
zero=0000000000000000000000000000000000000000000000000000000000000000
config='{"chainId":1,"homesteadBlock":0,"eip150Block":0,"eip155Block":0,"eip158Block":0,"byzantiumBlock":0,"constantinopleBlock":0,"petersburgBlock":0,"istanbulBlock":0,"berlinBlock":0,"londonBlock":0,"mergeNetsplitBlock":0,"shanghaiTime":0,"cancunTime":0,"terminalTotalDifficulty":0,"terminalTotalDifficultyPassed":true}'

pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; failures=$((failures + 1)); }
fatal() { fail "$1" >&2; exit 1; }

# same NAME GOT WANT: the case NAME passes when GOT is WANT.
same() {
  if [ "$2" = "$3" ]; then pass "$1"; else fail "$1: got $2, want $3"; fi
}

# finish FILE: print the last line and exit 1 when a case failed.
finish() {
  if [ "$failures" -eq 0 ]; then echo "$1: all passed"; exit 0; fi
  echo "$1: $failures failed"
  exit 1
}

# member I: the address of member position I (b1 for 0).
member() { printf '%040x' $((177 + $1)); }

# word HEX: HEX with zeros on the left to 32 bytes.
word() { printf '%64s' "$1" | tr ' ' 0; }

# text STRING: the bytes of STRING in hex.
text() { printf '%s' "$1" | od -An -tx1 | tr -d ' \n'; }

# keccak HEX: keccak256 of the bytes HEX. The contract copies the call data
# to memory, hashes it and returns the hash.
keccak() {
  _hash=$(evm run --code 0x3660006000373660002060005260206000f3 --input "0x$1" 2> "$out/hash.err") \
    || fatal "evm keccak: $(cat "$out/hash.err")"
  case $_hash in
    0x*) _hash=${_hash#0x} ;;
    *) fatal "evm keccak: invalid output $_hash" ;;
  esac
  case $_hash in
    *[!0-9a-f]*|'') fatal "evm keccak: invalid hash $_hash" ;;
  esac
  [ "${#_hash}" -eq 64 ] || fatal "evm keccak: invalid hash length ${#_hash}"
  printf '%s\n' "$_hash"
}

# The call data of the entries.
lang_in() { printf 'eecdf927%s' "$1"; }
verify_in() { printf '382262fc%s%064x' "$1" "$2"; }
cast_in() { printf '738198b4%064x' "$1"; }
amend_in() { printf '13723792%064x' "$1"; }
dispute_in() { printf '4db31205%s%064x%s' "$1" "$2" "$3"; }

# program FILE: creation.hex and runtime.hex of FILE (langc build) and its
# table in table.out. M and K are the member and candidate counts.
program() {
  "$langc" build "$1" -o "$out/creation.hex" 2> "$out/build.err" || fatal "langc build $1: $(cat "$out/build.err")"
  "$langc" build "$1" --runtime -o "$out/runtime.hex" 2> "$out/build.err" || fatal "langc build --runtime $1: $(cat "$out/build.err")"
  "$langc" table "$1" > "$out/table.out" 2> "$out/table.err" || fatal "langc table $1: $(cat "$out/table.err")"
  M=$(awk '$1 == "members" { print $2 }' "$out/table.out")
  K=$(awk '$1 == "candidates" { print $2 }' "$out/table.out")
}

# words N: the member words of member positions 0 to N - 1.
words() {
  _wi=0 _ws=''
  while [ "$_wi" -lt "$1" ]; do
    _ws=$_ws$(word "$(member "$_wi")")
    _wi=$((_wi + 1))
  done
  printf '%s' "$_ws"
}

# members N: keys gets the line member ADDR I KEY for each member position
# I below N. KEY is keccak256 of the word of ADDR (the member slot).
members() {
  : > "$out/keys"
  _i=0
  while [ "$_i" -lt "$1" ]; do
    _m=$(member "$_i")
    _key=$(keccak "$(word "$_m")") || fatal "cannot hash member $_m"
    printf 'member %s %s %s\n' "$_m" "$_i" "$_key" >> "$out/keys"
    _i=$((_i + 1))
  done
}

# fresh N: the storage after a deploy with N members (after members N):
# slot 0 holds N and the slot of member position I holds I + 1.
fresh() {
  {
    printf '%064x %064x\n' 0 "$1"
    awk '$1 == "member" { printf "%s %064x\n", $4, $3 + 1 }' "$out/keys"
  } | sort
}

# storage FILE: the storage of the account with code in the evm dump FILE,
# one line KEY VALUE for each slot, both 64 lowercase hex, sorted.
storage() {
  awk -v z="$zero" '
    $1 == "\"code\":" { f = 1 }
    $1 == "\"address\":" { f = 0 }
    f && length($1) == 69 && substr($1, 1, 3) == "\"0x" {
      v = $2
      gsub(/[",]/, "", v)
      printf "%s %s\n", tolower(substr($1, 4, 64)), tolower(substr(z v, length(v) + 1))
    }' "$1" | sort
}

# create CODEHEX [VALUE]: deploy CODEHEX from ee at TIMESTAMP 4660, with
# VALUE wei. created is revert, error or ok. When ok, state holds the
# storage of the new account, code.hex its code and out.hex the output.
create() {
  printf '{"config":%s,"timestamp":"0x%x","gasLimit":"0x1c9c380","difficulty":"0x0","alloc":{"0x%s":{"balance":"0x10"}}}\n' \
    "$config" 4660 "$deployer" > "$out/prestate.json"
  evm run --prestate "$out/prestate.json" --create --code "0x$1" --sender "0x$deployer" ${2:+--value $2} \
    --dump > "$out/create.out" 2> "$out/create.err" || fatal "evm create: $(cat "$out/create.err")"
  case $(cat "$out/create.out") in
    *'execution reverted'*) created=revert; return ;;
    *' error: '*) created=error; return ;;
  esac
  created=ok
  storage "$out/create.out" > "$out/state"
  awk '$1 == "\"code\":" { v = $2; gsub(/[",]/, "", v); print tolower(substr(v, 3)); exit }' "$out/create.out" > "$out/code.hex"
  awk 'NF { last = $0 } END { print tolower(substr(last, 3)) }' "$out/create.out" > "$out/out.hex"
}

# prestate TIME: the receiver aa with code.hex and the storage in state, at
# TIMESTAMP TIME. The config is the one of test/run.sh.
prestate() {
  _st=$(awk '{ printf "%s\"0x%s\":\"0x%s\"", (NR > 1 ? "," : ""), $1, $2 }' "$out/state")
  printf '{"config":%s,"timestamp":"0x%x","gasLimit":"0x1c9c380","difficulty":"0x0","alloc":{"0x%s":{"balance":"0x0","code":"0x%s","storage":{%s}}}}\n' \
    "$config" "$1" "$receiver" "$(tr -d '\n' < "$out/code.hex")" "$_st" > "$out/prestate.json"
}

# step SENDER INPUT TIME: call the receiver from SENDER with INPUT at
# TIMESTAMP TIME, on the storage in state. got gets the line result revert
# or result OUTPUT, then the line log TOPICS DATA for one log (all of its
# topics in order, one for Amended, two for Anchored and Disputed) or logs N for
# more, then the storage after the call. state gets that storage. The awk of the log is
# the one of test/run.sh.
step() {
  prestate "$3"
  evm run --prestate "$out/prestate.json" --receiver "0x$receiver" --sender "0x$1" \
    --input "0x$2" --debug --dump --nostack > "$out/run.out" 2> "$out/run.err" || fatal "evm call: $(cat "$out/run.err")"
  {
    # evm prints an empty line for an empty output; got shows it as 0x.
    case $(cat "$out/run.out") in
      *'execution reverted'*) echo 'result revert' ;;
      *' error: '*) echo 'result error' ;;
      *) _o=$(head -n 1 "$out/run.out"); echo "result ${_o:-0x}" ;;
    esac
    _n=$(awk '/^LOG[0-9]:/ { n++ } END { print n + 0 }' "$out/run.err")
    if [ "$_n" -eq 1 ]; then
      printf 'log%s %s\n' \
        "$(awk '$1 ~ /^0000000[0-3]$/ && NF == 2 { printf " %s", $2 }' "$out/run.err")" \
        "$(awk 'p && /\|/ { for (i = 2; i <= 17; i++) printf "%s", $i } /^LOG[0-9]:/ { p = 1 }' "$out/run.err")"
    elif [ "$_n" -gt 1 ]; then
      echo "logs $_n"
    fi
    storage "$out/run.out"
  } > "$out/got"
  awk '$1 != "result" && $1 != "log" && $1 != "logs"' "$out/got" > "$out/state"
}
