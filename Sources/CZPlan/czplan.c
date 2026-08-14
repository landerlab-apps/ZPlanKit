/* czplan.c — see czplan.h for the warning you must read. */

#include "include/czplan.h"
#include <math.h>
#ifndef M_LN2
#define M_LN2 0.69314718055994530942
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define ZP_VERSION "1.8.2"
const char *zp_version(void) { return ZP_VERSION; }

/* ------------------------------------------------------------------ */
/* Physical constants                                                  */
/* ------------------------------------------------------------------ */
#define P_SEALEVEL   1.01325      /* bar */
#define PH2O         0.0627      /* bar — alveolar water vapour, Buhlmann Rq=1.0
                                  * convention, matching Subsurface/MultiDeco.
                                  * (v1.6: the old ZPlan binary used 0, which
                                  * made every schedule ~6% too conservative.) */
#define G_ACC        9.80665
#define RHO_FRESH    1000.0
#define RHO_SALT     1025.0
#define FT_PER_M     3.2808399
#define L_PER_CUFT   28.3168466
#define DT           (1.0/60.0)   /* integration step: 1 second, in minutes */
#define FLY_ALT_M    3048.0       /* 10,000 ft cabin altitude */

/* ------------------------------------------------------------------ */
/* ZHL-16 coefficients (16 compartments; the 4-min "1b" N2 compartment  */
/* is omitted — see README, it is irrelevant for deco-length dives).    */
/* ------------------------------------------------------------------ */
static const double N2_HT[ZP_COMPARTMENTS] = {   /* standard ZH-L16 order; the optional
    * 1b compartment is last and is used only when Compartment1b: y */
    5.0, 8.0, 12.5, 18.5, 27.0, 38.3, 54.3, 77.0,
    109.0, 146.0, 187.0, 239.0, 305.0, 390.0, 498.0, 635.0,
    4.0 };
static const double N2_B[ZP_COMPARTMENTS] = {   /* standard ZH-L16 order; the optional
    * 1b compartment is last and is used only when Compartment1b: y */
    0.5578, 0.6514, 0.7222, 0.7825, 0.8126, 0.8434, 0.8693, 0.8910,
    0.9092, 0.9222, 0.9319, 0.9403, 0.9477, 0.9544, 0.9602, 0.9653,
    0.5050 };
/* ZHL-16B "a" values (table generation) */
static const double N2_A_B[ZP_COMPARTMENTS] = {   /* standard ZH-L16 order; the optional
    * 1b compartment is last and is used only when Compartment1b: y */
    1.1696, 1.0000, 0.8618, 0.7562, 0.6667, 0.5600, 0.4947, 0.4500,
    0.4187, 0.3798, 0.3497, 0.3223, 0.2850, 0.2737, 0.2523, 0.2327,
    1.2599 };
/* ZHL-16C "a" values (dive computers, more conservative) */
static const double N2_A_C[ZP_COMPARTMENTS] = {   /* standard ZH-L16 order; the optional
    * 1b compartment is last and is used only when Compartment1b: y */
    1.1696, 1.0000, 0.8618, 0.7562, 0.6200, 0.5043, 0.4410, 0.4000,
    0.3750, 0.3500, 0.3295, 0.3065, 0.2835, 0.2610, 0.2480, 0.2327,
    1.2599 };
static const double HE_HT[ZP_COMPARTMENTS] = {   /* standard ZH-L16 order; the optional
    * 1b compartment is last and is used only when Compartment1b: y */
    1.88, 3.02, 4.72, 6.99, 10.21, 14.48, 20.53, 29.11,
    41.20, 55.19, 70.69, 90.34, 115.29, 147.42, 188.24, 240.03,
    1.51 };
static const double HE_A[ZP_COMPARTMENTS] = {   /* standard ZH-L16 order; the optional
    * 1b compartment is last and is used only when Compartment1b: y */
    1.6189, 1.3830, 1.1919, 1.0458, 0.9220, 0.8205, 0.7305, 0.6502,
    0.5950, 0.5545, 0.5333, 0.5189, 0.5181, 0.5176, 0.5172, 0.5119,
    1.7424 };
static const double HE_B[ZP_COMPARTMENTS] = {   /* standard ZH-L16 order; the optional
    * 1b compartment is last and is used only when Compartment1b: y */
    0.4770, 0.5747, 0.6527, 0.7223, 0.7582, 0.7957, 0.8279, 0.8553,
    0.8757, 0.8903, 0.8997, 0.9073, 0.9122, 0.9171, 0.9217, 0.9267,
    0.4245 };

/* ------------------------------------------------------------------ */
/* O2 toxicity table, transcribed from ZPlan's o2.cfg                  */
/* (NOAA CNS %/min and REPEX OTU/min vs PO2).                          */
/* --------------------------------------------------------------------
 * U.S. Navy Thalmann EL-DCM — displayed as "VVAL-18" per owner request.
 * Parameters from the owner's parameters.py: nine NEDU VVAL-79 compartments
 * (NEDU TR 12-01 Table 3) + three fast [WORKING] compartments; crossover
 * pressures are [WORKING] estimates. MPTT slope = 1.0 fsw/fsw throughout.
 * All values converted from fsw to bar at 33 fsw = 1 atm (USN convention).
 * ------------------------------------------------------------------ */
#define VVAL_NC 12
#define FSW2BAR (1.01325 / 33.0)
#define VVAL_H2O_BAR (1.85 * FSW2BAR)      /* 47 mmHg alveolar water vapour */
static const double VVAL_HT_N2[VVAL_NC] = {
    1.5, 2.5, 3.5, 5.0, 10.0, 20.0, 40.0, 80.0, 120.0, 160.0, 200.0, 240.0 };
static const double VVAL_MPTT0_FSW[VVAL_NC] = {
    120.0, 115.0, 108.0, 99.3, 87.7, 78.0, 56.0, 48.5, 45.5, 44.5, 44.0, 43.5 };
/* Crossover pressures FITTED against U.S. Navy references (2026-08-07):
 *   132 fsw / 20 min -> 9:00 @ 20 fsw  (NEDU manned-trial, algorithm-exact)
 *   40 fsw 170/200/240 min -> within 1 min of Rev.7 Table 9-9
 * Override for re-fitting with env ZP_PCROSS="v0,...,v11". */
static double VVAL_PCROSS_FSW[VVAL_NC] = {
    1e9, 1e9, 1e9, 1e9, 23.0, 17.0, 13.0, 11.0, 8.0, 8.0, 8.0, 8.0 };
/* fitting hook: ZP_PCROSS="v0,v1,...,v11" overrides the table (temporary) */
static void vval_pcross_env(void) {
    static int done = 0;
    if (done) return; done = 1;
    const char *e = getenv("ZP_PCROSS");
    if (!e) return;
    char buf[256]; strncpy(buf, e, 255); buf[255] = 0;
    char *tok = strtok(buf, ","); int i = 0;
    while (tok && i < VVAL_NC) { VVAL_PCROSS_FSW[i++] = atof(tok); tok = strtok(NULL, ","); }
}
#define EXTRA_SLOW_GRADIENT_BAR    1.25
/* Safety bound per ascent segment. Restored to 5 minutes in v1.8.0 to match the
 * documented behaviour ("adds at most 5 minutes per stop"); it had been raised
 * to 15 to paper over the broken gradient criterion, which is now fixed at
 * source in offgas_gradient_at(). */
#define EXTRA_SLOW_MAX_DELAY_MIN   5.0
#define VVAL_HE_RATIO 2.6457513110645906   /* sqrt(28/4) */

