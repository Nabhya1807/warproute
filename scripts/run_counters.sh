#!/usr/bin/env bash
# Retry ./build/counters when the PMU self-test fails.
#
# All progress messages go to stdout, not stderr, so that a caller piping this
# script into tee captures the retry attempts alongside the successful run.
# The failure rate is data; it belongs in the saved log.
set -u
MAX_ATTEMPTS=10
for attempt in $(seq 1 "$MAX_ATTEMPTS"); do
  out=$(./build/counters 2>&1); status=$?
  if [ $status -eq 0 ]; then printf '%s\n' "$out"; exit 0; fi
  echo "attempt $attempt failed (exit $status), retrying"
  sleep 3
done
echo "ABORT: ${MAX_ATTEMPTS} consecutive self-test failures."
printf '%s\n' "$out"
exit 1
