/* vpmb_ref.c - standalone port of Erik C. Baker's VPM-B FORTRAN implementation.
 *
 * Reference: Baker's VPM decompression program in FORTRAN, released to the
 * community as "DISTRIBUTE FREELY - CREDIT THE AUTHORS". Subroutine names are
 * preserved from the original so the two can be read side by side.
 *
 * Purpose of this file: reproduce VPM.OUT exactly before any of this is allowed
 * near czplan.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define NC 16          /* Buhlmann compartments, as Baker uses them */
#define ATM 101325.0   /* one atmosphere, Pascals */

/* ---- half-times (min). Baker's arrays, verbatim. Note N2[0] is 5.0, i.e.
 * the ZHL-16 "1b" first compartment, not 4.0. ------------------------------ */
static const double HALF_HE[NC] = {
    1.88, 3.02, 4.72, 6.99, 10.21, 14.48, 20.53, 29.11,
    41.20, 55.19, 70.69, 90.34, 115.29, 147.42, 188.24, 240.03 };
static const double HALF_N2[NC] = {
    5.0, 8.0, 12.5, 18.5, 27.0, 38.3, 54.3, 77.0,
    109.0, 146.0, 187.0, 239.0, 305.0, 390.0, 498.0, 635.0 };

/* ---- program settings, from VPM.SET -------------------------------------- */
typedef struct {
    double min_deco_stop_time;
    double crit_radius_n2_um, crit_radius_he_um;
    int    crit_volume_on;
    double lambda;              /* fsw-min */
    double grad_onset_imperm_atm;
    double gamma;               /* surface tension */
    double gammac;              /* skin compression */
    double regen_time_const;    /* min */
    double p_other_gases_mmhg;
} vpm_settings;

static vpm_settings SET = {
    1.0, 0.6, 0.5, 1, 7500.0, 8.2, 0.0179, 0.257, 20160.0, 102.0
};

/* ---- units --------------------------------------------------------------- */
static double UNITS_FACTOR = 10.1325;   /* msw */
static double WVP          = 0.493;     /* msw, RQ 0.8 Schreiner */
static const double FRACTION_INERT = 0.79;

/* ---- model state --------------------------------------------------------- */
static double He_P[NC], N2_P[NC];
static double He_P_init[NC], N2_P_init[NC];
static double k_he[NC], k_n2[NC];
static double Max_Crush_He[NC], Max_Crush_N2[NC];
static double Adj_Crush_He[NC], Adj_Crush_N2[NC];
static double Init_Crit_Radius_He[NC], Init_Crit_Radius_N2[NC];
static double Adj_Crit_Radius_He[NC], Adj_Crit_Radius_N2[NC];
static double Regen_Radius_He[NC], Regen_Radius_N2[NC];
static double Init_Allow_Grad_He[NC], Init_Allow_Grad_N2[NC];
static double Allow_Grad_He[NC], Allow_Grad_N2[NC];
static double Deco_Grad_He[NC], Deco_Grad_N2[NC];
static double Surface_Phase_Vol_Time[NC], Last_Phase_Vol_Time[NC], Phase_Vol_Time[NC];
static double Amb_P_Onset_Imperm[NC], Gas_Tension_Onset_Imperm[NC];
static double Max_Actual_Gradient[NC];

static double Barometric_Pressure;
static double Constant_Pressure_Other_Gases;
static double Run_Time, Segment_Time;
static int    Segment_Number;
static double Depth_Start_of_Deco_Zone;
static double Ascent_Ceiling_Depth;
static double Deco_Stop_Depth, First_Stop_Depth;

/* gas mixes */
#define MAXMIX 8
static double FO2[MAXMIX], FHe[MAXMIX], FN2[MAXMIX];
static int    Mix_Number = 1;

/* ---- helpers ------------------------------------------------------------- */
static double haldane_eq(double initial, double inspired, double k, double t)
{
    return initial + (inspired - initial) * (1.0 - exp(-k * t));
}

static double schreiner_eq(double initial_inspired, double rate, double t,
                           double k, double initial)
{
    return initial_inspired + rate * (t - 1.0 / k)
         - (initial_inspired - initial - rate / k) * exp(-k * t);
}

/* Solve A*r^3 - B*r^2 - C = 0 on [lo,hi]. Baker uses Newton with a bisection
 * fallback; plain bisection reaches the same root and cannot diverge. */
static double radius_root_finder(double A, double B, double C,
                                 double lo, double hi)
{
    double flo = lo * (lo * (A * lo - B)) - C;
    double fhi = hi * (hi * (A * hi - B)) - C;
    double a = lo, b = hi, fa = flo, mid = 0.0, fm;
    int i;

    if (flo * fhi > 0.0) {
        /* Not bracketed - Baker errors out. Return the bound closest to zero. */
        return (fabs(flo) < fabs(fhi)) ? lo : hi;
    }
    for (i = 0; i < 200; i++) {
        mid = 0.5 * (a + b);
        fm  = mid * (mid * (A * mid - B)) - C;
        if (fm == 0.0 || (b - a) < 1.0e-18 || (b - a) / (fabs(mid) + 1e-30) < 1.0e-14)
            break;
        if (fa * fm < 0.0) { b = mid; }
        else               { a = mid; fa = fm; }
    }
    return mid;
}

