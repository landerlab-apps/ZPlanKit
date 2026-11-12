/* vval79_harness.c - total VVAL-79 stop time for one Navy air schedule.
 * usage: vval79_harness <depth fsw> <bottom time min> [ttis]
 * Depth is taken as fsw of pressure (33 fsw = 1 atm). With "ttis" the
 * travel time to each stop after the first is counted in that stop, as the
 * Navy tables print it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "czplan.h"

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    double fsw = 1.01325e5 / (33.0 * 0.3048 * 9.80665 * 1025.0);
    double d = atof(argv[1]) * fsw, bt = atof(argv[2]);
    char prof[1024];
    snprintf(prof, sizeof prof,
        "UseMetric: n\nSaltWater: y\nModel: vval79\nStopDistance: 10\nLastStopDepth: 20\n"
        "DescentRate: 0-2000, 75\nAscentRate: 2000-0, 30\nConservatism: 0\nDeepStops: n\n"
        "%.4f, %.4f, 21\n", d, bt - d / 75.0);
    zp_config c; char err[256]; static zp_result r;
    if (zp_parse_profile(prof, &c, err, sizeof err) || zp_plan(&c, &r)) return 1;
    double s = 0; int n = 0;
    for (int i = 0; i < r.n_lines; i++)
        if (r.lines[i].kind == ZP_LINE_NORMSTOP) { s += r.lines[i].stop_sec; n++; }
    if (argc > 3 && !strcmp(argv[3], "ttis") && n > 1) s += (n - 1) * 20.0;
    printf("%.1f\n", s / 60.0);
    return 0;
}
