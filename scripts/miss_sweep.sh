#!/usr/bin/env bash
# Buffer-size sweep, run through ./scripts/run_counters.sh (which retries
# ./build/counters on PMU self-test failure). Edits only BUF_BYTES in
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

BIN=build/counters

# Force a rebuild of $SRC and build. GNU make 3.81 (macOS default, used by the
# Unix Makefiles generator) compares mtimes in whole seconds, and "newer" means
# strictly newer. If the BUF_BYTES edit lands in the same second the previous
# object was built, make considers the target up to date and the old binary
# survives. Sleeping 1s before the touch puts the source mtime in a later
# second than any existing object, so the touch alone is not enough without it.
rebuild() {
  sleep 1
  touch "$SRC" || return 1
  cmake --build build
}

# Refuse a binary that is not strictly newer than the edited source. Compares
# nanosecond mtimes (stat -f %Fm) rather than bash's -nt, which in /bin/bash
# 3.2 (what sudo's secure_path resolves) only has 1-second resolution.
binary_is_fresh() {
  local bin_m src_m
  [ -x "$BIN" ] || { echo "$BIN missing or not executable" >&2; return 1; }
  bin_m="$(stat -f %Fm "$BIN")" || return 1
  src_m="$(stat -f %Fm "$SRC")" || return 1
  if ! awk -v b="$bin_m" -v s="$src_m" 'BEGIN { exit !(b > s) }'; then
    echo "$BIN mtime $bin_m is not newer than $SRC mtime $src_m" >&2
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
  if ! rebuild; then
    echo "WARNING: rebuild after restore failed" >&2
  elif ! binary_is_fresh; then
    echo "WARNING: $BIN is stale after restore; it does not reflect BUF_BYTES = ${DEFAULT}. Rebuild manually." >&2
  fi
}
trap restore EXIT
trap 'exit 130' INT TERM

stale_sizes=()
for size in "${SIZES[@]}"; do
  set_buf_bytes "$size" || exit 1
  if ! rebuild; then
    echo "ABORT: build failed for BUF_BYTES = ${size}" >&2
    exit 1
  fi
  echo "=== BUF_BYTES = ${size} ==="
  if ! binary_is_fresh; then
    echo "FAIL: stale binary for BUF_BYTES = ${size}: build did not relink $BIN after the source edit. No measurement taken for this size."
    stale_sizes+=("$size")
    echo
    continue
  fi
  ./scripts/run_counters.sh
  rc=$?
  [ $rc -eq 0 ] || echo "NOTE: ./scripts/run_counters.sh exited with status $rc for BUF_BYTES = ${size}"
  echo
done

if [ ${#stale_sizes[@]} -gt 0 ]; then
  echo "FAIL: sizes skipped due to stale binary:"
  printf '  %s\n' "${stale_sizes[@]}"
  exit 1
fi
