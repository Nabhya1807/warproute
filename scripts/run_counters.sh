#!/usr/bin/env bash
# Retry ./build/counters when the PMU self-test fails.
# Optional first argument: a different counter binary (default ./build/counters).
#
# All progress messages go to stdout, not stderr, so that a caller piping this
# script into tee captures the retry attempts alongside the successful run.
# The failure rate is data; it belongs in the saved log.
set -u
MAX_ATTEMPTS=10
BIN="${1:-./build/counters}"
# Exit 0 and exit 5 both stop the loop. Exit 5 means the measurement itself
# succeeded but the binary could not create its CSV (already exists, or the
# directory is missing); rerunning cannot fix that and only burns attempts.
# The real status is passed through so the caller can tell 5 apart from 0.
for attempt in $(seq 1 "$MAX_ATTEMPTS"); do
  out=$("$BIN" 2>&1); status=$?
  if [ $status -eq 0 ] || [ $status -eq 5 ]; then printf '%s\n' "$out"; exit $status; fi
  echo "attempt $attempt failed (exit $status), retrying"
  sleep 3
done
echo "ABORT: ${MAX_ATTEMPTS} consecutive self-test failures."
printf '%s\n' "$out"
exit 1
