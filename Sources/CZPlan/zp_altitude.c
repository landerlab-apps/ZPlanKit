/*
 * zp_altitude.c - ascent to altitude after diving, and surface oxygen.
 * Method 1: Buhlmann ZH-L16C on the engine's end tissues.
 * Method 2: Di Muro, Murphy, Vann, Howle 2020 interconnected model, P(DCS).
 * Copyright (C) 2026 Carlos Lander. GPL-3.0-or-later, as czplan.c.
 */
#include "include/czplan.h"
#include <math.h>
#include <string.h>

#ifndef M_LN2
#define M_LN2 0.69314718055994530942
#endif
#define ALT_NC      ZP_COMPARTMENTS
#define ALT_MAX_SEG 16
#define PH2O        0.0627
#define FN2_AIR     0.79
#define DT          0.1
#define ICM_PH2O    0.0618
#define ICM_DT      0.05
#define ICM_MAX_SEG 256

typedef struct {
    double duration_min;
    double alt_start_m, alt_end_m;
    double fo2;
    int    check;
} alt_segment;

#define ALT_MAX_SEG 16

typedef struct {
    double pn2[ALT_NC], phe[ALT_NC];
    bool   use_1b;
    double cns_pct;
} alt_tissues;

typedef struct {
    double peak_gf;
    double peak_time_min;
    int    peak_comp;
    double peak_alt_m;
    double cns_end_pct;
    double otu;
} alt_eval_result;

typedef struct {
    double target_alt_m;
    double climb_min;
    double stay_min;
    double gf_limit;
    double o2_fo2;
    double o2_min;
    bool   o2_last;
    double wait_min;
    double max_search_min;
} alt_scenario;

typedef struct {
    double gf_at_wait;
    bool   ok_at_wait;
    int    controlling_comp;
    double earliest_min;
    double earliest_air_min;
    double o2_needed_first_min;
    double o2_needed_last_min;
    double cns_o2_pct;
    double otu_o2;
} alt_answers;

typedef enum { ICM_UT = 0, ICM_EE1 = 1 } icm_model_id;
typedef enum { ICM_LIMIT_ADDED = 0, ICM_LIMIT_TOTAL = 1 } icm_limit_kind;

typedef struct {
    double dur_min;
    double pa0_atm, pa1_atm;
    double fo2;
} icm_segment;

typedef struct {
    const char *name;
    double lam[3];
    double S[3][3];
    double alpha[3];
    double b[3];
} icm_params;

typedef struct {
    double p[3];
    double zeta[3];
} icm_state;

typedef struct {
    double target_alt_m, climb_min, stay_min;
    bool   back_to_surface;
    double tail_min;
    double wait_min;
    double o2_fo2, o2_min;
    bool   o2_last;
    double p_limit;
    icm_limit_kind limit_kind;
    double max_search_min;
} icm_scenario;

typedef struct {
    double p_dive;
    double p_trip_no_dive;
    double p_at_wait;
    double added_at_wait;
    bool   ok_at_wait;
    double earliest_min;
    double earliest_air_min;
    double o2_needed_first_min;
    double o2_needed_last_min;
} icm_answers;


static double icm_probability(const icm_params *m, const icm_state *s);

/* ZH-L16C, ZPlanKit order: 16 compartments, then 1b */
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

/* ISA: P = 1.01325 (1 - 2.25577e-5 h)^5.25588 bar */
static double alt_pressure_bar(double alt_m) {
    if (alt_m < 0) alt_m = 0;
    return 1.01325 * pow(1.0 - 2.25577e-5 * alt_m, 5.25588);
}

static int ncomp(const alt_tissues *t) { return t->use_1b ? ALT_NC : 16; }

