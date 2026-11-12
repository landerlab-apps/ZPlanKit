#!/bin/sh
# navy_check.sh - XVal-He-4 and XVal-He-4B against the published MK 16 MOD 1
# 1.3 atm PO2 in helium schedules (NEDU TR 02-10 Appendix J, total ascent
# time), 60 to 200 fsw. Informational.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
CC=${CC:-cc}
$CC -O2 -ISources/CZPlan/include -o "$TMP/h" Reference/navy_harness.c \
    Sources/CZPlan/czplan.c Sources/CZPlan/zp_altitude.c -lm || exit 1
for model in xhe4 xhe4b; do
    awk '$1 >= 60 && $1 <= 200 { for (i = 3; i <= NF; i++) if ($i ~ /^[0-9]+:[0-9][0-9]$/) { split($i, a, ":"); print $1, $2, a[1] + a[2] / 60; break } }' \
        Reference/navy_tr0210_appJ.txt | while read -r d t want; do
        got=$("$TMP/h" "$model" "$d" "$t")
        echo "$want $got"
    done | awk -v m="$model" '$2 != "refused" {d=$2-$1; a=d<0?-d:d; n++; if(a<0.5)h++; if(a<1.01)w1++; if(a>w)w=a}
        $2 == "refused" {r++}
        END{printf "%-6s %d schedules: within 30 s %d, within 1 min %d, worst %.1f min, refused %d\n", m, n, h, w1, w, r}'
done