/* Baker writes DBLE(INT(x + 0.5)) throughout. Applied to a quantity that is
 * itself already offset by +0.5 this is a ceiling, which is what both uses
 * require: the first stop must round to a depth DEEPER than the ascent ceiling
 * (a 54.6 m ceiling cannot be honoured by stopping at 54 m), and a stop time
 * must round up to the next whole stop-time increment.                       */
static double ceil_inc(double x) { return floor(x + 1.0); }

/* ---- GAS_LOADINGS_ASCENT_DESCENT ---------------------------------------- */
static void gas_loadings_ascent_descent(double starting_depth,
                                        double ending_depth, double rate)
{
    int i;
    double starting_ambient, ii_he, ii_n2, rate_he, rate_n2;

    Segment_Time = (ending_depth - starting_depth) / rate;
    Run_Time    += Segment_Time;
    Segment_Number++;

    starting_ambient = starting_depth + Barometric_Pressure;
    ii_he   = (starting_ambient - WVP) * FHe[Mix_Number - 1];
    ii_n2   = (starting_ambient - WVP) * FN2[Mix_Number - 1];
    rate_he = rate * FHe[Mix_Number - 1];
    rate_n2 = rate * FN2[Mix_Number - 1];

    for (i = 0; i < NC; i++) {
        He_P_init[i] = He_P[i];
        N2_P_init[i] = N2_P[i];
        He_P[i] = schreiner_eq(ii_he, rate_he, Segment_Time, k_he[i], He_P_init[i]);
        N2_P[i] = schreiner_eq(ii_n2, rate_n2, Segment_Time, k_n2[i], N2_P_init[i]);
    }
}

/* ---- GAS_LOADINGS_CONSTANT_DEPTH ---------------------------------------- */
static void gas_loadings_constant_depth(double depth, double run_time_end)
{
    int i;
    double ambient, insp_he, insp_n2;

    Segment_Time = run_time_end - Run_Time;
    Run_Time     = run_time_end;
    Segment_Number++;

    ambient = depth + Barometric_Pressure;
    insp_he = (ambient - WVP) * FHe[Mix_Number - 1];
    insp_n2 = (ambient - WVP) * FN2[Mix_Number - 1];

    for (i = 0; i < NC; i++) {
        He_P[i] = haldane_eq(He_P[i], insp_he, k_he[i], Segment_Time);
        N2_P[i] = haldane_eq(N2_P[i], insp_n2, k_n2[i], Segment_Time);
    }
}

/* ---- ONSET_OF_IMPERMEABILITY -------------------------------------------- */
static void onset_of_impermeability(double starting_ambient_pressure,
                                    double ending_ambient_pressure,
                                    double rate, int i)
{
    double grad_onset = SET.grad_onset_imperm_atm * UNITS_FACTOR;
    double ii_he = (starting_ambient_pressure - WVP) * FHe[Mix_Number - 1];
    double ii_n2 = (starting_ambient_pressure - WVP) * FN2[Mix_Number - 1];
    double rate_he = rate * FHe[Mix_Number - 1];
    double rate_n2 = rate * FN2[Mix_Number - 1];
    double low_bound = 0.0;
    double high_bound = (ending_ambient_pressure - starting_ambient_pressure) / rate;
    double f_low, f_high, t, dchange, mid, hp, np, f_mid;
    int j;

    f_low = starting_ambient_pressure
          - (He_P_init[i] + N2_P_init[i] + Constant_Pressure_Other_Gases)
          - grad_onset;

    hp = schreiner_eq(ii_he, rate_he, high_bound, k_he[i], He_P_init[i]);
    np = schreiner_eq(ii_n2, rate_n2, high_bound, k_n2[i], N2_P_init[i]);
    f_high = ending_ambient_pressure - (hp + np + Constant_Pressure_Other_Gases)
           - grad_onset;

    if (f_high * f_low >= 0.0) {
        Amb_P_Onset_Imperm[i]      = starting_ambient_pressure;
        Gas_Tension_Onset_Imperm[i] = He_P_init[i] + N2_P_init[i]
                                    + Constant_Pressure_Other_Gases;
        return;
    }
    if (f_low < 0.0) { t = low_bound;  dchange = high_bound - low_bound; }
    else             { t = high_bound; dchange = low_bound - high_bound; }

    for (j = 0; j < 100; j++) {
        dchange *= 0.5;
        mid = t + dchange;
        hp = schreiner_eq(ii_he, rate_he, mid, k_he[i], He_P_init[i]);
        np = schreiner_eq(ii_n2, rate_n2, mid, k_n2[i], N2_P_init[i]);
        f_mid = (starting_ambient_pressure + rate * mid)
              - (hp + np + Constant_Pressure_Other_Gases) - grad_onset;
        if (f_mid <= 0.0) t = mid;
        if (fabs(dchange) < 1.0e-3 || f_mid == 0.0) break;
    }
    Amb_P_Onset_Imperm[i] = starting_ambient_pressure + rate * t;
    hp = schreiner_eq(ii_he, rate_he, t, k_he[i], He_P_init[i]);
    np = schreiner_eq(ii_n2, rate_n2, t, k_n2[i], N2_P_init[i]);
    Gas_Tension_Onset_Imperm[i] = hp + np + Constant_Pressure_Other_Gases;
}