/* GF needed = (Pt - Pamb) / (M - Pamb), M = Pamb / b + a, a and b weighted by N2/He */
static double alt_gf_required(const alt_tissues *t, double pamb, int *comp) {
    double worst = -1e9; int wi = -1;
    for (int i = 0; i < ncomp(t); i++) {
        double pn = t->pn2[i], ph = t->phe[i], pt = pn + ph;
        if (pt <= 0) continue;
        double a = (N2_A_C[i] * pn + HE_A[i] * ph) / pt;
        double b = (N2_B[i] * pn + HE_B[i] * ph) / pt;
        double m = pamb / b + a;
        double g = (pt - pamb) / (m - pamb);
        if (g > worst) { worst = g; wi = i; }
    }
    if (comp) *comp = wi;
    return worst;
}

static double noaa_limit(double po2) {
    static const double p[] = {0.6,0.7,0.8,0.9,1.0,1.1,1.2,1.3,1.4,1.5,1.6};
    static const double m[] = {720,570,450,360,300,240,210,180,150,120,45};
    if (po2 < 0.6) return 0;
    if (po2 >= 1.6) return 45;
    for (int i = 0; i < 10; i++)
        if (po2 < p[i + 1])
            return m[i] + (m[i + 1] - m[i]) * (po2 - p[i]) / (p[i + 1] - p[i]);
    return 45;
}

/* Haldane: P(t) = Pi + (P0 - Pi) exp(-ln2 t / T); Pi = (Pamb - PH2O) FN2 */
static void step(alt_tissues *t, double pamb, double fo2, double dt) {
    double palv = pamb - PH2O; if (palv < 0) palv = 0;
    double fn2 = (fo2 >= 0.2095 && fo2 <= 0.2105) ? FN2_AIR : 1.0 - fo2;
    double pin2 = palv * fn2;
    for (int i = 0; i < ALT_NC; i++) {
        t->pn2[i] = pin2 + (t->pn2[i] - pin2) * exp(-M_LN2 * dt / N2_HT[i]);
        t->phe[i] = t->phe[i] * exp(-M_LN2 * dt / HE_HT[i]);
    }
    double po2 = pamb * fo2 / 1.01325;
    double lim = noaa_limit(po2);
    if (lim > 0) t->cns_pct += 100.0 * dt / lim;
    else t->cns_pct *= exp(-M_LN2 * dt / 90.0);
}

/* OTU rate = ((pO2 - 0.5) / 0.5)^0.83 per minute */
static double otu_rate(double pamb, double fo2) {
    double po2 = pamb * fo2 / 1.01325;
    return po2 > 0.5 ? pow((po2 - 0.5) / 0.5, 0.83) : 0.0;
}

static void alt_eval(const alt_tissues *t0, const alt_segment *seg, int n, alt_eval_result *r) {
    alt_tissues t = *t0;
    memset(r, 0, sizeof *r);
    r->peak_gf = -1e9; r->peak_comp = -1;
    double clock = 0;
    for (int s = 0; s < n; s++) {
        int steps = (int)ceil(seg[s].duration_min / DT - 1e-9);
        double dt = steps > 0 ? seg[s].duration_min / steps : 0;
        double da = seg[s].alt_end_m - seg[s].alt_start_m;
        for (int k = 0; k < steps; k++) {
            double am = seg[s].alt_start_m + da * (k + 0.5) / steps;
            double a1 = seg[s].alt_start_m + da * (k + 1.0) / steps;
            double pm = alt_pressure_bar(am);
            step(&t, pm, seg[s].fo2, dt);
            r->otu += dt * otu_rate(pm, seg[s].fo2);
            clock += dt;
            if (!seg[s].check) continue;
            int c; double g = alt_gf_required(&t, alt_pressure_bar(a1), &c);
            if (g > r->peak_gf) {
                r->peak_gf = g; r->peak_comp = c;
                r->peak_time_min = clock; r->peak_alt_m = a1;
            }
        }
    }
    r->cns_end_pct = t.cns_pct;
}

