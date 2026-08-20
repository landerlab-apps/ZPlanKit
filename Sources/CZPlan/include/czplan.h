/*
 * czplan.h — Portable reimplementation of the ZPlan v1.03 decompression engine
 *            (Bühlmann ZHL-16B/C, OC + CC, Pyle/WKPP-style deep stops,
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
 * Copyright (C) 2026 Carlos Lander <scubalander@gmail.com>
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

typedef struct {
    /* general */
    bool   metric_output;      /* echo of UseMetric (affects report only) */
    bool   salt_water;
    bool   use_b_values;       /* deprecated (v1.5): ZHL-16C is the only Buhlmann set */
    /* Baker gradient factors (optional). When use_gf is true, GF lo/hi
     * replace the ZPlan Conservatism mechanism entirely. Fractions 0-1. */
    bool   use_gf;
    double gf_lo, gf_hi;
    /* Decompression model: 0 = Buhlmann ZHL-16 (b/c per use_b_values),
     * 1 = U.S. Navy Thalmann EL-DCM, displayed as "VVAL-18".
     * NOTE: the parameter set is VVAL-79 (NEDU TR 12-01) extended to twelve
     * compartments per the owner's parameters.py; the three fastest
     * compartments and the crossover pressures are WORKING values. */
    int    use_vval;
    /* VPM-B settings. Conservatism 0-4 scales both critical radii (0 is
     * Baker's nominal VPM-B). The radii themselves are the parameter that
     * actually differs between implementations: Baker ships 0.6/0.5 microns,
     * Subsurface 0.55/0.45. Only 0.6/0.5 reproduces Baker's published
     * VPM.OUT. */
    int    vpm_conservatism;
    double vpm_radius_n2_um;
    double vpm_radius_he_um;
    int    rmv_metric;         /* -1 follow UseMetric, 0 cu.ft, 1 litres */
    double slide_rate;         /* Scamahorn slide: PO2 burned off per minute */
    bool   use_1b;            /* include Buhlmann's optional 1b compartment */
    bool   ascent_credit;      /* legacy ZPlan predictive ascent rule */
    bool   extra_slow;         /* experimental: hold while offgas gradient > 1.25 bar */
    bool   ndl_gf_low;         /* NDL check uses GF-low (default: GF-high) */
    double altitude_m;
    double conservatism_pct;   /* 0-100 */
    double stop_distance_m;
    double last_stop_depth_m;
    bool   oxy_narc;
    zp_deepstop_mode deepstops;
    double pyle_stop_min;
    double rmv_l_min;          /* bottom RMV, litres/min at 1 bar */
    double deco_rmv_l_min;
    bool   use_oc_deco;
    double oc_deco_fo2[ZP_MAX_GASES];
    /* Helium fraction of each deco gas. Trimix and heliox deco mixes are a real
     * technique: switching from a trimix bottom gas to EAN50 maximises the
     * helium gradient but leaves inspired nitrogen almost unchanged, so
     * nitrogen washout very nearly stops. A 50/25 or 50/50 deco mix drives
     * inspired nitrogen to near zero instead. */
    double oc_deco_fhe[ZP_MAX_GASES];
    /* ICD advisory threshold, ata rise in an inspired inert partial
     * pressure at a gas switch. 0 disables. V-Planner uses 0.5. */
    double icd_warn_bar;
    int    n_oc_deco;
    double oc_deco_max_po2;
    double max_end_m;
    /* Extended stops on a deco mix switch. When the planner switches to a deco
     * gas, it holds at that depth for these extra minutes (0-10). The band is
     * chosen by the depth of the switch; switches shallower than 7 m get none. */
    double ext_stop_shallow_min;   /* switch at 7 m up to 30 m */
    double ext_stop_deep_min;      /* switch at 30 m or deeper */
    zp_rate_range descent[ZP_MAX_RATES]; int n_descent;
    zp_rate_range ascent [ZP_MAX_RATES]; int n_ascent;
    bool   use_deco_setpoint;
    zp_setpoint_range deco_sp[ZP_MAX_SETPOINTS]; int n_deco_sp;
    zp_waypoint wp[ZP_MAX_WAYPOINTS]; int n_wp;
    /* repetitive diving: initial tissue state (bar) and CNS%; if
       have_initial_tissues is false, tissues start saturated at altitude */
    bool   have_initial_tissues;
    double init_pn2[ZP_COMPARTMENTS];
    double init_phe[ZP_COMPARTMENTS];
    double init_cns_pct;
    double surface_interval_min; /* applied to initial tissues before dive */
} zp_config;

typedef enum {
    ZP_LINE_WAYPOINT, ZP_LINE_DEEPSTOP, ZP_LINE_NORMSTOP,
    ZP_LINE_GASSWITCH   /* gas changed while ascending, no stop here */
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
    double total_deco_min;
    double runtime_min;
    double cns_pct;
    double otu;
    double time_to_fly_hr;
    double decozone_start_m;   /* depth where the ascent first becomes ceiling-limited */
    double bottom_density_gl;  /* breathing-gas density at the deepest point, g/l */
    /* litres consumed per distinct OC gas */
    double gas_used_l[ZP_MAX_GASES];
    /* of which was breathed before leaving the bottom. Deco gases are zero
     * here, which is what distinguishes a back gas from a deco gas in the
     * consumption report: only a back gas needs its ascent share broken out. */
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

/* Parse a ZPlan-format profile.dat buffer into cfg. Returns 0 on success,
 * negative on error; err (optional) receives a message. Accepts both the
 * metric and imperial dialects; everything is converted to metric. */
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
#endif /* CZPLAN_H */