static double crushing_pressure_helper(double r_onset, double end_amb_pa,
                                       double amb_onset_pa, double tension_onset_pa,
                                       double grad_onset_pa)
{
    double A = end_amb_pa - amb_onset_pa + tension_onset_pa
             + (2.0 * (SET.gammac - SET.gamma)) / r_onset;
    double B = 2.0 * (SET.gammac - SET.gamma);
    double C = tension_onset_pa * r_onset * r_onset * r_onset;
    double high_bound = r_onset;
    double low_bound  = B / A;
    double r_end = radius_root_finder(A, B, C, low_bound, high_bound);
    double cp = grad_onset_pa + end_amb_pa - amb_onset_pa
              + tension_onset_pa * (1.0 - (r_onset * r_onset * r_onset)
                                        / (r_end * r_end * r_end));
    return (cp / ATM) * UNITS_FACTOR;
}

/* ---- CALC_CRUSHING_PRESSURE --------------------------------------------- */
static void calc_crushing_pressure(double starting_depth, double ending_depth,
                                   double rate)
{
    int i;
    double grad_onset    = SET.grad_onset_imperm_atm * UNITS_FACTOR;
    double grad_onset_pa = SET.grad_onset_imperm_atm * ATM;
    double starting_ambient = starting_depth + Barometric_Pressure;
    double ending_ambient   = ending_depth   + Barometric_Pressure;

    for (i = 0; i < NC; i++) {
        double start_tension = He_P_init[i] + N2_P_init[i] + Constant_Pressure_Other_Gases;
        double start_grad    = starting_ambient - start_tension;
        double end_tension   = He_P[i] + N2_P[i] + Constant_Pressure_Other_Gases;
        double end_grad      = ending_ambient - end_tension;
        double cp_he, cp_n2;

        double r_onset_he = 1.0 / (grad_onset_pa / (2.0 * (SET.gammac - SET.gamma))
                                   + 1.0 / Adj_Crit_Radius_He[i]);
        double r_onset_n2 = 1.0 / (grad_onset_pa / (2.0 * (SET.gammac - SET.gamma))
                                   + 1.0 / Adj_Crit_Radius_N2[i]);

        if (end_grad <= grad_onset) {          /* permeable range */
            cp_he = ending_ambient - end_tension;
            cp_n2 = ending_ambient - end_tension;
        } else {                                /* impermeable range */
            double end_amb_pa, amb_onset_pa, tension_onset_pa;
            if (start_grad == grad_onset) {
                Amb_P_Onset_Imperm[i]       = starting_ambient;
                Gas_Tension_Onset_Imperm[i] = start_tension;
            }
            if (start_grad < grad_onset)
                onset_of_impermeability(starting_ambient, ending_ambient, rate, i);

            end_amb_pa       = (ending_ambient / UNITS_FACTOR) * ATM;
            amb_onset_pa     = (Amb_P_Onset_Imperm[i] / UNITS_FACTOR) * ATM;
            tension_onset_pa = (Gas_Tension_Onset_Imperm[i] / UNITS_FACTOR) * ATM;

            cp_he = crushing_pressure_helper(r_onset_he, end_amb_pa, amb_onset_pa,
                                             tension_onset_pa, grad_onset_pa);
            cp_n2 = crushing_pressure_helper(r_onset_n2, end_amb_pa, amb_onset_pa,
                                             tension_onset_pa, grad_onset_pa);
        }
        if (cp_he > Max_Crush_He[i]) Max_Crush_He[i] = cp_he;
        if (cp_n2 > Max_Crush_N2[i]) Max_Crush_N2[i] = cp_n2;
    }
}

/* ---- NUCLEAR_REGENERATION ------------------------------------------------ */
static void nuclear_regeneration(double dive_time)
{
    int i;
    double two_dg = 2.0 * (SET.gammac - SET.gamma);

    for (i = 0; i < NC; i++) {
        double cp_he_pa = (Max_Crush_He[i] / UNITS_FACTOR) * ATM;
        double cp_n2_pa = (Max_Crush_N2[i] / UNITS_FACTOR) * ATM;
        double r_end_he = 1.0 / (cp_he_pa / two_dg + 1.0 / Adj_Crit_Radius_He[i]);
        double r_end_n2 = 1.0 / (cp_n2_pa / two_dg + 1.0 / Adj_Crit_Radius_N2[i]);
        double ratio_he, ratio_n2;

        Regen_Radius_He[i] = Adj_Crit_Radius_He[i]
            + (r_end_he - Adj_Crit_Radius_He[i]) * exp(-dive_time / SET.regen_time_const);
        Regen_Radius_N2[i] = Adj_Crit_Radius_N2[i]
            + (r_end_n2 - Adj_Crit_Radius_N2[i]) * exp(-dive_time / SET.regen_time_const);

        ratio_he = (r_end_he * (Adj_Crit_Radius_He[i] - Regen_Radius_He[i]))
                 / (Regen_Radius_He[i] * (Adj_Crit_Radius_He[i] - r_end_he));
        ratio_n2 = (r_end_n2 * (Adj_Crit_Radius_N2[i] - Regen_Radius_N2[i]))
                 / (Regen_Radius_N2[i] * (Adj_Crit_Radius_N2[i] - r_end_n2));

        Adj_Crush_He[i] = ((cp_he_pa * ratio_he) / ATM) * UNITS_FACTOR;
        Adj_Crush_N2[i] = ((cp_n2_pa * ratio_n2) / ATM) * UNITS_FACTOR;
    }
}