/* ------------------------------------------------------------------ */
typedef struct { double po2, cns, otu; } o2row;
static const o2row O2TAB[] = {
 {0.50,0.02,0.0},{0.51,0.02,0.0},{0.52,0.03,0.0},{0.54,0.10,0.0},
 {0.55,0.10,0.15},{0.56,0.11,0.15},{0.58,0.12,0.16},{0.59,0.13,0.20},
 {0.60,0.14,0.27},{0.61,0.14,0.29},{0.62,0.14,0.30},{0.63,0.145,0.32},
 {0.64,0.15,0.35},{0.65,0.155,0.37},{0.66,0.16,0.39},{0.67,0.165,0.42},
 {0.68,0.17,0.44},{0.69,0.175,0.46},{0.70,0.18,0.47},{0.71,0.18,0.49},
 {0.72,0.18,0.51},{0.73,0.185,0.53},{0.74,0.185,0.53},{0.75,0.19,0.56},
 {0.76,0.20,0.57},{0.77,0.20,0.59},{0.78,0.21,0.61},{0.79,0.21,0.63},
 {0.80,0.22,0.65},{0.81,0.225,0.65},{0.82,0.23,0.66},{0.83,0.235,0.68},
 {0.84,0.240,0.72},{0.85,0.245,0.74},{0.86,0.250,0.75},{0.87,0.255,0.78},
 {0.88,0.260,0.80},{0.89,0.270,0.81},{0.90,0.280,0.83},{0.91,0.29,0.85},
 {0.92,0.29,0.87},{0.93,0.30,0.89},{0.94,0.30,0.90},{0.95,0.305,0.92},
 {0.96,0.31,0.93},{0.97,0.315,0.95},{0.98,0.32,0.97},{0.99,0.325,0.99},
 {1.00,0.33,1.0},{1.01,0.34,1.02},{1.02,0.35,1.03},{1.03,0.355,1.05},
 {1.04,0.36,1.07},{1.05,0.37,1.08},{1.06,0.38,1.10},{1.07,0.385,1.12},
 {1.08,0.40,1.14},{1.09,0.41,1.15},{1.10,0.42,1.16},{1.11,0.43,1.18},
 {1.12,0.435,1.20},{1.13,0.4375,1.22},{1.14,0.439,1.24},{1.15,0.440,1.25},
 {1.16,0.445,1.26},{1.17,0.455,1.28},{1.18,0.460,1.30},{1.19,0.465,1.31},
 {1.20,0.470,1.32},{1.21,0.475,1.33},{1.22,0.48,1.34},{1.23,0.495,1.35},
 {1.24,0.51,1.375},{1.25,0.515,1.4},{1.26,0.52,1.45},{1.27,0.53,1.45},
 {1.28,0.54,1.46},{1.29,0.55,1.47},{1.30,0.56,1.48},{1.31,0.565,1.49},
 {1.32,0.57,1.50},{1.33,0.59,1.52},{1.34,0.60,1.53},{1.35,0.61,1.55},
 {1.36,0.62,1.56},{1.37,0.63,1.587},{1.38,0.64,1.59},{1.39,0.645,1.60},
 {1.40,0.65,1.63},{1.41,0.66,1.65},{1.42,0.68,1.66},{1.43,0.695,1.68},
 {1.44,0.71,1.69},{1.45,0.72,1.70},{1.46,0.74,1.72},{1.47,0.77,1.74},
 {1.48,0.78,1.76},{1.49,0.80,1.77},{1.50,0.83,1.78},{1.51,0.88,1.79},
 {1.52,0.93,1.82},{1.53,0.98,1.83},{1.54,1.04,1.84},{1.55,1.11,1.85},
 {1.56,1.19,1.86},{1.57,1.32,1.88},{1.58,1.47,1.89},{1.59,1.80,1.90},
 {1.60,2.22,1.92},{1.61,3.00,1.94},{1.62,5.00,1.96},{1.63,5.50,1.97},
 {1.64,5.75,1.99},{1.65,6.25,2.00},{1.67,7.69,2.02},{1.68,8.25,2.03},
 {1.69,9.00,2.05},{1.70,10.0,2.07},{1.71,11.0,2.10},{1.72,12.5,2.12},
 {1.73,15.0,2.13},{1.74,20.0,2.135},{1.75,21.0,2.14},{1.76,22.0,2.15},
 {1.77,25.0,2.17},{1.78,31.25,2.19},{1.79,40.0,2.20},{1.80,50.0,2.21},
 {1.82,50.0,2.25},{1.84,50.0,2.27},{1.85,50.0,2.28},{1.86,50.0,2.30},
 {1.88,50.0,2.32},{1.90,50.0,2.35},{1.95,50.0,2.42},{2.00,50.0,2.49},
 {2.20,100.0,3.0},{100.0,100.0,10.0}
};
#define O2N ((int)(sizeof(O2TAB)/sizeof(O2TAB[0])))

static void o2_rates(double po2, double *cns_min, double *otu_min) {
    if (po2 < O2TAB[0].po2) { *cns_min = 0; *otu_min = 0; return; }
    for (int i = 0; i < O2N - 1; i++) {
        if (po2 <= O2TAB[i+1].po2) {
            double f = (po2 - O2TAB[i].po2) / (O2TAB[i+1].po2 - O2TAB[i].po2);
            *cns_min = O2TAB[i].cns + f * (O2TAB[i+1].cns - O2TAB[i].cns);
            *otu_min = O2TAB[i].otu + f * (O2TAB[i+1].otu - O2TAB[i].otu);
            return;
        }
    }
    *cns_min = 100.0; *otu_min = 10.0;
}

/* ------------------------------------------------------------------ */
/* Simulation state                                                    */
/* ------------------------------------------------------------------ */
typedef struct {
    const zp_config *cfg;
    double p_surface;              /* bar at dive site */
    double bar_per_m;              /* water column pressure gradient */
    const double *n2a;             /* B or C coefficients */
    double pn2[ZP_COMPARTMENTS];
    double phe[ZP_COMPARTMENTS];
    double cns, otu;
    double runtime;                /* minutes */
    double depth;                  /* metres */
    /* current breathing source */
    double fo2, fhe; bool cc; double setpoint;
    /* accounting */
    double gas_l[ZP_MAX_GASES], gas_fo2[ZP_MAX_GASES], gas_fhe[ZP_MAX_GASES];
    int n_gas;
    double rmv;                    /* current RMV l/min */

    double gf_ref_depth;           /* first-stop depth anchoring the GF slope */
    double slide_peak, slide_t0;   /* Scamahorn slide state */
    double gf_force;               /* >0: override GF (NDL check) */
    bool   inter_stop;             /* this leg runs from one stop to the next */
    double es_delay_min;           /* cumulative extra-slow hold time (v1.8.0) */
    int    ncomp;                  /* active compartments: 17 Buhlmann, 12 VVAL */
    int    rmv_switched;           /* deco RMV engaged yet? */
    char *warn; size_t warnlen;
} sim;

static void warn(sim *s, const char *msg) {
    size_t used = strlen(s->warn);
    if (used + strlen(msg) + 2 < s->warnlen)
        snprintf(s->warn + used, s->warnlen - used, "%s\n", msg);
}

static double pamb(const sim *s, double depth_m) {
    return s->p_surface + depth_m * s->bar_per_m;
}
static double alveolar(double p_ambient) {
    double p = p_ambient - PH2O;
    return p > 0 ? p : 0;
}
/* inspired inert & O2 partial pressures for current source at depth */
static void inspired(const sim *s, double depth_m,
                     double *pin2, double *pihe, double *pio2) {
    double pa = alveolar(pamb(s, depth_m));
    if (s->cfg->use_vval) {                    /* USN convention: 1.85 fsw */
        pa = pamb(s, depth_m) - VVAL_H2O_BAR;
        if (pa < 0) pa = 0;
    }
    if (s->cc) {
        double po2 = s->setpoint;
        if (s->slide_peak > 0) {
            double rate = s->cfg->slide_rate > 0 ? s->cfg->slide_rate : 0.1;
            double e = s->slide_peak - rate * (s->runtime - s->slide_t0);
            if (e > po2) po2 = e;
        }
        if (po2 > pa) po2 = pa;               /* can't exceed ambient */
        double pinert = pa - po2;
        double ftot = s->fo2 + s->fhe;        /* diluent fractions */
        double fn2d = 1.0 - ftot;             /* diluent N2 fraction */
        double denom = fn2d + s->fhe;
        double rn2 = denom > 0 ? fn2d / denom : 1.0;
        *pin2 = pinert * rn2;
        *pihe = pinert * (1.0 - rn2);
        *pio2 = po2;
    } else {
        *pin2 = pa * (1.0 - s->fo2 - s->fhe);
        *pihe = pa * s->fhe;
        *pio2 = pa * s->fo2;
    }
}

/* one integration step of dt minutes at given depth */
static void tick(sim *s, double depth_m, double dt, bool is_bottom) {
    double pin2, pihe, pio2;
    inspired(s, depth_m, &pin2, &pihe, &pio2);
    (void)is_bottom;
    double pa_now = pamb(s, depth_m);
    for (int i = 0; i < s->ncomp; i++) {
        double dtn = dt, dth = dt;
        if (s->cfg->use_vval) {
            /* Thalmann EL-DCM: exponential uptake; during offgassing, a
             * compartment supersaturated beyond its crossover eliminates gas
             * at the constant rate k*Pcross (linear kinetics). */
            double kn2 = M_LN2 / VVAL_HT_N2[i];
            double supersat = (s->pn2[i] + s->phe[i]) - pa_now;
            double pcross = VVAL_PCROSS_FSW[i] * FSW2BAR;
            if (pin2 < s->pn2[i] && supersat > pcross) {
                double p = s->pn2[i] - kn2 * pcross * dt;
                s->pn2[i] = p > pin2 ? p : pin2;      /* never below inspired */
            } else {
                s->pn2[i] += (pin2 - s->pn2[i]) * (1.0 - exp(-kn2 * dtn));
            }
            double khe = M_LN2 / (VVAL_HT_N2[i] / VVAL_HE_RATIO);
            if (pihe < s->phe[i] && supersat > pcross) {
                double p = s->phe[i] - khe * pcross * dt;
                s->phe[i] = p > pihe ? p : pihe;
            } else {
                s->phe[i] += (pihe - s->phe[i]) * (1.0 - exp(-khe * dth));
            }
        } else {
            s->pn2[i] += (pin2 - s->pn2[i]) * (1.0 - exp(-M_LN2 * dtn / N2_HT[i]));
            s->phe[i] += (pihe - s->phe[i]) * (1.0 - exp(-M_LN2 * dth / HE_HT[i]));
        }
    }
    double cr, or_;
    o2_rates(pio2 / P_SEALEVEL, &cr, &or_);   /* table is defined in ATM */
    s->cns += cr * dt;
    s->otu += or_ * dt;
    /* OC gas consumption (litres at 1 ata) */
    if (!s->cc) {
        double litres = s->rmv * (pamb(s, depth_m) / P_SEALEVEL) * dt;
        int gi = -1;
        for (int i = 0; i < s->n_gas; i++)
            if (fabs(s->gas_fo2[i] - s->fo2) < 1e-6 &&
                fabs(s->gas_fhe[i] - s->fhe) < 1e-6) { gi = i; break; }
        if (gi < 0 && s->n_gas < ZP_MAX_GASES) {
            gi = s->n_gas++;
            s->gas_fo2[gi] = s->fo2; s->gas_fhe[gi] = s->fhe;
            s->gas_l[gi] = 0;
        }
        if (gi >= 0) s->gas_l[gi] += litres;
    }
    s->runtime += dt;
}

