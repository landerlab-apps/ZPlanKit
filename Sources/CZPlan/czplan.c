/* czplan.c — the Lplanner decompression engine.
 * See czplan.h for the warning you must read before using any of this.
 *
 * Copyright (C) 2026 Carlos Lander <scubalander@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details. That disclaimer is not a formality here: this software
 * computes decompression schedules, and a wrong schedule can injure or kill
 * the diver who follows it.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * The VPM-B implementation follows the Varying Permeability Model of D. E.
 * Yount and D. C. Hoffman, in the form Erik C. Baker released to the diving
 * community with the request that it be distributed freely and the authors
 * credited. This is an independent port; any error in it is mine, not theirs.
 *
 * The VVAL-18 implementation follows the U.S. Navy Thalmann EL-DCM as
 * published. The helium handling is an unvalidated extrapolation of my own -
 * the Navy publishes no helium parameters for that model.
 */

#include "include/czplan.h"
#include <math.h>
#ifndef M_LN2
#define M_LN2 0.69314718055994530942
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define ZP_VERSION "1.20.0"
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
/* Decompression model selector (zp_config.use_vval) */
#define ZP_MODEL_BUHLMANN 0
#define ZP_MODEL_VVAL     1
#define ZP_MODEL_VPMB     2
#define IS_VVAL(c) ((c)->use_vval == ZP_MODEL_VVAL)
#define IS_VPM(c)  ((c)->use_vval == ZP_MODEL_VPMB)
#define IS_BUHL(c) ((c)->use_vval == ZP_MODEL_BUHLMANN)

#define VPM_NC          16          /* the ZH-L16 set; our optional 1b
                                     * compartment (index 16) is not part of
                                     * Baker's model and is excluded */
#define VPM_GAMMA       0.0179      /* surface tension, N/m              */
#define VPM_GAMMAC      0.257       /* skin compression, N/m             */
#define VPM_GRAD_IMPERM_BAR (8.2 * P_SEALEVEL)  /* onset of impermeability */
#define VPM_LAMBDA_FSW  7500.0      /* critical volume parameter, fsw-min */
#define VPM_REGEN_MIN   20160.0     /* nuclear regeneration time constant */
#define VPM_OTHER_GASES 0.1359888   /* O2+CO2+H2O in tissue, bar (102 mmHg)*/
#define VPM_WV_BAR      0.0493      /* water vapour, Schreiner Rq 0.8, bar */
#define PA_PER_BAR      1.0e5

/* Conservatism 0-4 scales the critical radii. A larger nucleus is excited by
 * a smaller gradient, so higher levels give more decompression. Same ladder
 * Subsurface uses; level 0 is Baker's nominal VPM-B. */
static const double VPM_CONS_LVL[5] = { 1.0, 1.05, 1.12, 1.22, 1.35 };

typedef struct {
    double max_crush_n2[VPM_NC], max_crush_he[VPM_NC];  /* bar           */
    double crush_onset[VPM_NC];                         /* bar, tension  */
    double max_amb;                                     /* bar           */
    double regen_r_n2[VPM_NC], regen_r_he[VPM_NC];      /* metres        */
    double init_grad_n2[VPM_NC], init_grad_he[VPM_NC];  /* bar           */
    double grad_n2[VPM_NC], grad_he[VPM_NC];            /* bar, relaxed  */
    double spvt[VPM_NC];            /* surface phase volume time, min    */
    double first_ceiling;           /* bar; 0 until the first stop is set */
    double deco_min;                /* previous pass's deco time, 0 = none */
    double zone_runtime;            /* runtime entering the deco zone     */
    int    in_zone;
} vpm_state;

static vpm_state VPM;

#define VVAL_NC 12
#define FSW2BAR (1.01325 / 33.0)
/* VVal-79 gas-exchange constants, NEDU EL-DCA (Thalmann table-generation
 * report, Figures 30-34). All in fsw, converted here. */
#define VVAL_PH2O_BAR   (0.00 * FSW2BAR)   /* tissue water vapour            */
#define VVAL_PACO2_BAR  (1.50 * FSW2BAR)   /* arterial CO2                   */
#define VVAL_PVCO2_BAR  (2.30 * FSW2BAR)   /* venous CO2                     */
#define VVAL_PVO2_BAR   (2.00 * FSW2BAR)   /* venous O2                      */
#define VVAL_AMBAO2_BAR (0.00 * FSW2BAR)   /* ambient-arterial O2 gradient   */
#define VVAL_SDR         0.70              /* saturation/desaturation ratio  */
/* Metabolic oxygen extraction expressed as a partial-pressure drop: about
 * 5 mL O2/dL of blood at a dissolved solubility of 0.003 mL/dL/mmHg, so
 * 1667 mmHg = 72.4 fsw. Below that, haemoglobin unloads and venous PO2 stays
 * at its resting value; above it, dissolved oxygen alone carries metabolism
 * and venous PO2 tracks arterial. */
#define VVAL_O2_METABOLIC_BAR (72.4 * FSW2BAR)
#define VVAL_CNDSDR_FO2  0.80              /* SDR activation FO2 threshold   */
#define VVAL_H2O_BAR (1.85 * FSW2BAR)      /* superseded; kept for tissue.dat */
static const double VVAL_HT_N2[VVAL_NC] = {
    1.5, 2.5, 3.5, 5.0, 10.0, 20.0, 40.0, 80.0, 120.0, 160.0, 200.0, 240.0 };
static const double VVAL_MPTT0_FSW[VVAL_NC] = {
    120.0, 115.0, 108.0, 99.3, 87.7, 78.0, 56.0, 48.5, 45.5, 44.5, 44.0, 43.5 };
/* Helium MPTT0, per compartment. Cochran's twenty-compartment trimix model
 * "included fast compartments to compensate for helium gas" (owner's InDepth
 * article). The mechanism: give the fast compartments a helium ceiling low
 * enough to bite early in the ascent, so a helium-loaded diver is held deep.
 * Air is untouched by construction - with no helium in a compartment the
 * mix-weighted MPTT0 reduces exactly to the nitrogen value.
 * Overridable for fitting with ZP_MPTT_HE="v0,...,v11". */
static double VVAL_MPTT0_HE_FSW[VVAL_NC] = {
    120.0, 115.0, 108.0, 99.3, 87.7, 78.0, 56.0, 48.5, 45.5, 44.5, 44.0, 43.5 };
/* MPTT projection slope, per gas. Spec section 7 defines MPTT(D) = MPTT0 + a*D
 * and VVal-79 sets a = 1.0 throughout, which tolerates the same supersaturation
 * gradient at 54 m as at 3 m - the reason the model has no deep stops.
 *
 * a < 1 shrinks the tolerated gradient with depth. At D = 0 the term vanishes,
 * so the surfacing ceiling is untouched: deep stops that cost nothing shallow.
 * Nitrogen stays at 1.0 so air is unchanged. Override with ZP_SLOPE_HE. */
static double VVAL_SLOPE_N2 = 1.0;
static double VVAL_SLOPE_HE = 1.0;
static void vval_slope_env(void) {
    static int done = 0; if (done) return; done = 1;
    const char *e = getenv("ZP_SLOPE_HE"); if (e) VVAL_SLOPE_HE = atof(e);
}

static void vval_mptt_he_env(void) {
    static int done = 0; if (done) return; done = 1;
    const char *e = getenv("ZP_MPTT_HE"); if (!e) return;
    char buf[256]; strncpy(buf, e, 255); buf[255] = 0;
    char *tok = strtok(buf, ","); int i = 0;
    while (tok && i < VVAL_NC) { VVAL_MPTT0_HE_FSW[i++] = atof(tok); tok = strtok(NULL, ","); }
}
/* PBOVP, the crossover overpressure: 10.0 fsw for every compartment, per the
 * VVAL-79 spec section 9.
 *
 * These were previously per-compartment fitted values (23, 17, 13, 11, 8...).
 * That fit existed only to compensate for the wrong linear rate law above: with
 * the rate fixed to the spec form, the published uniform 10 fsw reproduces the
 * NEDU manned-trial anchor (132 fsw / 20 min -> 9:00 at 20 fsw) exactly, which
 * the fitted set no longer does once the rate is correct.
 * Override for re-fitting with env ZP_PCROSS="v0,...,v11". */
static double VVAL_PCROSS_FSW[VVAL_NC] = {
    14.0, 14.0, 14.0, 14.0, 14.0, 14.0, 14.0, 14.0, 14.0, 14.0, 14.0, 14.0 };
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
    double gas_bottom_l[ZP_MAX_GASES];
    bool   in_ascent;              /* bottom phase over: split consumption */
    int n_gas;
    double rmv;                    /* current RMV l/min */

    double gf_ref_depth;           /* first-stop depth anchoring the GF slope */
    double slide_peak, slide_t0;   /* Scamahorn slide state */
    double gf_force;               /* >0: override GF (NDL check) */
    bool   inter_stop;             /* this leg runs from one stop to the next */
    double es_delay_min;           /* cumulative extra-slow hold time (v1.8.0) */
    zp_result *out;                /* for recording mid-water gas switches */
    int    ncomp;                  /* active compartments: 17 Buhlmann, 12 VVAL */
    int    rmv_switched;           /* deco RMV engaged yet? */
    char *warn; size_t warnlen;
} sim;