/* ---- CALC_INITIAL_ALLOWABLE_GRADIENT ------------------------------------ */
static void calc_initial_allowable_gradient(void)
{
    int i;
    double num = 2.0 * SET.gamma * (SET.gammac - SET.gamma);

    for (i = 0; i < NC; i++) {
        double g_n2_pa = num / (Regen_Radius_N2[i] * SET.gammac);
        double g_he_pa = num / (Regen_Radius_He[i] * SET.gammac);
        Init_Allow_Grad_N2[i] = (g_n2_pa / ATM) * UNITS_FACTOR;
        Init_Allow_Grad_He[i] = (g_he_pa / ATM) * UNITS_FACTOR;
        Allow_Grad_N2[i] = Init_Allow_Grad_N2[i];
        Allow_Grad_He[i] = Init_Allow_Grad_He[i];
    }
}

/* ---- CALC_START_OF_DECO_ZONE -------------------------------------------- */
static void calc_start_of_deco_zone(double starting_depth, double rate)
{
    int i, j;
    double starting_ambient = starting_depth + Barometric_Pressure;
    double ii_he = (starting_ambient - WVP) * FHe[Mix_Number - 1];
    double ii_n2 = (starting_ambient - WVP) * FN2[Mix_Number - 1];
    double rate_he = rate * FHe[Mix_Number - 1];
    double rate_n2 = rate * FN2[Mix_Number - 1];
    double low_bound = 0.0;
    double high_bound = -1.0 * (starting_ambient / rate);

    Depth_Start_of_Deco_Zone = 0.0;

    for (i = 0; i < NC; i++) {
        double hp0 = He_P[i], np0 = N2_P[i];
        double f_low = hp0 + np0 + Constant_Pressure_Other_Gases - starting_ambient;
        double hp = schreiner_eq(ii_he, rate_he, high_bound, k_he[i], hp0);
        double np = schreiner_eq(ii_n2, rate_n2, high_bound, k_n2[i], np0);
        double f_high = hp + np + Constant_Pressure_Other_Gases;
        double t, dchange, mid, f_mid, cpt_depth;

        if (f_high * f_low >= 0.0) continue;   /* Baker raises; skip compartment */

        if (f_low < 0.0) { t = low_bound;  dchange = high_bound - low_bound; }
        else             { t = high_bound; dchange = low_bound - high_bound; }

        for (j = 0; j < 100; j++) {
            dchange *= 0.5;
            mid = t + dchange;
            hp = schreiner_eq(ii_he, rate_he, mid, k_he[i], hp0);
            np = schreiner_eq(ii_n2, rate_n2, mid, k_n2[i], np0);
            f_mid = hp + np + Constant_Pressure_Other_Gases
                  - (starting_ambient + rate * mid);
            if (f_mid <= 0.0) t = mid;
            if (fabs(dchange) < 1.0e-3 || f_mid == 0.0) break;
        }
        cpt_depth = (starting_ambient + rate * t) - Barometric_Pressure;
        if (cpt_depth > Depth_Start_of_Deco_Zone) Depth_Start_of_Deco_Zone = cpt_depth;
    }
}

/* ---- CALC_ASCENT_CEILING ------------------------------------------------ */
static void calc_ascent_ceiling(void)
{
    int i;
    double deepest = -1.0e30;

    for (i = 0; i < NC; i++) {
        double gas_loading = He_P[i] + N2_P[i];
        double wag, tol, ceil_i;

        if (gas_loading > 0.0) {
            wag = (Allow_Grad_He[i] * He_P[i] + Allow_Grad_N2[i] * N2_P[i]) / gas_loading;
            tol = (gas_loading + Constant_Pressure_Other_Gases) - wag;
        } else {
            wag = (Allow_Grad_He[i] < Allow_Grad_N2[i]) ? Allow_Grad_He[i] : Allow_Grad_N2[i];
            tol = Constant_Pressure_Other_Gases - wag;
        }
        if (tol < 0.0) tol = 0.0;
        ceil_i = tol - Barometric_Pressure;
        if (ceil_i > deepest) deepest = ceil_i;
    }
    Ascent_Ceiling_Depth = deepest;
}