static double alt_gf_for_departure(const alt_tissues *t, const alt_scenario *sc,
                            double d, double o2, bool last, int *comp) {
    alt_segment s[ALT_MAX_SEG]; int n = 0;
    if (o2 > d) o2 = d;
    if (o2 > 0 && !last) s[n++] = (alt_segment){ o2, 0, 0, sc->o2_fo2, 0 };
    if (d - o2 > 0)      s[n++] = (alt_segment){ d - o2, 0, 0, 0.21, 0 };
    if (o2 > 0 && last)  s[n++] = (alt_segment){ o2, 0, 0, sc->o2_fo2, 0 };
    s[n++] = (alt_segment){ sc->climb_min, 0, sc->target_alt_m, 0.21, 1 };
    s[n++] = (alt_segment){ sc->stay_min, sc->target_alt_m, sc->target_alt_m, 0.21, 1 };
    alt_eval_result r; alt_eval(t, s, n, &r);
    if (comp) *comp = r.peak_comp;
    return r.peak_gf;
}

static bool fits(const alt_tissues *t, const alt_scenario *sc, double d, double o2, bool last) {
    return alt_gf_for_departure(t, sc, d, o2, last, NULL) <= sc->gf_limit;
}

static double alt_earliest(const alt_tissues *t, const alt_scenario *sc, double o2, bool last) {
    double prev = -1;
    for (double d = 0; d <= sc->max_search_min; d += 30) {
        if (d < o2) continue;
        if (fits(t, sc, d, o2, last)) {
            double lo = prev < 0 ? o2 : prev, hi = d;
            while (hi - lo > 1) {
                double mid = floor((lo + hi) / 2);
                if (fits(t, sc, mid, o2, last)) hi = mid; else lo = mid;
            }
            return fits(t, sc, lo, o2, last) ? lo : hi;
        }
        prev = d;
    }
    return -1;
}

static double alt_o2_needed(const alt_tissues *t, const alt_scenario *sc, bool last) {
    double d = sc->wait_min;
    if (fits(t, sc, d, 0, last)) return 0;
    if (!fits(t, sc, d, d, last)) return -1;
    double lo = 0, hi = d;
    while (hi - lo > 1) {
        double mid = floor((lo + hi) / 2);
        if (fits(t, sc, d, mid, last)) hi = mid; else lo = mid;
    }
    return hi;
}

static void alt_answer(const alt_tissues *t, const alt_scenario *sc, alt_answers *a) {
    memset(a, 0, sizeof *a);
    a->gf_at_wait = alt_gf_for_departure(t, sc, sc->wait_min, sc->o2_min, sc->o2_last,
                                         &a->controlling_comp);
    a->ok_at_wait = a->gf_at_wait <= sc->gf_limit;
    a->earliest_min = alt_earliest(t, sc, sc->o2_min, sc->o2_last);
    a->earliest_air_min = alt_earliest(t, sc, 0, false);
    a->o2_needed_first_min = alt_o2_needed(t, sc, false);
    a->o2_needed_last_min = alt_o2_needed(t, sc, true);
    if (sc->o2_min > 0) {
        alt_tissues c = *t; c.cns_pct = 0;
        alt_segment o = { sc->o2_min, 0, 0, sc->o2_fo2, 0 };
        alt_eval_result r; alt_eval(&c, &o, 1, &r);
        a->cns_o2_pct = r.cns_end_pct; a->otu_o2 = r.otu;
    }
}

static void dive_step(alt_tissues *t, double depth_m, double dt) {
    step(t, 1.01325 + depth_m * 1025.0 * 9.80665 / 1e5, 0.21, dt);
}

