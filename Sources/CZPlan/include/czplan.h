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
    /* Baker gradient factors (optional). When use_gf is true, GF lo/hi
     * replace the ZPlan Conservatism mechanism entirely. Fractions 0-1. */
    bool   use_gf;
    double gf_lo, gf_hi;
    /* Decompression model: 0 = Buhlmann ZH-L16C,
     * 1 = U.S. Navy Thalmann EL-DCM with the VVal-79 air parameter set.
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
    bool   ndl_gf_low;         /* NDL check uses GF-low (default: GF-high) */
    double altitude_m;
    /* EQUILIBRATION at altitude - not acclimatisation. The U.S. Navy Diving
     * Manual (rev 7, 9-13.4) separates the two, and the distinction matters
     * here: equilibration is the body off-gassing its excess nitrogen to match
     * the thinner air, and takes about twelve hours; acclimatization is the
     * much slower adjustment to the lower oxygen partial pressure. This engine
     * models the first and nothing whatsoever of the second.
     *
     * A diver who drove up this morning is still carrying his sea-level
     * nitrogen. On a 30 m dive at 3000 m that is the difference between 8.6
     * and 18.6 minutes of decompression, so the assumption cannot be silent.
     * The Navy handles the same fact by treating the ascent to altitude as a
     * repetitive dive (their Table 9-5); the per-compartment washout below is
     * the continuous form of that idea.
     *
     *   altitude_equilibrated  true  = fully equilibrated at altitude
     *                          false = use hours_at_altitude (DEFAULT)
     *   hours_at_altitude      0     = arrived just now, sea-level tissues
     *                          n     = washed out for n hours since arriving
     *
     * Both are ignored when altitude_m is 0, where they cannot differ. */
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
    /* ---- oxygen ("air") breaks -------------------------------------
     * Off by default. When on, the diver is taken off the oxygen-rich deco
     * gas onto a leaner carried gas for air_break_min after every
     * o2_period_min of oxygen breathing, to limit CNS oxygen toxicity.
     *
     * Two modes, because the two reference implementations disagree and
     * neither is obviously right:
     *
     *   ZP_AB_NAVY       the break is gas-exchange dead time. Inert tensions
     *                    are frozen for its duration and the stop simply grows
     *                    by that much. This is how the US Navy Diving Manual
     *                    Air/O2 tables were generated (NEDU TR 07-09 and the
     *                    AB_DEAD parameter in TR 10-09), and it is the only
     *                    treatment anything published validates.
     *
     *   ZP_AB_SUBSURFACE the break is an ordinary gas segment, integrated on
     *                    the break gas like any other. The stop then grows by
     *                    whatever the model says rather than by a fixed
     *                    amount. This is what Subsurface does. It is
     *                    physically truer and it is not validated by anyone.
     *
     * CNS and OTU accrue on the break gas in BOTH modes. "Dead time" in the
     * Navy sense is about inert gas only; freezing the oxygen clock as well
     * would defeat the entire purpose of the break. */
    zp_airbreak_mode air_break_mode;
    double o2_period_min;      /* oxygen breathing per cycle. Navy 30. */
    double air_break_min;      /* break length. Navy 5. */
    double o2_ceiling_m;       /* breaks only at or above this. Navy 30 fsw. */
    double break_min_po2;      /* reject a break gas below this PO2 at depth */
    double o2_trigger_fo2;     /* FO2 at or above which the O2 clock runs */

    /* Second trigger condition, and the reason it is not a switch. Breaks are
     * taken when the diver is on oxygen at or above o2_ceiling_m, OR when the
     * accumulated CNS clock reaches cns_break_pct. 80 is reachable on open
     * circuit where 100 is not: a 70 m trimix plan with a 55-minute bottom
     * time runs to 94.8%, and closed circuit at setpoint 1.5 to 87.8%.
     *
     * The trigger does not disarm. CNS does not fall in the water - see the
     * note on o2_rates - so once the clock is past the threshold it stays
     * past it. That does not mean continuous breaks: the oxygen period still
     * has to elapse between them, so the cadence is the same 30/5 either
     * trigger fires it. */
    double cns_break_pct;      /* CNS % that also calls for a break. 80. */

    /* The diver's chosen break gas, 0 for automatic. When set and carried and
     * breathable at the stop it wins; otherwise the automatic order applies:
     * back gas first, then the leanest carried mix clearing break_min_po2.
     * A chosen gas that cannot be used is reported, never silently replaced. */
    double break_gas_fo2, break_gas_fhe;

    /* Travel gas. A hypoxic back gas cannot be breathed on the surface: 10/50
     * gives 0.101 bar there, and the plan has the diver on it from the first
     * second of the descent. With this on, the descent starts on the leanest
     * carried mix that IS breathable at the surface and switches to the back
     * gas at the first stop increment where the back gas clears
     * break_min_po2 - 9 m on a 10/50, where it reaches 0.192 bar.
     *
     * Engages only when the back gas is unsafe at the surface, so a normoxic
     * plan is untouched. The gas is chosen automatically; there is no setting
     * for it, because the rule (leanest mix breathable at the surface) is the
     * one a diver would apply anyway. Off by default: turning it on changes
     * the inert loading of the first minutes and the gas volumes, so existing
     * plans stay as they are until the diver asks. */
    bool   travel_gas;
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
    /* Set when the model declined to plan this dive at all (VVal-79 with
     * helium). n_lines is 0 and the reason is in warnings. */
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