/* ---- CALC_DECO_CEILING -------------------------------------------------- */
static double calc_deco_ceiling(void)
{
    int i;
    double deepest = -1.0e30;

    for (i = 0; i < NC; i++) {
        double gas_loading = He_P[i] + N2_P[i];
        double wag, tol, ceil_i;

        if (gas_loading > 0.0) {
            wag = (Deco_Grad_He[i] * He_P[i] + Deco_Grad_N2[i] * N2_P[i]) / gas_loading;
            tol = (gas_loading + Constant_Pressure_Other_Gases) - wag;
        } else {
            wag = (Deco_Grad_He[i] < Deco_Grad_N2[i]) ? Deco_Grad_He[i] : Deco_Grad_N2[i];
            tol = Constant_Pressure_Other_Gases - wag;
        }
        if (tol < 0.0) tol = 0.0;
        ceil_i = tol - Barometric_Pressure;
        if (ceil_i > deepest) deepest = ceil_i;
    }
    return deepest;
}

/* ---- PROJECTED_ASCENT --------------------------------------------------- */
static void projected_ascent(double starting_depth, double rate, double step_size)
{
    int i;
    double new_ambient = Deco_Stop_Depth + Barometric_Pressure;
    double starting_ambient = starting_depth + Barometric_Pressure;
    double ii_he = (starting_ambient - WVP) * FHe[Mix_Number - 1];
    double ii_n2 = (starting_ambient - WVP) * FN2[Mix_Number - 1];
    double rate_he = rate * FHe[Mix_Number - 1];
    double rate_n2 = rate * FN2[Mix_Number - 1];
    double init_he[NC], init_n2[NC], temp_load[NC], allow_load[NC];

    for (i = 0; i < NC; i++) { init_he[i] = He_P[i]; init_n2[i] = N2_P[i]; }

    for (;;) {
        double ending_ambient = new_ambient;
        double seg_t = (ending_ambient - starting_ambient) / rate;
        int violated = 0;

        for (i = 0; i < NC; i++) {
            double hp = schreiner_eq(ii_he, rate_he, seg_t, k_he[i], init_he[i]);
            double np = schreiner_eq(ii_n2, rate_n2, seg_t, k_n2[i], init_n2[i]);
            double wag;
            temp_load[i] = hp + np;
            if (temp_load[i] > 0.0)
                wag = (Allow_Grad_He[i] * hp + Allow_Grad_N2[i] * np) / temp_load[i];
            else
                wag = (Allow_Grad_He[i] < Allow_Grad_N2[i]) ? Allow_Grad_He[i] : Allow_Grad_N2[i];
            allow_load[i] = ending_ambient + wag - Constant_Pressure_Other_Gases;
        }
        for (i = 0; i < NC; i++) {
            if (temp_load[i] > allow_load[i]) {
                new_ambient    = ending_ambient + step_size;
                Deco_Stop_Depth += step_size;
                violated = 1;
                break;
            }
        }
        if (!violated) break;
    }
}

/* ---- BOYLES_LAW_COMPENSATION -------------------------------------------- */
static double calculate_deco_gradient(double allowable_gradient,
                                      double amb_first_pa, double amb_next_pa)
{
    double allow_first_pa = (allowable_gradient / UNITS_FACTOR) * ATM;
    double r_first = (2.0 * SET.gamma) / allow_first_pa;
    double A = amb_next_pa;
    double B = -2.0 * SET.gamma;
    double C = (amb_first_pa + (2.0 * SET.gamma) / r_first) * r_first * (r_first * r_first);
    double low_bound  = r_first;
    double high_bound = r_first * pow(amb_first_pa / amb_next_pa, 1.0 / 3.0);
    double r_end = radius_root_finder(A, B, C, low_bound, high_bound);
    double grad_pa = (2.0 * SET.gamma) / r_end;
    return (grad_pa / ATM) * UNITS_FACTOR;
}

static void boyles_law_compensation(double first_stop_depth,
                                    double deco_stop_depth, double step_size)
{
    int i;
    double next_stop = deco_stop_depth - step_size;
    double amb_first = first_stop_depth + Barometric_Pressure;
    double amb_next  = next_stop + Barometric_Pressure;
    double amb_first_pa = (amb_first / UNITS_FACTOR) * ATM;
    double amb_next_pa  = (amb_next  / UNITS_FACTOR) * ATM;

    for (i = 0; i < NC; i++) {
        Deco_Grad_He[i] = calculate_deco_gradient(Allow_Grad_He[i], amb_first_pa, amb_next_pa);
        Deco_Grad_N2[i] = calculate_deco_gradient(Allow_Grad_N2[i], amb_first_pa, amb_next_pa);
    }
}