/* Tolerated ambient pressure (bar) for one compartment, with conservatism
 * applied as a reduction of the allowed Bühlmann supersaturation gradient
 * (comparable to a symmetric gradient factor of (100-c)%). At c=0 this is
 * exactly Bühlmann: tol = (P - a) * b. Reducing the gradient both deepens
 * the first stop and lengthens shallow stops, which matches ZPlan's
 * documented behaviour and its actual output. */
/* Current gradient factor: GF_lo until the first stop is known, then a
 * linear slope from GF_lo at the first-stop depth to GF_hi at the surface
 * (Erik Baker, "Clearing Up The Confusion About Deep Stops"). */
static double gf_at(const sim *s, double depth_m) {
    if (!s->cfg->use_gf) return 1.0;
    if (s->gf_force > 0) return s->gf_force;
    double lo = s->cfg->gf_lo, hi = s->cfg->gf_hi;
    if (lo <= 0) lo = 0.30;
    if (hi <= 0) hi = 0.85;
    if (s->gf_ref_depth <= 0) return lo;
    double d = depth_m;
    if (d > s->gf_ref_depth) d = s->gf_ref_depth;
    if (d < 0) d = 0;
    return hi + (lo - hi) * d / s->gf_ref_depth;
}

static double ceiling_bar(const sim *s) {
    /* Pure Buhlmann tolerated ambient pressure: tol = (Ptissue - a) * b,
     * with a/b weighted by the N2/He mix in each compartment.
     * Conservatism is applied on the loading side (see tick()): on-gassing
     * time is padded by the stated percentage, per ZPlan's documentation
     * ("tacks on the stated percentage of additional time at each
     * calculation waypoint for purposes of determining tissue loading"). */
    double worst = 0;
    if (s->cfg->use_vval) {
        /* MPTT(D) = MPTT0 + 1.0 * D (fsw); tolerated ambient pressure is
         * therefore surface pressure plus the excess tension over MPTT0. */
        for (int i = 0; i < VVAL_NC; i++) {
            double pt = s->pn2[i] + s->phe[i];
            double tol = s->p_surface + (pt - VVAL_MPTT0_FSW[i] * FSW2BAR);
            if (tol > worst) worst = tol;
        }
        return worst;
    }
    for (int i = 0; i < s->ncomp; i++) {
        double pt = s->pn2[i] + s->phe[i];
        if (pt <= 0) continue;
        double a = (s->pn2[i] * s->n2a[i] + s->phe[i] * HE_A[i]) / pt;
        double b = (s->pn2[i] * N2_B[i]  + s->phe[i] * HE_B[i])  / pt;
        double tol;
        if (s->cfg->use_gf) {
            double gf = gf_at(s, s->depth);
            /* tolerated ambient with the M-value gradient scaled by GF */
            tol = (pt - a * gf) / (gf / b - gf + 1.0);
        } else {
            tol = (pt - a) * b;
        }
        if (tol > worst) worst = tol;
    }
    return worst;
}

static double rate_for(const zp_rate_range *r, int n, double depth,
                       double fallback) {
    for (int i = 0; i < n; i++) {
        double lo = r[i].from_m < r[i].to_m ? r[i].from_m : r[i].to_m;
        double hi = r[i].from_m < r[i].to_m ? r[i].to_m : r[i].from_m;
        if (depth >= lo - 1e-9 && depth <= hi + 1e-9) return r[i].rate_m_min;
    }
    return fallback;
}

/* travel between depths at configured rates; is_ascent picks the table */
/* Largest off-gassing gradient over all compartments at the current depth:
 * tissue inert tension minus AMBIENT pressure, i.e. supersaturation.
 *
 * v1.8.0: this previously measured tension minus *inspired* inert pressure.
 * On an oxygen decompression gas the inspired inert pressure is zero, so that
 * quantity collapsed to the raw tissue tension (2-3 bar) and sat permanently
 * above the 1.25 bar threshold — at 6 m the ambient pressure alone is 1.6 bar,
 * so the test passed even for a diver with no supersaturation at all. The rule
 * could then never clear and always burned its entire delay budget. Measuring
 * supersaturation is both the intended rule and a quantity that actually falls
 * as the diver off-gasses, so the hold ends when the diver is ready to ascend. */
static double offgas_gradient_at(const sim *s, double target_m) {
    (void)target_m;
    double pa = pamb(s, s->depth), worst = 0;
    for (int i = 0; i < s->ncomp; i++) {
        double g = (s->pn2[i] + s->phe[i]) - pa;    /* supersaturation */
        if (g > worst) worst = g;
    }
    return worst;
}

static void travel(sim *s, double to_m, bool ascent) {
    double dir = (to_m > s->depth) ? 1.0 : -1.0;
    double delayed = 0;
    while (fabs(s->depth - to_m) > 1e-9) {
        /* Extra-slow ascent rule: while the off-gassing gradient of any
         * compartment exceeds the threshold, hold depth instead of rising.
         * The ascent to the next stop is therefore stretched, not the stop
         * before it; the ceiling is never crossed because this only ever
         * adds time at the deeper depth. */
        if (s->cfg->extra_slow && ascent && dir < 0 && s->inter_stop &&
            delayed < EXTRA_SLOW_MAX_DELAY_MIN &&
            offgas_gradient_at(s, to_m) > EXTRA_SLOW_GRADIENT_BAR) {
            tick(s, s->depth, DT, false);
            delayed += DT;
            s->es_delay_min += DT;   /* counted as deco time by the caller */
            continue;
        }
        double rate = ascent
            ? rate_for(s->cfg->ascent,  s->cfg->n_ascent,  s->depth, 10.0)
            : rate_for(s->cfg->descent, s->cfg->n_descent, s->depth, 20.0);
        if (rate < 1.0) rate = 1.0;
        double step = rate * DT;
        if (step > fabs(to_m - s->depth)) step = fabs(to_m - s->depth);
        /* integrate at mid-depth of this small step */
        double mid = s->depth + dir * step / 2.0;
        tick(s, mid, (step / rate), false);
        s->depth += dir * step;
    }
}

/* END: depth at which breathing "reference air" gives the same narcotic
 * inspired pressure. Reference narcotic fraction: 1.0 if O2 counted
 * narcotic (air = N2+O2), else 0.79 (N2 only). */
/* Equivalent Air (Nitrogen) Depth: the air depth giving the same inspired
 * N2 partial pressure.  EAD = (fN2 / 0.79) * (D + 10.06) - 10.06  (metric,
 * using the actual water column of this dive). */
static double ead_m(const sim *s, double depth_m) {
    double fn2 = s->cc ? 0 : (1.0 - s->fo2 - s->fhe);
    if (s->cc) {
        double pa = pamb(s, depth_m);
        double po2 = s->setpoint < pa ? s->setpoint : pa;
        double pinert = pa - po2;
        double ftot = s->fo2 + s->fhe, fn2d = 1.0 - ftot;
        double denom = fn2d + s->fhe;
        double pn2 = pinert * (denom > 0 ? fn2d / denom : 1.0);
        double d = (pn2 / 0.79 - s->p_surface) / s->bar_per_m;
        return d > 0 ? d : 0;
    }
    double surf_m = s->p_surface / s->bar_per_m;
    double d = (fn2 / 0.79) * (depth_m + surf_m) - surf_m;
    return d > 0 ? d : 0;
}

