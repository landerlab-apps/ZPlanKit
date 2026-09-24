/*
 * czplan.h — Portable reimplementation of the ZPlan v1.03 decompression engine
 *            (Bühlmann ZH-L16C, OC + CC, Pyle/WKPP-style deep stops,
 *             NOAA CNS / REPEX OTU tracking, gas consumption, time-to-fly)
 *
 * Clean-room reimplementation based on the ZPlan documentation (README,
 * profile.dat comments, o2.cfg) by William M. Smithers, 1997-98.
 *
 * ============================ WARNING ============================
 * THIS SOFTWARE PLANS DECOMPRESSION DIVES AND CAN KILL YOU.
 * It is experimental, unvalidated, and certainly contains bugs.
 * Never dive a schedule from this program without independently
 * verifying it against trusted tables/software, and never use it
 * without formal mixed-gas decompression training.
 * =================================================================
 *
 * Copyright (C) 2026 Carlos Lander <carlos.lander@etik.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. It is distributed WITHOUT ANY WARRANTY - see the licence
 * for details, and see the warning above for why that matters more here than
 * it does in most software. A copy of the licence ships in LICENSE; if it is
 * missing, see <https://www.gnu.org/licenses/>.
 */

#ifndef CZPLAN_H
#define CZPLAN_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZP_MAX_WAYPOINTS   64
#define ZP_MAX_GASES       16
#define ZP_MAX_RATES       16
#define ZP_MAX_SETPOINTS   16
#define ZP_MAX_PLAN_LINES  256
#define ZP_COMPARTMENTS    17

typedef enum {
    ZP_DEEPSTOPS_NONE = 0,
    ZP_DEEPSTOPS_PYLE = 1,
    ZP_DEEPSTOPS_WKPP = 2
} zp_deepstop_mode;

typedef struct {
    double depth_m;      /* metres (internal canonical units) */
    double time_min;     /* minutes at this waypoint */
    double fo2;          /* fraction 0..1 */
    double fhe;          /* fraction 0..1 */
    bool   cc;           /* closed circuit? */
    double setpoint;     /* PO2 bar, if cc */
    double slide_max;    /* Scamahorn slide max PO2 (0 = none) */
} zp_waypoint;

typedef struct { double from_m, to_m, rate_m_min; } zp_rate_range;
typedef struct { double from_m, to_m, setpoint;   } zp_setpoint_range;

typedef enum { ZP_AB_OFF = 0, ZP_AB_NAVY, ZP_AB_SUBSURFACE } zp_airbreak_mode;

typedef struct {
    /* general */
    bool   metric_output;      /* echo of UseMetric (affects report only) */
    bool   salt_water;
    bool   use_gf;
    double gf_lo, gf_hi;
    int    use_vval;
    int    vpm_conservatism;
    double vpm_radius_n2_um;
    double vpm_radius_he_um;
    int    rmv_metric;         /* -1 follow UseMetric, 0 cu.ft, 1 litres */
    double slide_rate;         /* Scamahorn slide: PO2 burned off per minute */
    bool   use_1b;            /* include Buhlmann's optional 1b compartment */
    bool   ndl_gf_low;         /* NDL check uses GF-low (default: GF-high) */
    double altitude_m;
    bool   altitude_equilibrated;
    double hours_at_altitude;
    double conservatism_pct;   /* 0-100 */
    double stop_distance_m;
    double last_stop_depth_m;
    bool   oxy_narc;
    zp_deepstop_mode deepstops;
    double pyle_stop_min;
    double rmv_l_min;          /* bottom RMV, litres/min at 1 bar */
    double deco_rmv_l_min;
    zp_airbreak_mode air_break_mode;
    double o2_period_min;      /* oxygen breathing per cycle. Navy 30. */
    double air_break_min;      /* break length. Navy 5. */
    double o2_ceiling_m;       /* breaks only at or above this. Navy 30 fsw. */
    double break_min_po2;      /* reject a break gas below this PO2 at depth */
    double o2_trigger_fo2;     /* FO2 at or above which the O2 clock runs */

    double cns_break_pct;      /* CNS % that also calls for a break. 80. */

    double break_gas_fo2, break_gas_fhe;

    bool   travel_gas;
    bool   use_oc_deco;
    double oc_deco_fo2[ZP_MAX_GASES];
    double oc_deco_fhe[ZP_MAX_GASES];
    double icd_warn_bar;
    int    n_oc_deco;
    double oc_deco_max_po2;
    double max_end_m;
    double ext_stop_shallow_min;   /* switch at 7 m up to 30 m */
    double ext_stop_deep_min;      /* switch at 30 m or deeper */
    zp_rate_range descent[ZP_MAX_RATES]; int n_descent;
    zp_rate_range ascent [ZP_MAX_RATES]; int n_ascent;
    bool   use_deco_setpoint;
    zp_setpoint_range deco_sp[ZP_MAX_SETPOINTS]; int n_deco_sp;
    zp_waypoint wp[ZP_MAX_WAYPOINTS]; int n_wp;
    bool   have_initial_tissues;
    double init_pn2[ZP_COMPARTMENTS];
    double init_phe[ZP_COMPARTMENTS];
    double init_cns_pct;
    double surface_interval_min; /* applied to initial tissues before dive */
} zp_config;