/* ---- DECOMPRESSION_STOP ------------------------------------------------- */
static int decompression_stop(double deco_stop_depth, double step_size)
{
    int i;
    double last_run_time = Run_Time;
    double round_up = ceil_inc(last_run_time / SET.min_deco_stop_time)
                    * SET.min_deco_stop_time;
    double temp_segment_time;
    double ambient = deco_stop_depth + Barometric_Pressure;
    double next_stop = deco_stop_depth - step_size;
    double insp_he, insp_n2;

    Segment_Time = round_up - Run_Time;
    Run_Time     = round_up;
    temp_segment_time = Segment_Time;
    Segment_Number++;

    insp_he = (ambient - WVP) * FHe[Mix_Number - 1];
    insp_n2 = (ambient - WVP) * FN2[Mix_Number - 1];

    for (i = 0; i < NC; i++) {
        if (insp_he + insp_n2 > 0.0) {
            double wag = (Deco_Grad_He[i] * insp_he + Deco_Grad_N2[i] * insp_n2)
                       / (insp_he + insp_n2);
            if ((insp_he + insp_n2 + Constant_Pressure_Other_Gases - wag)
                > (next_stop + Barometric_Pressure)) {
                fprintf(stderr, "OFF-GASSING GRADIENT TOO SMALL AT %.1f\n", deco_stop_depth);
                return -1;
            }
        }
    }
    for (;;) {
        double ceiling;
        for (i = 0; i < NC; i++) {
            He_P[i] = haldane_eq(He_P[i], insp_he, k_he[i], Segment_Time);
            N2_P[i] = haldane_eq(N2_P[i], insp_n2, k_n2[i], Segment_Time);
        }
        ceiling = calc_deco_ceiling();
        if (ceiling > next_stop) {
            Segment_Time      = SET.min_deco_stop_time;
            temp_segment_time += SET.min_deco_stop_time;
            Run_Time          += SET.min_deco_stop_time;
            continue;
        }
        break;
    }
    Segment_Time = temp_segment_time;
    return 0;
}

/* ---- CRITICAL_VOLUME ---------------------------------------------------- */
static void critical_volume(double deco_phase_volume_time)
{
    int i;
    double lambda_pa = (SET.lambda / 33.0) * ATM;

    for (i = 0; i < NC; i++) {
        double pvt = deco_phase_volume_time + Surface_Phase_Vol_Time[i];
        double adj_he_pa = (Adj_Crush_He[i] / UNITS_FACTOR) * ATM;
        double adj_n2_pa = (Adj_Crush_N2[i] / UNITS_FACTOR) * ATM;
        double ig_he_pa  = (Init_Allow_Grad_He[i] / UNITS_FACTOR) * ATM;
        double ig_n2_pa  = (Init_Allow_Grad_N2[i] / UNITS_FACTOR) * ATM;
        double B, C, g;

        B = ig_he_pa + (lambda_pa * SET.gamma) / (SET.gammac * pvt);
        C = (SET.gamma * (SET.gamma * (lambda_pa * adj_he_pa)))
          / (SET.gammac * (SET.gammac * pvt));
        g = (B + sqrt(B * B - 4.0 * C)) / 2.0;
        Allow_Grad_He[i] = (g / ATM) * UNITS_FACTOR;

        B = ig_n2_pa + (lambda_pa * SET.gamma) / (SET.gammac * pvt);
        C = (SET.gamma * (SET.gamma * (lambda_pa * adj_n2_pa)))
          / (SET.gammac * (SET.gammac * pvt));
        g = (B + sqrt(B * B - 4.0 * C)) / 2.0;
        Allow_Grad_N2[i] = (g / ATM) * UNITS_FACTOR;
    }
}

/* ---- CALC_SURFACE_PHASE_VOLUME_TIME ------------------------------------- */
static void calc_surface_phase_volume_time(void)
{
    int i;
    double surf_n2 = (Barometric_Pressure - WVP) * FRACTION_INERT;

    for (i = 0; i < NC; i++) {
        if (N2_P[i] > surf_n2) {
            Surface_Phase_Vol_Time[i] =
                (He_P[i] / k_he[i] + (N2_P[i] - surf_n2) / k_n2[i])
                / (He_P[i] + N2_P[i] - surf_n2);
        } else if (N2_P[i] <= surf_n2 && (He_P[i] + N2_P[i]) >= surf_n2) {
            double decay = 1.0 / (k_n2[i] - k_he[i])
                         * log((surf_n2 - N2_P[i]) / He_P[i]);
            double integral = He_P[i] / k_he[i] * (1.0 - exp(-k_he[i] * decay))
                            + (N2_P[i] - surf_n2) / k_n2[i] * (1.0 - exp(-k_n2[i] * decay));
            Surface_Phase_Vol_Time[i] = integral / (He_P[i] + N2_P[i] - surf_n2);
        } else {
            Surface_Phase_Vol_Time[i] = 0.0;
        }
    }
}

/* ---- CALC_MAX_ACTUAL_GRADIENT ------------------------------------------- */
static void calc_max_actual_gradient(double deco_stop_depth)
{
    int i;
    for (i = 0; i < NC; i++) {
        double cg = (He_P[i] + N2_P[i] + Constant_Pressure_Other_Gases)
                  - (deco_stop_depth + Barometric_Pressure);
        if (cg > Max_Actual_Gradient[i]) Max_Actual_Gradient[i] = cg;
    }
}