static int alt_reference_ndl(double gf, double depth, alt_tissues *out) {
    alt_tissues t; memset(&t, 0, sizeof t);
    double pin = (1.01325 - PH2O) * FN2_AIR;
    for (int i = 0; i < ALT_NC; i++) t.pn2[i] = pin;
    double tdesc = depth / 18.0, tasc = depth / 9.0;
    int nd = (int)ceil(tdesc / DT);
    for (int k = 0; k < nd; k++) dive_step(&t, depth * (k + 0.5) / nd, tdesc / nd);
    int ndl = -1;
    for (int bt = 1; bt <= 600; bt++) {
        for (int k = 0; k < 10; k++) dive_step(&t, depth, 0.1);
        if (alt_gf_required(&t, 1.01325, NULL) > gf) break;
        ndl = bt;
        alt_tissues u = t;
        int na = (int)ceil(tasc / DT);
        for (int k = 0; k < na; k++) dive_step(&u, depth * (1 - (k + 0.5) / na), tasc / na);
        u.cns_pct = 0;
        *out = u;
    }
    return ndl;
}

static double alt_dan_anchor_gf(const alt_tissues *ref) {
    alt_scenario sc = { .target_alt_m = 2438, .climb_min = 20, .stay_min = 240,
                        .gf_limit = 1, .o2_fo2 = 1 };
    return alt_gf_for_departure(ref, &sc, 720, 0, false, NULL);
}

/* Di Muro, Murphy, Vann, Howle 2020, Table 1 (table scaling undone), atm and min */
static const icm_params UT = {
    "UT (Di Muro 2020)",
    { -0.917, -0.0084, -0.00221 },
    { { 1, -0.066, 0.572 }, { 0, 1, -2.899 }, { 0, 0, 1 } },
    { 0.024, 0.000652, 0.00046 },
    { -0.076, 0.435, -0.0367 }
};
static const icm_params EE1 = {
    "EE1 (Di Muro 2020)",
    { -0.424, -0.0214, -0.00217 },
    { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
    { 0.219, 0.0067, 0.00186 },
    { 1.427, 0.370, -0.0519 }
};

static const icm_params *icm_model(icm_model_id id) { return id == ICM_EE1 ? &EE1 : &UT; }

static void inv3(const double a[3][3], double r[3][3]) {
    double d = a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])
             - a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])
             + a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
    r[0][0] =  (a[1][1]*a[2][2]-a[1][2]*a[2][1])/d;
    r[0][1] = -(a[0][1]*a[2][2]-a[0][2]*a[2][1])/d;
    r[0][2] =  (a[0][1]*a[1][2]-a[0][2]*a[1][1])/d;
    r[1][0] = -(a[1][0]*a[2][2]-a[1][2]*a[2][0])/d;
    r[1][1] =  (a[0][0]*a[2][2]-a[0][2]*a[2][0])/d;
    r[1][2] = -(a[0][0]*a[1][2]-a[0][2]*a[1][0])/d;
    r[2][0] =  (a[1][0]*a[2][1]-a[1][1]*a[2][0])/d;
    r[2][1] = -(a[0][0]*a[2][1]-a[0][1]*a[2][0])/d;
    r[2][2] =  (a[0][0]*a[1][1]-a[0][1]*a[1][0])/d;
}

/* pa,n = (1 - FIO2)(Pamb - PH2O); saturated: p = pa,n u */
static void icm_state_saturated(double surf, icm_state *s) {
    double pan = 0.79 * (surf - ICM_PH2O);
    for (int i = 0; i < 3; i++) { s->p[i] = pan; s->zeta[i] = 0; }
}

static double rho(const icm_params *m, double p, int i, double pa) {
    double r = (p - (pa + m->b[i])) / pa;
    return r > 0 ? r : 0;
}

/*
 * dp/dt = A p + f pa,n,  A = S D S^-1,  f = -A u
 * In phi = S^-1 p: phi' = D phi + g pa,n,  g = -D S^-1 u, solved exactly per step
 *   phi(t+h) = phi e^(lambda h) + g pa,n (e^(lambda h) - 1) / lambda
 * rho_i = max(0, (p_i - (Pamb + b_i)) / Pamb), zeta_i += trapezoid(rho_i)
 */