/* Breathing-gas density in g/l at depth (0 C, ideal gas). */
static double gas_density_gl(const sim *s, double depth_m) {
    double fo2 = s->fo2, fhe = s->fhe;
    if (s->cc) {
        double pa = pamb(s, depth_m);
        double po2 = s->setpoint < pa ? s->setpoint : pa;
        fo2 = po2 / pa;
        double rem = 1.0 - fo2, ftot = s->fo2 + s->fhe;
        double fn2d = 1.0 - ftot, denom = fn2d + s->fhe;
        fhe = rem * (denom > 0 ? s->fhe / denom : 0.0);
    }
    double fn2 = 1.0 - fo2 - fhe;
    double molar = fo2 * 31.999 + fn2 * 28.013 + fhe * 4.003;   /* g/mol */
    double p = pamb(s, depth_m);                                 /* bar    */
    return p * molar / (0.0831446 * 273.15);
}

static double end_m(const sim *s, double depth_m) {
    double pa = pamb(s, depth_m);
    double fnarc;
    if (s->cc) {
        /* approximate with actual inspired fractions */
        double pin2, pihe, pio2; inspired(s, depth_m, &pin2, &pihe, &pio2);
        double tot = pin2 + pihe + pio2;
        fnarc = tot > 0 ? (pin2 + (s->cfg->oxy_narc ? pio2 : 0)) / tot : 0;
        double ref = s->cfg->oxy_narc ? 1.0 : 0.79;
        double p_end = pa * fnarc / ref;
        double d = (p_end - s->p_surface) / s->bar_per_m;
        return d > 0 ? d : 0;
    }
    fnarc = (1.0 - s->fo2 - s->fhe) + (s->cfg->oxy_narc ? s->fo2 : 0.0);
    double ref = s->cfg->oxy_narc ? 1.0 : 0.79;
    double p_end = pa * fnarc / ref;
    double d = (p_end - s->p_surface) / s->bar_per_m;
    return d > 0 ? d : 0;
}

static double display_ppo2(const sim *s, double depth_m) {
    double pa = pamb(s, depth_m) / P_SEALEVEL;   /* ATA, as the original */
    if (s->cc) return s->setpoint < pa ? s->setpoint : pa;
    return pa * s->fo2;
}

/* pick best OC deco gas for a stop depth; returns index into cfg list
 * or -1 if none qualifies. ZPlan-style rounding slack on max PO2. */
static int best_deco_gas(const sim *s, double depth_m) {
    const zp_config *c = s->cfg;
    if (!c->use_oc_deco || c->n_oc_deco == 0) return -1;
    double pa = alveolar(pamb(s, depth_m));
    int best = -1; double best_fo2 = -1;
    for (int i = 0; i < c->n_oc_deco; i++) {
        double fo2 = c->oc_deco_fo2[i];
        double po2 = pa * fo2;
        if (po2 > c->oc_deco_max_po2 + 0.03) continue;    /* rounding slack */
        double fnarc = (1.0 - fo2) + (c->oxy_narc ? fo2 : 0.0);
        double ref = c->oxy_narc ? 1.0 : 0.79;
        double p_end = pamb(s, depth_m) * fnarc / ref;
        double endd = (p_end - s->p_surface) / s->bar_per_m;
        if (endd > c->max_end_m + 1e-6) continue;
        if (fo2 > best_fo2) { best_fo2 = fo2; best = i; }
    }
    return best;
}

/* deco setpoint for a depth (CC deco), or -1 if none applies */
static double deco_setpoint_for(const zp_config *c, double depth) {
    if (!c->use_deco_setpoint) return -1;
    for (int i = 0; i < c->n_deco_sp; i++) {
        double lo = c->deco_sp[i].to_m, hi = c->deco_sp[i].from_m;
        if (lo > hi) { double t = lo; lo = hi; hi = t; }
        if (depth >= lo - 1e-9 && depth <= hi + 1e-9)
            return c->deco_sp[i].setpoint;
    }
    return -1;
}

/* choose breathing source for deco at a given depth (mutates s) */
static void select_deco_source(sim *s, double depth) {
    double sp = deco_setpoint_for(s->cfg, depth);
    if (sp > 0) { s->cc = true; s->setpoint = sp; return; }
    if (sp == 0) s->cc = false; /* explicit OC range */
    int g = best_deco_gas(s, depth);
    if (g >= 0) {
        s->cc = false;
        s->fo2 = s->cfg->oc_deco_fo2[g];
        s->fhe = 0.0;
    }
    /* else: keep whatever we were breathing (bottom mix / setpoint) */
}


/* Predictive leave criterion (Readme sec.10 of the original): a stop may be
 * left when the PLANNED ascent from here to `to_m` — at the user's ascent
 * rates, with off-gassing credited along the way — never takes the diver
 * shallower than the instantaneous ceiling. With slow shallow ascent rates,
 * stops shorten or vanish, exactly as the original advertises. */