/* ---- profile description ------------------------------------------------ */
typedef struct { double depth; int mix; double rate; double step; } change_t;
static change_t Change[8];
static int Number_of_Changes;

typedef struct {
    double depth, stop_time, run_time;
    int    mix;
} stop_rec;
static stop_rec Stops[128];
static int      N_Stops;

/* ---- the critical volume loop ------------------------------------------- */
static double He_Start_Ascent[NC], N2_Start_Ascent[NC];
static double He_Start_Zone[NC],   N2_Start_Zone[NC];
static double Run_Time_Start_of_Ascent, Run_Time_Start_of_Deco_Zone;
static int    Segment_Number_Start_of_Ascent;

static void deco_stop_loop_block(double *starting_depth, double *rate,
                                 double *step_size, int record)
{
    double last_run_time = 0.0;

    for (;;) {
        int i;
        gas_loadings_ascent_descent(*starting_depth, Deco_Stop_Depth, *rate);
        if (record) calc_max_actual_gradient(Deco_Stop_Depth);
        if (Deco_Stop_Depth <= 0.0) break;

        for (i = 1; i < Number_of_Changes; i++) {
            if (Change[i].depth >= Deco_Stop_Depth) {
                Mix_Number = Change[i].mix;
                *rate      = Change[i].rate;
                *step_size = Change[i].step;
            }
        }
        boyles_law_compensation(First_Stop_Depth, Deco_Stop_Depth, *step_size);
        if (decompression_stop(Deco_Stop_Depth, *step_size) != 0) return;

        if (record) {
            double stop_time;
            if (last_run_time == 0.0)
                stop_time = ceil_inc(Segment_Time / SET.min_deco_stop_time)
                          * SET.min_deco_stop_time;
            else
                stop_time = Run_Time - last_run_time;
            Stops[N_Stops].depth     = Deco_Stop_Depth;
            Stops[N_Stops].stop_time = stop_time;
            Stops[N_Stops].run_time  = Run_Time;
            Stops[N_Stops].mix       = Mix_Number;
            N_Stops++;
        }
        *starting_depth = Deco_Stop_Depth;
        Deco_Stop_Depth = Deco_Stop_Depth - *step_size;
        last_run_time   = Run_Time;
    }
}

static void critical_volume_loop(void)
{
    double deco_phase_volume_time;
    int    schedule_converged = 0;
    int    i, guard = 0;

    for (i = 0; i < NC; i++) Last_Phase_Vol_Time[i] = 0.0;

    for (;;) {
        double starting_depth, rate, step_size;

        if (++guard > 50) { fprintf(stderr, "critical volume loop did not settle\n"); return; }

        rate      = Change[0].rate;
        step_size = Change[0].step;

        calc_ascent_ceiling();
        if (Ascent_Ceiling_Depth <= 0.0) Deco_Stop_Depth = 0.0;
        else Deco_Stop_Depth = ceil_inc(Ascent_Ceiling_Depth / step_size) * step_size;

        if (Deco_Stop_Depth > Depth_Start_of_Deco_Zone) {
            fprintf(stderr, "STEP SIZE TOO LARGE TO DECOMPRESS\n"); return;
        }
        projected_ascent(Depth_Start_of_Deco_Zone, rate, step_size);
        if (Deco_Stop_Depth > Depth_Start_of_Deco_Zone) {
            fprintf(stderr, "STEP SIZE TOO LARGE TO DECOMPRESS (projected)\n"); return;
        }

        if (Deco_Stop_Depth == 0.0) {          /* no stops required */
            for (i = 0; i < NC; i++) { He_P[i] = He_Start_Ascent[i]; N2_P[i] = N2_Start_Ascent[i]; }
            Run_Time       = Run_Time_Start_of_Ascent;
            Segment_Number = Segment_Number_Start_of_Ascent;
            Mix_Number     = Change[0].mix;
            gas_loadings_ascent_descent(Change[0].depth, 0.0, rate);
            N_Stops = 0;
            return;
        }

        starting_depth   = Depth_Start_of_Deco_Zone;
        First_Stop_Depth = Deco_Stop_Depth;
        deco_stop_loop_block(&starting_depth, &rate, &step_size, 0);

        deco_phase_volume_time = Run_Time - Run_Time_Start_of_Deco_Zone;
        calc_surface_phase_volume_time();

        for (i = 0; i < NC; i++) {
            Phase_Vol_Time[i] = deco_phase_volume_time + Surface_Phase_Vol_Time[i];
            if (fabs(Phase_Vol_Time[i] - Last_Phase_Vol_Time[i]) <= 1.0)
                schedule_converged = 1;
        }

        if (schedule_converged || !SET.crit_volume_on) {
            /* final schedule, recomputed from the start of ascent */
            for (i = 0; i < NC; i++) { He_P[i] = He_Start_Ascent[i]; N2_P[i] = N2_Start_Ascent[i]; }
            Run_Time        = Run_Time_Start_of_Ascent;
            Segment_Number  = Segment_Number_Start_of_Ascent;
            starting_depth  = Change[0].depth;
            Mix_Number      = Change[0].mix;
            rate            = Change[0].rate;
            step_size       = Change[0].step;
            Deco_Stop_Depth = First_Stop_Depth;
            N_Stops         = 0;
            deco_stop_loop_block(&starting_depth, &rate, &step_size, 1);
            return;
        } else {
            critical_volume(deco_phase_volume_time);
            Run_Time   = Run_Time_Start_of_Deco_Zone;
            Mix_Number = Change[0].mix;
            for (i = 0; i < NC; i++) {
                Last_Phase_Vol_Time[i] = Phase_Vol_Time[i];
                He_P[i] = He_Start_Zone[i];
                N2_P[i] = N2_Start_Zone[i];
            }
        }
    }
}

