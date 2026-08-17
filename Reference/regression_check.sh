#!/bin/sh
# regression_check.sh - ZHL-16C and VVAL-18 must not move.
#
# Builds the engine twice, once from a git reference (default HEAD) and once
# from the working tree, and diffs the schedules both models produce across a
# spread of profiles. Anything that changes is reported and the script fails.
#
# VPM-B (model 2) is reported for information but is NOT a failure condition,
# since that is the model under active development.
#
#   usage: Reference/regression_check.sh [git-ref]
#
# Run this before committing any engine change.

set -e
REF=${1:-HEAD}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/base/include" "$TMP/work"
git show "$REF:Sources/CZPlan/czplan.c"         > "$TMP/base/czplan.c"
git show "$REF:Sources/CZPlan/include/czplan.h" > "$TMP/base/include/czplan.h"

CC=${CC:-cc}
$CC -O2 -I"$TMP/base/include" -o "$TMP/base_h" \
    Reference/regression_harness.c "$TMP/base/czplan.c" -lm
$CC -O2 -ISources/CZPlan/include -o "$TMP/work_h" \
    Reference/regression_harness.c Sources/CZPlan/czplan.c -lm

# depth  time  fo2  fhe  salt
PROFILES="
30 25 0.21 0.00 1
40 20 0.21 0.00 1
40 30 0.32 0.00 0
45 20 0.21 0.00 1
55 25 0.18 0.45 1
70 26 0.18 0.45 1
80 27 0.15 0.45 1
18 60 0.21 0.00 1
"

run() {                      # $1 = binary, $2 = output file
    : > "$2"
    echo "$PROFILES" | while read -r d t o h s; do
        [ -z "$d" ] && continue
        for m in 0 1 2; do "$1" "$m" "$d" "$t" "$o" "$h" "$s" >> "$2"; done
    done
}

run "$TMP/base_h" "$TMP/base.txt"
run "$TMP/work_h" "$TMP/work.txt"

status=0
for m in 0 1; do
    case $m in
        0) name="ZHL-16C " ;;
        1) name="VVAL-18 " ;;
    esac
    grep "^model=$m " "$TMP/base.txt" > "$TMP/b.$m"
    grep "^model=$m " "$TMP/work.txt" > "$TMP/w.$m"
    if cmp -s "$TMP/b.$m" "$TMP/w.$m"; then
        printf '%s unchanged against %s (%s profiles)\n' \
               "$name" "$REF" "$(wc -l < "$TMP/b.$m" | tr -d ' ')"
    else
        printf '%s CHANGED against %s:\n' "$name" "$REF"
        diff "$TMP/b.$m" "$TMP/w.$m" || true
        status=1
    fi
done

grep "^model=2 " "$TMP/base.txt" > "$TMP/b.2" || true
grep "^model=2 " "$TMP/work.txt" > "$TMP/w.2" || true
if cmp -s "$TMP/b.2" "$TMP/w.2"; then
    echo "VPM-B    unchanged (informational)"
else
    echo "VPM-B    changed (informational, not a failure):"
    diff "$TMP/b.2" "$TMP/w.2" || true
fi

[ $status -eq 0 ] && echo "PASS" || echo "FAIL - a settled model moved"
exit $status
