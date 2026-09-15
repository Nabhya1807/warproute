#!/usr/bin/env bash
# Buffer-size sweep for ./build/counters. Edits only BUF_BYTES in
# src/counters_main.cpp, rebuilds, runs, and restores BUF_BYTES = 32 * 1024
# on exit (normal, build failure, or interrupt).
#
# Intended to be run under sudo from the repo root; no sudo inside.

set -u

cd "$(dirname "$0")/.." || { echo "ABORT: cannot cd to repo root" >&2; exit 1; }

SRC=src/counters_main.cpp
PATTERN='^static constexpr size_t BUF_BYTES = [^;]+;$'
DEFAULT='32 * 1024'

SIZES=(
  '32 * 1024'
  '256 * 1024'
  '512 * 1024'
  '1024 * 1024'
  '2 * 1024 * 1024'
  '64 * 1024 * 1024'
)

# Rewrite the BUF_BYTES line. Writes back through the existing file (cat >)
# rather than sed -i, so the source file keeps its owner when run under sudo.
set_buf_bytes() {
  local value="$1" tmp
  if [ "$(grep -cE "$PATTERN" "$SRC")" != "1" ]; then
    echo "ABORT: BUF_BYTES line in $SRC not found exactly once" >&2
    return 1
  fi
  tmp="$(mktemp)" || return 1
  sed -E "s/^(static constexpr size_t BUF_BYTES = )[^;]+;\$/\1${value};/" "$SRC" > "$tmp" \
    && cat "$tmp" > "$SRC"
  local rc=$?
  rm -f "$tmp"
  [ $rc -eq 0 ] || return 1
  if ! grep -qxF "static constexpr size_t BUF_BYTES = ${value};" "$SRC"; then
    echo "ABORT: BUF_BYTES edit to '${value}' did not take effect" >&2
    return 1
  fi
}

restored=0
restore() {
  [ $restored -eq 1 ] && return
  restored=1
  echo "=== restoring BUF_BYTES = ${DEFAULT} ==="
  if ! set_buf_bytes "$DEFAULT"; then
    echo "WARNING: failed to restore BUF_BYTES in $SRC; fix manually" >&2
    return
  fi
  if ! cmake --build build; then
    echo "WARNING: rebuild after restore failed" >&2
  fi
}
trap restore EXIT
trap 'exit 130' INT TERM

for size in "${SIZES[@]}"; do
  set_buf_bytes "$size" || exit 1
  if ! cmake --build build; then
    echo "ABORT: build failed for BUF_BYTES = ${size}" >&2
    exit 1
  fi
  echo "=== BUF_BYTES = ${size} ==="
  ./build/counters
  rc=$?
  [ $rc -eq 0 ] || echo "NOTE: ./build/counters exited with status $rc for BUF_BYTES = ${size}"
  echo
done