static void vpm_crush(const sim *s, double p_amb);

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
    double p_ambient = pamb(s, depth_m);
    /* VPM-B is specified with the Schreiner water vapour (Rq 0.8); Buhlmann
     * uses Rq 1.0. Same distinction Subsurface makes. */
    double pa = IS_VPM(s->cfg)
              ? (p_ambient - VPM_WV_BAR > 0 ? p_ambient - VPM_WV_BAR : 0)
              : alveolar(p_ambient);
    if (IS_VVAL(s->cfg)) {
        /* NEDU EL-DCA "UPDT7 Initialize" (Figure 31), both branches:
         *
         *   constant PO2 : PAO2 = PO2 * SURFP * (1 - PH2O/PAMB) - AMBAO2
         *   open circuit : PAO2 = (PAMB - PH2O) * FO2          - AMBAO2
         *   both then    : PA_inert = MAX(PAMB - (PAO2 + PACO2 + PH2O), 0)
         *
         * Two things this gets right that the old code did not. The CO2
         * correction is a FLAT subtraction from the inert tension, not
         * something scaled by the inert fraction — the old
         * "PAMB - 1.85 fsw, then multiply by F_inert" form only coincided
         * with this on air, because 0.79 x 1.85 = 1.46 = 1.50. And constant
         * PO2 is a genuinely separate formula: the arterial O2 is fixed by
         * the setpoint and does NOT scale with depth, so every bit of a depth
         * change goes into the inert gas.
         *
         * The Navy model carries one inert gas. The total inert tension is
         * split between N2 and He in the ratio the breathing source supplies
         * them, which is the only generalisation that leaves air unchanged. */
        double amb = pamb(s, depth_m);
        double pao2;
        if (s->cc) {
            double sp = s->setpoint;
            if (s->slide_peak > 0) {
                double rate = s->cfg->slide_rate > 0 ? s->cfg->slide_rate : 0.1;
                double e = s->slide_peak - rate * (s->runtime - s->slide_t0);
                if (e > sp) sp = e;
            }
            pao2 = sp * P_SEALEVEL * (1.0 - VVAL_PH2O_BAR / amb) - VVAL_AMBAO2_BAR;
            if (pao2 > amb) pao2 = amb;
        } else {
            pao2 = (amb - VVAL_PH2O_BAR) * s->fo2 - VVAL_AMBAO2_BAR;
        }
        if (pao2 < 0) pao2 = 0;
        double pinert = amb - (pao2 + VVAL_PACO2_BAR + VVAL_PH2O_BAR);
        if (pinert < 0) pinert = 0;
        double fin = s->cc ? (1.0 - s->fo2) : (1.0 - s->fo2);
        double fhe_share = fin > 1e-9 ? s->fhe / fin : 0.0;
        if (fhe_share > 1.0) fhe_share = 1.0;
        *pihe = pinert * fhe_share;
        *pin2 = pinert * (1.0 - fhe_share);
        *pio2 = pao2;
        return;
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
        if (IS_VVAL(s->cfg)) {
            /* Thalmann EL-DCM: exponential uptake; during offgassing, a
             * compartment supersaturated beyond its crossover eliminates gas
             * at the constant rate k*Pcross (linear kinetics). */
            /* Thalmann EL-DCM: exponential uptake; a compartment
             * supersaturated beyond its crossover eliminates linearly.
             *
             * The linear rate is the SLOPE OF THE EXPONENTIAL CURVE AT THE
             * CROSSOVER BOUNDARY, so the two regimes join smoothly
             * (VVAL-79 spec section 6). The exponential slope where
             * Pt = P_amb + PBOVP is
             *
             *     dPt/dt = -k * (P_amb + PBOVP - P_art)
             *
             * This used to be k * PBOVP, which is not the slope of anything
             * and — fatally — depends on neither depth nor the gas being
             * breathed. A washout rate that cannot vary with depth makes every
             * 3 m of ceiling cost the same time, so stop times came out flat;
             * one that cannot vary with gas means a switch to a rich deco mix
             * could not accelerate elimination at all. On 70 m / 26 min with
             * 18/45 and EAN50 the stop at 21 m ran 14:00 and the stop below it
             * 5:18 — the schedule got LONGER after the gas switch, which no
             * decompression model should do. It is now 1:00 against 5:18. */
            /* "Set Time Constants" (Figure 32): the desaturation constant is
             * scaled by SDR when running constant PO2, OR when FN2 is at or
             * below (1 - CNDSDR_FO2). Note the first condition: on a
             * rebreather SDR applies unconditionally, whatever the FO2. */
            bool sdr_on = s->cc || (1.0 - s->fo2 - s->fhe) <= (1.0 - VVAL_CNDSDR_FO2);
            double sdr = sdr_on ? VVAL_SDR : 1.0;

            /* "UPDT7" (Figure 30) and the crossover routines (33, 34): the
             * linear/exponential boundary is the VENOUS saturation reference
             * plus the gas-phase overpressure, not ambient plus it.
             *     PVSAT = PAMB - (PVO2 + PVCO2 + PH2O)
             *     boundary tension PVN2 = PVSAT + PBOVP
             * and the linear rate is the exponential slope there,
             *     dP/dt = KDsat * (PA_inert - PVN2). */
                /* Oxygen window. PVSAT is the pressure left for inert gas in
             * venous blood, so what matters here is venous PO2, not the window
             * itself.
             *
             * Venous PO2 is buffered by haemoglobin: it sits at its resting
             * value however rich the mix, until dissolved oxygen alone can
             * supply metabolism - about 2.2 ata. The oxygen WINDOW does open
             * enormously before then (114 mmHg on air at the surface, 1170 on
             * O2 at 6 m), but that benefit reaches the model through the
             * ARTERIAL term, where PA_inert = PAMB - PAO2 - PACO2 - PH2O
             * already falls as PAO2 rises. It is not missing.
             *
             * Above 2.2 ata venous PO2 does track arterial, which matters for
             * chamber and treatment depths. Below it this expression returns
             * the resting value exactly, so no recreational or technical
             * schedule moves. */
            double pvo2 = pio2 - VVAL_O2_METABOLIC_BAR;
            if (pvo2 < VVAL_PVO2_BAR) pvo2 = VVAL_PVO2_BAR;
            double pvsat = pa_now - (pvo2 + VVAL_PVCO2_BAR + VVAL_PH2O_BAR);
            double pcross = VVAL_PCROSS_FSW[i] * FSW2BAR;
            double pvn2 = pvsat + pcross;
            double supersat = (s->pn2[i] + s->phe[i]) - pvsat;

            double kn2 = M_LN2 / VVAL_HT_N2[i];
            double kdn2 = kn2 * sdr;
            if (pin2 < s->pn2[i] && supersat > pcross) {
                double rate = kdn2 * (pvn2 - pin2);
                if (rate < 0) rate = 0;
                double p = s->pn2[i] - rate * dt;
                s->pn2[i] = p > pin2 ? p : pin2;      /* never below inspired */
            } else {
                double k = (pin2 < s->pn2[i]) ? kdn2 : kn2;
                s->pn2[i] += (pin2 - s->pn2[i]) * (1.0 - exp(-k * dtn));
            }
            double khe = M_LN2 / (VVAL_HT_N2[i] / VVAL_HE_RATIO);
            double kdhe = khe * sdr;
            if (pihe < s->phe[i] && supersat > pcross) {
                double rate = kdhe * (pvn2 - pihe);
                if (rate < 0) rate = 0;
                double p = s->phe[i] - rate * dt;
                s->phe[i] = p > pihe ? p : pihe;
            } else {
                double k = (pihe < s->phe[i]) ? kdhe : khe;
                s->phe[i] += (pihe - s->phe[i]) * (1.0 - exp(-k * dth));
            }
        } else {
            s->pn2[i] += (pin2 - s->pn2[i]) * (1.0 - exp(-M_LN2 * dtn / N2_HT[i]));
            s->phe[i] += (pihe - s->phe[i]) * (1.0 - exp(-M_LN2 * dth / HE_HT[i]));
        }
    }
    /* Start of the decompression zone, for every model.
     *
     * This is the depth at which the leading compartment's total inert tension
     * first reaches ambient pressure on the way up - the crossover from merely
     * off-gassing into supersaturation, where a bubble can grow. Everything
     * shallower than it is the decompression zone, so the first stop always
     * lies ABOVE this depth. Reporting the first stop under this name, which
     * is what the engine used to do, made the two identical and told the diver
     * nothing.
     *
     * Note the criterion is tension >= ambient, not tension >= inspired inert
     * pressure. Off-gassing in the plain sense begins almost the instant the
     * diver leaves the bottom, since the inspired inert pressure falls with
     * him; that depth is not useful and is not what anyone means by the deco
     * zone. Baker's CALC_START_OF_DECO_ZONE uses ambient, and so does this.
     * The constant for the metabolic gases is included, as he includes it.
     *
     * Recorded on the FIRST crossing only, and reset per pass by run_plan, so
     * the critical volume loop cannot walk it shallower on later iterations. */
    if (s->in_ascent && s->out && s->out->decozone_start_m <= 0) {
        for (int i = 0; i < s->ncomp; i++)
            if (s->pn2[i] + s->phe[i] + VPM_OTHER_GASES >= pa_now) {
                s->out->decozone_start_m = depth_m;
                break;
            }
    }
    if (IS_VPM(s->cfg)) {
        vpm_crush(s, pa_now);
        /* Baker measures the phase volume time from the moment the leading
         * compartment's total tension reaches ambient - the start of the
         * decompression zone - not from the first stop. */
        if (s->in_ascent && !VPM.in_zone) {
            for (int i = 0; i < VPM_NC; i++)
                if (s->pn2[i] + s->phe[i] + VPM_OTHER_GASES >= pa_now) {
                    VPM.in_zone = 1; VPM.zone_runtime = s->runtime; break;
                }
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
            s->gas_l[gi] = 0; s->gas_bottom_l[gi] = 0;
        }
        if (gi >= 0) {
            s->gas_l[gi] += litres;
            if (!s->in_ascent) s->gas_bottom_l[gi] += litres;
        }
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
/* ------------------------------------------------------------------ */
/* VPM-B — Yount/Hoffman varying permeability, Erik C. Baker's          */
/* implementation. Ported from Baker's FORTRAN ("DISTRIBUTE FREELY -    */
/* CREDIT THE AUTHORS") and cross-read against Subsurface core/deco.c.  */
/* Validated against Baker's own VPM.OUT: see Reference/vpmb_ref.c and  */
/* Reference/vpmb_validation.md.                                        */
/*                                                                      */
/* Baker computes the nucleus mechanics in Pascals with radii in metres.*/
/* That is kept verbatim; conversion to bar happens only at the edges.  */
/* ------------------------------------------------------------------ */


static double vpm_r_n2(const zp_config *c) {
    double r = c->vpm_radius_n2_um > 0 ? c->vpm_radius_n2_um : 0.6;
    int lv = c->vpm_conservatism; if (lv < 0) lv = 0; if (lv > 4) lv = 4;
    return r * VPM_CONS_LVL[lv] * 1.0e-6;
}
static double vpm_r_he(const zp_config *c) {
    double r = c->vpm_radius_he_um > 0 ? c->vpm_radius_he_um : 0.5;
    int lv = c->vpm_conservatism; if (lv < 0) lv = 0; if (lv > 4) lv = 4;
    return r * VPM_CONS_LVL[lv] * 1.0e-6;
}

/* Solve A*r^3 - B*r^2 - C = 0 by bisection on [lo,hi]. Baker uses Newton with
 * a bisection fallback; plain bisection reaches the same root and cannot
 * diverge on a degenerate bracket. */
static double vpm_root(double A, double B, double C, double lo, double hi) {
    double flo = lo * (lo * (A * lo - B)) - C;
    double fhi = hi * (hi * (A * hi - B)) - C;
    double a = lo, b = hi, fa = flo, mid = lo, fm;
    if (flo * fhi > 0.0) return (fabs(flo) < fabs(fhi)) ? lo : hi;
    for (int i = 0; i < 160; i++) {
        mid = 0.5 * (a + b);
        fm  = mid * (mid * (A * mid - B)) - C;
        if (fm == 0.0 || (b - a) <= 0.0) break;
        if (fa * fm < 0.0) b = mid; else { a = mid; fa = fm; }
    }
    return mid;
}

/* Inner pressure of a nucleus that has gone impermeable (bar). */
static double vpm_inner_pressure(double crit_r, double onset_tension,
                                 double p_amb) {
    double dg = 2.0 * (VPM_GAMMAC - VPM_GAMMA);
    double r_onset = 1.0 /
        (VPM_GRAD_IMPERM_BAR * PA_PER_BAR / dg + 1.0 / crit_r);
    double A = (p_amb - VPM_GRAD_IMPERM_BAR) * PA_PER_BAR + dg / r_onset;
    double B = dg;
    double C = onset_tension * PA_PER_BAR * r_onset * r_onset * r_onset;
    double r = vpm_root(A, B, C, B / A, r_onset);
    return onset_tension * (r_onset * r_onset * r_onset) / (r * r * r);
}

static void vpm_clear(void) {
    memset(&VPM, 0, sizeof VPM);
}

/* CALC_CRUSHING_PRESSURE. Called every tick; the maximum over the dive is
 * what the model uses, so evaluating continuously handles multi-level and
 * repetitive descents without special cases. */
static void vpm_crush(const sim *s, double p_amb) {
    for (int i = 0; i < VPM_NC; i++) {
        double tension  = s->pn2[i] + s->phe[i] + VPM_OTHER_GASES;
        double gradient = p_amb - tension;
        double cn2, che;
        if (gradient <= VPM_GRAD_IMPERM_BAR) {      /* permeable */
            cn2 = che = gradient;
            VPM.crush_onset[i] = tension;
        } else {                                     /* impermeable */
            if (VPM.max_amb >= p_amb) continue;
            cn2 = p_amb - vpm_inner_pressure(vpm_r_n2(s->cfg),
                                             VPM.crush_onset[i], p_amb);
            che = p_amb - vpm_inner_pressure(vpm_r_he(s->cfg),
                                             VPM.crush_onset[i], p_amb);
        }
        if (cn2 > VPM.max_crush_n2[i]) VPM.max_crush_n2[i] = cn2;
        if (che > VPM.max_crush_he[i]) VPM.max_crush_he[i] = che;
    }
    if (p_amb > VPM.max_amb) VPM.max_amb = p_amb;
}

/* NUCLEAR_REGENERATION followed by CALC_INITIAL_ALLOWABLE_GRADIENT. */
static void vpm_regenerate(const zp_config *c, double dive_min) {
    double dg  = 2.0 * (VPM_GAMMAC - VPM_GAMMA);
    double num = 2.0 * VPM_GAMMA * (VPM_GAMMAC - VPM_GAMMA);
    double rn = vpm_r_n2(c), rh = vpm_r_he(c);
    double decay = exp(-dive_min / VPM_REGEN_MIN);
    for (int i = 0; i < VPM_NC; i++) {
        double crushed_n2 = 1.0 /
            (VPM.max_crush_n2[i] * PA_PER_BAR / dg + 1.0 / rn);
        double crushed_he = 1.0 /
            (VPM.max_crush_he[i] * PA_PER_BAR / dg + 1.0 / rh);
        VPM.regen_r_n2[i] = rn + (crushed_n2 - rn) * decay;
        VPM.regen_r_he[i] = rh + (crushed_he - rh) * decay;
        VPM.init_grad_n2[i] =
            num / (VPM.regen_r_n2[i] * VPM_GAMMAC) / PA_PER_BAR;
        VPM.init_grad_he[i] =
            num / (VPM.regen_r_he[i] * VPM_GAMMAC) / PA_PER_BAR;
        VPM.grad_n2[i] = VPM.init_grad_n2[i];
        VPM.grad_he[i] = VPM.init_grad_he[i];
    }
}

/* CRITICAL_VOLUME: relax the gradients given the phase volume time. Always
 * computed from the INITIAL gradients, never compounded, so the loop is a
 * fixed-point iteration rather than a ratchet. */
static void vpm_next_gradient(double deco_min) {
    double lambda_pa = (VPM_LAMBDA_FSW / 33.0) * (P_SEALEVEL * PA_PER_BAR);
    for (int i = 0; i < VPM_NC; i++) {
        double pvt = deco_min + VPM.spvt[i];
        if (pvt < 1e-6) continue;
        double ig, crush, B, C, disc;

        ig = VPM.init_grad_n2[i] * PA_PER_BAR;
        crush = VPM.max_crush_n2[i] * PA_PER_BAR;
        B = ig + (lambda_pa * VPM_GAMMA) / (VPM_GAMMAC * pvt);
        C = (VPM_GAMMA * VPM_GAMMA * lambda_pa * crush)
          / (VPM_GAMMAC * VPM_GAMMAC * pvt);
        disc = B * B - 4.0 * C;
        if (disc > 0) VPM.grad_n2[i] = (B + sqrt(disc)) / 2.0 / PA_PER_BAR;

        ig = VPM.init_grad_he[i] * PA_PER_BAR;
        crush = VPM.max_crush_he[i] * PA_PER_BAR;
        B = ig + (lambda_pa * VPM_GAMMA) / (VPM_GAMMAC * pvt);
        C = (VPM_GAMMA * VPM_GAMMA * lambda_pa * crush)
          / (VPM_GAMMAC * VPM_GAMMAC * pvt);
        disc = B * B - 4.0 * C;
        if (disc > 0) VPM.grad_he[i] = (B + sqrt(disc)) / 2.0 / PA_PER_BAR;
    }
}

/* CALC_SURFACE_PHASE_VOLUME_TIME — the out-of-water part of the integration
 * of supersaturation gradient x time, with Baker's three-branch helium case. */
static void vpm_surface_phase(const sim *s) {
    double surf_n2 = (s->p_surface - VPM_WV_BAR) * 0.79;
    for (int i = 0; i < VPM_NC; i++) {
        double he = s->phe[i], n2 = s->pn2[i];
        double khe = M_LN2 / HE_HT[i], kn2 = M_LN2 / N2_HT[i];
        if (n2 > surf_n2) {
            VPM.spvt[i] = (he / khe + (n2 - surf_n2) / kn2)
                        / (he + n2 - surf_n2);
        } else if (he + n2 >= surf_n2 && he > 1e-12) {
            double decay = 1.0 / (kn2 - khe) * log((surf_n2 - n2) / he);
            double integral = he / khe * (1.0 - exp(-khe * decay))
                            + (n2 - surf_n2) / kn2 * (1.0 - exp(-kn2 * decay));
            VPM.spvt[i] = integral / (he + n2 - surf_n2);
        } else {
            VPM.spvt[i] = 0.0;
        }
    }
}

/* Boyle's law compensation. As the bubble rises it expands, so the gradient
 * it tolerates shrinks. Using pV = (G + P_amb)/G^3 = const avoids solving for
 * radii: G^3 - B*G - C = 0. Algebraically identical to Baker's radius form. */
static double vpm_solve_cubic2(double B, double C) {
    double disc = 27.0 * C * C - 4.0 * B * B * B;
    if (disc < 0.0)
        return 2.0 * sqrt(B / 3.0) *
               cos(acos(3.0 * C * sqrt(3.0 / B) / (2.0 * B)) / 3.0);
    double den = pow(9.0 * C + sqrt(3.0 * disc), 1.0 / 3.0);
    return pow(2.0 / 3.0, 1.0 / 3.0) * B / den + den / pow(18.0, 1.0 / 3.0);
}

static double vpm_boyle_gradient(double next_p, double g_first) {
    double B = (g_first * g_first * g_first) / (VPM.first_ceiling + g_first);
    double C = next_p * B;
    double g = vpm_solve_cubic2(B, C);
    return g > 0 ? g : g_first;
}

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
    if (IS_VPM(s->cfg)) {
        /* VPM-B tolerated ambient pressure. The Boyle-compensated gradient
         * depends on the ambient pressure we are solving FOR, so the ceiling
         * is a fixed point: start from the current depth and iterate until it
         * settles. Above the first stop no compensation applies. */
        double ref = pamb(s, s->depth);
        for (int it = 0; it < 24; it++) {
            double w = 0;
            for (int i = 0; i < VPM_NC; i++) {
                double pt = s->pn2[i] + s->phe[i];
                double gn2 = VPM.grad_n2[i], ghe = VPM.grad_he[i];
                if (VPM.first_ceiling > 0 && ref < VPM.first_ceiling) {
                    gn2 = vpm_boyle_gradient(ref, gn2);
                    ghe = vpm_boyle_gradient(ref, ghe);
                }
                double g = (pt > 0)
                         ? (gn2 * s->pn2[i] + ghe * s->phe[i]) / pt
                         : (gn2 < ghe ? gn2 : ghe);
                double tol = pt + VPM_OTHER_GASES - g;
                if (tol < 0) tol = 0;
                if (tol > w) w = tol;
            }
            if (fabs(w - ref) < 1e-3) { ref = w; break; }
            ref = w;
        }
        return ref;
    }
    if (IS_VVAL(s->cfg)) {
        /* MPTT(D) = MPTT0 + 1.0 * D (fsw); tolerated ambient pressure is
         * therefore surface pressure plus the excess tension over MPTT0. */
        for (int i = 0; i < VVAL_NC; i++) {
            double pt = s->pn2[i] + s->phe[i];
            if (pt <= 0) continue;
            /* Mix-weighted maximum permissible tension, the same weighting
             * Buhlmann uses for a and b. With phe = 0 this is exactly the
             * nitrogen value, so every air schedule is untouched. */
            double m = (s->pn2[i] * VVAL_MPTT0_FSW[i] + s->phe[i] * VVAL_MPTT0_HE_FSW[i]) / pt;
            /* Mix-weighted projection slope. D_ceil = (tension - MPTT0)/a, so
             * the tolerated ambient is p_surface + (pt - MPTT0)/a. At
             * pt = MPTT0 this is p_surface whatever a is — surfacing unchanged. */
            double a = (s->pn2[i] * VVAL_SLOPE_N2 + s->phe[i] * VVAL_SLOPE_HE) / pt;
            if (a < 0.05) a = 0.05;
            double tol = s->p_surface + (pt - m * FSW2BAR) / a;
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

/* Forward declarations: travel() switches gas mid-water and therefore needs
 * these, but they are defined further down alongside the other deco helpers. */
static void   select_deco_source(sim *s, double depth);
static double ext_stop_for(const zp_config *c, double depth_m);
static double display_ppo2(const sim *s, double depth_m);
static double end_m(const sim *s, double depth_m);
static double ead_m(const sim *s, double depth_m);

/* Deepest depth strictly between the current depth and `to_m` at which a
 * richer deco gas first becomes permitted, or -1 if there is none.
 *
 * Divers switch at the mix's MOD, not at whatever stop happens to come next:
 * EAN50 at 1.6 goes on the bottle at 21 m even when the next stop is 18 m,
 * because that is where the oxygen window opens. Returning the deepest such
 * depth lets travel() recurse and pick up several mixes on one leg. */
static double gas_switch_depth(const sim *s, double to_m) {
    const zp_config *c = s->cfg;
    if (!c->use_oc_deco || c->n_oc_deco == 0) return -1;
    double best = -1;
    for (int i = 0; i < c->n_oc_deco; i++) {
        double fo2 = c->oc_deco_fo2[i];
        if (fo2 <= s->fo2 + 1e-6) continue;          /* no richer than current */
        /* The MOD, then snapped DOWN to the stop grid. Divers switch on the
         * grid, not at a raw computed depth: EAN50 at 1.6 works out at 22.0 m,
         * and the convention is to take the 21 m rung rather than argue about
         * the last 0.6 m. Oxygen at 1.6 works out at 6.0 m and stays at 6 m.
         * Snapping downward also guarantees the switch is never deeper than the
         * mix allows. StopDistance drives the grid, so a CCR diver working in
         * 6 m increments gets switches on 6 m rungs for free. */
        double max_pa = c->oc_deco_max_po2 / fo2 * P_SEALEVEL;
        double d = (max_pa - s->p_surface) / s->bar_per_m;
        double grid = c->stop_distance_m > 0 ? c->stop_distance_m : 3.0;
        d = floor(d / grid + 1e-9) * grid;
        double fhe = c->oc_deco_fhe[i];
        double fnarc = (1.0 - fo2 - fhe) + (c->oxy_narc ? fo2 : 0.0);
        double ref = c->oxy_narc ? 1.0 : 0.79;
        double p_end = (s->p_surface + d * s->bar_per_m) * fnarc / ref;
        if ((p_end - s->p_surface) / s->bar_per_m > c->max_end_m + 1e-6) continue;
        if (d >= s->depth - 1e-9) continue;          /* already available */
        if (d <= to_m + 1e-9) continue;              /* target reaches it anyway */
        if (d > best) best = d;
    }
    return best;
}

/* Put the diver on the best gas here and note it in the plan. */
static void do_gas_switch(sim *s) {
    double pre_fo2 = s->fo2, pre_fhe = s->fhe; bool pre_cc = s->cc;
    select_deco_source(s, s->depth);
    if (fabs(s->fo2 - pre_fo2) < 1e-6 && fabs(s->fhe - pre_fhe) < 1e-6 &&
        s->cc == pre_cc) return;

    double ext = ext_stop_for(s->cfg, s->depth);
    if (ext > 1e-9) {
        double left = ext;
        while (left > 1e-9) { double dt = left<DT?left:DT; tick(s,s->depth,dt,false); left -= dt; }
        if (s->out) s->out->total_deco_min += ext;
    }
    if (s->out && s->out->n_lines < ZP_MAX_PLAN_LINES) {
        zp_plan_line *L = &s->out->lines[s->out->n_lines++];
        L->kind = ZP_LINE_GASSWITCH; L->depth_m = s->depth;
        L->stop_sec = ext * 60.0; L->runtime_min = s->runtime;
        L->fo2 = s->fo2; L->fhe = s->fhe; L->cc = s->cc;
        L->setpoint = s->setpoint;
        L->ppo2 = display_ppo2(s, s->depth);
        L->end_m = end_m(s, s->depth);
        L->ead_m = ead_m(s, s->depth);
    }
}

static void travel(sim *s, double to_m, bool ascent) {
    /* Split the leg at any MOD crossed on the way up, switch there, continue.
     * Recursion handles several mixes coming on line during one ascent. */
    if (ascent && s->depth > to_m + 1e-9) {
        double sw = gas_switch_depth(s, to_m);
        if (sw > to_m + 1e-9 && sw < s->depth - 1e-9) {
            travel(s, sw, true);
            do_gas_switch(s);
            travel(s, to_m, true);
            return;
        }
    }
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
    int best = -1; double best_fo2 = -1;
    for (int i = 0; i < c->n_oc_deco; i++) {
        double fo2 = c->oc_deco_fo2[i];
        /* Maximum operating depth is an AMBIENT-pressure convention — EAN50 at
         * 1.6 switches at 21 m, which is what a diver's own MOD table says.
         * This used to test alveolar pressure (ambient minus water vapour),
         * which belongs in the tissue model but not here: it permitted a switch
         * roughly 2 m deeper than the stated limit and made the report show a
         * PO2 above the configured maximum.
         *
         * Expressed as a depth so the tolerance can be too: 0.2 m absorbs the
         * difference between this engine's water column (0.10125 bar/m off
         * 1.01325 bar) and the round 10 m-per-bar arithmetic divers use, so a
         * conventional 18 m switch on EAN50 at 1.4 is not rejected by a
         * centimetre. PO2 is taken in ATA, matching the report column, so the
         * displayed figure never exceeds the limit that was set. */
        double max_pa = c->oc_deco_max_po2 / fo2 * P_SEALEVEL;      /* bar */
        double max_depth = (max_pa - s->p_surface) / s->bar_per_m;  /* metres */
        if (depth_m > max_depth + 0.2) continue;
        double fhe = c->oc_deco_fhe[i];
        double fnarc = (1.0 - fo2 - fhe) + (c->oxy_narc ? fo2 : 0.0);
        double ref = c->oxy_narc ? 1.0 : 0.79;
        double p_end = pamb(s, depth_m) * fnarc / ref;
        double endd = (p_end - s->p_surface) / s->bar_per_m;
        if (endd > c->max_end_m + 1e-6) continue;
        if (fo2 > best_fo2) { best_fo2 = fo2; best = i; }
    }
    return best;
}

/* Extra hold, in minutes, for a deco mix switch happening at this depth.
 * Bands are fixed in metres regardless of the display units: 7 m up to 30 m,
 * and 30 m or deeper. A switch shallower than 7 m is not extended — the diver
 * is already holding a long final stop there. */
static double ext_stop_for(const zp_config *c, double depth_m) {
    if (depth_m >= 30.0 - 1e-9) return c->ext_stop_deep_min;
    if (depth_m >= 7.0  - 1e-9) return c->ext_stop_shallow_min;
    return 0.0;
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
/* Choose what the diver is breathing at a deco depth.
 *
 * A closed-circuit dive may leave the loop for an open-circuit deco mix and
 * later return to a deco setpoint. That is deliberate, not an oversight: US
 * Navy practice permits shifting off the rig for decompression and back again,
 * so a schedule that reads SP1.20 -> EAN50 -> SP1.30 is a real one. Do not
 * "fix" this by suppressing OC deco gases on closed circuit.
 *
 * Precedence is: an explicit deco setpoint for this depth wins; a setpoint of
 * zero means the range was declared open circuit; otherwise the richest deco
 * gas within Max PO2 and Max END is taken if one qualifies; failing all of
 * that, keep breathing whatever we already were. */
static void select_deco_source(sim *s, double depth) {
    /* Every gas change funnels through here - do_gas_switch is only one of four
     * callers - so the isobaric counterdiffusion advisory belongs here too. */
    double icd_fo2 = s->fo2, icd_fhe = s->fhe; bool icd_cc = s->cc;

    double sp = deco_setpoint_for(s->cfg, depth);
    if (sp > 0) { s->cc = true; s->setpoint = sp; return; }
    if (sp == 0) s->cc = false; /* explicit OC range */
    int g = best_deco_gas(s, depth);
    if (g >= 0) {
        s->cc = false;
        s->fo2 = s->cfg->oc_deco_fo2[g];
        s->fhe = s->cfg->oc_deco_fhe[g];
    }
    /* else: keep whatever we were breathing (bottom mix / setpoint) */

    /* Isobaric counterdiffusion advisory, V-Planner's criterion: flag when the
     * RISE in an inspired inert partial pressure at the switch exceeds a
     * threshold, default 0.5 ata.
     *
     * Deliberately NOT the "rule of fifths". That rule works in gas FRACTIONS
     * weighted by solubility, i.e. in absolute quantities of gas, where every
     * other thing this engine does is partial pressures - which is what sets
     * the diffusion gradient. It also forbids 18/45 to EAN50, a switch that is
     * standard practice; on this criterion that switch is quiet at 21 m
     * (+0.41 ata) and correctly flags at 30 m (+0.53). The rule of fifths has
     * had no empirical evaluation; the partial-pressure form is what V-Planner
     * ships and it discriminates where it should.
     *
     * Advisory only. It never changes the schedule. */
    if (s->cfg->icd_warn_bar > 0 && !icd_cc && !s->cc &&
        (fabs(s->fo2 - icd_fo2) > 1e-6 || fabs(s->fhe - icd_fhe) > 1e-6)) {
        double pa = pamb(s, depth);
        double dn2 = ((1.0 - s->fo2 - s->fhe) - (1.0 - icd_fo2 - icd_fhe)) * pa;
        double dhe = (s->fhe - icd_fhe) * pa;
        double rise = dn2 > dhe ? dn2 : dhe;
        if (rise > s->cfg->icd_warn_bar) {
            char m[240];
            snprintf(m, sizeof m,
                "Isobaric counterdiffusion: at the %.0f m switch the inspired %s "
                "rises %.2f ata, above the %.2f limit. Consider switching "
                "shallower, or to a mix that still carries helium.",
                depth / (s->cfg->metric_output ? 1.0 : 0.3048),
                dn2 > dhe ? "nitrogen" : "helium", rise, s->cfg->icd_warn_bar);
            warn(s, m);
        }
    }
}


/* Predictive leave criterion (Readme sec.10 of the original): a stop may be
 * left when the PLANNED ascent from here to `to_m` — at the user's ascent
 * rates, with off-gassing credited along the way — never takes the diver
 * shallower than the instantaneous ceiling. With slow shallow ascent rates,
 * stops shorten or vanish, exactly as the original advertises. */
/* LST_DOMode 0: leaving the LAST stop is judged by allowing an instantaneous
 * ascent to the surface from one stop increment, travelling the diver up to
 * that depth first so the exchange during that travel counts. Requiring the
 * ceiling to reach zero while still at the last stop is LST_DOMode 1, the most
 * conservative of the three treatments the NEDU report defines. */
static bool can_surface_from_increment(const sim *s, double inc_m) {
    sim c = *s;
    while (c.depth - inc_m > 1e-9) {
        double rate = rate_for(c.cfg->ascent, c.cfg->n_ascent, c.depth, 10.0);
        double step = rate * DT;
        if (step > c.depth - inc_m) step = c.depth - inc_m;
        double mid = c.depth - step / 2.0;
        tick(&c, mid, step / rate, false);
        c.depth -= step;
    }
    return (ceiling_bar(&c) - c.p_surface) / c.bar_per_m <= 1e-6;
}

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
    s->out = out;
    s->p_surface = P_SEALEVEL *
        pow(1.0 - 2.25577e-5 * cfg->altitude_m, 5.25588);
    s->bar_per_m = (cfg->salt_water ? RHO_SALT : RHO_FRESH) * G_ACC / 1e5;
    s->n2a = N2_A_C;            /* v1.5: ZHL-16C only */
    vval_pcross_env();
    vval_mptt_he_env();
    vval_slope_env();
    s->ncomp = IS_VVAL(cfg) ? VVAL_NC
             : IS_VPM(cfg)  ? VPM_NC
             : (cfg->use_1b ? ZP_COMPARTMENTS : ZP_COMPARTMENTS - 1);

    s->gf_ref_depth = -1.0;

    if (IS_VPM(cfg)) {
        /* Re-accumulated identically on each critical-volume pass; the
         * relaxed gradients and the surface phase times are what carry over. */
        memset(VPM.max_crush_n2, 0, sizeof VPM.max_crush_n2);
        memset(VPM.max_crush_he, 0, sizeof VPM.max_crush_he);
        memset(VPM.crush_onset,  0, sizeof VPM.crush_onset);
        VPM.max_amb = 0.0;
    }

    /* initial tissues */
    double pn2_sat = alveolar(s->p_surface) * 0.79;
    if (IS_VVAL(cfg)) pn2_sat = (s->p_surface - VVAL_H2O_BAR) * 0.79;
    if (IS_VPM(cfg))  pn2_sat = (s->p_surface - VPM_WV_BAR) * 0.79;
    for (int i = 0; i < ZP_COMPARTMENTS; i++) {
        s->pn2[i] = cfg->have_initial_tissues ? cfg->init_pn2[i] : pn2_sat;
        s->phe[i] = cfg->have_initial_tissues ? cfg->init_phe[i] : 0.0;
    }
    s->cns = cfg->have_initial_tissues ? cfg->init_cns_pct : 0.0;

    /* VVAL-79 is a NITROGEN model. There is no published Navy helium
     * parameter set for it: the Surface-Supplied He-O2 table in the Diving
     * Manual Rev 7 Change A is an edited 1939 table, not model-derived, and
     * NEDU's replacement work (Twenty-First Century Surface-Supplied Heliox
     * Decompression Table Development) uses a different probabilistic
     * linear-exponential model and recommends the MK 16 MOD 1 table instead.
     *
     * The helium handling here — half-times scaled by sqrt(28/4), a mix-weighted
     * MPTT and projection slope — is this engine's own extrapolation and has no
     * reference to be validated against.
     *
     * The total time is NO LONGER the thing to warn about. Since 1.12.0 (linear
     * rate, SDR, venous crossover) and 1.14/1.15 (helium slope and MPTT), VVAL-79
     * trimix runs LONGER than VPM-B, not shorter — measured across 45-90 m on
     * 21/35 through 13/55, it exceeds VPM-B at nominal conservatism every time.
     * The warning used to say the opposite; that dated from 1.12.0 and was left
     * standing after the helium work made it false.
     *
     * What is still wrong is the SHAPE. VVAL-79 has neither gradient factors nor
     * a bubble term, so nothing pulls its first stop deep on a helium mix: on
     * 80 m / 27 min with 15/45 it first stops at 33 m where VPM-B stops at 51 m.
     * That is the caution worth giving, and it is now the one given. */
    if (IS_VVAL(cfg)) {
        for (int w = 0; w < cfg->n_wp; w++)
            if (cfg->wp[w].fhe > 0.001) {
                warn(s, "VVAL-79 is a nitrogen model: the U.S. Navy publishes no "
                        "helium parameters for it, and the helium handling here is "
                        "this project's own unvalidated extrapolation. It begins "
                        "decompression far shallower on helium than a bubble model "
                        "does. Use VPM-B, or ZHL16-C with gradient factors, for "
                        "trimix.");
                break;
            }
    }
    if (cfg->have_initial_tissues && cfg->surface_interval_min > 0) {
        double t = cfg->surface_interval_min;
        for (int i = 0; i < s->ncomp; i++) {
            double htn = IS_VVAL(cfg) ? VVAL_HT_N2[i] : N2_HT[i];
            double hth = IS_VVAL(cfg) ? VVAL_HT_N2[i]/VVAL_HE_RATIO : HE_HT[i];
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
    if (cfg->conservatism_pct > 0 && !(cfg->use_gf && !IS_VVAL(cfg)) && !IS_VPM(cfg)) {
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

    /* Everything from here is the ascent, including deco. Consumption before
     * this point is bottom gas spent getting to and staying at depth. */
    s->in_ascent = true;
    /* Re-measured on every pass. VPM-B's critical volume loop runs the ascent
     * more than once and the tissues differ each time; leaving the first
     * pass's value in place would report a zone start that belongs to a
     * schedule the diver is not being given. */
    if (s->out) s->out->decozone_start_m = 0;

    if (IS_VPM(cfg)) {
        /* Crushing pressure has been accumulating all the way down and along
         * the bottom. Regenerate the nuclei over the dive time, derive the
         * initial allowable gradients, then apply the Critical Volume
         * Algorithm using the deco time measured on the previous pass. */
        vpm_regenerate(cfg, s->runtime);
        if (VPM.deco_min > 0) vpm_next_gradient(VPM.deco_min);
        VPM.first_ceiling = 0;
        VPM.in_zone = 0;
        VPM.zone_runtime = s->runtime;
    }

    double leg_start = s->runtime;
    double vpm_prev_rt = -1.0;      /* run time ending the previous VPM stop */
    double vpm_first_stop = -1.0;   /* VPM-B first stop, set once (see below) */
    while (s->depth > 1e-9) {
        /* mandated deep stop below us? honour it first */
        double next_deep = (deep_idx < n_deep) ? deep[deep_idx].depth : -1;
        if (next_deep > 0 && next_deep < s->depth - 1e-9) {
            travel(s, next_deep, true);
            if (!s->rmv_switched) { s->rmv = cfg->deco_rmv_l_min; s->rmv_switched = 1; }

            /* v1.9.1: deep stops get the deco gas too, if one is permitted at
             * this depth. They used to be breathed on back gas whatever the
             * depth, so a Pyle stop at 18 m stayed on air even though EAN50 is
             * usable from 21.6 m — throwing away exactly the oxygen window the
             * switch exists to exploit, on the stops where the gradient is
             * largest. The switch happens on arrival, so the hold itself and
             * every stop above it are computed on the new mix. */
            double pre_fo2 = s->fo2, pre_fhe = s->fhe; bool pre_cc = s->cc;
            select_deco_source(s, s->depth);
            bool switched = fabs(s->fo2 - pre_fo2) > 1e-6 ||
                            fabs(s->fhe - pre_fhe) > 1e-6 || s->cc != pre_cc;

            /* An extended stop configured for this band applies here as well. */
            double hold = deep[deep_idx].time_min;
            if (switched) {
                double ext = ext_stop_for(cfg, s->depth);
                if (ext > hold) hold = ext;
            }

            double t = hold;
            while (t > 1e-9) { double dt = t<DT?t:DT; tick(s,s->depth,dt,false); t-=dt; }
            if (out->n_lines < ZP_MAX_PLAN_LINES) {
                zp_plan_line *L = &out->lines[out->n_lines++];
                L->kind = ZP_LINE_DEEPSTOP; L->depth_m = s->depth;
                L->stop_sec = hold * 60.0;
                L->runtime_min = s->runtime;
                L->fo2 = s->fo2; L->fhe = s->fhe; L->cc = s->cc;
                L->setpoint = s->setpoint;
                L->ppo2 = display_ppo2(s, s->depth);
                L->end_m = end_m(s, s->depth);
                L->ead_m = ead_m(s, s->depth);
            }
            out->total_deco_min += hold;
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

        /* VPM-B sets the first stop the way Baker does: take the ascent
         * ceiling ONCE, round it UP to the next deeper grid point, and go
         * straight there. Climbing the grid one step at a time instead lets
         * the diver off-gas on every leg, so the ceiling recedes ahead of him
         * and the first stop lands too shallow - measured at 42 m against
         * MultiDeco's 48 m on 70 m / 26 min with 18/45, and unmoved by any
         * amount of conservatism, which is the giveaway. The first stop also
         * anchors Boyle compensation, so getting it wrong propagates through
         * every stop above it. Buhlmann and VVAL-18 keep the climbing rule. */
        if (IS_VPM(cfg) && vpm_first_stop < -0.5) {
            double cd = (ceiling_bar(s) - s->p_surface) / s->bar_per_m;
            if (cd > 1e-9) {
                vpm_first_stop = ceil(cd / stop_iv - 1e-9) * stop_iv;
                if (vpm_first_stop > s->depth)
                    vpm_first_stop = round_to_stop(s->depth, stop_iv);
                if (vpm_first_stop < last_stop) vpm_first_stop = last_stop;
            } else {
                vpm_first_stop = 0.0;          /* no decompression required */
            }
        }
        /* Hold the ascent at that depth until the stop has been taken; once
         * it has, VPM.first_ceiling is set and this goes inert. */
        bool vpm_hold_first = IS_VPM(cfg) && vpm_first_stop > 1e-9
                           && VPM.first_ceiling <= 0
                           && g < vpm_first_stop - 1e-9;

        /* may we ascend to g right now, given the planned ascent? */
        if (!vpm_hold_first && can_leave(s, g)) {
            travel(s, g, true);
            if (g <= 1e-9) break;               /* surfaced */
            /* travel() switches at the mix's MOD mid-water, so by here the
             * gas is normally already correct. This grid-depth check remains as
             * a backstop for a gas that only becomes legal because MaxEND is
             * satisfied at the stop rather than on the way to it. */
            double pre_fo2 = s->fo2, pre_fhe = s->fhe; bool pre_cc = s->cc;
            select_deco_source(s, s->depth);
            if ((fabs(s->fo2 - pre_fo2) > 1e-6 || fabs(s->fhe - pre_fhe) > 1e-6 ||
                 s->cc != pre_cc) && out->n_lines < ZP_MAX_PLAN_LINES) {
                /* Record the switch so the plan shows where it happens. It is
                 * not a stop, so it carries no time. */
                /* Extended stop on the switch, if configured for this band.
                 * Held before the line is filled in so runtime is the time the
                 * diver leaves, matching every other stop line. */
                double ext = ext_stop_for(cfg, s->depth);
                if (ext > 1e-9) {
                    double left = ext;
                    while (left > 1e-9) {
                        double dt = left < DT ? left : DT;
                        tick(s, s->depth, dt, false); left -= dt;
                    }
                    out->total_deco_min += ext;
                }
                zp_plan_line *L = &out->lines[out->n_lines++];
                L->kind = ZP_LINE_GASSWITCH; L->depth_m = s->depth;
                L->stop_sec = ext * 60.0; L->runtime_min = s->runtime;
                L->fo2 = s->fo2; L->fhe = s->fhe; L->cc = s->cc;
                L->setpoint = s->setpoint;
                L->ppo2 = display_ppo2(s, s->depth);
                L->end_m = end_m(s, s->depth);
                L->ead_m = ead_m(s, s->depth);
            }
            continue;                            /* passing through, no stop */
        }

        /* we must stop here (snap to grid if mid-water) */
        double here = round_to_stop(s->depth, stop_iv);
        if (here < last_stop) here = last_stop;
        if (fabs(here - s->depth) > 1e-6) travel(s, here, true);

        if (!s->rmv_switched) { s->rmv = cfg->deco_rmv_l_min; s->rmv_switched = 1; }
        if (s->gf_ref_depth <= 0) s->gf_ref_depth = here;
        /* Capture BEFORE first_ceiling is assigned - the assignment is what
         * marks the first stop as taken, so testing it afterwards can never
         * see zero. */
        bool vpm_is_first_stop = IS_VPM(cfg) && VPM.first_ceiling <= 0
                              && vpm_first_stop > 1e-9
                              && fabs(here - vpm_first_stop) < 1e-6;
        if (IS_VPM(cfg) && VPM.first_ceiling <= 0)
            VPM.first_ceiling = pamb(s, here);
        double stop_pre_fo2 = s->fo2, stop_pre_fhe = s->fhe;
        bool stop_pre_cc = s->cc;
        select_deco_source(s, s->depth);
        bool switched_here = fabs(s->fo2 - stop_pre_fo2) > 1e-6 ||
                             fabs(s->fhe - stop_pre_fhe) > 1e-6 ||
                             s->cc != stop_pre_cc;

        double quantum = 1.0;                    /* whole-minute stops */
        if (here <= last_stop + 1e-9 && cfg->deepstops != ZP_DEEPSTOPS_NONE
            && !IS_VPM(cfg))
            quantum = 0.1;                       /* 6 s final stop, deep mode */
        double min_time = 0;
        for (int mi = 0; mi < n_min; mi++)
            if (fabs(min_hold[mi].depth - here) < 1e-6)
                min_time = min_hold[mi].time_min;
        /* Extended stop on a deco mix switch: hold at least this long here.
         * Going through min_time rather than adding afterwards means the extra
         * time also off-gasses the diver, so the stops above it shorten. */
        if (switched_here) {
            double ext = ext_stop_for(cfg, here);
            if (ext > min_time) min_time = ext;
        }

        double arrive_rt = s->runtime;   /* for whole-minute rounding below */
        double stop_time = 0;
        bool at_last = (here <= last_stop + 1e-9);
        double gg = at_last ? 0
                  : (here - stop_iv < last_stop ? last_stop : here - stop_iv);
        bool lst_do0 = at_last && IS_VVAL(cfg);

        /* VPM-B follows Baker: on arriving at a stop the run time is rounded
         * up to the next whole stop increment BEFORE the ceiling is tested,
         * so the travel leg is absorbed into this stop rather than padded on
         * afterwards. Stop times and run times then both come out as whole
         * minutes - which is what a diver actually copies onto a slate. */
        if (IS_VPM(cfg)) {
            /* Baker takes the first stop unconditionally. By the time the
             * diver has travelled there the ceiling has often receded far
             * enough to allow the next step already - but the stop is still
             * made, and the arrival round-up alone is what fills it. The one
             * minute at 54 m in VPM.OUT is exactly this and nothing else.
             * Without it the engine reached the right depth, emitted nothing,
             * and carried on climbing to a first stop two increments shallow. */
            bool need = (lst_do0 ? !can_surface_from_increment(s, stop_iv)
                                 : !can_leave(s, gg)) || min_time > 1e-9
                        || vpm_is_first_stop;
            if (need) {
                if (vpm_prev_rt < 0) vpm_prev_rt = floor(arrive_rt + 1e-9);
                double pad = ceil(arrive_rt - 1e-9) - arrive_rt;
                while (pad > 1e-9) {
                    double dt = pad < DT ? pad : DT;
                    tick(s, s->depth, dt, false); pad -= dt;
                }
                stop_time = s->runtime - arrive_rt;
            }
        }

        while (((lst_do0 ? !can_surface_from_increment(s, stop_iv)
                         : !can_leave(s, gg)) || stop_time < min_time - 1e-9)
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
        if (stop_time > 1e-9 && IS_VPM(cfg)) {
            /* Report Baker's quantity: the difference between consecutive
             * whole run times. Integral by construction. */
            stop_time = s->runtime - vpm_prev_rt;
            vpm_prev_rt = s->runtime;
        }
        else if (stop_time > 1e-9 && !IS_VVAL(cfg)) {
            double leg = arrive_rt - leg_start;
            double total = leg + stop_time;
            double want = ceil(total - 1e-9);
            double pad = want - total;
            while (pad > 1e-9) { double dt = pad<DT?pad:DT; tick(s,s->depth,dt,false); pad-=dt; }
            stop_time += (want - total);
            out->total_deco_min += (want - total);
        }
        if (stop_time > 1e-9 && first_norm < 0) first_norm = here;
        /* decozone_start_m used to be assigned here, from the first stop that
         * carried any time - so the report's "Deco zone start" was just the
         * first stop under another name. It is now measured in tick(), where
         * the tension actually crosses ambient. See the note there. */
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

    /* Gas density advisory, after Anthony and Mitchell.
     *
     * Density is p(ata) * M / 22.414 - the ideal gas at 0 C referenced to one
     * atmosphere, which is the convention their limits were derived under. It
     * reproduces their anchors: air at 30 m comes out 5.1 g/L and at 39 m
     * 6.3 g/L, against the quoted 5.2 and 6.2.
     *
     * Above roughly 6.2 g/L the work of breathing and CO2 retention rise
     * steeply, and CO2 retention is itself a risk factor for both oxygen
     * toxicity and inert gas narcosis - which is why this is worth saying on
     * the plan rather than leaving to the diver to look up. Advisory only: it
     * changes no schedule, and it is a property of the gas, so it applies
     * whichever model is in use. */
    if (out->bottom_density_gl > 6.2) {
        char m[192];
        snprintf(m, sizeof m,
                 "Bottom gas density %.1f g/L exceeds the 6.2 g/L limit "
                 "(Anthony & Mitchell). Work of breathing and CO2 retention "
                 "rise steeply above it. Add helium.", out->bottom_density_gl);
        warn(s, m);
    } else if (out->bottom_density_gl > 5.2) {
        char m[192];
        snprintf(m, sizeof m,
                 "Bottom gas density %.1f g/L is above the 5.2 g/L ideal "
                 "(Anthony & Mitchell), below the 6.2 g/L limit.",
                 out->bottom_density_gl);
        warn(s, m);
    }
    out->runtime_min = s->runtime;
    out->cns_pct = s->cns;
    out->otu = s->otu;
    out->n_gas_used = s->n_gas;
    out->total_oc_l = 0;
    for (int i = 0; i < s->n_gas; i++) {
        out->gas_used_l[i] = s->gas_l[i];
        out->gas_bottom_l[i] = s->gas_bottom_l[i];
        out->gas_fo2[i] = s->gas_fo2[i];
        out->gas_fhe[i] = s->gas_fhe[i];
        out->total_oc_l += s->gas_l[i];
    }
    memcpy(out->end_pn2, s->pn2, sizeof s->pn2);
    memcpy(out->end_phe, s->phe, sizeof s->phe);
    out->end_cns_pct = s->cns;

    /* The out-of-water half of the phase volume integration, from the tissue
     * state we surface with. Feeds the next critical-volume pass. */
    if (IS_VPM(cfg)) vpm_surface_phase(s);

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

static int zp_plan_pass(const zp_config *cfg, zp_result *out) {
    /* With gradient factors, GF-low provides the deep-stop function; the
     * Pyle post-process is disabled (owner spec, v1.4). */
    if (cfg->use_gf && IS_BUHL(cfg) && cfg->deepstops != ZP_DEEPSTOPS_NONE)
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

int zp_plan(const zp_config *cfg, zp_result *out) {
    if (!IS_VPM(cfg)) return zp_plan_pass(cfg, out);

    /* VPM-B CRITICAL VOLUME LOOP.
     *
     * The allowable gradients are relaxed until the volume of gas released
     * settles. Each relaxation is computed from the INITIAL gradients rather
     * than compounded, so this is a fixed-point iteration on deco time and it
     * converges rather than ratcheting. Baker's rule: converged when the total
     * phase volume time changes by one minute or less in any one compartment.
     *
     * Note this differs by about two minutes from Baker's own VPM.EXE on the
     * 80 msw benchmark, which stops after a single relaxation. See
     * Reference/vpmb_validation.md. */
    vpm_clear();
    double last_pvt[VPM_NC];
    for (int i = 0; i < VPM_NC; i++) last_pvt[i] = 0.0;

    int rc = 0;
    for (int pass = 0; pass < 16; pass++) {
        rc = zp_plan_pass(cfg, out);
        if (rc) return rc;

        double deco = VPM.in_zone ? (out->runtime_min - VPM.zone_runtime)
                                  : out->total_deco_min;
        if (deco < 0) deco = 0;

        int converged = 0;
        for (int i = 0; i < VPM_NC; i++) {
            double pvt = deco + VPM.spvt[i];
            if (pass > 0 && fabs(pvt - last_pvt[i]) <= 1.0) converged = 1;
            last_pvt[i] = pvt;
        }
        if (converged || deco <= 0) break;
        VPM.deco_min = deco;
    }
    return rc;
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
    cfg->vpm_conservatism = 0;        /* Baker nominal VPM-B */
    cfg->vpm_radius_n2_um = 0.6;      /* VPM.SET defaults */
    cfg->vpm_radius_he_um = 0.5;
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
            else if (!strcmp(key, "vpmconservatism")) {
                cfg->vpm_conservatism = atoi(val);
                if (cfg->vpm_conservatism < 0) cfg->vpm_conservatism = 0;
                if (cfg->vpm_conservatism > 4) cfg->vpm_conservatism = 4;
            }
            else if (!strcmp(key, "vpmradiusn2")) cfg->vpm_radius_n2_um = atof(val);
            else if (!strcmp(key, "vpmradiushe")) cfg->vpm_radius_he_um = atof(val);
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
                if (!strncmp(val, "vval", 4)) cfg->use_vval = ZP_MODEL_VVAL;
                else if (!strncmp(val, "vpm", 3)) cfg->use_vval = ZP_MODEL_VPMB;
                else cfg->use_vval = ZP_MODEL_BUHLMANN;  /* any Buhlmann -> ZHL-16C */
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
                    if (*tok) {
                        /* "50" or "50/25". atof() alone silently swallowed the
                         * helium: a diver who entered 50/25 got a schedule
                         * computed for EAN50, on a gas they were not breathing. */
                        double o2 = atof(tok), he = 0.0;
                        char *slash = strchr(tok, '/');
                        if (slash) he = atof(slash + 1);
                        if (o2 + he > 100.0 + 1e-9) { he = 0.0; }
                        cfg->oc_deco_fo2[cfg->n_oc_deco] = o2/100.0;
                        cfg->oc_deco_fhe[cfg->n_oc_deco] = he/100.0;
                        cfg->n_oc_deco++;
                    }
                    tok = strtok(NULL, ",");
                }
            }
            else if (!strcmp(key, "ocdecomaxpo2")) cfg->oc_deco_max_po2 = atof(val);
            else if (!strcmp(key, "extstopshallow")) {
                double v = atof(val); if (v < 0) v = 0; if (v > 10) v = 10;
                cfg->ext_stop_shallow_min = v;
            }
            else if (!strcmp(key, "extstopdeep")) {
                double v = atof(val); if (v < 0) v = 0; if (v > 10) v = 10;
                cfg->ext_stop_deep_min = v;
            }
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
            else if (!strcmp(key, "icdwarn")) {
                double v = atof(val);
                cfg->icd_warn_bar = (v >= 0 && v <= 3.0) ? v : 0.0;
            }
            else if (!strcmp(key, "heslope")) {
                double v = atof(val);
                if (v >= 0.2 && v <= 1.5) VVAL_SLOPE_HE = v;
            }
            else if (!strcmp(key, "hemptt")) {
                double v = atof(val);
                if (v >= 0.5 && v <= 2.5)
                    for (int i = 0; i < VVAL_NC; i++)
                        VVAL_MPTT0_HE_FSW[i] = VVAL_MPTT0_FSW[i] * v;
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
    if (IS_VVAL(cfg))
        APP("              U.S. Navy EL-DCM (VVAL-18)\n\n");
    else if (IS_VPM(cfg))
        APP("        VPM-B  (Yount/Hoffman, Baker)  conservatism %d\n\n",
            cfg->vpm_conservatism);
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
    APP("        depth   stop    run   gas       PO2   EAD\n");
    APP(" ------------------------------------------------------\n");
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
                 * PO2 anyway, and its inert content is already visible in EAD.
                 *
                 * The "CC " prefix has gone too. Every row of a closed-circuit
                 * dive carried it, so it distinguished nothing, and "SP" says
                 * closed circuit on its own — an open-circuit row has no
                 * setpoint to print. Dropping it let the gas field shrink from
                 * eleven columns to nine, which is the width of the widest
                 * thing left in it ("TMX 21/25"). */
                snprintf(gas, sizeof gas, "SP%.2f", L->setpoint);
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
            /* Short inter-stop ascents are normally folded into the stop
             * that follows, which is the MultiDeco convention and matches how
             * ZHL-16C and VVAL-18 have always been reported.
             *
             * VPM-B does not fold. Baker's engine already absorbs the travel
             * leg by rounding the run time up on ARRIVAL, so folding it a
             * second time in the report double-counts it: a whole-minute 1:00
             * stop at 48 m came out as 1:49, and no amount of staring at the
             * decompression code was going to explain it, because the code
             * was right. Ascents get their own arrow instead. */
            bool fold = (!descending && travel > 0.02 && travel < 1.5 &&
                         !gas_changed && L->kind != ZP_LINE_WAYPOINT
                         && !IS_VPM(cfg));
            if (travel > 0.02 && !fold) {
                int tm = (int)travel, ts2 = (int)((travel - tm) * 60 + 0.5);
                if (ts2 == 60) { tm++; ts2 = 0; }
                APP(" %s %5.0f%-2s %3d:%02d %6.0f   %-9s\n",
                    descending ? "\u2193    " : "\u2191    ",
                    L->depth_m * dscale, du, tm, ts2,
                    ceil(arrive - 1e-6),
                    (descending && gas_changed) ? gas : "");
            }
            if (L->kind == ZP_LINE_GASSWITCH) {
                /* Same columns as a stop, marked Gas and carrying no time. */
                double hold = L->stop_sec / 60.0;
                int gm = (int)hold, gs = (int)((hold - gm) * 60 + 0.5);
                if (gs == 60) { gm++; gs = 0; }
                /* GasSw, not Gas: the left column is an event column, and this
                 * row is an event. Five characters, matching DStop, so nothing
                 * else in the layout moves. */
                APP(" %s %5.0f%-2s %3d:%02d %6.0f   %-9s %4.2f %4.0f%-2s\n",
                    "GasSw", L->depth_m * dscale, du, gm, gs,
                    ceil(L->runtime_min - 1e-6), gas,
                    L->ppo2, L->ead_m * dscale, du);
                prev_end = L->runtime_min; prev_depth = L->depth_m;
                pfo2 = L->fo2; pfhe = L->fhe; pcc = (int)L->cc; psp = L->setpoint;
                continue;
            }
            double disp = L->stop_sec / 60.0 + (fold ? travel : 0);
            int mm = (int)disp, ss = (int)((disp - mm) * 60 + 0.5);
            if (ss == 60) { mm++; ss = 0; }
            if (L->stop_sec > 0.5 || L->kind == ZP_LINE_WAYPOINT)
                APP(" %s %5.0f%-2s %3d:%02d %6.0f   %-9s %4.2f %4.0f%-2s\n",
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
        APP(" ------------------------------------------------------\n");
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
        const char *unit = rmv_l ? "Ltr." : "Cu.Ft.";
        if (res->n_gas_used) APP("Consumption (bottom + ascent):\n");
        for (int i = 0; i < res->n_gas_used; i++) {
            /* Name the mix the way the plan table does, rather than "of 21.0%". */
            char name[16];
            double fo2 = res->gas_fo2[i], fhe = res->gas_fhe[i];
            if (fhe > 0.001)                  snprintf(name, sizeof name, "TMX %.0f/%.0f", fo2*100, fhe*100);
            else if (fabs(fo2 - 0.21) < 0.005) snprintf(name, sizeof name, "Air");
            else if (fo2 > 0.995)              snprintf(name, sizeof name, "O2");
            else                               snprintf(name, sizeof name, "EAN%.0f", fo2*100);

            double total  = res->gas_used_l[i]   / vdiv;
            double bottom = res->gas_bottom_l[i] / vdiv;
            double ascent = total - bottom;
            /* Only a back gas is split: it is the one carried down, so the
             * ascent share is what has to still be in the cylinder when the
             * bottom phase ends. A deco gas is breathed on the way up only, so
             * its total is the whole story. */
            if (bottom > 1e-9)
                APP(rmv_l ? "  %-10s %.1f + %.1f = %.1f %s\n"
                          : "  %-10s %.2f + %.2f = %.2f %s\n",
                    name, bottom, ascent, total, unit);
            else
                APP(rmv_l ? "  %-10s %.1f %s\n" : "  %-10s %.2f %s\n",
                    name, total, unit);
        }
    }
    if (res->warnings[0]) APP("\n%s", res->warnings);
    #undef APP
    return (int)off;
}
