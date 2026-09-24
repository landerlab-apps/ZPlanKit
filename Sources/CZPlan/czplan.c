/* czplan.c — the Lplanner decompression engine.
 * See czplan.h for the warning you must read before using any of this.
 *
 * Copyright (C) 2026 Carlos Lander <carlos.lander@etik.com>
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
 * The VVAL-79 implementation follows the U.S. Navy Thalmann EL-DCM as
 * published. Note-Carlos: Until I find a way to add Helium parameters safely, VAAL-79 only handles OC/CCR with AIR and Nitrox mixes.
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

#define ZP_VERSION "1.39.0"
const char *zp_version(void) { return ZP_VERSION; }

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
#define FLY_ALT_M    3048.0

static const double N2_HT[ZP_COMPARTMENTS] = {
    5.0, 8.0, 12.5, 18.5, 27.0, 38.3, 54.3, 77.0,
    109.0, 146.0, 187.0, 239.0, 305.0, 390.0, 498.0, 635.0,
    4.0 };
static const double N2_B[ZP_COMPARTMENTS] = {
    0.5578, 0.6514, 0.7222, 0.7825, 0.8126, 0.8434, 0.8693, 0.8910,
    0.9092, 0.9222, 0.9319, 0.9403, 0.9477, 0.9544, 0.9602, 0.9653,
    0.5050 };
static const double N2_A_C[ZP_COMPARTMENTS] = {
    1.1696, 1.0000, 0.8618, 0.7562, 0.6200, 0.5043, 0.4410, 0.4000,
    0.3750, 0.3500, 0.3295, 0.3065, 0.2835, 0.2610, 0.2480, 0.2327,
    1.2599 };
static const double HE_HT[ZP_COMPARTMENTS] = {
    1.88, 3.02, 4.72, 6.99, 10.21, 14.48, 20.53, 29.11,
    41.20, 55.19, 70.69, 90.34, 115.29, 147.42, 188.24, 240.03,
    1.51 };
static const double HE_A[ZP_COMPARTMENTS] = {
    1.6189, 1.3830, 1.1919, 1.0458, 0.9220, 0.8205, 0.7305, 0.6502,
    0.5950, 0.5545, 0.5333, 0.5189, 0.5181, 0.5176, 0.5172, 0.5119,
    1.7424 };
static const double HE_B[ZP_COMPARTMENTS] = {
    0.4770, 0.5747, 0.6527, 0.7223, 0.7582, 0.7957, 0.8279, 0.8553,
    0.8757, 0.8903, 0.8997, 0.9073, 0.9122, 0.9171, 0.9217, 0.9267,
    0.4245 };

/* (NOAA CNS %/min and REPEX OTU/min vs PO2).                          */
#define ZP_MODEL_BUHLMANN 0
#define ZP_MODEL_VVAL79   1
#define ZP_MODEL_VPMB     2
#define IS_VVAL(c) ((c)->use_vval == ZP_MODEL_VVAL79)
#define IS_VPM(c)  ((c)->use_vval == ZP_MODEL_VPMB)
#define IS_BUHL(c) ((c)->use_vval == ZP_MODEL_BUHLMANN)

#define VPM_NC          16
#define VPM_GAMMA       0.0179      /* surface tension, N/m              */
#define VPM_GAMMAC      0.257       /* skin compression, N/m             */
#define VPM_GRAD_IMPERM_BAR (8.2 * P_SEALEVEL)
#define VPM_LAMBDA_FSW  7500.0      /* critical volume parameter, fsw-min */
#define VPM_REGEN_MIN   20160.0
#define VPM_OTHER_GASES 0.1359888   /* O2+CO2+H2O in tissue, bar (102 mmHg)*/
#define VPM_WV_BAR      0.0493      /* water vapour, Schreiner Rq 0.8, bar */
#define PA_PER_BAR      1.0e5

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
    double deco_min;
    double zone_runtime;
    int    in_zone;
} vpm_state;

static vpm_state VPM;

#define VVAL_NC 9
#define FSW2BAR (1.01325 / 33.0)
#define VVAL_PH2O_BAR   (0.00 * FSW2BAR)
#define VVAL_PACO2_BAR  (1.50 * FSW2BAR)
#define VVAL_PVCO2_BAR  (2.30 * FSW2BAR)
#define VVAL_PVO2_BAR   (2.00 * FSW2BAR)
#define VVAL_AMBAO2_BAR (0.00 * FSW2BAR)
#define VVAL_SDR         0.70
/* 5 mL O2/dL / 0.003 mL/dL/mmHg = 1667 mmHg = 72.4 fsw */
#define VVAL_O2_METABOLIC_BAR (72.4 * FSW2BAR)
#define VVAL_CNDSDR_FO2  0.80
#define VVAL_H2O_BAR (1.85 * FSW2BAR)
static const double VVAL_HT_N2[VVAL_NC] = {
    5.0, 10.0, 20.0, 40.0, 80.0, 120.0, 160.0, 200.0, 240.0 };
static const double VVAL_MPTT0_FSW[VVAL_NC] = {
    99.3, 87.7, 78.0, 56.0, 48.5, 45.5, 44.5, 44.0, 43.5 };
#define VVAL_SLOPE 1.0
/* PBOVP, the overpressure at which washout turns linear: 10.0 fsw for
 * VVal-18M (scient-c_298 Table C.2). sPBOVP = 0.0 fsw at the surface. */
#define VVAL_PBOVP      10.0
#define VVAL_SPBOVP      0.0

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