static double icm_run(const icm_params *m, icm_state *st, const icm_segment *s, int n) {
    double Si[3][3]; inv3(m->S, Si);
    double w[3], g[3], phi[3], rprev[3];
    for (int i = 0; i < 3; i++) {
        w[i] = Si[i][0] + Si[i][1] + Si[i][2];
        g[i] = -m->lam[i] * w[i];
        phi[i] = Si[i][0]*st->p[0] + Si[i][1]*st->p[1] + Si[i][2]*st->p[2];
    }
    double pa_start = n > 0 ? s[0].pa0_atm : 1.0;
    for (int i = 0; i < 3; i++) rprev[i] = rho(m, st->p[i], i, pa_start);
    for (int k = 0; k < n; k++) {
        int steps = (int)ceil(s[k].dur_min / ICM_DT - 1e-9);
        if (steps < 1) continue;
        double h = s[k].dur_min / steps, e[3];
        for (int i = 0; i < 3; i++) e[i] = exp(m->lam[i] * h);
        double fn2 = fabs(s[k].fo2 - 0.21) < 1e-3 ? 0.79 : 1.0 - s[k].fo2;
        double dp = s[k].pa1_atm - s[k].pa0_atm;
        for (int j = 0; j < steps; j++) {
            double pm = s[k].pa0_atm + dp * (j + 0.5) / steps;
            double pe = s[k].pa0_atm + dp * (j + 1.0) / steps;
            double pan = pm - ICM_PH2O; if (pan < 0) pan = 0; pan *= fn2;
            for (int i = 0; i < 3; i++)
                phi[i] = phi[i] * e[i] + g[i] * pan * (e[i] - 1.0) / m->lam[i];
            for (int i = 0; i < 3; i++) {
                st->p[i] = m->S[i][0]*phi[0] + m->S[i][1]*phi[1] + m->S[i][2]*phi[2];
                double r = rho(m, st->p[i], i, pe);
                st->zeta[i] += 0.5 * (rprev[i] + r) * h;
                rprev[i] = r;
            }
        }
    }
    return icm_probability(m, st);
}

/* P(DCS) = 1 - exp(-sum alpha_i zeta_i) */
static double icm_probability(const icm_params *m, const icm_state *s) {
    return 1.0 - exp(-(m->alpha[0]*s->zeta[0] + m->alpha[1]*s->zeta[1] + m->alpha[2]*s->zeta[2]));
}

static double icm_p_after(const icm_params *m, double surf, const icm_state *surfaced,
                   const icm_scenario *sc, double d, double o2, bool last, bool trip) {
    icm_segment s[8]; int n = 0;
    if (o2 > d) o2 = d;
    if (o2 > 0 && !last) s[n++] = (icm_segment){ o2, surf, surf, sc->o2_fo2 };
    if (d - o2 > 0)      s[n++] = (icm_segment){ d - o2, surf, surf, 0.21 };
    if (o2 > 0 && last)  s[n++] = (icm_segment){ o2, surf, surf, sc->o2_fo2 };
    double pt = alt_pressure_bar(sc->target_alt_m) / 1.01325;
    double tail = sc->tail_min > 0 ? sc->tail_min : 1440;
    if (!trip) s[n++] = (icm_segment){ 2880, surf, surf, 0.21 };
    else {
        s[n++] = (icm_segment){ sc->climb_min, surf, pt, 0.21 };
        s[n++] = (icm_segment){ sc->stay_min, pt, pt, 0.21 };
        if (sc->back_to_surface) {
            s[n++] = (icm_segment){ 20, pt, surf, 0.21 };
            s[n++] = (icm_segment){ tail, surf, surf, 0.21 };
        } else s[n++] = (icm_segment){ tail, pt, pt, 0.21 };
    }
    icm_state st = *surfaced;
    return icm_run(m, &st, s, n);
}

