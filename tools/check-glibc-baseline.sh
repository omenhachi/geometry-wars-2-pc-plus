#!/usr/bin/env bash
# Fails when an ELF file under the given paths needs a glibc newer than MAX_GLIBC, or when a glibc runtime component
# is among them. Files are recognised by their ELF magic, whatever their name or mode.
#
#   tools/check-glibc-baseline.sh [--max VERSION] PATH...    check files and folders (default max: 2.31)
#   tools/check-glibc-baseline.sh --self-test                check the version comparison
set -uo pipefail

MAX_GLIBC=2.31

# Components of glibc itself, which an AppImage must take from the machine it runs on.
GLIBC_RUNTIME='^(libc\.so\.6|libm\.so\.6|libmvec\.so\.1|libdl\.so\.2|libpthread\.so\.0|librt\.so\.1|libutil\.so\.1|libresolv\.so\.2|libanl\.so\.1|libnsl\.so\.1|libthread_db\.so\.1|libBrokenLocale\.so\.1|libnss_[a-z0-9_]+\.so\.2|ld-linux(-[a-z0-9_-]+)?\.so\.[0-9]+|ld-[0-9.]+\.so)$'

# True when dotted version $1 is newer than $2: numeric per component, a missing component counts as 0.
version_gt() {
  local -a a b
  local i x y
  IFS=. read -ra a <<< "$1"
  IFS=. read -ra b <<< "$2"
  for ((i = 0; i < ${#a[@]} || i < ${#b[@]}; i++)); do
    x=$((10#${a[i]:-0}))
    y=$((10#${b[i]:-0}))
    ((x > y)) && return 0
    ((x < y)) && return 1
  done
  return 1
}

self_test() {
  local failed=0 v
  for v in 2.4 2.14 2.30 2.31; do
    version_gt "$v" 2.31 && { echo "FAIL: $v must not exceed 2.31" >&2; failed=1; }
  done
  for v in 2.31.1 2.32 2.35 2.100 3.0; do
    version_gt "$v" 2.31 || { echo "FAIL: $v must exceed 2.31" >&2; failed=1; }
  done
  [ "$failed" = 0 ] && echo "version comparison: ok"
  return "$failed"
}

is_elf() { [ "$(head -c 4 "$1" | od -An -tx1 | tr -d ' \n')" = 7f454c46 ]; }

highest_glibc() {
  readelf -V -W "$1" 2>/dev/null | grep -oE 'Name: GLIBC_[0-9]+(\.[0-9]+)*' | sed 's/^Name: GLIBC_//' | sort -V | tail -1
}

case "${1:-}" in
  --self-test) self_test; exit ;;
  --max) [ $# -ge 2 ] || { echo "usage: $0 [--max VERSION] PATH..." >&2; exit 2; }; MAX_GLIBC="$2"; shift 2 ;;
esac
[ $# -ge 1 ] || { echo "usage: $0 [--max VERSION] PATH..." >&2; exit 2; }
[[ "$MAX_GLIBC" =~ ^[0-9]+(\.[0-9]+)+$ ]] || { echo "ERROR: invalid glibc version '$MAX_GLIBC'" >&2; exit 2; }
self_test >/dev/null || { self_test >&2; exit 1; }

problems=0
checked=0
while IFS= read -r -d '' file; do
  [ -L "$file" ] && continue
  base="$(basename "$file")"
  if [[ "$base" =~ $GLIBC_RUNTIME ]]; then
    echo "  glibc runtime is bundled: $file" >&2
    problems=$((problems + 1))
  fi
  is_elf "$file" || continue
  soname="$(readelf -d -W "$file" 2>/dev/null | sed -nE 's/.*\(SONAME\).*\[(.*)\].*/\1/p')"
  if [[ -n "$soname" && "$soname" =~ $GLIBC_RUNTIME ]]; then
    echo "  glibc runtime is bundled (SONAME $soname): $file" >&2
    problems=$((problems + 1))
  fi
  checked=$((checked + 1))
  need="$(highest_glibc "$file")"
  printf '  %-14s %s\n' "${need:+GLIBC_}${need:-no glibc}" "$file"
  if [ -n "$need" ] && version_gt "$need" "$MAX_GLIBC"; then
    echo "  $file needs GLIBC_$need, above the glibc $MAX_GLIBC baseline" >&2
    problems=$((problems + 1))
  fi
done < <(find "$@" -type f -print0 | LC_ALL=C sort -z)

[ "$checked" -gt 0 ] || { echo "ERROR: no ELF files found under: $*" >&2; exit 1; }
echo "  $checked ELF files checked, baseline glibc $MAX_GLIBC"
[ "$problems" = 0 ]