typedef enum {
    ZP_LINE_WAYPOINT, ZP_LINE_DEEPSTOP, ZP_LINE_NORMSTOP,
    ZP_LINE_GASSWITCH,  /* gas changed while ascending, no stop here */
    ZP_LINE_AIRBREAK,   /* oxygen break: time on the leaner break gas */
    ZP_LINE_TRAVELGAS   /* descent: switch off the travel gas onto back gas */
} zp_line_kind;

typedef struct {
    zp_line_kind kind;
    double depth_m;
    double stop_sec;        /* time spent at this depth (excl. travel) */
    double runtime_min;     /* runtime when leaving this depth */
    double fo2, fhe;
    bool   cc; double setpoint;
    double ppo2;            /* inspired PO2, ATM (raw ambient, as original displays) */
    double end_m;           /* equivalent narcotic depth */
    double ead_m;           /* equivalent air (nitrogen) depth */
} zp_plan_line;

typedef struct {
    zp_plan_line lines[ZP_MAX_PLAN_LINES];
    int    n_lines;
    bool   refused;
    double total_deco_min;
    double runtime_min;
    double cns_pct;
    double otu;
    double time_to_fly_hr;
    double decozone_start_m;   /* depth where the ascent first becomes ceiling-limited */
    double bottom_density_gl;  /* breathing-gas density at the deepest point, g/l */
    /* litres consumed per distinct OC gas */
    double gas_used_l[ZP_MAX_GASES];
    double gas_bottom_l[ZP_MAX_GASES];
    double gas_fo2[ZP_MAX_GASES];
    double gas_fhe[ZP_MAX_GASES];
    int    n_gas_used;
    double total_oc_l;
    /* end-of-dive tissue state, for repetitive planning */
    double end_pn2[ZP_COMPARTMENTS];
    double end_phe[ZP_COMPARTMENTS];
    double end_cns_pct;
    char   warnings[2048];
} zp_result;

/* Fill cfg with sane defaults (imperial off, salt, ZHL16C, etc.). */
void zp_config_init(zp_config *cfg);

int zp_parse_profile(const char *text, zp_config *cfg,
                     char *err, size_t errlen);

/* Run the planner. Returns 0 on success. */
int zp_plan(const zp_config *cfg, zp_result *out);

/* Render a ZPlan-style text report into buf. Returns bytes written. */
int zp_report(const zp_config *cfg, const zp_result *res,
              char *buf, size_t buflen);

/* Library version string */
const char *zp_version(void);

#ifdef __cplusplus
}
#endif
#include "zp_altitude.h"
#endif /* CZPLAN_H */
