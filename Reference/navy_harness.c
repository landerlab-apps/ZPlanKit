/* navy_harness.c - one MK 16 MOD 1 heliox dive, 1.3 atm PO2, Navy conventions:
 * descent 60 fsw/min, ascent 30 fsw/min, 10 fsw stops, 20 fsw last stop.
 * usage: navy_harness model depth_fsw bottom_time_min -> total ascent time, min */
#include "czplan.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 4) return 2;
    double d = atof(argv[2]), bt = atof(argv[3]);
    char prof[1024];
    snprintf(prof, sizeof prof,
        "UseMetric: n\nSaltWater: y\nSetpointUnit: ata\nModel: %s\nStopDistance: 10\nLastStopDepth: 20\n"
        "DescentRate: 0-400, 60\nAscentRate: 400-0, 30\n%.0f, %.6f, 12/88, 1.3\n",
        argv[1], d, bt - d / 60.0);
    zp_config cfg; char err[256] = "";
    if (zp_parse_profile(prof, &cfg, err, sizeof err)) return 1;
    static zp_result r;
    zp_plan(&cfg, &r);
    if (r.refused) { printf("refused\n"); return 0; }
    printf("%.4f\n", r.runtime_min - bt);
    return 0;
}