static void o2tab_lookup(double po2, double *cns_min, double *otu_min) {
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

/* CNS %/min: two-line fit to the log of the NOAA single-exposure table
 * (Baker, "Oxygen Toxicity Calculations"), valid to 1.60 bar; above that the
 * O2TAB values are used. OTU: Baker Eq. 2, REPEX exponent 5/6. Input is the
 * alveolar PO2, ambient less water vapour. */
#define CNS_FIT_TOP  1.60
#define CNS_LIMIT_PCT 100.0
#define CNS_WARN_PCT   80.0

static void o2_rates(double po2, double *cns_min, double *otu_min) {
    if (po2 <= 0.5) { *cns_min = 0; *otu_min = 0; return; }
    if (po2 <= CNS_FIT_TOP) {
        double e = (po2 <= 1.5) ? (-11.7853 + 1.93873 * po2)
                                : (-23.6349 + 9.80829 * po2);
        *cns_min = exp(e) * 6000.0;         /* per-second fraction -> %/min */
    } else {
        double dummy;
        o2tab_lookup(po2, cns_min, &dummy);
    }
    double pm = 2.0 * po2 - 1.0;            /* (Pa + Pb) - 1, Pa == Pb here */
    *otu_min = pow(pm, 5.0 / 6.0);
}

typedef struct {
    const zp_config *cfg;
    double p_surface;              /* bar at dive site */
    double bar_per_m;
    const double *n2a;
    double pn2[ZP_COMPARTMENTS];
    double phe[ZP_COMPARTMENTS];
    double cns, otu;
    double runtime;                /* minutes */
    double depth;                  /* metres */
    double fo2, fhe; bool cc; double setpoint;
    double gas_l[ZP_MAX_GASES], gas_fo2[ZP_MAX_GASES], gas_fhe[ZP_MAX_GASES];
    double gas_bottom_l[ZP_MAX_GASES];
    bool   in_ascent;
    int n_gas;
    double rmv;                    /* current RMV l/min */

    double gf_ref_depth;
    double slide_peak, slide_t0;
    double gf_force;
    zp_result *out;
    int    ncomp;
    int    rmv_switched;
    double on_o2_min;
    bool   freeze_inert;
    bool   cns_armed;
    bool   cc_cns_warned;
    bool   chosen_gas_warned;
    char *warn; size_t warnlen;
} sim;

static void vpm_crush(const sim *s, double p_amb);

#define WARN_COLS 55

static void warn(sim *s, const char *msg) {
    size_t used = strlen(s->warn);
    if (used + 2 >= s->warnlen) return;
    char  *out  = s->warn + used;
    size_t room = s->warnlen - used;
    size_t n = 0, col = 0;

    for (const char *p = msg; *p; ) {
        if (*p == '\n') {
            if (n + 2 >= room) break;
            out[n++] = '\n'; col = 0; p++;
            continue;
        }
        while (*p == ' ') p++;
        if (!*p) break;
        const char *w = p;
        size_t cols = 0;
        while (*p && *p != ' ' && *p != '\n') {
            if (((unsigned char)*p & 0xC0) != 0x80) cols++;
            p++;
        }
        size_t bytes = (size_t)(p - w);
        size_t need  = bytes + (col ? 1 : 0);
        if (col && col + 1 + cols > WARN_COLS) {
            if (n + 1 >= room) break;
            out[n++] = '\n'; col = 0; need = bytes;
        }
        if (n + need + 2 >= room) break;
        if (col) { out[n++] = ' '; col++; }
        memcpy(out + n, w, bytes); n += bytes; col += cols;
    }
    if (n + 2 <= room) { out[n++] = '\n'; out[n] = 0; }
    else out[room - 1] = 0;
}

static double pamb(const sim *s, double depth_m) {
    return s->p_surface + depth_m * s->bar_per_m;
}
static double alveolar(double p_ambient) {
    double p = p_ambient - PH2O;
    return p > 0 ? p : 0;
}
static void inspired(const sim *s, double depth_m,
                     double *pin2, double *pihe, double *pio2) {
    double p_ambient = pamb(s, depth_m);
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
         * The inert tension is split between N2 and He in the ratio the
         * breathing source supplies them. */
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
        if (po2 > pa) po2 = pa;
        double pinert = pa - po2;
        double ftot = s->fo2 + s->fhe;
        double fn2d = 1.0 - ftot;
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
    for (int i = 0; !s->freeze_inert && i < s->ncomp; i++) {
        double dtn = dt, dth = dt;
        if (IS_VVAL(s->cfg)) {
            /* Thalmann EL-DCM: exponential uptake, linear elimination above
             * the crossover, at the exponential slope there (VVAL-79 spec
             * section 6):  dPt/dt = -k * (P_amb + PBOVP - P_art) */
            bool sdr_on = s->cc || (1.0 - s->fo2 - s->fhe) <= (1.0 - VVAL_CNDSDR_FO2);
            double sdr = sdr_on ? VVAL_SDR : 1.0;

            /* "UPDT7" (Figure 30) and the crossover routines (33, 34): the
             * linear/exponential boundary is the VENOUS saturation reference
             * plus the gas-phase overpressure, not ambient plus it.
             *     PVSAT = PAMB - (PVO2 + PVCO2 + PH2O)
             *     boundary tension PVN2 = PVSAT + PBOVP
             * and the linear rate is the exponential slope there,
             *     dP/dt = KDsat * (PA_inert - PVN2). */
            double pvo2 = pio2 - VVAL_O2_METABOLIC_BAR;
            if (pvo2 < VVAL_PVO2_BAR) pvo2 = VVAL_PVO2_BAR;
            double pvsat = pa_now - (pvo2 + VVAL_PVCO2_BAR + VVAL_PH2O_BAR);
            double pbovp = depth_m <= 1e-9 ? VVAL_SPBOVP : VVAL_PBOVP;
            double pcross = pbovp * FSW2BAR;
            double pvn2 = pvsat + pcross;
            double supersat = (s->pn2[i] + s->phe[i]) - pvsat;

            double kn2 = M_LN2 / VVAL_HT_N2[i];
            double kdn2 = kn2 * sdr;
            if (pin2 < s->pn2[i] && supersat > pcross) {
                double rate = kdn2 * (pvn2 - pin2);
                if (rate < 0) rate = 0;
                double p = s->pn2[i] - rate * dt;
                s->pn2[i] = p > pin2 ? p : pin2;
            } else {
                double k = (pin2 < s->pn2[i]) ? kdn2 : kn2;
                s->pn2[i] += (pin2 - s->pn2[i]) * (1.0 - exp(-k * dtn));
            }
            double khe = M_LN2 / VVAL_HT_N2[i];
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
    if (s->in_ascent && s->out && s->out->decozone_start_m <= 0) {
        for (int i = 0; i < s->ncomp; i++)
            if (s->pn2[i] + s->phe[i] + VPM_OTHER_GASES >= pa_now) {
                s->out->decozone_start_m = depth_m;
                break;
            }
    }
    if (IS_VPM(s->cfg)) {
        vpm_crush(s, pa_now);
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

static void vpm_crush(const sim *s, double p_amb) {
    for (int i = 0; i < VPM_NC; i++) {
        double tension  = s->pn2[i] + s->phe[i] + VPM_OTHER_GASES;
        double gradient = p_amb - tension;
        double cn2, che;
        if (gradient <= VPM_GRAD_IMPERM_BAR) {
            cn2 = che = gradient;
            VPM.crush_onset[i] = tension;
        } else {
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
            double m = VVAL_MPTT0_FSW[i];
            double tol = s->p_surface + (pt - m * FSW2BAR) / VVAL_SLOPE;
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

static void   select_deco_source(sim *s, double depth);
static double ext_stop_for(const zp_config *c, double depth_m);
static double display_ppo2(const sim *s, double depth_m);
static double end_m(const sim *s, double depth_m);
static double ead_m(const sim *s, double depth_m);

static double gas_switch_depth(const sim *s, double to_m) {
    const zp_config *c = s->cfg;
    if (!c->use_oc_deco || c->n_oc_deco == 0) return -1;
    double best = -1;
    for (int i = 0; i < c->n_oc_deco; i++) {
        double fo2 = c->oc_deco_fo2[i];
        if (fo2 <= s->fo2 + 1e-6) continue;
        double max_pa = c->oc_deco_max_po2 / fo2 * P_SEALEVEL;
        double d = (max_pa - s->p_surface) / s->bar_per_m;
        double grid = c->stop_distance_m > 0 ? c->stop_distance_m : 3.0;
        d = floor(d / grid + 1e-9) * grid;
        double fhe = c->oc_deco_fhe[i];
        double fnarc = (1.0 - fo2 - fhe) + (c->oxy_narc ? fo2 : 0.0);
        double ref = c->oxy_narc ? 1.0 : 0.79;
        double p_end = (s->p_surface + d * s->bar_per_m) * fnarc / ref;
        if ((p_end - s->p_surface) / s->bar_per_m > c->max_end_m + 1e-6) continue;
        if (d >= s->depth - 1e-9) continue;
        if (d <= to_m + 1e-9) continue;
        if (d > best) best = d;
    }
    return best;
}

static bool travel_gas_needed(const sim *s, double wp_fo2) {
    const zp_config *c = s->cfg;
    if (!c->travel_gas || wp_fo2 <= 0) return false;
    return s->p_surface * wp_fo2 < c->break_min_po2 - 1e-9;
}

static double travel_switch_depth(const sim *s, double wp_fo2, double stop_iv)
{
    const zp_config *c = s->cfg;
    if (stop_iv < 1e-9) stop_iv = 3.0;
    for (double d = stop_iv; d < 100.0; d += stop_iv)
        if (pamb(s, d) * wp_fo2 >= c->break_min_po2 - 1e-9) return d;
    return -1;
}

static bool pick_travel_gas(const sim *s, double switch_m,
                            double *tfo2, double *tfhe)
{
    const zp_config *c = s->cfg;
    double pmax = c->oc_deco_max_po2 > 0 ? c->oc_deco_max_po2 : 1.6;
    double best_fo2 = 2.0, best_fhe = 0.0; bool found = false;
    for (int pass = 0; pass < 2; pass++) {
        int n = pass == 0 ? c->n_oc_deco : c->n_wp;
        for (int i = 0; i < n; i++) {
            double fo2 = pass == 0 ? c->oc_deco_fo2[i] : c->wp[i].fo2;
            double fhe = pass == 0 ? c->oc_deco_fhe[i] : c->wp[i].fhe;
            if (pass == 1 && c->wp[i].cc) continue;
            if (fo2 <= 0) continue;
            if (s->p_surface * fo2 < c->break_min_po2 - 1e-9) continue;
            if (pamb(s, switch_m) * fo2 / P_SEALEVEL > pmax + 1e-6) continue;
            if (fo2 < best_fo2) { best_fo2 = fo2; best_fhe = fhe; found = true; }
        }
    }
    if (found) { *tfo2 = best_fo2; *tfhe = best_fhe; }
    return found;
}

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
    while (fabs(s->depth - to_m) > 1e-9) {
        double rate = ascent
            ? rate_for(s->cfg->ascent,  s->cfg->n_ascent,  s->depth, 10.0)
            : rate_for(s->cfg->descent, s->cfg->n_descent, s->depth, 20.0);
        if (rate < 1.0) rate = 1.0;
        double step = rate * DT;
        if (step > fabs(to_m - s->depth)) step = fabs(to_m - s->depth);
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

/* Subsurface core/planner.cpp: O2 12 * 60 s, break 6 * 60 s */
#define SUBSURF_O2_MIN     12.0
#define SUBSURF_BREAK_MIN   6.0

static bool can_surface_from_increment(const sim *s, double inc_m);
static bool can_leave(const sim *s, double to_m);

static bool on_oxygen_phase(const sim *s, double depth_m) {
    const zp_config *c = s->cfg;
    if (c->air_break_mode == ZP_AB_OFF || s->cc) return false;
    if (depth_m > c->o2_ceiling_m + 1e-9) return false;
    return s->fo2 >= c->o2_trigger_fo2 - 1e-9;
}

static bool on_cns_phase(const sim *s) {
    const zp_config *c = s->cfg;
    if (c->air_break_mode == ZP_AB_OFF || s->cc) return false;
    if (s->cns < c->cns_break_pct - 1e-9) return false;
    return s->fo2 < c->o2_trigger_fo2 - 1e-9;
}

static bool break_phase(const sim *s, double depth_m) {
    return on_oxygen_phase(s, depth_m) || on_cns_phase(s);
}

static bool break_gas_ok(const sim *s, double depth_m, double fo2) {
    const zp_config *c = s->cfg;
    double p = pamb(s, depth_m);
    if (fo2 <= 0) return false;
    if (fo2 >= s->fo2 - 1e-6) return false;
    if (p * fo2 < c->break_min_po2 - 1e-9) return false;
    double pmax = c->oc_deco_max_po2 > 0 ? c->oc_deco_max_po2 : 1.6;
    if (p * fo2 / P_SEALEVEL > pmax + 1e-6) return false;
    return true;
}

static bool back_gas(const zp_config *c, double *fo2, double *fhe) {
    double dmax = -1; int wmax = -1;
    for (int w = 0; w < c->n_wp; w++)
        if (c->wp[w].depth_m > dmax) { dmax = c->wp[w].depth_m; wmax = w; }
    if (wmax < 0 || c->wp[wmax].cc || c->wp[wmax].fo2 <= 0) return false;
    *fo2 = c->wp[wmax].fo2; *fhe = c->wp[wmax].fhe;
    return true;
}

static bool pick_break_gas(const sim *s, double depth_m,
                           double *bfo2, double *bfhe, bool *chosen_bad)
{
    const zp_config *c = s->cfg;
    if (chosen_bad) *chosen_bad = false;

    if (c->break_gas_fo2 > 0) {
        if (break_gas_ok(s, depth_m, c->break_gas_fo2)) {
            *bfo2 = c->break_gas_fo2; *bfhe = c->break_gas_fhe;
            return true;
        }
        if (chosen_bad) *chosen_bad = true;
    }

    double gfo2, gfhe;
    if (back_gas(c, &gfo2, &gfhe) && break_gas_ok(s, depth_m, gfo2)) {
        *bfo2 = gfo2; *bfhe = gfhe;
        return true;
    }

    double best_fo2 = 2.0, best_fhe = 0.0; bool found = false;
    for (int pass = 0; pass < 2; pass++) {
        int n = pass == 0 ? c->n_oc_deco : c->n_wp;
        for (int i = 0; i < n; i++) {
            double fo2 = pass == 0 ? c->oc_deco_fo2[i] : c->wp[i].fo2;
            double fhe = pass == 0 ? c->oc_deco_fhe[i] : c->wp[i].fhe;
            if (!break_gas_ok(s, depth_m, fo2)) continue;
            if (fo2 < best_fo2) { best_fo2 = fo2; best_fhe = fhe; found = true; }
        }
    }
    if (found) { *bfo2 = best_fo2; *bfhe = best_fhe; }
    return found;
}

static int best_deco_gas(const sim *s, double depth_m) {
    const zp_config *c = s->cfg;
    if (!c->use_oc_deco || c->n_oc_deco == 0) return -1;
    int best = -1; double best_fo2 = -1;
    for (int i = 0; i < c->n_oc_deco; i++) {
        double fo2 = c->oc_deco_fo2[i];
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

static double ext_stop_for(const zp_config *c, double depth_m) {
    if (depth_m >= 30.0 - 1e-9) return c->ext_stop_deep_min;
    if (depth_m >= 7.0  - 1e-9) return c->ext_stop_shallow_min;
    return 0.0;
}

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

static void select_deco_source(sim *s, double depth) {
    double icd_fo2 = s->fo2, icd_fhe = s->fhe; bool icd_cc = s->cc;

    double sp = deco_setpoint_for(s->cfg, depth);
    if (sp > 0) { s->cc = true; s->setpoint = sp; return; }
    if (sp == 0) s->cc = false;
    int g = best_deco_gas(s, depth);
    if (g >= 0) {
        s->cc = false;
        s->fo2 = s->cfg->oc_deco_fo2[g];
        s->fhe = s->cfg->oc_deco_fhe[g];
    }

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
    sim c = *s;
    if (s->cfg->use_gf && s->gf_force <= 0) {
        bool no_stop_yet = s->gf_ref_depth <= 0 && to_m <= 1e-9;
        if (no_stop_yet) {
            double lo = s->cfg->gf_lo > 0 ? s->cfg->gf_lo : 0.30;
            double hi = s->cfg->gf_hi > 0 ? s->cfg->gf_hi : 0.85;
            c.gf_force = s->cfg->ndl_gf_low ? lo : hi;
        } else {
            c.gf_force = gf_at(s, to_m);
        }
    }
    double ceil_d = (ceiling_bar(&c) - c.p_surface) / c.bar_per_m;
    return to_m >= ceil_d - 1e-6;
}

typedef struct { double depth; double time_min; } extra_stop;

static double run_air_break(sim *s, double here, zp_result *out,
                            bool lst_do0, double gg, double stop_iv,
                            double *seg_start, int *breaks_here)
{
    const zp_config *c = s->cfg;
    double bfo2, bfhe; bool chosen_bad = false;
    if (!pick_break_gas(s, here, &bfo2, &bfhe, &chosen_bad)) {
        s->on_o2_min = 0;
        if (!strstr(s->warn, "no leaner breathable gas"))
            warn(s, "A break was due but no leaner breathable gas is carried "
                    "at that depth; the break was skipped.");
        return 0;
    }
    if (chosen_bad && !s->chosen_gas_warned) {
        s->chosen_gas_warned = true;
        warn(s, "The break gas chosen in settings is not breathable at the "
                "break depth; the automatic choice was used instead.");
    }
    if (here <= c->last_stop_depth_m + 1e-9) {
        sim probe = *s; probe.out = NULL;
        double t = c->air_break_min;
        while (t > 1e-9) { double dt = t < DT ? t : DT;
                           tick(&probe, here, dt, false); t -= dt; }
        bool done = lst_do0 ? can_surface_from_increment(&probe, stop_iv)
                            : can_leave(&probe, gg);
        if (done) { s->on_o2_min = 0; return 0; }
    }
    double o_fo2 = s->fo2, o_fhe = s->fhe;
    s->fo2 = bfo2; s->fhe = bfhe;
    s->freeze_inert = (c->air_break_mode == ZP_AB_NAVY);
    double t = c->air_break_min;
    while (t > 1e-9) { double dt = t < DT ? t : DT;
                       tick(s, here, dt, false); t -= dt; }
    s->freeze_inert = false;
    if (out && out->n_lines < ZP_MAX_PLAN_LINES) {
        double pre = (s->runtime - c->air_break_min) - *seg_start;
        if (pre > 1e-9) {
            zp_plan_line *P = &out->lines[out->n_lines++];
            P->kind = ZP_LINE_NORMSTOP; P->depth_m = here;
            P->stop_sec = pre * 60.0;
            P->runtime_min = s->runtime - c->air_break_min;
            P->fo2 = o_fo2; P->fhe = o_fhe; P->cc = s->cc;
            P->setpoint = s->setpoint;
            double keep_fo2 = s->fo2, keep_fhe = s->fhe;
            s->fo2 = o_fo2; s->fhe = o_fhe;
            P->ppo2 = display_ppo2(s, here);
            P->end_m = end_m(s, here);
            P->ead_m = ead_m(s, here);
            s->fo2 = keep_fo2; s->fhe = keep_fhe;
        }
    }
    if (out && out->n_lines < ZP_MAX_PLAN_LINES) {
        zp_plan_line *L = &out->lines[out->n_lines++];
        L->kind = ZP_LINE_AIRBREAK; L->depth_m = here;
        L->stop_sec = c->air_break_min * 60.0;
        L->runtime_min = s->runtime;
        L->fo2 = bfo2; L->fhe = bfhe; L->cc = false; L->setpoint = 0;
        L->ppo2 = display_ppo2(s, here);
        L->end_m = end_m(s, here);
        L->ead_m = ead_m(s, here);
    }
    s->fo2 = o_fo2; s->fhe = o_fhe;
    s->on_o2_min = 0;
    *seg_start = s->runtime;
    (*breaks_here)++;
    return c->air_break_min;
}

static double round_to_stop_base(double d, double interval, double base) {
    if (d <= base + 1e-9) return base;
    return base + ceil((d - base) / interval - 1e-9) * interval;
}
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
    s->n2a = N2_A_C;
    s->ncomp = IS_VVAL(cfg) ? VVAL_NC
             : IS_VPM(cfg)  ? VPM_NC
             : (cfg->use_1b ? ZP_COMPARTMENTS : ZP_COMPARTMENTS - 1);

    s->gf_ref_depth = -1.0;

    if (IS_VPM(cfg)) {
        memset(VPM.max_crush_n2, 0, sizeof VPM.max_crush_n2);
        memset(VPM.max_crush_he, 0, sizeof VPM.max_crush_he);
        memset(VPM.crush_onset,  0, sizeof VPM.crush_onset);
        VPM.max_amb = 0.0;
    }

    double pn2_alt = alveolar(s->p_surface) * 0.79;
    double pn2_sea = alveolar(P_SEALEVEL)   * 0.79;
    if (IS_VVAL(cfg)) {
        pn2_alt = (s->p_surface - VVAL_H2O_BAR) * 0.79;
        pn2_sea = (P_SEALEVEL   - VVAL_H2O_BAR) * 0.79;
    }
    if (IS_VPM(cfg)) {
        pn2_alt = (s->p_surface - VPM_WV_BAR) * 0.79;
        pn2_sea = (P_SEALEVEL   - VPM_WV_BAR) * 0.79;
    }
    const double *ht_n2 = IS_VVAL(cfg) ? VVAL_HT_N2 : N2_HT;
    int ht_n = IS_VVAL(cfg) ? VVAL_NC : ZP_COMPARTMENTS;
    for (int i = 0; i < ZP_COMPARTMENTS; i++) {
        double start;
        if (cfg->have_initial_tissues) {
            start = cfg->init_pn2[i];
        } else if (cfg->altitude_equilibrated || cfg->altitude_m <= 1e-9) {
            start = pn2_alt;
        } else {
            double hrs = cfg->hours_at_altitude > 0 ? cfg->hours_at_altitude : 0.0;
            double ht  = ht_n2[i < ht_n ? i : ht_n - 1];
            start = pn2_alt + (pn2_sea - pn2_alt) * exp(-M_LN2 * hrs * 60.0 / ht);
        }
        s->pn2[i] = start;
        s->phe[i] = cfg->have_initial_tissues ? cfg->init_phe[i] : 0.0;
    }
    s->cns = cfg->have_initial_tissues ? cfg->init_cns_pct : 0.0;
    s->cns_armed = false;
    s->cc_cns_warned = false;
    s->chosen_gas_warned = false;

    if (IS_VVAL(cfg)) {
        bool any_he = false;
        for (int w = 0; w < cfg->n_wp; w++)
            if (cfg->wp[w].fhe > 0.001) any_he = true;
        for (int i = 0; i < cfg->n_oc_deco; i++)
            if (cfg->oc_deco_fhe[i] > 0.001) any_he = true;
        if (any_he) {
            warn(s, "VVAL-79 involves air and nitrox only. Since helium "
                    "parameters for the Thalmann algorithm are not provided "
                    "by the U.S. Navy, no schedule has been computed. Use "
                    "VPM-B or ZHL-16C with gradient factors for trimix "
                    "diving.");
            out->n_lines = 0;
            out->refused = true;
            return 0;
        }
    }
    if (cfg->have_initial_tissues && cfg->surface_interval_min > 0) {
        double t = cfg->surface_interval_min;
        for (int i = 0; i < s->ncomp; i++) {
            double htn = IS_VVAL(cfg) ? VVAL_HT_N2[i] : N2_HT[i];
            double hth = IS_VVAL(cfg) ? VVAL_HT_N2[i] : HE_HT[i];
            s->pn2[i] = pn2_alt + (s->pn2[i]-pn2_alt)*exp(-M_LN2*t/htn);
            s->phe[i] = s->phe[i]*exp(-M_LN2*t/hth);
        }
        s->cns *= exp(-M_LN2 * t / 90.0);   /* 90-min O2 half-time */
    }
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
            double extra = pn2_alt * c * w;
            s->pn2[i] += extra * fn2;
            s->phe[i] += extra * fhe;
        }
    }

    if (cfg->use_deco_setpoint) {
        bool any_cc = false;
        for (int w = 0; w < cfg->n_wp; w++) if (cfg->wp[w].cc) any_cc = true;
        if (!any_cc) ((zp_config *)cfg)->use_deco_setpoint = false;
    }
    out->n_lines = 0; out->total_deco_min = 0;
    s->depth = 0; s->runtime = 0; s->rmv = cfg->rmv_l_min;

    if (cfg->n_wp == 0) return -1;

    for (int w = 0; w < cfg->n_wp; w++) {
        const zp_waypoint *wp = &cfg->wp[w];
        bool going_deeper = wp->depth_m > s->depth;
        if (going_deeper) {
            double tfo2 = 0, tfhe = 0, swm = -1;
            double siv = cfg->stop_distance_m > 0 ? cfg->stop_distance_m : 3.0;
            bool use_travel = false;
            if (w == 0 && s->depth < 1e-9 && !wp->cc &&
                travel_gas_needed(s, wp->fo2)) {
                swm = travel_switch_depth(s, wp->fo2, siv);
                if (swm > 0 && swm < wp->depth_m - 1e-9 &&
                    pick_travel_gas(s, swm, &tfo2, &tfhe))
                    use_travel = true;
                else
                    warn(s, "Travel gas is on and the back gas is hypoxic at "
                            "the surface, but no carried mix is breathable "
                            "there; the descent starts on the back gas.");
            }
            if (use_travel) {
                s->fo2 = tfo2; s->fhe = tfhe;
                s->cc = false; s->setpoint = 0;
                travel(s, swm, false);
                if (out && out->n_lines < ZP_MAX_PLAN_LINES) {
                    zp_plan_line *L = &out->lines[out->n_lines++];
                    L->kind = ZP_LINE_TRAVELGAS; L->depth_m = swm;
                    L->stop_sec = 0; L->runtime_min = s->runtime;
                    L->fo2 = tfo2; L->fhe = tfhe; L->cc = false;
                    L->setpoint = 0;
                    L->ppo2 = display_ppo2(s, swm);
                    L->end_m = end_m(s, swm);
                    L->ead_m = ead_m(s, swm);
                }
            }
            s->fo2 = wp->fo2; s->fhe = wp->fhe;
            s->cc = wp->cc; s->setpoint = wp->setpoint;
            travel(s, wp->depth_m, false);
        } else {
            travel(s, wp->depth_m, true);
            s->fo2 = wp->fo2; s->fhe = wp->fhe;
            s->cc = wp->cc; s->setpoint = wp->setpoint;
        }
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

    double stop_iv = cfg->stop_distance_m > 0 ? cfg->stop_distance_m : 3.0;
    double last_stop = cfg->last_stop_depth_m > 0 ? cfg->last_stop_depth_m : 3.0;
    double first_norm = -1;
    int deep_idx = 0;

    s->in_ascent = true;
    if (s->out) s->out->decozone_start_m = 0;

    if (IS_VPM(cfg)) {
        vpm_regenerate(cfg, s->runtime);
        if (VPM.deco_min > 0) vpm_next_gradient(VPM.deco_min);
        VPM.first_ceiling = 0;
        VPM.in_zone = 0;
        VPM.zone_runtime = s->runtime;
    }

    double leg_start = s->runtime;
    double vpm_prev_rt = -1.0;
    double vpm_first_stop = -1.0;
    while (s->depth > 1e-9) {
        double next_deep = (deep_idx < n_deep) ? deep[deep_idx].depth : -1;
        if (next_deep > 0 && next_deep < s->depth - 1e-9) {
            travel(s, next_deep, true);
            if (!s->rmv_switched) { s->rmv = cfg->deco_rmv_l_min; s->rmv_switched = 1; }

            double pre_fo2 = s->fo2, pre_fhe = s->fhe; bool pre_cc = s->cc;
            select_deco_source(s, s->depth);
            bool switched = fabs(s->fo2 - pre_fo2) > 1e-6 ||
                            fabs(s->fhe - pre_fhe) > 1e-6 || s->cc != pre_cc;

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

        double g;
        if (s->depth <= last_stop + 1e-9) g = 0;
        else {
            g = last_stop + ceil((s->depth - last_stop) / stop_iv - 1.0 - 1e-9) * stop_iv;
            if (g < last_stop) g = last_stop;
            if (g >= s->depth - 1e-9) g = s->depth - stop_iv;
            if (g < 0) g = 0;
        }

        if (IS_VPM(cfg) && vpm_first_stop < -0.5) {
            double cd = (ceiling_bar(s) - s->p_surface) / s->bar_per_m;
            if (cd > 1e-9) {
                vpm_first_stop = ceil(cd / stop_iv - 1e-9) * stop_iv;
                if (vpm_first_stop > s->depth)
                    vpm_first_stop = round_to_stop(s->depth, stop_iv);
                if (vpm_first_stop < last_stop) vpm_first_stop = last_stop;
            } else {
                vpm_first_stop = 0.0;
            }
        }
        bool vpm_hold_first = IS_VPM(cfg) && vpm_first_stop > 1e-9
                           && VPM.first_ceiling <= 0
                           && g < vpm_first_stop - 1e-9;

        if (!vpm_hold_first && can_leave(s, g)) {
            travel(s, g, true);
            if (g <= 1e-9) break;
            double pre_fo2 = s->fo2, pre_fhe = s->fhe; bool pre_cc = s->cc;
            select_deco_source(s, s->depth);
            if ((fabs(s->fo2 - pre_fo2) > 1e-6 || fabs(s->fhe - pre_fhe) > 1e-6 ||
                 s->cc != pre_cc) && out->n_lines < ZP_MAX_PLAN_LINES) {
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
            continue;
        }

        double here = round_to_stop_base(s->depth, stop_iv, last_stop);
        if (here < last_stop) here = last_stop;
        if (fabs(here - s->depth) > 1e-6) travel(s, here, true);

        if (!s->rmv_switched) { s->rmv = cfg->deco_rmv_l_min; s->rmv_switched = 1; }
        if (s->gf_ref_depth <= 0) s->gf_ref_depth = here;
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

        double quantum = 1.0;
        if (here <= last_stop + 1e-9 && cfg->deepstops != ZP_DEEPSTOPS_NONE
            && !IS_VPM(cfg))
            quantum = 0.1;
        double min_time = 0;
        for (int mi = 0; mi < n_min; mi++)
            if (fabs(min_hold[mi].depth - here) < 1e-6)
                min_time = min_hold[mi].time_min;
        if (switched_here) {
            double ext = ext_stop_for(cfg, here);
            if (ext > min_time) min_time = ext;
        }

        double arrive_rt = s->runtime;
        double stop_time = 0;
        double seg_start = arrive_rt;
        int    breaks_here = 0;
        bool at_last = (here <= last_stop + 1e-9);
        double gg = at_last ? 0
                  : (here - stop_iv < last_stop ? last_stop : here - stop_iv);
        bool lst_do0 = at_last && IS_VVAL(cfg);

        if (IS_VPM(cfg)) {
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
            if (s->cc && cfg->air_break_mode != ZP_AB_OFF &&
                s->cns >= cfg->cns_break_pct - 1e-9 && !s->cc_cns_warned) {
                s->cc_cns_warned = true;
                char m[224];
                snprintf(m, sizeof m,
                         "CNS reached %.0f%% on closed circuit. No break is "
                         "planned: lower the setpoint to reduce the oxygen "
                         "exposure.", cfg->cns_break_pct);
                warn(s, m);
            }
            if (on_cns_phase(s) && !s->cns_armed) {
                s->cns_armed = true;
                if (s->on_o2_min < cfg->o2_period_min)
                    s->on_o2_min = cfg->o2_period_min;
            }
            if (break_phase(s, here) &&
                s->on_o2_min >= cfg->o2_period_min - 1e-9) {
                double bt = run_air_break(s, here, out, lst_do0, gg, stop_iv,
                                          &seg_start, &breaks_here);
                if (bt > 1e-9) { stop_time += bt; continue; }
            }
            double t = quantum;
            while (t > 1e-9) { double dt = t<DT?t:DT; tick(s,s->depth,dt,false); t-=dt; }
            stop_time += quantum;
            if (break_phase(s, here)) s->on_o2_min += quantum;
        }

        if (stop_time > 1e-9 && IS_VPM(cfg)) {
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
        }
        if (stop_time > 1e-9 && first_norm < 0) first_norm = here;
        int norm_idx = -1;
        if (stop_time > 1e-9 && out->n_lines < ZP_MAX_PLAN_LINES) {
            norm_idx = out->n_lines;
            zp_plan_line *L = &out->lines[out->n_lines++];
            L->kind = ZP_LINE_NORMSTOP; L->depth_m = here;
            L->stop_sec = (breaks_here ? (s->runtime - seg_start) : stop_time) * 60.0;
            L->runtime_min = s->runtime;
            L->fo2 = s->fo2; L->fhe = s->fhe; L->cc = s->cc;
            L->setpoint = s->setpoint;
            L->ppo2 = display_ppo2(s, here);
            L->end_m = end_m(s, here);
            L->ead_m = ead_m(s, here);
        }
        out->total_deco_min += stop_time;

        leg_start = s->runtime;
        (void)norm_idx;
        travel(s, gg, true);
        if (gg <= 1e-9) break;
    }

    {
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

    if (out->cns_pct >= CNS_LIMIT_PCT - 1e-9) {
        char m[224];
        snprintf(m, sizeof m,
                 "CNS %.0f%% reaches or exceeds the 100%% NOAA single-exposure "
                 "limit. Reduce the bottom time, lower the deco PO2, or extend "
                 "the surface interval before diving this plan.", out->cns_pct);
        warn(s, m);
    } else if (out->cns_pct >= CNS_WARN_PCT - 1e-9 && !s->cc_cns_warned) {
        char m[224];
        snprintf(m, sizeof m,
                 "CNS %.0f%% is above %.0f%% of the NOAA single-exposure limit. "
                 "Little margin is left for a longer bottom time or a richer "
                 "deco mix.", out->cns_pct, CNS_WARN_PCT);
        warn(s, m);
    }
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

    if (IS_VPM(cfg)) vpm_surface_phase(s);

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
    if (cfg->use_gf && IS_BUHL(cfg) && cfg->deepstops != ZP_DEEPSTOPS_NONE)
        ((zp_config *)cfg)->deepstops = ZP_DEEPSTOPS_NONE;
    memset(out, 0, sizeof *out);
    extra_stop deep[32]; int n_deep = 0;
    extra_stop base[32]; int n_base = 0;

    if (cfg->deepstops != ZP_DEEPSTOPS_NONE) {
        double max_depth = 0;
        for (int w = 0; w < cfg->n_wp; w++)
            if (cfg->wp[w].depth_m > max_depth) max_depth = cfg->wp[w].depth_m;
        double stop_iv = cfg->stop_distance_m > 0 ? cfg->stop_distance_m : 3.0;
        double t_each = (cfg->deepstops == ZP_DEEPSTOPS_PYLE)
                      ? (cfg->pyle_stop_min > 0 ? cfg->pyle_stop_min : 2.0)
                      : 1.0;

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
            double fn2 = -1;
            if (run_plan(cfg, &tmp, deep, n_deep, base, n_base, &fn2)) break;
            if (fn2 > 0) first_norm = fn2;
        }
        for (int i = 0; i < n_deep; i++)
            for (int j = i + 1; j < n_deep; j++)
                if (deep[j].depth > deep[i].depth) {
                    extra_stop t2 = deep[i]; deep[i] = deep[j]; deep[j] = t2;
                }

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

void zp_config_init(zp_config *cfg) {
    memset(cfg, 0, sizeof *cfg);
    cfg->metric_output = false;
    cfg->salt_water = true;
    cfg->altitude_equilibrated = false;
    cfg->hours_at_altitude = 0.0;
    cfg->conservatism_pct = 0;
    cfg->stop_distance_m = 3.048;
    cfg->last_stop_depth_m = 3.048;
    cfg->deepstops = ZP_DEEPSTOPS_NONE;
    cfg->pyle_stop_min = 2.0;
    cfg->vpm_conservatism = 0;
    cfg->vpm_radius_n2_um = 0.6;
    cfg->vpm_radius_he_um = 0.5;
    cfg->rmv_l_min = 0.6 * L_PER_CUFT;
    cfg->deco_rmv_l_min = 0.6 * L_PER_CUFT;
    cfg->oc_deco_max_po2 = 1.55;
    cfg->max_end_m = 130 / FT_PER_M;
    cfg->air_break_mode  = ZP_AB_OFF;
    cfg->o2_period_min   = 30.0;
    cfg->air_break_min   = 5.0;
    cfg->o2_ceiling_m    = 6.0;
    cfg->break_min_po2   = 0.18;
    cfg->cns_break_pct   = 80.0;
    cfg->break_gas_fo2   = 0.0;
    cfg->break_gas_fhe   = 0.0;
    cfg->travel_gas      = false;
    cfg->o2_trigger_fo2  = 1.00;
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
    cfg->rmv_metric = -1;

    for (const char *q = text; *q; ) {
        char l2[512];
        size_t m2 = 0;
        while (q[m2] && q[m2] != '\n' && m2 < sizeof l2 - 1) m2++;
        memcpy(l2, q, m2); l2[m2] = 0;
        q += m2; if (*q == '\n') q++;
        trim(l2);
        if (!l2[0] || l2[0] == '#') continue;
        char *c2 = strchr(l2, ':');
        if (!c2 || !isalpha((unsigned char)l2[0])) continue;
        *c2 = 0;
        char k2[64]; strncpy(k2, l2, sizeof k2 - 1); k2[63] = 0;
        trim(k2);
        for (char *k = k2; *k; k++) *k = (char)tolower((unsigned char)*k);
        char *v2 = c2 + 1; trim(v2);
        if (!strcmp(k2, "usemetric")) {
            metric = truthy(v2);
            cfg->metric_output = metric;
            d2m = metric ? 1.0 : 1.0 / FT_PER_M;
            v2l = metric ? 1.0 : L_PER_CUFT;
        } else if (!strcmp(k2, "rmvmetric")) {
            cfg->rmv_metric = truthy(v2) ? 1 : 0;
        }
    }

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
            else if (!strcmp(key, "usebvalues")) { }
            else if (!strcmp(key, "debuglevel")) { }
            else if (!strcmp(key, "altitude")) cfg->altitude_m = atof(val) * d2m;
            else if (!strcmp(key, "altitudeequil"))
                cfg->altitude_equilibrated = truthy(val);
            else if (!strcmp(key, "hoursataltitude")) {
                double v = atof(val);
                cfg->hours_at_altitude = v > 0 ? v : 0.0;
            }
            else if (!strcmp(key, "conservatism")) {
                cfg->conservatism_pct = atof(val);
                if (cfg->conservatism_pct > 50) cfg->conservatism_pct = 50;
                if (cfg->conservatism_pct < 0) cfg->conservatism_pct = 0;
            }
            else if (!strcmp(key, "precision")) { }
            else if (!strcmp(key, "stopdistance")) cfg->stop_distance_m = atof(val) * d2m;
            else if (!strcmp(key, "laststopdepth")) cfg->last_stop_depth_m = atof(val) * d2m;
            else if (!strcmp(key, "oxynarc")) cfg->oxy_narc = truthy(val);
            else if (!strcmp(key, "deepstops")) {
                if (*val=='p'||*val=='P') cfg->deepstops = ZP_DEEPSTOPS_PYLE;
                else if (*val=='w'||*val=='W') cfg->deepstops = ZP_DEEPSTOPS_PYLE;
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
            else if (!strcmp(key, "tissuefile")) { }
            else if (!strcmp(key, "surfaceinterval")) {
                int h = 0, m = 0;
                if (sscanf(val, "%d:%d", &h, &m) >= 1)
                    cfg->surface_interval_min = h * 60.0 + m;
            }
            else if (!strcmp(key, "planfile")) { }
            else if (!strcmp(key, "rmv")) cfg->rmv_l_min = atof(val) *
                (cfg->rmv_metric < 0 ? v2l : (cfg->rmv_metric ? 1.0 : L_PER_CUFT));
            else if (!strcmp(key, "decormv")) cfg->deco_rmv_l_min = atof(val) *
                (cfg->rmv_metric < 0 ? v2l : (cfg->rmv_metric ? 1.0 : L_PER_CUFT));
            else if (!strcmp(key, "usegf")) cfg->use_gf = truthy(val);
            else if (!strcmp(key, "gradientfactors")) {
                double lo = 0, hi = 0;
                if (sscanf(val, "%lf , %lf", &lo, &hi) == 2) {
                    cfg->gf_lo = lo > 1.5 ? lo / 100.0 : lo;
                    cfg->gf_hi = hi > 1.5 ? hi / 100.0 : hi;
                    if (cfg->gf_lo > 0 && cfg->gf_hi > 0) cfg->use_gf = true;
                }
            }
            else if (!strcmp(key, "gflow"))  { cfg->gf_lo = atof(val) > 1.0 ? atof(val)/100.0 : atof(val); cfg->use_gf = true; }
            else if (!strcmp(key, "model")) {
                char v[32]; strncpy(v, val, sizeof v - 1); v[sizeof v - 1] = 0;
                trim(v);
                for (char *q = v; *q; q++) *q = (char)tolower((unsigned char)*q);
                if (!strncmp(v, "vval", 4)) cfg->use_vval = ZP_MODEL_VVAL79;
                else if (!strncmp(v, "vpm", 3)) cfg->use_vval = ZP_MODEL_VPMB;
                else {
                    cfg->use_vval = ZP_MODEL_BUHLMANN;
                    if (strncmp(v, "buhl", 4) && strncmp(v, "zhl", 3) &&
                        strncmp(v, "z16", 3) && *v && err)
                        snprintf(err, errlen,
                                 "line %d: unknown model '%s'; using Buhlmann "
                                 "ZHL-16C", lineno, val);
                }
            }
            else if (!strcmp(key, "rmvmetric")) cfg->rmv_metric = truthy(val) ? 1 : 0;
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
            else if (!strcmp(key, "heslope")) { }
            else if (!strcmp(key, "hemptt"))  { }
            else if (!strcmp(key, "airbreaks")) {
                char v[32]; strncpy(v, val, sizeof v - 1); v[31] = 0; trim(v);
                for (char *q = v; *q; q++) *q = (char)tolower((unsigned char)*q);
                if (!strcmp(v, "navy") || !strcmp(v, "deadtime"))
                    cfg->air_break_mode = ZP_AB_NAVY;
                else if (!strcmp(v, "subsurface") || !strcmp(v, "modelled")
                         || !strcmp(v, "modeled"))
                    cfg->air_break_mode = ZP_AB_SUBSURFACE;
                else if (truthy(val)) cfg->air_break_mode = ZP_AB_NAVY;
                else cfg->air_break_mode = ZP_AB_OFF;
            }
            else if (!strcmp(key, "o2period")) cfg->o2_period_min = atof(val);
            else if (!strcmp(key, "airbreaktime")) cfg->air_break_min = atof(val);
            else if (!strcmp(key, "o2ceiling")) cfg->o2_ceiling_m = atof(val) * d2m;
            else if (!strcmp(key, "breakminpo2")) cfg->break_min_po2 = atof(val);
            else if (!strcmp(key, "o2triggerfo2")) cfg->o2_trigger_fo2 = atof(val);
            else if (!strcmp(key, "cnsbreakpct")) cfg->cns_break_pct = atof(val);
            else if (!strcmp(key, "travelgas")) cfg->travel_gas = truthy(val);
            else if (!strcmp(key, "breakgas")) {
                double a = 0, b = 0;
                int nf = sscanf(val, "%lf/%lf", &a, &b);
                if (nf >= 1 && a > 0) {
                    cfg->break_gas_fo2 = a > 1.0 ? a / 100.0 : a;
                    cfg->break_gas_fhe = (nf == 2 && b > 0)
                                       ? (b > 1.0 ? b / 100.0 : b) : 0.0;
                } else { cfg->break_gas_fo2 = 0; cfg->break_gas_fhe = 0; }
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

static const char *dep_s(double v) {
    static char b[4][16]; static int i;
    i = (i + 1) & 3;
    if (fabs(v - floor(v + 0.5)) < 0.05) snprintf(b[i], sizeof b[i], "%5.0f", v);
    else                                 snprintf(b[i], sizeof b[i], "%5.1f", v);
    return b[i];
}
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
    if (res->refused) {
        APP("              U.S. Navy EL-DCM (VVAL-79)\n\n");
        APP("                     NO PLAN COMPUTED\n\n");
        if (res->warnings[0]) APP("%s", res->warnings);
        return (int)off;
    }
    if (IS_VVAL(cfg))
        APP("              U.S. Navy EL-DCM (VVAL-79)\n\n");
    else if (IS_VPM(cfg))
        APP("        VPM-B  (Yount/Hoffman, Baker)  conservatism %d\n\n",
            cfg->vpm_conservatism);
    else if (cfg->use_gf)
        APP("     Buhlmann ZHL-16C + gradient factors %.0f/%.0f\n\n",
            (cfg->gf_lo > 0 ? cfg->gf_lo : 0.30) * 100.0,
            (cfg->gf_hi > 0 ? cfg->gf_hi : 0.85) * 100.0);
    else
        APP("                  Buhlmann ZHL-16C\n\n");
    if (cfg->altitude_m > 1e-9) {
        if (cfg->altitude_equilibrated)
            APP("        Altitude %.0f%s, diver equilibrated\n\n",
                cfg->altitude_m * dscale, du);
        else if (cfg->hours_at_altitude > 0)
            APP("        Altitude %.0f%s, %.0f h at altitude\n\n",
                cfg->altitude_m * dscale, du, cfg->hours_at_altitude);
        else
            APP("        Altitude %.0f%s, diver just arrived\n\n",
                cfg->altitude_m * dscale, du);
    }
    APP("                        DIVE PLAN\n\n");
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
            bool fold = (!descending && travel > 0.02 && travel < 1.5 &&
                         !gas_changed && L->kind != ZP_LINE_WAYPOINT
                         && !IS_VPM(cfg));
            if (travel > 0.02 && !fold) {
                int tm = (int)travel, ts2 = (int)((travel - tm) * 60 + 0.5);
                if (ts2 == 60) { tm++; ts2 = 0; }
                APP(" %s %s%-2s %3d:%02d %6.0f   %-9s\n",
                    descending ? "\u2193    " : "\u2191    ",
                    dep_s(L->depth_m * dscale), du, tm, ts2,
                    ceil(arrive - 1e-6),
                    (descending && gas_changed) ? gas : "");
            }
            if (L->kind == ZP_LINE_AIRBREAK) {
                double hold = L->stop_sec / 60.0;
                int bm = (int)hold, bs = (int)((hold - bm) * 60 + 0.5);
                if (bs == 60) { bm++; bs = 0; }
                APP(" %s %s%-2s %3d:%02d %6.0f   %-9s %4.2f %4.0f%-2s\n",
                    "Break", dep_s(L->depth_m * dscale), du, bm, bs,
                    ceil(L->runtime_min - 1e-6), gas,
                    L->ppo2, L->ead_m * dscale, du);
                prev_end = L->runtime_min; prev_depth = L->depth_m;
                pfo2 = L->fo2; pfhe = L->fhe; pcc = (int)L->cc; psp = L->setpoint;
                continue;
            }
            if (L->kind == ZP_LINE_TRAVELGAS) {
                prev_end = L->runtime_min; prev_depth = L->depth_m;
                pfo2 = L->fo2; pfhe = L->fhe; pcc = (int)L->cc; psp = L->setpoint;
                continue;
            }
            if (L->kind == ZP_LINE_GASSWITCH) {
                double hold = L->stop_sec / 60.0;
                int gm = (int)hold, gs = (int)((hold - gm) * 60 + 0.5);
                if (gs == 60) { gm++; gs = 0; }
                APP(" %s %s%-2s %3d:%02d %6.0f   %-9s %4.2f %4.0f%-2s\n",
                    "GasSw", dep_s(L->depth_m * dscale), du, gm, gs,
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
                APP(" %s %s%-2s %3d:%02d %6.0f   %-9s %4.2f %4.0f%-2s\n",
                    L->kind == ZP_LINE_DEEPSTOP ? "DStop" : "\u2014    ",
                    dep_s(L->depth_m * dscale), du, mm, ss,
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
            APP(" \u2191     %s%-2s %3d:%02d %6.0f\n",
                dep_s(0.0), du, tm, ts2, ceil(res->runtime_min - 1e-6));
        }
        APP(" ------------------------------------------------------\n");
    }
    if (res->decozone_start_m > 0)
        APP("Deco zone start: %.0f%s", res->decozone_start_m * dscale, du);
    if (res->bottom_density_gl > 0)
        APP("%sBottom gas density: %.1f g/l\n",
            res->decozone_start_m > 0 ? "    " : "", res->bottom_density_gl);
    else if (res->decozone_start_m > 0) APP("\n");
    {
        double deco = ceil(res->total_deco_min - 1e-6);
        if (!(deco > 0)) deco = 0;
        APP("\nTOTAL DECO TIME: %.0f minutes.\n", deco);
    }
    APP("DIVE RUN TIME: %.0f minutes.\n", ceil(res->runtime_min - 1e-6));
    APP("CNS Total: %.1f%%\n", res->cns_pct);
    APP("OTU's: %.0f\n\n", res->otu);
    APP("Time to Fly: %.1f Hours\n\n", res->time_to_fly_hr);
    {
        bool rmv_l = cfg->rmv_metric >= 0 ? (cfg->rmv_metric == 1) : metric;
        double vdiv = rmv_l ? 1.0 : L_PER_CUFT;
        const char *unit = rmv_l ? "Ltr." : "Cu.Ft.";
        if (res->n_gas_used) APP("Consumption (bottom + ascent):\n");
        for (int i = 0; i < res->n_gas_used; i++) {
            char name[16];
            double fo2 = res->gas_fo2[i], fhe = res->gas_fhe[i];
            if (fhe > 0.001)                  snprintf(name, sizeof name, "TMX %.0f/%.0f", fo2*100, fhe*100);
            else if (fabs(fo2 - 0.21) < 0.005) snprintf(name, sizeof name, "Air");
            else if (fo2 > 0.995)              snprintf(name, sizeof name, "O2");
            else                               snprintf(name, sizeof name, "EAN%.0f", fo2*100);

            double total  = res->gas_used_l[i]   / vdiv;
            double bottom = res->gas_bottom_l[i] / vdiv;
            double ascent = total - bottom;
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
