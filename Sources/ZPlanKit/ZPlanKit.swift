//
//  ZPlanKit.swift — Swift API for the ZPlanKit decompression engine.
//  v1.0.0
//
//  ┌─────────────────────────────────────────────────────────────────┐
//  │  WARNING: Decompression software can get you bent or killed.    │
//  │  This engine is experimental and for EDUCATIONAL purposes.      │
//  │  For trained mixed-gas decompression divers ONLY.               │
//  │  Validate every schedule against                                │
//  │  independent tables/software before diving it.                  │
//  └─────────────────────────────────────────────────────────────────┘
//
import CZPlan
import Foundation

public enum ZPlanError: Error, LocalizedError {
    case parseError(String)
    case planningFailed

    public var errorDescription: String? {
        switch self {
        case .parseError(let msg): return "Profile parse error: \(msg)"
        case .planningFailed:      return "Decompression planning failed"
        }
    }
}

/// One line of the dive plan (waypoint, deep stop, or normal stop).
public struct PlanLine {
    public enum Kind { case waypoint, deepStop, normStop, gasSwitch }
    public let kind: Kind
    public let depthMeters: Double
    public let stopSeconds: Double     // time at depth, excluding travel
    public let runtimeMinutes: Double  // runtime when leaving this depth
    public let fO2: Double
    public let fHe: Double
    public let isClosedCircuit: Bool
    public let setpoint: Double
    public let ppO2: Double            // ATM, raw ambient (as the original displays)
    public let endMeters: Double       // equivalent narcotic depth
}

/// Consumption of one open-circuit gas, litres at 1 atm.
public struct GasUse {
    public let fO2: Double
    public let fHe: Double
    public let liters: Double
    /// Of which was breathed before leaving the bottom. Zero for a deco gas,
    /// which is what marks it as one; a back gas has a bottom share and an
    /// ascent share worth planning separately.
    public let bottomLiters: Double
    /// Breathed on the way up, after leaving the bottom.
    public var ascentLiters: Double { liters - bottomLiters }
}

/// Post-dive tissue state, for repetitive-dive planning.
/// Serialise with `tissueFileText` — the format is read/write compatible
/// with the original ZPlan `tissue.dat` (values in ATM, 17 compartments,
/// N2 block then He block, `CNS:` header line).
public struct TissueState {
    public let pN2Bar: [Double]   // 17 compartments, 1b (4.0 min) first
    public let pHeBar: [Double]
    public let cnsPercent: Double

    private static let atm = 1.01325

    public var tissueFileText: String {
        var s = String(format: "CNS:%f\n", cnsPercent)
        for v in pN2Bar { s += String(format: "%f\n", v / Self.atm) }
        for v in pHeBar { s += String(format: "%f\n", v / Self.atm) }
        return s
    }
}

/// Complete result of a planning run.
public struct DivePlan {
    public let lines: [PlanLine]
    public let totalDecoMinutes: Double
    public let runtimeMinutes: Double
    public let cnsPercent: Double
    public let otu: Double
    public let timeToFlyHours: Double
    public let gasUsed: [GasUse]
    public let totalOpenCircuitLiters: Double
    public let tissueState: TissueState
    public let warnings: String
    /// The classic ZPlan-style text report, ready to display or print.
    public let reportText: String
}

public enum ZPlan {

    public static var version: String { String(cString: zp_version()) }

    /// Plan a dive from a ZPlan-format `profile.dat` text.
    ///
    /// - Parameters:
    ///   - profile: contents of a profile.dat (metric or imperial dialect)
    ///   - tissueFile: contents of a tissue.dat for repetitive dives
    ///     (original ZPlan files are accepted), or nil for a clean diver.
    /// - Returns: the full `DivePlan`.
    public static func plan(profile: String, tissueFile: String? = nil) throws -> DivePlan {
        var cfg = zp_config()
        var err = [CChar](repeating: 0, count: 256)

        let rc = profile.withCString { zp_parse_profile($0, &cfg, &err, 256) }
        if rc != 0 { throw ZPlanError.parseError(String(cString: err)) }

        if let t = tissueFile { Self.load(tissueText: t, into: &cfg) }

        var res = zp_result()
        if zp_plan(&cfg, &res) != 0 { throw ZPlanError.planningFailed }

        var buf = [CChar](repeating: 0, count: 16384)
        _ = zp_report(&cfg, &res, &buf, 16384)

        return DivePlan(cResult: res, report: String(cString: buf))
    }

