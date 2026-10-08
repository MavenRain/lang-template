#!/usr/bin/env bash
# Make a new language from this template.
#
# Usage: bin/new-lang.sh NAME HOST [DEST]
#
# NAME is the language name (^[a-z][a-z0-9-]*$). HOST is mech, assay,
# tcc-json, tcc-evm-contract, tcc-wasm or tcc-evm. DEST
# is the new directory; the default is ../NAME beside the template root. The
# script copies the template files and the host kit hosts/HOST into DEST,
# replaces {{LANG}} with NAME and {{HOST}} with HOST, and runs git init. It
# never commits.
#
# Paths in DEST:
#   SPEC.template.md             -> SPEC.md
#   formers/FORMERS.md           -> formers/FORMERS.md
#   hosts/HOST/FORMERS.md        -> formers/HOST.md
#   design/DESIGN.md             -> design/DESIGN.md
#   probe/CAPABILITY.template.md -> probe/CAPABILITY.md
#   probe/guard.py               -> probe/guard.py
#   docs/STATUS.template.md      -> docs/STATUS.md
#   docs/VALIDATION.template.md  -> docs/VALIDATION.md
#   hosts/HOST/README.md         -> docs/host/README.md
#   hosts/HOST/docs/PATH         -> docs/host/PATH
#   hosts/HOST/.gitignore        -> appended to .gitignore
#   hosts/HOST/PATH (all others) -> PATH
#   LICENSE-MIT, LICENSE-APACHE, .gitignore are copied; README.md is new.
# The script refuses overlapping template and host paths in DEST.
set -euo pipefail

usage() {
  printf 'usage: %s NAME HOST [DEST]\n' "${0##*/}" >&2
  printf '  NAME  language name, matching ^[a-z][a-z0-9-]*$\n' >&2
  printf '  HOST  mech, assay, tcc-json, tcc-evm-contract, tcc-wasm or tcc-evm\n' >&2
  printf '  DEST  new directory (default: ../NAME beside the template root)\n' >&2
}

refuse() {
  printf 'new-lang: %s\n' "$1" >&2
  exit 1
}

[[ $# -ge 2 && $# -le 3 ]] || { usage; exit 2; }

name=$1
host=$2
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
dest=${3:-$root/../$name}

[[ $name =~ ^[a-z][a-z0-9-]*$ ]] || refuse "bad NAME '$name': use ^[a-z][a-z0-9-]*\$"
case $host in
  mech | assay | tcc-json | tcc-evm-contract | tcc-wasm | tcc-evm) ;;
  *) refuse "unknown HOST '$host': use mech, assay, tcc-json, tcc-evm-contract, tcc-wasm or tcc-evm" ;;
esac
[[ $host != tcc-json || $name != instances ]] || refuse "NAME 'instances' is reserved by the tcc-json document format"
kit=$root/hosts/$host
[[ -d $kit ]] || refuse "the host kit hosts/$host is missing"
[[ ! -e $dest && ! -L $dest ]] || refuse "DEST '$dest' exists"

# The template files, as SOURCE:TARGET pairs.
template_files=(
  "SPEC.template.md:SPEC.md"
  "formers/FORMERS.md:formers/FORMERS.md"
  "design/DESIGN.md:design/DESIGN.md"
  "probe/CAPABILITY.template.md:probe/CAPABILITY.md"
  "probe/guard.py:probe/guard.py"
  "docs/STATUS.template.md:docs/STATUS.md"
  "docs/VALIDATION.template.md:docs/VALIDATION.md"
  "LICENSE-MIT:LICENSE-MIT"
  "LICENSE-APACHE:LICENSE-APACHE"
  ".gitignore:.gitignore"
)