static double icm_added(const icm_params *m, double surf, const icm_state *st,
                 const icm_scenario *sc, double d, double o2, bool last) {
    return icm_p_after(m, surf, st, sc, d, o2, last, true)
         - icm_p_after(m, surf, st, sc, d, o2, last, false);
}

static bool icm_fits(const icm_params *m, double surf, const icm_state *st,
                 const icm_scenario *sc, double d, double o2, bool last) {
    double v = sc->limit_kind == ICM_LIMIT_TOTAL
             ? icm_p_after(m, surf, st, sc, d, o2, last, true)
             : icm_added(m, surf, st, sc, d, o2, last);
    return v <= sc->p_limit;
}

static double icm_earliest(const icm_params *m, double surf, const icm_state *st,
                       const icm_scenario *sc, double o2, bool last) {
    double prev = -1;
    for (double d = 0; d <= sc->max_search_min; d += 30) {
        if (d < o2) continue;
        if (icm_fits(m, surf, st, sc, d, o2, last)) {
            double lo = prev < 0 ? o2 : prev, hi = d;
            while (hi - lo > 1) {
                double mid = floor((lo + hi) / 2);
                if (icm_fits(m, surf, st, sc, mid, o2, last)) hi = mid; else lo = mid;
            }
            return icm_fits(m, surf, st, sc, lo, o2, last) ? lo : hi;
        }
        prev = d;
    }
    return -1;
}

static double icm_o2_needed(const icm_params *m, double surf, const icm_state *st,
                        const icm_scenario *sc, bool last) {
    double d = sc->wait_min;
    if (icm_fits(m, surf, st, sc, d, 0, last)) return 0;
    if (!icm_fits(m, surf, st, sc, d, d, last)) return -1;
    double lo = 0, hi = d;
    while (hi - lo > 1) {
        double mid = floor((lo + hi) / 2);
        if (icm_fits(m, surf, st, sc, d, mid, last)) hi = mid; else lo = mid;
    }
    return hi;
}

static void icm_answer(const icm_params *m, double surf, const icm_state *st,
                const icm_scenario *sc, icm_answers *a) {
    memset(a, 0, sizeof *a);
    a->p_dive = icm_p_after(m, surf, st, sc, 0, 0, false, false);
    icm_state clean; icm_state_saturated(surf, &clean);
    a->p_trip_no_dive = icm_p_after(m, surf, &clean, sc, 0, 0, false, true);
    a->p_at_wait = icm_p_after(m, surf, st, sc, sc->wait_min, sc->o2_min, sc->o2_last, true);
    a->added_at_wait = icm_added(m, surf, st, sc, sc->wait_min, sc->o2_min, sc->o2_last);
    a->ok_at_wait = icm_fits(m, surf, st, sc, sc->wait_min, sc->o2_min, sc->o2_last);
    a->earliest_min = icm_earliest(m, surf, st, sc, sc->o2_min, sc->o2_last);
    a->earliest_air_min = icm_earliest(m, surf, st, sc, 0, false);
    a->o2_needed_first_min = icm_o2_needed(m, surf, st, sc, false);
    a->o2_needed_last_min = icm_o2_needed(m, surf, st, sc, true);
}

