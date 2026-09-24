/* regression_harness.c - print a model's schedule in one deterministic line.
 *
 * Purpose: prove that a change to one decompression model has not moved any
 * other one. Built twice - once against git HEAD, once against the working
 * tree - and the two outputs diffed. See regression_check.sh.
 *
 * usage: harness <model 0|1|2> <depth m> <time min> <fo2> <fhe> <salt 0|1>
 *                [gf low %] [gf high %] [last stop m]
 */

#include <stdio.h>
#include <stdlib.h>
#include "czplan.h"

int main(int argc, char **argv)
{
    if (argc < 7) { fprintf(stderr, "bad args\n"); return 2; }

    zp_config c;
    zp_config_init(&c);
    c.use_vval       = atoi(argv[1]);
    c.metric_output  = true;
    c.salt_water     = atoi(argv[6]) != 0;
    c.stop_distance_m   = 3.0;
    c.last_stop_depth_m = argc > 9 ? atof(argv[9]) : 3.0;
    if (argc > 8 && atof(argv[7]) > 0) {
        c.use_gf = true;
        c.gf_lo  = atof(argv[7]) / 100.0;
        c.gf_hi  = atof(argv[8]) / 100.0;
    }

    c.n_wp = 1;
    c.wp[0].depth_m  = atof(argv[2]);
    c.wp[0].time_min = atof(argv[3]);
    c.wp[0].fo2      = atof(argv[4]);
    c.wp[0].fhe      = atof(argv[5]);

    c.use_oc_deco = true;
    c.n_oc_deco   = 2;
    c.oc_deco_fo2[0] = 0.50;
    c.oc_deco_fo2[1] = 1.00;
    c.oc_deco_max_po2 = 1.61;
    c.max_end_m       = 30.0;

    c.rmv_l_min      = 20.0;
    c.deco_rmv_l_min = 15.0;

    c.n_descent = 1;
    c.descent[0].from_m = 0; c.descent[0].to_m = 999; c.descent[0].rate_m_min = 20;
    c.n_ascent = 1;
    c.ascent[0].from_m = 0;  c.ascent[0].to_m = 999;  c.ascent[0].rate_m_min = 10;

    zp_result r;
    if (zp_plan(&c, &r)) { printf("PLAN FAILED\n"); return 1; }

    double total = 0.0;
    printf("model=%s depth=%s time=%s mix=%s/%s salt=%s gf=%s/%s last=%s |",
           argv[1], argv[2], argv[3], argv[4], argv[5], argv[6],
           argc > 8 ? argv[7] : "-", argc > 8 ? argv[8] : "-",
           argc > 9 ? argv[9] : "3");
    for (int i = 0; i < r.n_lines; i++)
        if (r.lines[i].kind == ZP_LINE_NORMSTOP ||
            r.lines[i].kind == ZP_LINE_DEEPSTOP) {
            printf(" %.0f:%.1f", r.lines[i].depth_m, r.lines[i].stop_sec / 60.0);
            total += r.lines[i].stop_sec / 60.0;
        }
    printf(" | deco=%.1f runtime=%.1f cns=%.1f otu=%.1f\n",
           total, r.runtime_min, r.cns_pct, r.otu);
    return 0;
}