static bool can_leave(const sim *s, double to_m) {
    /* Standard criterion (MultiDeco / Subsurface / TechDeco): the diver may
     * ascend only when the INSTANTANEOUS ceiling is already shallower than
     * the target depth. ZPlan's original predictive rule credited the
     * off-gassing that happens during the ascent itself, which produced
     * shallower first stops and ~5 min shorter schedules; it is kept behind
     * AscentCredit: y for compatibility with old plans. */
    if (!getenv("ZP_ASCENT_CREDIT") && !s->cfg->ascent_credit) {
        double ceil_d = (ceiling_bar(s) - s->p_surface) / s->bar_per_m;
        return to_m >= ceil_d - 1e-6;
    }
    /* Experimental extra-slow rule: hold while any compartment's
     * off-gassing gradient at the next stop would exceed 1.25 bar. */
    sim c = *s;                       /* clone; totals in it are discarded */
    /* NDL determination: a direct ascent to the surface with no stops yet
     * is judged at GF-high (standard) unless NdlGF: low is set. */
    if (s->cfg->use_gf && s->gf_ref_depth <= 0 && to_m <= 1e-9 && !s->cfg->ndl_gf_low)
        c.gf_force = (s->cfg->gf_hi > 0 ? s->cfg->gf_hi : 0.85);
    while (c.depth - to_m > 1e-9) {
        double rate = rate_for(c.cfg->ascent, c.cfg->n_ascent, c.depth, 10.0);
        double step = rate * DT;
        if (step > c.depth - to_m) step = c.depth - to_m;
        double mid = c.depth - step / 2.0;
        tick(&c, mid, step / rate, false);
        c.depth -= step;
        double ceil_d = (ceiling_bar(&c) - c.p_surface) / c.bar_per_m;
        if (c.depth < ceil_d - 1e-3) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* The planner                                                         */
/* ------------------------------------------------------------------ */
typedef struct { double depth; double time_min; } extra_stop;

static double round_to_stop(double d, double interval) {
    double n = ceil(d / interval - 1e-9);
    return n * interval;
}

static int run_plan(const zp_config *cfg, zp_result *out,
                    const extra_stop *deep, int n_deep,
                    const extra_stop *min_hold, int n_min,
                    double *first_norm_stop_out) {
    sim S; memset(&S, 0, sizeof S);
    sim *s = &S;
    s->cfg = cfg;
    s->warn = out->warnings; s->warnlen = sizeof out->warnings;
    s->p_surface = P_SEALEVEL *
        pow(1.0 - 2.25577e-5 * cfg->altitude_m, 5.25588);
    s->bar_per_m = (cfg->salt_water ? RHO_SALT : RHO_FRESH) * G_ACC / 1e5;
    s->n2a = N2_A_C;            /* v1.5: ZHL-16C only */
    vval_pcross_env();
    s->ncomp = cfg->use_vval ? VVAL_NC
             : (cfg->use_1b ? ZP_COMPARTMENTS : ZP_COMPARTMENTS - 1);

    s->gf_ref_depth = -1.0;

    /* initial tissues */
    double pn2_sat = alveolar(s->p_surface) * 0.79;
    if (cfg->use_vval) pn2_sat = (s->p_surface - VVAL_H2O_BAR) * 0.79;
    for (int i = 0; i < ZP_COMPARTMENTS; i++) {
        s->pn2[i] = cfg->have_initial_tissues ? cfg->init_pn2[i] : pn2_sat;
        s->phe[i] = cfg->have_initial_tissues ? cfg->init_phe[i] : 0.0;
    }
    s->cns = cfg->have_initial_tissues ? cfg->init_cns_pct : 0.0;
    if (cfg->have_initial_tissues && cfg->surface_interval_min > 0) {
        double t = cfg->surface_interval_min;
        for (int i = 0; i < s->ncomp; i++) {
            double htn = cfg->use_vval ? VVAL_HT_N2[i] : N2_HT[i];
            double hth = cfg->use_vval ? VVAL_HT_N2[i]/VVAL_HE_RATIO : HE_HT[i];
            s->pn2[i] = pn2_sat + (s->pn2[i]-pn2_sat)*exp(-M_LN2*t/htn);
            s->phe[i] = s->phe[i]*exp(-M_LN2*t/hth);
        }
        s->cns *= exp(-M_LN2 * t / 90.0);   /* 90-min O2 half-time */
    }
    /* Conservatism (0-50 %): preload the compartments with additional inert
     * gas, weighted from fast (none) to slow (full percentage) — as if a
     * previous dive had been made. When the planned profile uses trimix,
     * the extra inert gas is split between N2 and He in the proportion of
     * the deepest waypoint's inert fractions. */
    if (cfg->conservatism_pct > 0 && !(cfg->use_gf && !cfg->use_vval)) {
        double c = cfg->conservatism_pct / 100.0;
        if (c > 0.5) c = 0.5;
        double fn2 = 1.0, fhe = 0.0, dmax = -1.0;
        for (int w = 0; w < cfg->n_wp; w++)
            if (cfg->wp[w].depth_m > dmax) {
                dmax = cfg->wp[w].depth_m;
                double in2 = 1.0 - cfg->wp[w].fo2 - cfg->wp[w].fhe;
                double tot = in2 + cfg->wp[w].fhe;
                fn2 = tot > 0 ? in2 / tot : 1.0;
                fhe = 1.0 - fn2;
            }
        for (int i = 0; i < s->ncomp; i++) {
            double w = s->ncomp > 1 ? (double)i / (s->ncomp - 1) : 1.0;
            double extra = pn2_sat * c * w;
            s->pn2[i] += extra * fn2;
            s->phe[i] += extra * fhe;
        }
    }

    /* v1.8.1: the two VVAL-18 notes ("gradient factors ignored" and "helium
     * halftimes are an unvalidated approximation") are suppressed at the owner's
     * request, matching the personal-use policy already applied to the legal
     * disclaimer boilerplate. The underlying caveats still stand and are
     * documented in czplan.h and the README; they are simply no longer printed
     * in the plan. Nothing about the schedule itself changes. */
    if (cfg->use_deco_setpoint) {
        bool any_cc = false;
        for (int w = 0; w < cfg->n_wp; w++) if (cfg->wp[w].cc) any_cc = true;
        if (!any_cc) ((zp_config *)cfg)->use_deco_setpoint = false; /* OC dive */
    }
    out->n_lines = 0; out->total_deco_min = 0;
    s->depth = 0; s->runtime = 0; s->rmv = cfg->rmv_l_min;

    if (cfg->n_wp == 0) return -1;

    /* ---------------- bottom portion: waypoints ---------------- */
    for (int w = 0; w < cfg->n_wp; w++) {
        const zp_waypoint *wp = &cfg->wp[w];
        bool going_deeper = wp->depth_m > s->depth;
        if (going_deeper) {
            /* switch on leaving previous waypoint */
            s->fo2 = wp->fo2; s->fhe = wp->fhe;
            s->cc = wp->cc; s->setpoint = wp->setpoint;
            travel(s, wp->depth_m, false);
        } else {
            travel(s, wp->depth_m, true);
            s->fo2 = wp->fo2; s->fhe = wp->fhe;
            s->cc = wp->cc; s->setpoint = wp->setpoint;
        }
        /* Scamahorn Slide: PO2 starts at the slide maximum and declines by
         * SlideRate per minute until it reaches the setpoint. */
        s->slide_peak = 0;
        if (wp->slide_max > wp->setpoint && wp->cc) {
            s->slide_peak = wp->slide_max;
            s->slide_t0 = s->runtime;
        }
        double t = wp->time_min;
        while (t > 1e-9) {
            double dt = t < DT ? t : DT;
            tick(s, s->depth, dt, true);
            t -= dt;
        }
        if (out->n_lines < ZP_MAX_PLAN_LINES) {
            zp_plan_line *L = &out->lines[out->n_lines++];
            L->kind = ZP_LINE_WAYPOINT; L->depth_m = wp->depth_m;
            L->stop_sec = wp->time_min * 60.0;
            L->runtime_min = s->runtime;
            L->fo2 = s->fo2; L->fhe = s->fhe; L->cc = s->cc;
            L->setpoint = s->setpoint;
            L->ppo2 = display_ppo2(s, s->depth);
            L->end_m = end_m(s, s->depth);
                L->ead_m = ead_m(s, s->depth);
        }
    }
    double max_depth = 0;
    for (int w = 0; w < cfg->n_wp; w++)
        if (cfg->wp[w].depth_m > max_depth) max_depth = cfg->wp[w].depth_m;

    /* ---------------- ascent & decompression ----------------
       (deco RMV engages on arrival at the first stop, per original) */
    double stop_iv = cfg->stop_distance_m > 0 ? cfg->stop_distance_m : 3.0;
    double last_stop = cfg->last_stop_depth_m > 0 ? cfg->last_stop_depth_m : 3.0;
    double first_norm = -1;
    int deep_idx = 0;

    double leg_start = s->runtime;
    while (s->depth > 1e-9) {
        /* mandated deep stop below us? honour it first */
        double next_deep = (deep_idx < n_deep) ? deep[deep_idx].depth : -1;
        if (next_deep > 0 && next_deep < s->depth - 1e-9) {
            travel(s, next_deep, true);
            if (!s->rmv_switched) { s->rmv = cfg->deco_rmv_l_min; s->rmv_switched = 1; }
            double t = deep[deep_idx].time_min;
            while (t > 1e-9) { double dt = t<DT?t:DT; tick(s,s->depth,dt,false); t-=dt; }
            if (out->n_lines < ZP_MAX_PLAN_LINES) {
                zp_plan_line *L = &out->lines[out->n_lines++];
                L->kind = ZP_LINE_DEEPSTOP; L->depth_m = s->depth;
                L->stop_sec = deep[deep_idx].time_min * 60.0;
                L->runtime_min = s->runtime;
                L->fo2 = s->fo2; L->fhe = s->fhe; L->cc = s->cc;
                L->setpoint = s->setpoint;
                L->ppo2 = display_ppo2(s, s->depth);
                L->end_m = end_m(s, s->depth);
                L->ead_m = ead_m(s, s->depth);
            }
            out->total_deco_min += deep[deep_idx].time_min;
            deep_idx++;
            continue;
        }

        /* next stop-grid depth below the current one (…, 2*iv, iv chain
           down to last_stop, then surface) */
        double g;
        if (s->depth <= last_stop + 1e-9) g = 0;
        else {
            g = ceil(s->depth / stop_iv - 1.0 - 1e-9) * stop_iv;
            if (g < last_stop) g = last_stop;
            if (g >= s->depth - 1e-9) g = s->depth - stop_iv;
            if (g < 0) g = 0;
        }

        /* may we ascend to g right now, given the planned ascent? */
        if (can_leave(s, g)) {
            travel(s, g, true);
            if (g <= 1e-9) break;               /* surfaced */
            continue;                            /* passing through, no stop */
        }

        /* we must stop here (snap to grid if mid-water) */
        double here = round_to_stop(s->depth, stop_iv);
        if (here < last_stop) here = last_stop;
        if (fabs(here - s->depth) > 1e-6) travel(s, here, true);

        if (!s->rmv_switched) { s->rmv = cfg->deco_rmv_l_min; s->rmv_switched = 1; }
        if (s->gf_ref_depth <= 0) s->gf_ref_depth = here;
        select_deco_source(s, s->depth);

        double quantum = 1.0;                    /* whole-minute stops */
        if (here <= last_stop + 1e-9 && cfg->deepstops != ZP_DEEPSTOPS_NONE)
            quantum = 0.1;                       /* 6 s final stop, deep mode */
        double min_time = 0;
        for (int mi = 0; mi < n_min; mi++)
            if (fabs(min_hold[mi].depth - here) < 1e-6)
                min_time = min_hold[mi].time_min;

        double arrive_rt = s->runtime;   /* for whole-minute rounding below */
        double stop_time = 0;
        double gg = (here <= last_stop + 1e-9) ? 0
                  : (here - stop_iv < last_stop ? last_stop : here - stop_iv);
        while ((!can_leave(s, gg) || stop_time < min_time - 1e-9)
               && stop_time < 24.0*60.0) {
            double t = quantum;
            while (t > 1e-9) { double dt = t<DT?t:DT; tick(s,s->depth,dt,false); t-=dt; }
            stop_time += quantum;
            /* once the model itself is satisfied, further time is extra-slow */
        }

        /* A zero-length stop can occur when the GF slope anchors here and
         * the now-sloped look-ahead immediately clears; the anchor (set
         * above, conservative) is kept, but no stop line is emitted. */
        /* MultiDeco / TechDeco convention: the ascent leg into a stop counts
         * toward that stop, and the total is rounded up to a whole minute. */
        if (stop_time > 1e-9 && !cfg->use_vval) {
            double leg = arrive_rt - leg_start;
            double total = leg + stop_time;
            double want = ceil(total - 1e-9);
            double pad = want - total;
            while (pad > 1e-9) { double dt = pad<DT?pad:DT; tick(s,s->depth,dt,false); pad-=dt; }
            stop_time += (want - total);
            out->total_deco_min += (want - total);
        }
        if (stop_time > 1e-9 && first_norm < 0) first_norm = here;
        if (stop_time > 1e-9 && out->decozone_start_m <= 0)
            out->decozone_start_m = here;
        int norm_idx = -1;
        if (stop_time > 1e-9 && out->n_lines < ZP_MAX_PLAN_LINES) {
            norm_idx = out->n_lines;
            zp_plan_line *L = &out->lines[out->n_lines++];
            L->kind = ZP_LINE_NORMSTOP; L->depth_m = here;
            L->stop_sec = stop_time * 60.0;
            L->runtime_min = s->runtime;
            L->fo2 = s->fo2; L->fhe = s->fhe; L->cc = s->cc;
            L->setpoint = s->setpoint;
            L->ppo2 = display_ppo2(s, here);
            L->end_m = end_m(s, here);
            L->ead_m = ead_m(s, here);
        }
        out->total_deco_min += stop_time;

        leg_start = s->runtime;
        s->inter_stop = (gg > 1e-9 && stop_time > 1e-9);
        /* only between real stops: never on the run-up from the bottom, and
         * never on the final ascent to the surface */
        {   /* v1.8.0: time spent held by the extra-slow rule is decompression
             * time and must be reported as such. Previously only stop_time was
             * counted, so switching the rule on made TOTAL DECO TIME *fall*
             * while the diver actually stayed under water considerably longer. */
            double es_before = s->es_delay_min;
            travel(s, gg, true);
            double held = s->es_delay_min - es_before;
            out->total_deco_min += held;

            /* v1.8.2: charge the hold to the stop it happens at, not to the
             * ascent leg. The rule holds depth at the stop before rising, but
             * the delay used to land in the travel line, which then read as a
             * 6:01 ascent over 3 m — alarming and physically misleading. It now
             * shows as extra time at the stop, and the ascent reads normally. */
            if (held > 1e-9 && norm_idx >= 0) {
                out->lines[norm_idx].stop_sec    += held * 60.0;
                out->lines[norm_idx].runtime_min += held;
            }
        }
        s->inter_stop = false;
        if (gg <= 1e-9) break;
    }

    {   /* gas density at the deepest point of the dive */
        double dmax = 0; int wmax = -1;
        for (int w = 0; w < cfg->n_wp; w++)
            if (cfg->wp[w].depth_m > dmax) { dmax = cfg->wp[w].depth_m; wmax = w; }
        if (wmax >= 0) {
            sim t = *s;
            t.fo2 = cfg->wp[wmax].fo2; t.fhe = cfg->wp[wmax].fhe;
            t.cc = cfg->wp[wmax].cc;   t.setpoint = cfg->wp[wmax].setpoint;
            out->bottom_density_gl = gas_density_gl(&t, dmax);
        }
    }
    out->runtime_min = s->runtime;
    out->cns_pct = s->cns;
    out->otu = s->otu;
    out->n_gas_used = s->n_gas;
    out->total_oc_l = 0;
    for (int i = 0; i < s->n_gas; i++) {
        out->gas_used_l[i] = s->gas_l[i];
        out->gas_fo2[i] = s->gas_fo2[i];
        out->gas_fhe[i] = s->gas_fhe[i];
        out->total_oc_l += s->gas_l[i];
    }
    memcpy(out->end_pn2, s->pn2, sizeof s->pn2);
    memcpy(out->end_phe, s->phe, sizeof s->phe);
    out->end_cns_pct = s->cns;

    /* ------- time to fly: off-gas at surface on air until the ceiling
       tolerates 10,000 ft cabin pressure ------- */
    {
        double p_cabin = pow(1.0 - 2.25577e-5 * FLY_ALT_M, 5.25588); /* bar,
                     matches original's stricter (unit-conflated) threshold */
        double pn2s = alveolar(s->p_surface) * 0.79;
        double mins = 0;
        while (mins < 96.0 * 60.0) {
            if (ceiling_bar(s) <= p_cabin) break;
            double step = 3.0; /* minutes */
            for (int i = 0; i < ZP_COMPARTMENTS; i++) {
                s->pn2[i] = pn2s + (s->pn2[i]-pn2s)*exp(-M_LN2*step/N2_HT[i]);
                s->phe[i] = s->phe[i]*exp(-M_LN2*step/HE_HT[i]);
            }
            mins += step;
        }
        out->time_to_fly_hr = ceil((mins + 30.0) / 30.0 - 1e-9) / 2.0;
        if (out->time_to_fly_hr < 0.5) out->time_to_fly_hr = 0.5;
    }

    if (first_norm_stop_out) *first_norm_stop_out = first_norm;
    return 0;
}

int zp_plan(const zp_config *cfg, zp_result *out) {
    /* With gradient factors, GF-low provides the deep-stop function; the
     * Pyle post-process is disabled (owner spec, v1.4). */
    if (cfg->use_gf && !cfg->use_vval && cfg->deepstops != ZP_DEEPSTOPS_NONE)
        ((zp_config *)cfg)->deepstops = ZP_DEEPSTOPS_NONE;
    memset(out, 0, sizeof *out);
    extra_stop deep[32]; int n_deep = 0;
    extra_stop base[32]; int n_base = 0;

    if (cfg->deepstops != ZP_DEEPSTOPS_NONE) {
        /* Pass 1: straight schedule — its stops become minimum holds, and
         * its first normal stop seeds Pyle's mean-depth rule. The original
         * then iterates: insert a stop, re-run, re-read the first normal
         * stop, insert the next mean — so we do the same. */
        double max_depth = 0;
        for (int w = 0; w < cfg->n_wp; w++)
            if (cfg->wp[w].depth_m > max_depth) max_depth = cfg->wp[w].depth_m;
        double stop_iv = cfg->stop_distance_m > 0 ? cfg->stop_distance_m : 3.0;
        double t_each = (cfg->deepstops == ZP_DEEPSTOPS_PYLE)
                      ? (cfg->pyle_stop_min > 0 ? cfg->pyle_stop_min : 2.0)
                      : 1.0;   /* WKPP-style short stops (approximation) */

        zp_result tmp; double first_norm = -1;
        int r = run_plan(cfg, &tmp, NULL, 0, NULL, 0, &first_norm);
        if (r) return r;
        for (int i = 0; i < tmp.n_lines && n_base < 32; i++)
            if (tmp.lines[i].kind == ZP_LINE_NORMSTOP) {
                base[n_base].depth = tmp.lines[i].depth_m;
                base[n_base].time_min = tmp.lines[i].stop_sec / 60.0;
                n_base++;
            }

        double bottom = max_depth;
        while (first_norm > 0 && n_deep < 16 &&
               bottom - first_norm > 3.0 * stop_iv + 1e-9) {
            double d = round_to_stop((bottom + first_norm) / 2.0, stop_iv);
            if (d >= bottom - 1e-9 || d <= first_norm + 1e-9) break;
            deep[n_deep].depth = d;
            deep[n_deep].time_min = t_each;
            n_deep++;
            bottom = d;
            /* re-run with the stops so far; the first normal stop may
             * shift, changing where the next mean lands */
            double fn2 = -1;
            if (run_plan(cfg, &tmp, deep, n_deep, base, n_base, &fn2)) break;
            if (fn2 > 0) first_norm = fn2;
        }
        /* sort deepest-first for the ascent loop */
        for (int i = 0; i < n_deep; i++)
            for (int j = i + 1; j < n_deep; j++)
                if (deep[j].depth > deep[i].depth) {
                    extra_stop t2 = deep[i]; deep[i] = deep[j]; deep[j] = t2;
                }

        /* Legacy final-stop expansion: the original re-applies its
         * conservatism padding on each Pyle iteration, so its final stop
         * grows by roughly (deep-stop time) x (1 + 5*cons). We adopt that
         * as a MINIMUM on the last stop (the ceiling may demand more),
         * so schedules are never shorter than the reference engine's. */
        {
            double deep_total = 0;
            for (int i = 0; i < n_deep; i++) deep_total += deep[i].time_min;
            double last_stop = cfg->last_stop_depth_m > 0 ? cfg->last_stop_depth_m : 3.0;
            double factor = 1.0;
            int found = -1;
            for (int i = 0; i < n_base; i++)
                if (fabs(base[i].depth - last_stop) < 1e-6) found = i;
            if (found >= 0) base[found].time_min += deep_total * factor;
            else if (n_base < 32 && deep_total > 0) {
                base[n_base].depth = last_stop;
                base[n_base].time_min = deep_total * factor;
                n_base++;
            }
        }
        memset(out, 0, sizeof *out);
    }
    double fn;
    int r = run_plan(cfg, out, deep, n_deep, base, n_base, &fn);
    return r;
}

/* ------------------------------------------------------------------ */
/* profile.dat parser (ZPlan dialect)                                  */
/* ------------------------------------------------------------------ */
void zp_config_init(zp_config *cfg) {
    memset(cfg, 0, sizeof *cfg);
    cfg->metric_output = false;
    cfg->salt_water = true;
    cfg->use_b_values = false;
    cfg->conservatism_pct = 0;
    cfg->stop_distance_m = 3.048;   /* 10 ft */
    cfg->last_stop_depth_m = 3.048;
    cfg->deepstops = ZP_DEEPSTOPS_NONE;
    cfg->pyle_stop_min = 2.0;
    cfg->rmv_l_min = 0.6 * L_PER_CUFT;
    cfg->deco_rmv_l_min = 0.6 * L_PER_CUFT;
    cfg->oc_deco_max_po2 = 1.55;
    cfg->max_end_m = 130 / FT_PER_M;
}

static bool truthy(const char *v) { return *v=='y'||*v=='Y'||*v=='1'; }

static void trim(char *s) {
    char *p = s; while (isspace((unsigned char)*p)) p++;
    memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (isspace((unsigned char)s[n-1]) || s[n-1]=='\r')) s[--n] = 0;
}

int zp_parse_profile(const char *text, zp_config *cfg,
                     char *err, size_t errlen) {
    zp_config_init(cfg);
    bool metric = false;
    double d2m = 1.0 / FT_PER_M;         /* depth unit -> metres */
    double v2l = L_PER_CUFT;             /* RMV unit -> litres */
    cfg->rmv_metric = -1;                /* follow UseMetric unless overridden */
    char line[512];
    const char *p = text;
    int lineno = 0;
    while (*p) {
        size_t n = 0;
        while (p[n] && p[n] != '\n' && n < sizeof line - 1) n++;
        memcpy(line, p, n); line[n] = 0;
        p += n; if (*p == '\n') p++;
        lineno++;
        trim(line);
        if (!line[0] || line[0] == '#') continue;

        char *colon = strchr(line, ':');
        bool is_kv = false;
        if (colon) {
            /* keys are alphabetic; waypoint lines start with a digit */
            is_kv = isalpha((unsigned char)line[0]);
        }
        if (is_kv) {
            *colon = 0;
            char key[64]; strncpy(key, line, sizeof key - 1); key[63] = 0;
            trim(key);
            char *val = colon + 1; trim(val);
            for (char *k = key; *k; k++) *k = (char)tolower((unsigned char)*k);

            if (!strcmp(key, "usemetric")) {
                metric = truthy(val);
                cfg->metric_output = metric;
                d2m = metric ? 1.0 : 1.0 / FT_PER_M;
                v2l = metric ? 1.0 : L_PER_CUFT;
            }
            else if (!strcmp(key, "saltwater")) cfg->salt_water = truthy(val);
            else if (!strcmp(key, "usebvalues")) { /* v1.5: ZHL-16C only */ }
            else if (!strcmp(key, "debuglevel")) { /* ignored */ }
            else if (!strcmp(key, "altitude")) cfg->altitude_m = atof(val) * d2m;
            else if (!strcmp(key, "conservatism")) {
                cfg->conservatism_pct = atof(val);
                if (cfg->conservatism_pct > 50) cfg->conservatism_pct = 50;
                if (cfg->conservatism_pct < 0) cfg->conservatism_pct = 0;
            }
            else if (!strcmp(key, "precision")) { /* engine is always exact */ }
            else if (!strcmp(key, "stopdistance")) cfg->stop_distance_m = atof(val) * d2m;
            else if (!strcmp(key, "laststopdepth")) cfg->last_stop_depth_m = atof(val) * d2m;
            else if (!strcmp(key, "oxynarc")) cfg->oxy_narc = truthy(val);
            else if (!strcmp(key, "deepstops")) {
                if (*val=='p'||*val=='P') cfg->deepstops = ZP_DEEPSTOPS_PYLE;
                else if (*val=='w'||*val=='W') cfg->deepstops = ZP_DEEPSTOPS_PYLE; /* WKPP removed */
                else cfg->deepstops = ZP_DEEPSTOPS_NONE;
            }
            else if (!strcmp(key, "pylestoptime")) cfg->pyle_stop_min = atof(val);
            else if (!strcmp(key, "tissuefile")) { /* handled by caller */ }
            else if (!strcmp(key, "surfaceinterval")) {
                int h = 0, m = 0;
                if (sscanf(val, "%d:%d", &h, &m) >= 1)
                    cfg->surface_interval_min = h * 60.0 + m;
            }
            else if (!strcmp(key, "planfile")) { /* handled by caller */ }
            else if (!strcmp(key, "rmv")) cfg->rmv_l_min = atof(val) *
                (cfg->rmv_metric < 0 ? v2l : (cfg->rmv_metric ? 1.0 : L_PER_CUFT));
            else if (!strcmp(key, "decormv")) cfg->deco_rmv_l_min = atof(val) *
                (cfg->rmv_metric < 0 ? v2l : (cfg->rmv_metric ? 1.0 : L_PER_CUFT));
            else if (!strcmp(key, "usegf")) cfg->use_gf = truthy(val);
            else if (!strcmp(key, "gradientfactors")) {
                double lo = 0, hi = 0;
                if (sscanf(val, "%lf , %lf", &lo, &hi) == 2) {
                    /* accept "45, 85" or "0.45, 0.85"; any positive values,
                     * including above 100 % (beyond the raw Buhlmann ceiling) */
                    cfg->gf_lo = lo > 1.5 ? lo / 100.0 : lo;
                    cfg->gf_hi = hi > 1.5 ? hi / 100.0 : hi;
                    if (cfg->gf_lo > 0 && cfg->gf_hi > 0) cfg->use_gf = true;
                }
            }
            else if (!strcmp(key, "gflow"))  { cfg->gf_lo = atof(val) > 1.0 ? atof(val)/100.0 : atof(val); cfg->use_gf = true; }
            else if (!strcmp(key, "model")) {
                if (!strncmp(val, "vval", 4)) cfg->use_vval = 1;
                else cfg->use_vval = 0;      /* any Buhlmann request -> ZHL-16C */
                cfg->use_b_values = false;
            }
            else if (!strcmp(key, "rmvmetric")) cfg->rmv_metric = truthy(val) ? 1 : 0;
            else if (!strcmp(key, "extraslow")) cfg->extra_slow = truthy(val);
            else if (!strcmp(key, "ascentcredit")) cfg->ascent_credit = truthy(val);
            else if (!strcmp(key, "compartment1b")) cfg->use_1b = truthy(val);
            else if (!strcmp(key, "ndlgf")) cfg->ndl_gf_low = (*val=='l'||*val=='L');
            else if (!strcmp(key, "gfhigh")) { cfg->gf_hi = atof(val) > 1.0 ? atof(val)/100.0 : atof(val); cfg->use_gf = true; }
            else if (!strcmp(key, "sliderate")) cfg->slide_rate = atof(val);
            else if (!strcmp(key, "useocdeco")) cfg->use_oc_deco = truthy(val);
            else if (!strcmp(key, "ocdecogas")) {
                cfg->n_oc_deco = 0;
                char *tok = strtok(val, ",");
                while (tok && cfg->n_oc_deco < ZP_MAX_GASES) {
                    trim(tok);
                    if (*tok) cfg->oc_deco_fo2[cfg->n_oc_deco++] = atof(tok)/100.0;
                    tok = strtok(NULL, ",");
                }
            }
            else if (!strcmp(key, "ocdecomaxpo2")) cfg->oc_deco_max_po2 = atof(val);
            else if (!strcmp(key, "maxend")) cfg->max_end_m = atof(val) * d2m;
            else if (!strcmp(key, "descentrate") || !strcmp(key, "ascentrate")) {
                double a, b, r;
                if (sscanf(val, "%lf-%lf,%lf", &a, &b, &r) == 3 ||
                    sscanf(val, "%lf - %lf , %lf", &a, &b, &r) == 3) {
                    zp_rate_range *dst; int *cnt;
                    if (key[0]=='d') { dst = cfg->descent; cnt = &cfg->n_descent; }
                    else             { dst = cfg->ascent;  cnt = &cfg->n_ascent; }
                    if (*cnt < ZP_MAX_RATES) {
                        dst[*cnt].from_m = a * d2m;
                        dst[*cnt].to_m   = b * d2m;
                        dst[*cnt].rate_m_min = r * d2m;
                        (*cnt)++;
                    }
                }
            }
            else if (!strcmp(key, "usedecosetpoint")) cfg->use_deco_setpoint = truthy(val);
            else if (!strcmp(key, "decosetpoint")) {
                double a, b, sp;
                if (sscanf(val, "%lf-%lf,%lf", &a, &b, &sp) == 3 &&
                    cfg->n_deco_sp < ZP_MAX_SETPOINTS) {
                    cfg->deco_sp[cfg->n_deco_sp].from_m = a * d2m;
                    cfg->deco_sp[cfg->n_deco_sp].to_m   = b * d2m;
                    cfg->deco_sp[cfg->n_deco_sp].setpoint = sp;
                    cfg->n_deco_sp++;
                }
            }
            else {
                if (err) snprintf(err, errlen,
                    "line %d: unknown key '%s' (ignored)", lineno, key);
            }
        } else if (isdigit((unsigned char)line[0])) {
            /* waypoint: depth, time, o2(/He)(, setpoint(-slide)) */
            if (cfg->n_wp >= ZP_MAX_WAYPOINTS) continue;
            zp_waypoint *w = &cfg->wp[cfg->n_wp];
            memset(w, 0, sizeof *w);
            char buf[512]; strncpy(buf, line, sizeof buf - 1); buf[511] = 0;
            char *f1 = strtok(buf, ",");
            char *f2 = strtok(NULL, ",");
            char *f3 = strtok(NULL, ",");
            char *f4 = strtok(NULL, ",");
            if (!f1 || !f2 || !f3) {
                if (err) snprintf(err, errlen, "line %d: bad waypoint", lineno);
                return -1;
            }
            w->depth_m = atof(f1) * d2m;
            w->time_min = atof(f2);
            trim(f3);
            char *slash = strchr(f3, '/');
            w->fo2 = atof(f3) / 100.0;
            w->fhe = slash ? atof(slash + 1) / 100.0 : 0.0;
            if (f4) {
                trim(f4);
                char *dash = strchr(f4, '-');
                w->cc = true;
                w->setpoint = atof(f4);
                if (dash) w->slide_max = atof(dash + 1);
            }
            cfg->n_wp++;
        }
    }
    if (cfg->n_wp == 0) {
        if (err) snprintf(err, errlen, "no dive waypoints found");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Report                                                              */
/* ------------------------------------------------------------------ */
int zp_report(const zp_config *cfg, const zp_result *res,
              char *buf, size_t buflen) {
    size_t off = 0;
    #define APP(...) do { \
        int _n = snprintf(buf + off, buflen > off ? buflen - off : 0, __VA_ARGS__); \
        if (_n > 0) off += (size_t)_n; } while (0)

    bool metric = cfg->metric_output;
    double dscale = metric ? 1.0 : FT_PER_M;
    const char *du = metric ? "m" : "ft";

    APP("                          v%s\n", zp_version());
    if (cfg->use_vval)
        APP("              U.S. Navy EL-DCM (VVAL-18)\n\n");
    else if (cfg->use_gf)
        APP("     Buhlmann ZHL-16C + gradient factors %.0f/%.0f\n\n",
            (cfg->gf_lo > 0 ? cfg->gf_lo : 0.30) * 100.0,
            (cfg->gf_hi > 0 ? cfg->gf_hi : 0.85) * 100.0);
    else
        APP("                  Buhlmann ZHL-16C\n\n");
    APP("                        DIVE PLAN\n\n");
    /* Columns: symbol · depth · stop · run · gas.
     *   \u2193 descent   \u2191 ascent   \u2014 stop   DStop deep stop
     * Short ascents between stops without a gas switch are folded into the
     * following stop's duration (owner spec, v1.4). */
    APP("        depth   stop    run   gas         PO2   EAD\n");
    APP(" --------------------------------------------------------\n");
    {
        double prev_end = 0, prev_depth = 0;
        double pfo2 = -1, pfhe = -1, psp = -1; int pcc = -1;
        for (int i = 0; i < res->n_lines; i++) {
            const zp_plan_line *L = &res->lines[i];
            double arrive = L->runtime_min - L->stop_sec / 60.0;
            double travel = arrive - prev_end;
            bool gas_changed = (fabs(L->fo2 - pfo2) > 1e-6 ||
                                fabs(L->fhe - pfhe) > 1e-6 ||
                                (int)L->cc != pcc ||
                                (L->cc && fabs(L->setpoint - psp) > 1e-6));
            char gas[48];
            if (L->cc)
                /* Setpoint only. The diluent used to be printed here too
                 * ("CC 21/0 SP 1.50"), which is 15 characters in an 11-column
                 * field: it pushed PO2 and EAD out of alignment and wrapped the
                 * row. On closed circuit the diluent does not set the inspired
                 * PO2 anyway, and its inert content is already visible in EAD. */
                snprintf(gas, sizeof gas, "CC SP%.2f", L->setpoint);
            else if (L->fhe > 0.001)
                snprintf(gas, sizeof gas, "TMX %.0f/%.0f", L->fo2*100, L->fhe*100);
            else if (fabs(L->fo2 - 0.21) < 0.005)
                snprintf(gas, sizeof gas, "Air");
            else if (L->fo2 > 0.995)
                snprintf(gas, sizeof gas, "O2");
            else
                snprintf(gas, sizeof gas, "EAN%.0f", L->fo2*100);

            bool descending = L->depth_m > prev_depth + 1e-9;
            /* fold short inter-stop ascents without a gas switch */
            bool fold = (!descending && travel > 0.02 && travel < 1.5 &&
                         !gas_changed && L->kind != ZP_LINE_WAYPOINT);
            if (travel > 0.02 && !fold) {
                int tm = (int)travel, ts2 = (int)((travel - tm) * 60 + 0.5);
                if (ts2 == 60) { tm++; ts2 = 0; }
                APP(" %s %5.0f%-2s %3d:%02d %6.0f   %-11s\n",
                    descending ? "\u2193    " : "\u2191    ",
                    L->depth_m * dscale, du, tm, ts2,
                    ceil(arrive - 1e-6),
                    (descending && gas_changed) ? gas : "");
            }
            double disp = L->stop_sec / 60.0 + (fold ? travel : 0);
            int mm = (int)disp, ss = (int)((disp - mm) * 60 + 0.5);
            if (ss == 60) { mm++; ss = 0; }
            if (L->stop_sec > 0.5 || L->kind == ZP_LINE_WAYPOINT)
                APP(" %s %5.0f%-2s %3d:%02d %6.0f   %-11s %4.2f %4.0f%-2s\n",
                    L->kind == ZP_LINE_DEEPSTOP ? "DStop" : "\u2014    ",
                    L->depth_m * dscale, du, mm, ss,
                    ceil(L->runtime_min - 1e-6),
                    (gas_changed && !(descending && travel > 0.02 && !fold)) ? gas : "",
                    L->ppo2, L->ead_m * dscale, du);
            prev_end = L->runtime_min; prev_depth = L->depth_m;
            pfo2 = L->fo2; pfhe = L->fhe; pcc = (int)L->cc; psp = L->setpoint;
        }
        double travel = res->runtime_min - prev_end;
        if (travel > 0.02) {
            int tm = (int)travel, ts2 = (int)((travel - tm) * 60 + 0.5);
            if (ts2 == 60) { tm++; ts2 = 0; }
            APP(" \u2191     %5.0f%-2s %3d:%02d %6.0f\n",
                0.0, du, tm, ts2, ceil(res->runtime_min - 1e-6));
        }
        APP(" --------------------------------------------------------\n");
    }
    if (res->decozone_start_m > 0)
        APP("Deco zone start: %.0f%s", res->decozone_start_m * dscale, du);
    if (res->bottom_density_gl > 0)
        APP("%sBottom gas density: %.1f g/l\n",
            res->decozone_start_m > 0 ? "    " : "", res->bottom_density_gl);
    else if (res->decozone_start_m > 0) APP("\n");
    {   /* clamp: ceil(0 - 1e-6) is -0.0, which printf renders as "-0 minutes" */
        double deco = ceil(res->total_deco_min - 1e-6);
        if (deco < 0) deco = 0;
        APP("\nTOTAL DECO TIME: %.0f minutes.\n", deco);
    }
    APP("DIVE RUN TIME: %.0f minutes.\n", ceil(res->runtime_min - 1e-6));
    APP("CNS Total: %.1f%%\n", res->cns_pct);
    APP("OTU's: %.0f\n\n", res->otu);
    APP("Time to Fly: %.1f Hours\n\n", res->time_to_fly_hr);
    /* RMV/consumption units follow RmvMetric when set, else UseMetric */
    {
        bool rmv_l = cfg->rmv_metric >= 0 ? (cfg->rmv_metric == 1) : metric;
        double vdiv = rmv_l ? 1.0 : L_PER_CUFT;
        for (int i = 0; i < res->n_gas_used; i++)
            APP(rmv_l ? "%.1f Ltr. of %.1f%% consumed.\n"
                      : "%.2f Cu.Ft. of %.1f%% consumed.\n",
                res->gas_used_l[i] / vdiv, res->gas_fo2[i]*100);
        if (res->n_gas_used)
            APP(rmv_l ? "%.1f Ltr. total open circuit gas consumed.\n"
                      : "%.2f Cu.Ft. total open circuit gas consumed.\n",
                res->total_oc_l / vdiv);
    }
    if (res->warnings[0]) APP("\n%s", res->warnings);
    #undef APP
    return (int)off;
}