static int dive_segments(const zp_config *cfg, const zp_result *r, icm_segment *out,
                         double *surf_atm, bool *n2only) {
    double surf = alt_pressure_bar(cfg->altitude_m) / 1.01325;
    double apm = (cfg->salt_water ? 1025.0 : 1000.0) * 9.80665 / 1e5 / 1.01325;
    int n = 0; double t = 0, d = 0, g = 0.21;
    *n2only = true;
    for (int i = 0; i < r->n_lines && n < ICM_MAX_SEG - 4; i++) {
        const zp_plan_line *L = &r->lines[i];
        if (L->fhe > 0 || L->cc) *n2only = false;
        double arr = L->runtime_min - L->stop_sec / 60.0;
        if (arr > t + 1e-9)
            out[n++] = (icm_segment){ arr - t, surf + d * apm, surf + L->depth_m * apm, g };
        g = L->fo2;
        if (L->stop_sec > 0)
            out[n++] = (icm_segment){ L->stop_sec / 60.0, surf + L->depth_m * apm,
                                      surf + L->depth_m * apm, g };
        t = L->runtime_min; d = L->depth_m;
    }
    double rate = 9.0;
    for (int i = 0; i < cfg->n_ascent; i++)
        if (d <= cfg->ascent[i].from_m + 1e-9 && d > cfg->ascent[i].to_m - 1e-9)
            rate = cfg->ascent[i].rate_m_min;
    if (d > 0) out[n++] = (icm_segment){ d / rate, surf + d * apm, surf, g };
    *surf_atm = surf;
    return n;
}

static void load_state(const double *v, icm_state *s) {
    for (int i = 0; i < 3; i++) { s->p[i] = v[i]; s->zeta[i] = v[3 + i]; }
}
static void save_state(const icm_state *s, double *v) {
    for (int i = 0; i < 3; i++) { v[i] = s->p[i]; v[3 + i] = s->zeta[i]; }
}

void zp_icm_after_dive(const zp_config *cfg, const zp_result *res,
                       const double *prev, double si, double *out) {
    icm_segment seg[ICM_MAX_SEG]; double surf; bool n2;
    int n = dive_segments(cfg, res, seg, &surf, &n2);
    for (int k = 0; k < 2; k++) {
        const icm_params *m = icm_model(k ? ICM_EE1 : ICM_UT);
        icm_state s;
        if (prev) {
            load_state(prev + 6 * k, &s);
            icm_segment w = { si > 0 ? si : 0, surf, surf, 0.21 };
            icm_run(m, &s, &w, 1);
        } else icm_state_saturated(surf, &s);
        icm_run(m, &s, seg, n);
        save_state(&s, out + 6 * k);
    }
    out[12] = (n2 && (!prev || prev[12] > 0.5)) ? 1 : 0;
}

