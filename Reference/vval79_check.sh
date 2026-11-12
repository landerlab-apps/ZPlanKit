#!/bin/sh
# vval79_check.sh - VVAL-79 against the published NEDU VVal-79 air schedules
# (TR 12-01 Appendix E, total stop time). Informational: prints how many of
# the schedules the engine reproduces exactly and within 1 and 2 minutes.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
CC=${CC:-cc}
$CC -O2 -ISources/CZPlan/include -o "$TMP/h" Reference/vval79_harness.c \
    Sources/CZPlan/czplan.c Sources/CZPlan/zp_altitude.c -lm || exit 1
for mode in plain ttis; do
    grep -v '^#' Reference/vval79_tr1201_tableE.txt | while read -r d t want; do
        got=$("$TMP/h" "$d" "$t" "$mode")
        echo "$want $got"
    done | awk -v m="$mode" '{d=$2-$1; a=d<0?-d:d; n++; s+=d; sa+=a; if(a<0.05)e++; if(a<=1.01)w1++; if(a<=2.01)w2++}
        END{printf "VVAL-79 %-5s %d schedules: exact %d, within 1 min %d, within 2 min %d, bias %+.2f, mean |diff| %.2f\n", m, n, e, w1, w2, s/n, sa/n}'
done