# The path in DEST of a host kit file.
host_target() {
  case $1 in
    FORMERS.md) printf 'formers/%s.md' "$host" ;;
    README.md) printf 'docs/host/README.md' ;;
    docs/*) printf 'docs/host/%s' "${1#docs/}" ;;
    *) printf '%s' "$1" ;;
  esac
}

list=$(mktemp "${TMPDIR:-/tmp}/new-lang.XXXXXX")
made=0
cleanup() {
  local status=$?
  rm -f "$list"
  if [[ $status -ne 0 && $made -eq 1 ]]; then
    printf 'new-lang: failed; removing %s\n' "$dest" >&2
    rm -rf -- "$dest"
  fi
}
trap cleanup EXIT

# List the host kit files. In a git work tree, leave out ignored files.
if git -C "$kit" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git -C "$kit" ls-files -z --cached --others --exclude-standard >"$list"
else
  (cd "$kit" && find . -type f -print0) >"$list"
fi

collisions=""
targets=(README.md)
for pair in "${template_files[@]}"; do
  targets+=("${pair#*:}")
done
while IFS= read -r -d '' rel; do
  rel=${rel#./}
  [[ $rel == .gitignore ]] && continue
  target=$(host_target "$rel")
  for existing in "${targets[@]}"; do
    if [[ $target == "$existing" || $target == "$existing/"* || $existing == "$target/"* ]]; then
      collisions+="  hosts/$host/$rel -> $target (conflicts with $existing)"$'\n'
    fi
  done
  targets+=("$target")
done <"$list"
[[ -z $collisions ]] || refuse "host paths collide in DEST:"$'\n'"$collisions"

for pair in "${template_files[@]}"; do
  [[ -f $root/${pair%%:*} ]] || refuse "template file ${pair%%:*} is missing"
done

mkdir -p -- "$(dirname -- "$dest")"
# Claim only a directory created by this invocation, including for cleanup.
mkdir -- "$dest" || refuse "DEST '$dest' could not be created"
made=1
dest=$(cd -- "$dest" && pwd)

for pair in "${template_files[@]}"; do
  target=${pair#*:}
  mkdir -p "$(dirname "$dest/$target")"
  cp -p "$root/${pair%%:*}" "$dest/$target"
done

# Reserve the generated README before checking paths on this filesystem.
: >"$dest/README.md"

while IFS= read -r -d '' rel; do
  rel=${rel#./}
  [[ -f $kit/$rel ]] || continue
  if [[ $rel == .gitignore ]]; then
    cat "$kit/.gitignore" >>"$dest/.gitignore"
    continue
  fi
  target=$(host_target "$rel")
  mkdir -p "$(dirname "$dest/$target")"
  [[ ! -e $dest/$target && ! -L $dest/$target ]] || refuse "host path collides in DEST: $target"
  cp -p "$kit/$rel" "$dest/$target"
done <"$list"

cat >"$dest/README.md" <<'EOF'
# {{LANG}}

{{LANG}} is a domain-specific language. It comes from lang-template with
the {{HOST}} host.

- `SPEC.md`: the specification. Fill each section.
- `design/DESIGN.md`: the domain design. It is the only source of the core
  types and the core operations.
- `formers/FORMERS.md`: the type formers. `formers/{{HOST}}.md` gives their
  status on the {{HOST}} host.
- `probe/CAPABILITY.md`: the host probe.
- `docs/STATUS.md` and `docs/VALIDATION.md`: the status and the gate results.
- `docs/host/README.md`: how to build and test the host kit, and how to
  replace the sample domain in `domain/`.
EOF

# Replace the placeholders in each text file.
find "$dest" -type f -print0 | NAME="$name" HOST="$host" xargs -0 perl -e '
  for my $file (@ARGV) {
    next unless -T $file;
    open my $in, "<", $file or die "new-lang: $file: $!\n";
    my $text = do { local $/; <$in> };
    close $in;
    next if index($text, "\0") >= 0;
    my $new = $text;
    $new =~ s/\{\{LANG\}\}/$ENV{NAME}/g;
    $new =~ s/\{\{HOST\}\}/$ENV{HOST}/g;
    next if $new eq $text;
    open my $out, ">", $file or die "new-lang: $file: $!\n";
    print {$out} $new;
    close $out;
  }
'

git -C "$dest" init -q -b main

case $host in
  mech) gate="make check test" ;;
  assay) gate="bash gate.sh" ;;
  tcc-json | tcc-evm-contract | tcc-wasm | tcc-evm) gate="make check" ;;
esac

cat <<EOF
Made $dest (host $host). Nothing is staged or committed.

Next steps:
  1. Run the host probe and fill probe/CAPABILITY.md.
  2. Paste the domain design into design/DESIGN.md.
  3. Fill SPEC.md. Section 3 cites formers/FORMERS.md and formers/$host.md.
  4. Replace the sample domain in domain/. docs/host/README.md tells how.
  5. Run the gate in $dest: $gate
  6. Review the files, then stage and commit them yourself.
EOF