void zp_altitude(const zp_config *cfg, const zp_result *res, const double *icm,
                 const double *req, double *ans) {
    for (int i = 0; i < ZPA_ANS_N; i++) ans[i] = NAN;
    static const double hours[ZPA_CURVE_N] = { 1, 2, 4, 6, 8, 12, 18, 24 };
    for (int i = 0; i < ZPA_CURVE_N; i++) ans[ZPA_CURVE_HOURS + i] = hours[i];
    bool flight = req[ZPA_REQ_FLIGHT] > 0.5, o2last = req[ZPA_REQ_O2_LAST] > 0.5;
    double fo2 = req[ZPA_REQ_MASK_FO2];
    if (fo2 > 1) fo2 /= 100;
    if (fo2 < 0.21) fo2 = 0.21;
    if (fo2 > 1) fo2 = 1;
    double o2 = req[ZPA_REQ_O2_MIN] > 0 ? req[ZPA_REQ_O2_MIN] : 0;
    double wait = req[ZPA_REQ_WAIT_MIN] > 0 ? req[ZPA_REQ_WAIT_MIN] : 0;

    alt_tissues t; memset(&t, 0, sizeof t);
    memcpy(t.pn2, res->end_pn2, sizeof t.pn2);
    memcpy(t.phe, res->end_phe, sizeof t.phe);
    t.use_1b = cfg->use_1b; t.cns_pct = res->end_cns_pct;
    double gfh = cfg->use_gf ? cfg->gf_hi : 1.0;
    alt_tissues ref; int ndl = alt_reference_ndl(gfh, 18.0, &ref);
    alt_scenario a1 = { .target_alt_m = req[ZPA_REQ_ALT_M], .climb_min = req[ZPA_REQ_TRAVEL_MIN],
                        .stay_min = req[ZPA_REQ_STAY_MIN],
                        .gf_limit = req[ZPA_REQ_M1_DIVE_GF] > 0.5 || ndl <= 0 ? gfh : alt_dan_anchor_gf(&ref),
                        .o2_fo2 = fo2, .o2_min = o2, .o2_last = o2last, .wait_min = wait,
                        .max_search_min = 72 * 60 };
    bool m1 = cfg->use_vval != 1;
    ans[ZPA_M1_AVAILABLE] = m1;
    ans[ZPA_M1_LIMIT_GF] = a1.gf_limit;
    if (m1) {
        alt_answers q; alt_answer(&t, &a1, &q);
        ans[ZPA_M1_GF_AT_WAIT] = q.gf_at_wait;
        ans[ZPA_M1_OK] = q.ok_at_wait;
        ans[ZPA_M1_EARLIEST] = q.earliest_min;
        ans[ZPA_M1_EARLIEST_AIR] = q.earliest_air_min;
        ans[ZPA_M1_O2_FIRST] = q.o2_needed_first_min;
        ans[ZPA_M1_O2_LAST] = q.o2_needed_last_min;
        ans[ZPA_M1_O2_CNS] = q.cns_o2_pct;
        ans[ZPA_M1_O2_OTU] = q.otu_o2;
        for (int i = 0; i < ZPA_CURVE_N; i++)
            ans[ZPA_CURVE_GF + i] = alt_gf_for_departure(&t, &a1, hours[i] * 60, o2, o2last, NULL);
    }

    bool ee1 = req[ZPA_REQ_M2_EE1] > 0.5;
    ans[ZPA_M2_EE1] = ee1;
    bool m2 = icm && icm[12] > 0.5;
    ans[ZPA_M2_AVAILABLE] = m2;
    if (!m2) return;
    double pct = req[ZPA_REQ_M2_LIMIT_PCT];
    if (pct < 0.1) pct = 0.1;
    if (pct > 20) pct = 20;
    double surf = alt_pressure_bar(cfg->altitude_m) / 1.01325;
    const icm_params *m = icm_model(ee1 ? ICM_EE1 : ICM_UT);
    icm_state st; load_state(icm + (ee1 ? 6 : 0), &st);
    icm_scenario s2 = { .target_alt_m = req[ZPA_REQ_ALT_M], .climb_min = req[ZPA_REQ_TRAVEL_MIN],
                        .stay_min = req[ZPA_REQ_STAY_MIN], .back_to_surface = flight,
                        .tail_min = 1440, .wait_min = wait, .o2_fo2 = fo2, .o2_min = o2,
                        .o2_last = o2last, .p_limit = pct / 100,
                        .limit_kind = req[ZPA_REQ_M2_TOTAL] > 0.5 ? ICM_LIMIT_TOTAL : ICM_LIMIT_ADDED,
                        .max_search_min = 72 * 60 };
    icm_answers q; icm_answer(m, surf, &st, &s2, &q);
    ans[ZPA_M2_P_DIVE] = q.p_dive;
    ans[ZPA_M2_P_TRIP_NO_DIVE] = q.p_trip_no_dive;
    ans[ZPA_M2_P_AT_WAIT] = q.p_at_wait;
    ans[ZPA_M2_ADDED_AT_WAIT] = q.added_at_wait;
    ans[ZPA_M2_OK] = q.ok_at_wait;
    ans[ZPA_M2_EARLIEST] = q.earliest_min;
    ans[ZPA_M2_EARLIEST_AIR] = q.earliest_air_min;
    ans[ZPA_M2_O2_FIRST] = q.o2_needed_first_min;
    ans[ZPA_M2_O2_LAST] = q.o2_needed_last_min;
    for (int i = 0; i < ZPA_CURVE_N; i++)
        ans[ZPA_CURVE_ADDED + i] = icm_added(m, surf, &st, &s2, hours[i] * 60, o2, o2last);
}