/* ---- driver -------------------------------------------------------------- */
int main(void)
{
    int i;
    double total = 0.0;

    Barometric_Pressure = UNITS_FACTOR;      /* sea level */
    Constant_Pressure_Other_Gases = (SET.p_other_gases_mmhg / 760.0) * UNITS_FACTOR;

    for (i = 0; i < NC; i++) {
        k_he[i] = log(2.0) / HALF_HE[i];
        k_n2[i] = log(2.0) / HALF_N2[i];
        Max_Crush_He[i] = Max_Crush_N2[i] = 0.0;
        Max_Actual_Gradient[i] = 0.0;
        Surface_Phase_Vol_Time[i] = 0.0;
        Amb_P_Onset_Imperm[i] = Gas_Tension_Onset_Imperm[i] = 0.0;
        Init_Crit_Radius_N2[i] = SET.crit_radius_n2_um * 1.0e-6;
        Init_Crit_Radius_He[i] = SET.crit_radius_he_um * 1.0e-6;
        Adj_Crit_Radius_N2[i]  = Init_Crit_Radius_N2[i];
        Adj_Crit_Radius_He[i]  = Init_Crit_Radius_He[i];
        He_P[i] = 0.0;
        N2_P[i] = (Barometric_Pressure - WVP) * FRACTION_INERT;
    }

    /* VPM.IN: TRIMIX DIVE TO 80 MSW */
    FO2[0] = 0.15; FHe[0] = 0.45; FN2[0] = 0.40;
    FO2[1] = 0.36; FHe[1] = 0.00; FN2[1] = 0.64;
    FO2[2] = 1.00; FHe[2] = 0.00; FN2[2] = 0.00;

    Run_Time = 0.0; Segment_Number = 0; Mix_Number = 1;

    /* profile code 1: descent 0 -> 80 at 23 msw/min on mix 1 */
    gas_loadings_ascent_descent(0.0, 80.0, 23.0);
    calc_crushing_pressure(0.0, 80.0, 23.0);

    /* profile code 2: constant depth 80 to run time 30 on mix 1 */
    gas_loadings_constant_depth(80.0, 30.0);

    /* profile code 99: decompression */
    Number_of_Changes = 3;
    Change[0].depth = 80.0; Change[0].mix = 1; Change[0].rate = -10.0; Change[0].step = 3.0;
    Change[1].depth = 33.0; Change[1].mix = 2; Change[1].rate = -10.0; Change[1].step = 3.0;
    Change[2].depth =  6.0; Change[2].mix = 3; Change[2].rate =  -3.0; Change[2].step = 3.0;

    for (i = 0; i < NC; i++) { He_Start_Ascent[i] = He_P[i]; N2_Start_Ascent[i] = N2_P[i]; }
    Run_Time_Start_of_Ascent       = Run_Time;
    Segment_Number_Start_of_Ascent = Segment_Number;

    nuclear_regeneration(Run_Time);
    calc_initial_allowable_gradient();
    calc_start_of_deco_zone(Change[0].depth, Change[0].rate);

    printf("Leading compartment enters the decompression zone at %6.1f mswg\n",
           Depth_Start_of_Deco_Zone);
    printf("       Deepest possible decompression stop is %6.1f mswg\n\n",
           floor(Depth_Start_of_Deco_Zone / Change[0].step) * Change[0].step);

    Mix_Number = Change[0].mix;
    gas_loadings_ascent_descent(Change[0].depth, Depth_Start_of_Deco_Zone, Change[0].rate);
    Run_Time_Start_of_Deco_Zone = Run_Time;
    for (i = 0; i < NC; i++) { He_Start_Zone[i] = He_P[i]; N2_Start_Zone[i] = N2_P[i]; }

    critical_volume_loop();

    printf("  DECO STOP   STOP TIME    RUN TIME   MIX\n");
    printf("  ---------   ---------    --------   ---\n");
    for (i = 0; i < N_Stops; i++) {
        printf("   %6.0f      %6.0f      %6.0f      %d\n",
               Stops[i].depth, Stops[i].stop_time, Stops[i].run_time, Stops[i].mix);
        total += Stops[i].stop_time;
    }
    printf("\n  Total stop time %.0f min, run time %.0f min, first stop %.0f m\n",
           total, Run_Time, N_Stops ? Stops[0].depth : 0.0);
    return 0;
}
