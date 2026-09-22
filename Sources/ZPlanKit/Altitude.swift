//
//  Altitude.swift - ascent to altitude after diving, and surface oxygen.
//
import CZPlan
import Foundation

public enum TripMode: String, Codable, CaseIterable { case car, airplane }
public enum AltitudeMethod1Limit: String, Codable, CaseIterable { case danAnchored, diveGFHigh }
public enum AltitudeMethod2Model: String, Codable, CaseIterable { case ut = "UT", ee1 = "EE1" }

public struct AltitudeTrip: Codable, Equatable {
    public var mode: TripMode = .car
    public var altitudeMeters = 2000.0
    public var waitMinutes = 360.0
    public var travelMinutes = 60.0
    public var stayMinutes = 1440.0
    public var oxygenMinutes = 0.0
    public var oxygenBeforeLeaving = false
    public init() {}
}

public struct AltitudeSettings: Codable, Equatable {
    public var method1Limit: AltitudeMethod1Limit = .danAnchored
    public var method2Model: AltitudeMethod2Model = .ut
    public var method2LimitPercent = 1.0
    public var method2Total = false
    public var maskO2Percent = 100.0
    public init() {}
}

public struct AltitudeAnswer {
    let v: [Double]
    private func d(_ i: Int) -> Double? { v[i].isNaN ? nil : v[i] }
    private func t(_ i: Int) -> Double? { d(i).flatMap { $0 < 0 ? nil : $0 } }

    public var method1Available: Bool { v[Int(ZPA_M1_AVAILABLE)] > 0.5 }
    public var method1LimitGF: Double { v[Int(ZPA_M1_LIMIT_GF)] }
    public var method1GFAtWait: Double { v[Int(ZPA_M1_GF_AT_WAIT)] }
    public var method1OK: Bool { v[Int(ZPA_M1_OK)] > 0.5 }
    public var method1Earliest: Double? { t(Int(ZPA_M1_EARLIEST)) }
    public var method1EarliestAir: Double? { t(Int(ZPA_M1_EARLIEST_AIR)) }
    public var method1OxygenFromSurfacing: Double? { t(Int(ZPA_M1_O2_FIRST)) }
    public var method1OxygenBeforeLeaving: Double? { t(Int(ZPA_M1_O2_LAST)) }
    public var oxygenCNS: Double { d(Int(ZPA_M1_O2_CNS)) ?? 0 }
    public var oxygenOTU: Double { d(Int(ZPA_M1_O2_OTU)) ?? 0 }

    public var method2Available: Bool { v[Int(ZPA_M2_AVAILABLE)] > 0.5 }
    public var method2Name: String { v[Int(ZPA_M2_EE1)] > 0.5 ? "EE1 (Di Muro 2020)" : "UT (Di Muro 2020)" }
    public var method2PDive: Double { v[Int(ZPA_M2_P_DIVE)] }
    public var method2PTripWithoutDive: Double { v[Int(ZPA_M2_P_TRIP_NO_DIVE)] }
    public var method2PAtWait: Double { v[Int(ZPA_M2_P_AT_WAIT)] }
    public var method2AddedAtWait: Double { v[Int(ZPA_M2_ADDED_AT_WAIT)] }
    public var method2OK: Bool { v[Int(ZPA_M2_OK)] > 0.5 }
    public var method2Earliest: Double? { t(Int(ZPA_M2_EARLIEST)) }
    public var method2EarliestAir: Double? { t(Int(ZPA_M2_EARLIEST_AIR)) }
    public var method2OxygenFromSurfacing: Double? { t(Int(ZPA_M2_O2_FIRST)) }
    public var method2OxygenBeforeLeaving: Double? { t(Int(ZPA_M2_O2_LAST)) }

    public struct Row: Identifiable {
        public let id: Int
        public let hours: Double
        public let gf: Double?
        public let added: Double?
    }
    public var curve: [Row] {
        (0..<Int(ZPA_CURVE_N)).map { i in
            Row(id: i, hours: v[Int(ZPA_CURVE_HOURS) + i],
                gf: d(Int(ZPA_CURVE_GF) + i), added: d(Int(ZPA_CURVE_ADDED) + i))
        }
    }
}

extension ZPlan {
    private static func run(profile: String, tissueFile: String?,
                            _ body: (inout zp_config, inout zp_result) -> Void) throws {
        var cfg = zp_config()
        var err = [CChar](repeating: 0, count: 256)
        let rc = profile.withCString { zp_parse_profile($0, &cfg, &err, 256) }
        if rc != 0 { throw ZPlanError.parseError(String(cString: err)) }
        if let t = tissueFile { load(tissueText: t, into: &cfg) }
        var res = zp_result()
        if zp_plan(&cfg, &res) != 0 { throw ZPlanError.planningFailed }
        body(&cfg, &res)
    }

    public static func interconnectedState(profile: String, tissueFile: String?,
                                           previous: [Double]?,
                                           surfaceIntervalMinutes: Double) throws -> [Double] {
        var out = [Double](repeating: 0, count: Int(ZPA_ICM_N))
        try run(profile: profile, tissueFile: tissueFile) { cfg, res in
            if let p = previous, p.count == Int(ZPA_ICM_N) {
                zp_icm_after_dive(&cfg, &res, p, surfaceIntervalMinutes, &out)
            } else {
                zp_icm_after_dive(&cfg, &res, nil, 0, &out)
            }
        }
        return out
    }

    public static func altitude(profile: String, tissueFile: String?, state: [Double]?,
                                trip: AltitudeTrip, settings: AltitudeSettings) throws -> AltitudeAnswer {
        var req = [Double](repeating: 0, count: Int(ZPA_REQ_N))
        req[Int(ZPA_REQ_FLIGHT)] = trip.mode == .airplane ? 1 : 0
        req[Int(ZPA_REQ_ALT_M)] = trip.altitudeMeters
        req[Int(ZPA_REQ_WAIT_MIN)] = trip.waitMinutes
        req[Int(ZPA_REQ_TRAVEL_MIN)] = trip.travelMinutes
        req[Int(ZPA_REQ_STAY_MIN)] = trip.stayMinutes
        req[Int(ZPA_REQ_O2_MIN)] = trip.oxygenMinutes
        req[Int(ZPA_REQ_O2_LAST)] = trip.oxygenBeforeLeaving ? 1 : 0
        req[Int(ZPA_REQ_MASK_FO2)] = settings.maskO2Percent / 100
        req[Int(ZPA_REQ_M1_DIVE_GF)] = settings.method1Limit == .diveGFHigh ? 1 : 0
        req[Int(ZPA_REQ_M2_EE1)] = settings.method2Model == .ee1 ? 1 : 0
        req[Int(ZPA_REQ_M2_LIMIT_PCT)] = settings.method2LimitPercent
        req[Int(ZPA_REQ_M2_TOTAL)] = settings.method2Total ? 1 : 0
        var ans = [Double](repeating: .nan, count: Int(ZPA_ANS_N))
        try run(profile: profile, tissueFile: tissueFile) { cfg, res in
            if let s = state, s.count == Int(ZPA_ICM_N) {
                zp_altitude(&cfg, &res, s, req, &ans)
            } else {
                zp_altitude(&cfg, &res, nil, req, &ans)
            }
        }
        return AltitudeAnswer(v: ans)
    }
}
