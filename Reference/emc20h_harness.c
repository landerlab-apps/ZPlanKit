/* emc20h_harness.c - total EMC-20H stop time for one profile, in minutes.
 * usage: emc20h_harness <profile.dat> */
#include <stdio.h>
#include <stdlib.h>
#include "czplan.h"

int main(int argc, char **argv)
{
    static char text[8192];
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "r");
    if (!f) return 2;
    size_t n = fread(text, 1, sizeof text - 1, f);
    text[n] = 0;
    fclose(f);
    zp_config c; char err[256];
    if (zp_parse_profile(text, &c, err, sizeof err)) return 1;
    static zp_result r;
    if (zp_plan(&c, &r) || r.refused) return 1;
    double s = 0.0, first = 0.0;
    for (int i = 0; i < r.n_lines; i++)
        if (r.lines[i].kind == ZP_LINE_NORMSTOP) {
            if (first == 0.0) first = r.lines[i].depth_m * 3.2808399;
            s += r.lines[i].stop_sec;
        }
    printf("%.0f %.2f\n", first, s / 60.0);
    return 0;
}
