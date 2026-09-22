/*
 * zp_altitude.h - ascent to altitude after diving, and surface oxygen.
 * Copyright (C) 2026 Carlos Lander. GPL-3.0-or-later, as czplan.h.
 */
#ifndef ZP_ALTITUDE_H
#define ZP_ALTITUDE_H
#ifdef __cplusplus
extern "C" {
#endif

enum {
    ZPA_REQ_FLIGHT, ZPA_REQ_ALT_M, ZPA_REQ_WAIT_MIN, ZPA_REQ_TRAVEL_MIN, ZPA_REQ_STAY_MIN,
    ZPA_REQ_O2_MIN, ZPA_REQ_O2_LAST, ZPA_REQ_MASK_FO2, ZPA_REQ_M1_DIVE_GF,
    ZPA_REQ_M2_EE1, ZPA_REQ_M2_LIMIT_PCT, ZPA_REQ_M2_TOTAL,
    ZPA_REQ_N
};

#define ZPA_CURVE_N 8

enum {
    ZPA_M1_AVAILABLE, ZPA_M1_LIMIT_GF, ZPA_M1_GF_AT_WAIT, ZPA_M1_OK, ZPA_M1_EARLIEST,
    ZPA_M1_EARLIEST_AIR, ZPA_M1_O2_FIRST, ZPA_M1_O2_LAST, ZPA_M1_O2_CNS, ZPA_M1_O2_OTU,
    ZPA_M2_AVAILABLE, ZPA_M2_EE1, ZPA_M2_P_DIVE, ZPA_M2_P_TRIP_NO_DIVE, ZPA_M2_P_AT_WAIT,
    ZPA_M2_ADDED_AT_WAIT, ZPA_M2_OK, ZPA_M2_EARLIEST, ZPA_M2_EARLIEST_AIR,
    ZPA_M2_O2_FIRST, ZPA_M2_O2_LAST,
    ZPA_CURVE_HOURS,
    ZPA_CURVE_GF = ZPA_CURVE_HOURS + ZPA_CURVE_N,
    ZPA_CURVE_ADDED = ZPA_CURVE_GF + ZPA_CURVE_N,
    ZPA_ANS_N = ZPA_CURVE_ADDED + ZPA_CURVE_N
};

#define ZPA_ICM_N 13

void zp_icm_after_dive(const zp_config *cfg, const zp_result *res,
                       const double *prev, double surface_interval_min, double *out);

void zp_altitude(const zp_config *cfg, const zp_result *res, const double *icm,
                 const double *req, double *ans);

#ifdef __cplusplus
}
#endif
#endif
