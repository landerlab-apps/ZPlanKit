#!/bin/sh
# emc20h_check.sh - EMC-20H against Cochran Analyst 4 Pro planner output.
#
# Each Reference/emc20h/*.dat is an Analyst Planning Simulator plan rebuilt
# as a profile; its first line holds Analyst's total stop time and first stop.
# Fails when a total differs by more than TOL minutes (default 0.6).
#
#   usage: Reference/emc20h_check.sh [tol]

TOL=${1:-0.6}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
CC=${CC:-cc}
$CC -O2 -ISources/CZPlan/include -o "$TMP/h" Reference/emc20h_harness.c \
    Sources/CZPlan/czplan.c Sources/CZPlan/zp_altitude.c -lm || exit 1

n=0; bad=0; sum=0
for f in Reference/emc20h/*.dat; do
    want=$(sed -n '1s/^# Analyst \([0-9.]*\) min.*/\1/p' "$f")
    got=$("$TMP/h" "$f" | cut -d' ' -f2)
    d=$(awk -v a="$got" -v b="$want" 'BEGIN{d=a-b; if(d<0)d=-d; printf "%.2f", d}')
    n=$((n+1))
    sum=$(awk -v s="$sum" -v d="$d" 'BEGIN{print s+d}')
    if awk -v d="$d" -v t="$TOL" 'BEGIN{exit !(d>t)}'; then
        echo "FAIL $(basename "$f"): Analyst $want, engine $got"; bad=$((bad+1))
    fi
done
awk -v s="$sum" -v n="$n" 'BEGIN{printf "EMC-20H  %d Analyst plans, mean |total difference| %.2f min\n", n, s/n}'
[ $bad -eq 0 ] && echo "PASS" || { echo "FAIL ($bad over $TOL min)"; exit 1; }