    private static let atm = 1.01325

    static func load(tissueText: String, into cfg: inout zp_config) {
        var n2: [Double] = [], he: [Double] = [], cns = 0.0
        for raw in tissueText.split(whereSeparator: \.isNewline) {
            let line = raw.trimmingCharacters(in: .whitespaces)
            if line.hasPrefix("CNS:") { cns = Double(line.dropFirst(4)) ?? 0; continue }
            guard let v = Double(line) else { continue }
            if n2.count < 17 { n2.append(v * atm) } else if he.count < 17 { he.append(v * atm) }
        }
        guard n2.count == 17, he.count == 17 else { return }
        withUnsafeMutableBytes(of: &cfg.init_pn2) { $0.bindMemory(to: Double.self).baseAddress!.update(from: n2, count: 17) }
        withUnsafeMutableBytes(of: &cfg.init_phe) { $0.bindMemory(to: Double.self).baseAddress!.update(from: he, count: 17) }
        cfg.init_cns_pct = cns
        cfg.have_initial_tissues = true
    }
}

private extension DivePlan {
    init(cResult r: zp_result, report: String) {
        var r = r
        var out: [PlanLine] = []
        withUnsafeBytes(of: &r.lines) { rawBuf in
            let p = rawBuf.bindMemory(to: zp_plan_line.self)
            for i in 0..<Int(r.n_lines) {
                let L = p[i]
                let kind: PlanLine.Kind =
                    L.kind == ZP_LINE_WAYPOINT  ? .waypoint  :
                    L.kind == ZP_LINE_DEEPSTOP  ? .deepStop  :
                    L.kind == ZP_LINE_GASSWITCH ? .gasSwitch : .normStop
                out.append(PlanLine(kind: kind,
                                    depthMeters: L.depth_m,
                                    stopSeconds: L.stop_sec,
                                    runtimeMinutes: L.runtime_min,
                                    fO2: L.fo2, fHe: L.fhe,
                                    isClosedCircuit: L.cc,
                                    setpoint: L.setpoint,
                                    ppO2: L.ppo2,
                                    endMeters: L.end_m))
            }
        }
        func arr17(_ t: (Double, Double, Double, Double, Double, Double, Double, Double, Double,
                         Double, Double, Double, Double, Double, Double, Double, Double)) -> [Double] {
            [t.0,t.1,t.2,t.3,t.4,t.5,t.6,t.7,t.8,t.9,t.10,t.11,t.12,t.13,t.14,t.15,t.16]
        }
        var gases: [GasUse] = []
        let gl = arrN(r.gas_used_l), go = arrN(r.gas_fo2), gh = arrN(r.gas_fhe)
        let gb = arrN(r.gas_bottom_l)
        for i in 0..<Int(r.n_gas_used) {
            gases.append(GasUse(fO2: go[i], fHe: gh[i], liters: gl[i],
                                bottomLiters: gb[i]))
        }
        self.lines = out
        self.totalDecoMinutes = r.total_deco_min
        self.runtimeMinutes = r.runtime_min
        self.cnsPercent = r.cns_pct
        self.otu = r.otu
        self.timeToFlyHours = r.time_to_fly_hr
        self.gasUsed = gases
        self.totalOpenCircuitLiters = r.total_oc_l
        self.tissueState = TissueState(pN2Bar: arr17(r.end_pn2),
                                       pHeBar: arr17(r.end_phe),
                                       cnsPercent: r.end_cns_pct)
        self.warnings = withUnsafeBytes(of: &r.warnings) { raw in
            String(decoding: raw.prefix(while: { $0 != 0 }), as: UTF8.self)
        }
        self.reportText = report
    }
}

/// Helper: fixed-size C double arrays imported as tuples → [Double].
private func arrN<T>(_ tuple: T) -> [Double] {
    var t = tuple
    return withUnsafeBytes(of: &t) { Array($0.bindMemory(to: Double.self)) }
}
