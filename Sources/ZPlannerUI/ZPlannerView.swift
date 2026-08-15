//
//  ZPlannerView.swift — v1.8.0
//
//  Form-based planner front end. Monochrome, no graphics.
//  Top bar: Config · Log · Calculate.
//  iPhone: two tabs (Dive entry / Plan). Mac: side-by-side.
//
import Combine
import SwiftUI
import ZPlanKit
#if os(macOS)
import AppKit
#else
import UIKit
#endif

#if os(macOS)
/// Print the plan text through the standard macOS print panel.
///
/// Requires the `com.apple.security.print` entitlement. The app enables App
/// Sandbox, and without that key macOS refuses with "This application does not
/// support printing" — there is no automatic build setting for it, so it is
/// declared in Lplanner.entitlements.
func printPlan(_ text: String) {
    let info = NSPrintInfo.shared
    info.isVerticallyCentered = false
    let pageWidth = info.paperSize.width - info.leftMargin - info.rightMargin

    let tv = NSTextView(frame: NSRect(x: 0, y: 0, width: pageWidth, height: 1))
    tv.string = text
    tv.font = NSFont.monospacedSystemFont(ofSize: 10, weight: .regular)
    // Paper is always light. Left to the semantic defaults, a plan printed in
    // Dark Mode came out white on white — the text view resolves .textColor
    // against the app's appearance, not the printer's.
    tv.appearance = NSAppearance(named: .aqua)
    tv.textColor = .black
    tv.backgroundColor = .white
    tv.drawsBackground = true
    tv.isVerticallyResizable = true
    tv.isHorizontallyResizable = false

    // Size the view to its whole content. A fixed one-page height silently
    // truncated any plan longer than a page, since NSPrintOperation paginates
    // the view it is given rather than the text inside it.
    if let lm = tv.layoutManager, let tc = tv.textContainer {
        tc.containerSize = NSSize(width: pageWidth, height: .greatestFiniteMagnitude)
        tc.widthTracksTextView = true
        lm.ensureLayout(for: tc)
        let used = lm.usedRect(for: tc).size
        tv.frame = NSRect(x: 0, y: 0, width: pageWidth, height: ceil(used.height) + 24)
    }

    let op = NSPrintOperation(view: tv, printInfo: info)
    op.run()
}
#else
/// Print the plan through AirPrint. Print previously existed on macOS only, so
/// the button was simply absent on iPhone and iPad.
func printPlan(_ text: String) {
    let formatter = UISimpleTextPrintFormatter(text: text)
    formatter.font = UIFont.monospacedSystemFont(ofSize: 10, weight: .regular)
    // Explicit, for the same reason as the macOS path: paper is always light,
    // so the ink must not follow the app's appearance.
    formatter.color = .black

    let info = UIPrintInfo.printInfo()
    info.outputType = .general
    info.jobName = "Dive Plan"

    let controller = UIPrintInteractionController.shared
    controller.printInfo = info
    controller.printFormatter = formatter

    // On iPad the print panel is a popover and needs an anchor; presenting
    // without one does nothing there.
    let scene = UIApplication.shared.connectedScenes
        .compactMap { $0 as? UIWindowScene }
        .first { $0.activationState == .foregroundActive }
    if let view = (scene?.keyWindow ?? scene?.windows.first)?.rootViewController?.view {
        let anchor = CGRect(x: view.bounds.maxX - 60, y: 40, width: 1, height: 1)
        controller.present(from: anchor, in: view, animated: true, completionHandler: nil)
    } else {
        controller.present(animated: true, completionHandler: nil)
    }
}
#endif


// MARK: - Brand colours

extension Color {
    /// Page background, following the system appearance.
    ///
    /// The brand is monochrome, not "white". Hard-coding `Color.white` kept the
    /// page light in Dark Mode while the text fields — which take their fill
    /// from the system — went black, which is exactly what made the iPad
    /// unreadable at night: black boxes on a white page.
    ///
    /// Everything else uses `.primary` / `.secondary`, which are greyscale in
    /// both appearances and so cost the brand nothing.
    static var planPaper: Color {
        #if os(macOS)
        Color(NSColor.textBackgroundColor)
        #else
        Color(UIColor.systemBackground)
        #endif
    }
}

// MARK: - Disclaimer

/// Shown by the Info button and reproduced in the documentation of every build.
/// `algorithms` names the models the build actually ships, so Lplanner79 says
/// VVAL-79 rather than VVAL-18.
public struct Disclaimer {
    public static var algorithms = "A. A. Buhlmann's algorithm or VVAL-18 algorithm"

    public static var text: String {
        "This generated dive schedule could indirectly kill you and probably has "
        + "bugs. The author does not warrant that it accurately reflects "
        + algorithms + ". This dive schedule is experimental, and you use it at "
        + "your own risk."
    }
}

// MARK: - Manual

/// Short how-to shown in the Info sheet, under the disclaimer.
public struct Manual {
    public static let text = """
    ENTERING A DIVE
    Type Depth, Time and O2 % — plus He % for trimix — then press Add >>. \
    Repeat for each level. Tap a level to edit it, use the arrows to reorder, \
    × to remove. Click a level's box to leave it out without deleting it.

    CLOSED CIRCUIT
    Tap the OC chip so it reads CCR — on a tablet or Mac, switch Open to \
    Closed. Set (setpoint) and Sld (Scamahorn slide) then appear beside the mix.

    DECO GASES
    Click Yes and list the mixes, e.g. 50, 100. The planner picks the richest \
    one allowed by Max PO2 and Max END. A GasSw row marks where the switch \
    happens. Config can also hold you there for a few extra minutes — see \
    Extended stops.

    SETTINGS STRIP
    On a phone the settings sit in one strip of chips above the tabs. It folds \
    to a single summary line on the Plan tab so the schedule gets the full \
    screen; the chevron opens or closes it by hand. altGF is a plain on/off \
    there — its two numbers are set in Config.

    CONFIG
    Units, water, altitude, model, gradient factors, deep stops, ascent and \
    descent rates, RMVs. Each section carries its own explanation.

    SURFACE INTERVAL AND RESIDUAL GAS
    When you surface, press "Next dive" to carry your inert gas loading \
    forward into the dive you plan next. \
    It is kept when the app is closed and ages with real time. While gas is \
    carried you must state a surface interval — 48 hr, 24 hr or Actual — \
    before Calculate will work.

    Always use your exact surface interval time or a shorter duration if \
    you're uncertain about how long to wait between dives.

    Press Clear to declare yourself clean again.

    LOG
    Every successful Calculate is recorded automatically, with the dive and \
    settings that produced it. Swipe an entry to delete it, or press Clear.

    SHARE AND PRINT
    Both become available once a plan has been calculated.
    """
}

// MARK: - Model

struct DiveLevel: Identifiable, Codable {
    var id = UUID()
    var enabled = true
    var d = "", t = "", o2 = "", he = "", set = "", sld = ""
    var summary: String {
        var s = "\(d), \(t), \(o2)"
        if let h = Double(he), h > 0 { s += "/\(he)" }
        if !set.isEmpty { s += ", \(set)" }
        if !sld.isEmpty { s += "-\(sld)" }
        return s
    }
}

struct LogEntry: Identifiable, Codable {
    var id = UUID()
    var date = Date()
    /// Which dive and settings produced this plan, so entries are identifiable.
    let summary: String
    let text: String

    /// Date as well as time — entries from different days were indistinguishable.
    var stamp: String {
        let f = DateFormatter()
        f.dateFormat = "d MMM  HH:mm"
        return f.string(from: date)
    }
}

/// Everything the diver typed in: levels, gases and every Config setting.
///
/// The whole lot is stored together deliberately. Levels alone would be unsafe
/// to restore — a level of "45" means 45 m or 45 ft depending on `depthsMetric`,
/// so restoring dive data without the units that were in force when it was
/// entered could silently reinterpret a 45 m dive as 45 ft.
struct PlannerState: Codable {
    var depthsMetric = true, rmvMetric = true, saltWater = true, o2Narcotic = false
    var model = "c"
    var useGF = false, gfLow = "30", gfHigh = "85", altGfLow = "90", altGfHigh = "90"
    var extraSlow = false, ndlLow = false
    var altitude = "0", conservatism = 10.0
    var deepStops = "p", pyleTime = 1, stopDistance = "3", lastStop = "3"
    var descentRates = "0-100, 15"
    var ascentRates = "70-30, 18\n30-12, 9\n12-0, 3"
    var decoSetpoints = "", slideRate = "0.1", maxPO2 = "1.6", maxEND = "40"
    var bottomRMV = "19", decoRMV = "14"
    var extStopShallow = 0, extStopDeep = 0
    var si48 = false, si24 = false, siActual = ""
    var decoGasesOn = true, decoGases = "50"
    var circuitClosed = false, plus3m = false, plus5min = false, useAltGF = false
    var levels: [DiveLevel] = []

    /// Residual inert gas carried between sessions.
    ///
    /// `baselineTissue` is the diver's loading at the START of the dive being
    /// planned, in tissue.dat format, with the wall-clock time it was recorded.
    /// Storing the state *before* the current dive rather than after it is what
    /// lets a plan be edited and recalculated any number of times without the
    /// dive stacking on top of itself.
    var baselineTissue: String? = nil
    var baselineDate: Date? = nil
}

/// Files under Application Support/Lplanner. Both the log and the entered dive
/// state previously lived only in memory and were lost when the app closed.
enum Store {
    private static func url(_ name: String) -> URL? {
        guard let dir = try? FileManager.default.url(
            for: .applicationSupportDirectory, in: .userDomainMask,
            appropriateFor: nil, create: true) else { return nil }
        let app = dir.appendingPathComponent("Lplanner", isDirectory: true)
        try? FileManager.default.createDirectory(at: app, withIntermediateDirectories: true)
        return app.appendingPathComponent(name)
    }

    private static func read<T: Decodable>(_ name: String, _ type: T.Type) -> T? {
        guard let u = url(name), let d = try? Data(contentsOf: u) else { return nil }
        let dec = JSONDecoder()
        dec.dateDecodingStrategy = .iso8601
        return try? dec.decode(T.self, from: d)
    }

    private static func write<T: Encodable>(_ name: String, _ value: T) {
        guard let u = url(name) else { return }
        let enc = JSONEncoder()
        enc.dateEncodingStrategy = .iso8601
        enc.outputFormatting = .prettyPrinted
        guard let d = try? enc.encode(value) else { return }
        try? d.write(to: u, options: .atomic)
    }

    // A corrupt file must never stop the planner starting, hence the defaults.
    static func loadLog() -> [LogEntry] { read("log.json", [LogEntry].self) ?? [] }
    static func saveLog(_ e: [LogEntry]) { write("log.json", e) }
    static func loadState() -> PlannerState { read("state.json", PlannerState.self) ?? PlannerState() }
    static func saveState(_ s: PlannerState) { write("state.json", s) }
}

final class PlannerModel: ObservableObject {
    // ---- Config sheet ----
    @Published var depthsMetric = true          // Depths: Feet / Meters
    @Published var rmvMetric = true             // RMVs: Cu.ft / Liters
    @Published var saltWater = true             // Water: Fresh / Salt
    @Published var o2Narcotic = false           // O2 Narcotic: No / Yes
    @Published var model = "c"                  // "c" (ZHL16-C) or "vval"
    @Published var useGF = false
    @Published var gfLow = "30"
    @Published var gfHigh = "85"
    @Published var altGfLow = "90"
    @Published var altGfHigh = "90"
    @Published var extraSlow = false
    @Published var ndlLow = false
    @Published var altitude = "0"
    @Published var conservatism = 10.0          // 0-100 %
    @Published var deepStops = "p"              // n / p
    @Published var pyleTime = 1                 // 1-5 min
    @Published var stopDistance = "3"
    @Published var lastStop = "3"
    @Published var descentRates = "0-100, 15"
    @Published var ascentRates = "70-30, 18\n30-12, 9\n12-0, 3"
    @Published var decoSetpoints = ""           // e.g. "80-30, 1.4\n29-0, 1.2"
    @Published var slideRate = "0.1"
    @Published var maxPO2 = "1.6"
    @Published var maxEND = "40"
    @Published var bottomRMV = "19"
    @Published var decoRMV = "14"
    /// Extra hold on a deco mix switch, per depth band, 0-10 min.
    @Published var extStopShallow = 0
    @Published var extStopDeep = 0
    // ---- Main window rows ----
    @Published var si48 = false
    @Published var si24 = false
    @Published var siActual = ""                // H:MM
    @Published var decoGasesOn = true
    @Published var decoGases = "50"
    @Published var circuitClosed = false        // Open / Closed
    @Published var plus3m = false               // add 3 m / 10 ft to deepest level
    @Published var plus5min = false             // add 5 min to deepest level
    @Published var useAltGF = false             // use Alternative GF pair
    // ---- Levels ----
    @Published var levels: [DiveLevel] = []
    @Published var entry = DiveLevel()
    @Published var editingID: UUID? = nil
    // ---- Output ----
    @Published var planText = ""
    @Published var notes = ""
    /// Restored from disk so the log survives quitting the app.
    @Published var log: [LogEntry] = Store.loadLog()
    /// Loading at the start of the dive being planned; survives quitting.
    @Published var baselineTissue: String? = nil
    @Published var baselineDate: Date? = nil
    /// Loading at the END of the most recent calculation, not yet committed.
    ///
    /// Published, because `canCommit` is derived from it and drives the button:
    /// a plain property would not republish when commitDive consumed it.
    @Published private var resultTissue: String? = nil
    private var autosave: AnyCancellable?

    init() {
        apply(Store.loadState())
        // objectWillChange fires before the property is written, so the debounce
        // both coalesces bursts of typing and guarantees we snapshot after the
        // change has landed. Covers every field without per-property plumbing.
        autosave = objectWillChange
            .debounce(for: .seconds(1), scheduler: RunLoop.main)
            .sink { [weak self] in self?.saveState() }
    }

    private func apply(_ s: PlannerState) {
        depthsMetric = s.depthsMetric; rmvMetric = s.rmvMetric
        saltWater = s.saltWater; o2Narcotic = s.o2Narcotic
        model = s.model
        useGF = s.useGF; gfLow = s.gfLow; gfHigh = s.gfHigh
        altGfLow = s.altGfLow; altGfHigh = s.altGfHigh
        extraSlow = s.extraSlow; ndlLow = s.ndlLow
        altitude = s.altitude; conservatism = s.conservatism
        deepStops = s.deepStops; pyleTime = s.pyleTime
        stopDistance = s.stopDistance; lastStop = s.lastStop
        descentRates = s.descentRates; ascentRates = s.ascentRates
        decoSetpoints = s.decoSetpoints; slideRate = s.slideRate
        maxPO2 = s.maxPO2; maxEND = s.maxEND
        bottomRMV = s.bottomRMV; decoRMV = s.decoRMV
        extStopShallow = s.extStopShallow; extStopDeep = s.extStopDeep
        si48 = s.si48; si24 = s.si24; siActual = s.siActual
        decoGasesOn = s.decoGasesOn; decoGases = s.decoGases
        circuitClosed = s.circuitClosed
        plus3m = s.plus3m; plus5min = s.plus5min; useAltGF = s.useAltGF
        levels = s.levels
        baselineTissue = s.baselineTissue
        baselineDate = s.baselineDate
    }

    private var snapshot: PlannerState {
        var s = PlannerState()
        s.depthsMetric = depthsMetric; s.rmvMetric = rmvMetric
        s.saltWater = saltWater; s.o2Narcotic = o2Narcotic
        s.model = model
        s.useGF = useGF; s.gfLow = gfLow; s.gfHigh = gfHigh
        s.altGfLow = altGfLow; s.altGfHigh = altGfHigh
        s.extraSlow = extraSlow; s.ndlLow = ndlLow
        s.altitude = altitude; s.conservatism = conservatism
        s.deepStops = deepStops; s.pyleTime = pyleTime
        s.stopDistance = stopDistance; s.lastStop = lastStop
        s.descentRates = descentRates; s.ascentRates = ascentRates
        s.decoSetpoints = decoSetpoints; s.slideRate = slideRate
        s.maxPO2 = maxPO2; s.maxEND = maxEND
        s.bottomRMV = bottomRMV; s.decoRMV = decoRMV
        s.extStopShallow = extStopShallow; s.extStopDeep = extStopDeep
        s.si48 = si48; s.si24 = si24; s.siActual = siActual
        s.decoGasesOn = decoGasesOn; s.decoGases = decoGases
        s.circuitClosed = circuitClosed
        s.plus3m = plus3m; s.plus5min = plus5min; s.useAltGF = useAltGF
        s.levels = levels
        s.baselineTissue = baselineTissue
        s.baselineDate = baselineDate
        return s
    }

    /// Write the entered dive state to disk. Also called on the way out.
    func saveState() { Store.saveState(snapshot) }

    /// True when residual loading from an earlier dive is being carried.
    var hasResidual: Bool { baselineTissue != nil }

    /// Real time since the residual was recorded, in minutes.
    var elapsedMinutes: Double {
        guard let d = baselineDate else { return 0 }
        return max(0, Date().timeIntervalSince(d) / 60.0)
    }

    var elapsedText: String {
        let m = Int(elapsedMinutes.rounded())
        return String(format: "%d:%02d", m / 60, m % 60)
    }

    var repetitive: Bool { si48 || si24 || !siActual.isEmpty }

    /// While residual gas is carried, a surface interval must be stated before a
    /// plan can be produced. Guessing it from the clock would let a diver get a
    /// schedule without ever confronting the fact that a previous dive is still
    /// loaded, which is the one thing a repetitive plan must not hide.
    var canCalculate: Bool { !hasResidual || repetitive }

    /// A typed surface interval wins, so what-if planning still works. Otherwise
    /// the real elapsed time since the residual was recorded is used, which is
    /// what makes the tracking advance while the app is closed.
    var surfaceInterval: String {
        if !siActual.isEmpty { return siActual }
        if si48 { return "48:00" }
        if si24 { return "24:00" }
        if hasResidual { return elapsedText }
        return "900:00"
    }

    var profileText: String {
        var p = """
        UseMetric: \(depthsMetric ? "y" : "n")
        RmvMetric: \(rmvMetric ? "y" : "n")
        SaltWater: \(saltWater ? "y" : "n")
        Model: \(model == "vval" ? "vval18" : "zhl16c")
        Altitude: \(altitude)
        Conservatism: \(Int(conservatism))
        Precision: 1
        StopDistance: \(stopDistance)
        LastStopDepth: \(lastStop)
        OxyNarc: \(o2Narcotic ? "y" : "n")
        DeepStops: \(deepStops)
        PyleStopTime: \(pyleTime)
        TissueFile:
        SurfaceInterval: \(surfaceInterval)
        Rmv: \(bottomRMV)
        DecoRmv: \(decoRMV)
        SlideRate: \(slideRate)
        UseOCDeco: \(decoGasesOn ? "y" : "n")
        OcDecoGas: \(decoGases)
        OcDecoMaxPO2: \(maxPO2)
        MaxEND: \(maxEND)
        ExtStopShallow: \(extStopShallow)
        ExtStopDeep: \(extStopDeep)
        """
        if (useGF || useAltGF) && model != "vval" {
            let lo = useAltGF ? altGfLow : gfLow
            let hi = useAltGF ? altGfHigh : gfHigh
            p += "\nGradientFactors: \(lo), \(hi)"
        }
        p += "\nExtraSlow: \(extraSlow ? "y" : "n")"
        p += "\nNdlGF: \(ndlLow ? "low" : "high")"
        for r in descentRates.split(whereSeparator: \.isNewline) { p += "\nDescentRate: \(r)" }
        for r in ascentRates.split(whereSeparator: \.isNewline)  { p += "\nAscentRate: \(r)" }
        if decoSetpoints.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            p += "\nUseDecoSetpoint: n"
        } else {
            p += "\nUseDecoSetpoint: y"
            for r in decoSetpoints.split(whereSeparator: \.isNewline) { p += "\nDecoSetpoint: \(r)" }
        }
        p += "\n"
        // +3m / +5min apply to the deepest enabled level
        let enabled = levels.filter { $0.enabled }
        let deepest = enabled.map { Double($0.d) ?? 0 }.max() ?? 0
        for l in enabled {
            var d = Double(l.d) ?? 0
            var t = Double(l.t) ?? 0
            if d >= deepest - 0.001 && deepest > 0 {
                if plus3m   { d += depthsMetric ? 3 : 10 }
                if plus5min { t += 5 }
            }
            var line = "\(fmt(d)), \(fmt(t)), \(l.o2)"
            if let h = Double(l.he), h > 0 { line += "/\(l.he)" }
            if !l.set.isEmpty { line += ", \(l.set)" }
            if !l.sld.isEmpty { line += "-\(l.sld)" }
            p += line + "\n"
        }
        return p
    }

    private func fmt(_ v: Double) -> String {
        v == v.rounded() ? String(Int(v)) : String(v)
    }

    /// Add a new level, or commit changes to the one being edited.
    func addEntry() {
        guard !entry.d.isEmpty, !entry.t.isEmpty, !entry.o2.isEmpty else { return }
        if let id = editingID, let i = levels.firstIndex(where: { $0.id == id }) {
            let wasEnabled = levels[i].enabled
            levels[i] = entry
            levels[i].enabled = wasEnabled
            editingID = nil
        } else {
            levels.append(entry)
        }
        entry = DiveLevel()
    }

    /// Load an existing level back into the entry fields for editing.
    func beginEdit(_ l: DiveLevel) {
        entry = l
        editingID = l.id
        if !l.set.isEmpty || !l.sld.isEmpty { circuitClosed = true }
    }

    func cancelEdit() {
        editingID = nil
        entry = DiveLevel()
    }

    func move(_ l: DiveLevel, up: Bool) {
        guard let i = levels.firstIndex(where: { $0.id == l.id }) else { return }
        let j = up ? i - 1 : i + 1
        guard levels.indices.contains(j) else { return }
        levels.swapAt(i, j)
    }

    func calculate() {
        guard canCalculate else {
            notes = "Residual gas is carried from an earlier dive. "
                  + "Set the surface interval — 48 hr, 24 hr, or Actual — before calculating."
            planText = ""
            return
        }
        guard levels.contains(where: { $0.enabled }) else {
            notes = "No enabled dive levels — add a Depth / Time / O2 row first."
            planText = ""
            return
        }
        do {
            // Always planned from the baseline, never from the previous
            // result. Recalculating an edited dive therefore never stacks the
            // dive on top of itself.
            let r = try ZPlan.plan(profile: profileText, tissueFile: baselineTissue)
            planText = r.reportText
            notes = r.warnings
            resultTissue = r.tissueState.tissueFileText
            // Log at the moment of calculation. Logging used to happen when the
            // Log button was pressed, which saved whatever planText happened to
            // hold — i.e. the previous calculation if any setting had changed
            // since — and appended a duplicate every time the log was merely
            // viewed. Recording it here means an entry always matches the
            // settings that produced it.
            appendLog()
        } catch {
            planText = ""
            notes = error.localizedDescription
        }
    }

    /// Short description of the dive and the settings behind a logged plan.
    private var diveSummary: String {
        let du = depthsMetric ? "m" : "ft"
        let enabled = levels.filter { $0.enabled }
        var dive = enabled.map { l -> String in
            var s = "\(l.d)\(du)/\(l.t)min \(l.o2)%"
            if let h = Double(l.he), h > 0 { s += "/\(l.he)he" }
            return s
        }.joined(separator: " + ")
        if dive.isEmpty { dive = "no levels" }

        var modelText: String
        if model == "vval" {
            modelText = "VVAL-18"
        } else if gfOn {
            let lo = useAltGF ? altGfLow : gfLow
            let hi = useAltGF ? altGfHigh : gfHigh
            modelText = "ZHL16-C GF\(lo)/\(hi)"
        } else {
            modelText = "ZHL16-C cons \(Int(conservatism))%"
        }

        var extras: [String] = []
        if circuitClosed { extras.append("CCR") }
        if decoGasesOn && !decoGases.trimmingCharacters(in: .whitespaces).isEmpty {
            extras.append("deco \(decoGases)")
        }
        if deepStops == "p" && !gfOn { extras.append("Pyle \(pyleTime) min") }
        if extraSlow { extras.append("extra-slow") }
        if extStopShallow > 0 || extStopDeep > 0 {
            extras.append("ext stops \(extStopDeep)/\(extStopShallow) min")
        }
        if plus3m { extras.append(depthsMetric ? "+3m" : "+10ft") }
        if plus5min { extras.append("+5min") }
        if repetitive { extras.append("SI \(surfaceInterval)") }

        return ([dive, modelText] + extras).joined(separator: " · ")
    }

    /// Gradient factors active — they override Conservatism.
    var gfOn: Bool { (useGF || useAltGF) && model != "vval" }

    private func appendLog() {
        guard !planText.isEmpty else { return }
        // Compare against the whole log, not just the newest entry. Checking
        // only the first meant a plan you had deleted came straight back the
        // next time you pressed Calculate on the same settings, which read as
        // "deleted entries reappear".
        if log.contains(where: { $0.text == planText }) { return }
        log.insert(LogEntry(summary: diveSummary, text: planText), at: 0)
        Store.saveLog(log)
    }

    func removeLog(_ e: LogEntry) {
        log.removeAll { $0.id == e.id }
        Store.saveLog(log)
    }

    func clearLog() {
        log.removeAll()
        Store.saveLog(log)
    }

    /// Persist after a swipe-to-delete, which mutates `log` directly.
    func persistLog() { Store.saveLog(log) }

    /// Carry the loading from the calculated dive forward, timestamped now.
    /// Deliberately explicit: calculating a plan must not commit tissue, or
    /// editing and recalculating one dive would compound onto itself.
    func commitDive() {
        guard let t = resultTissue else { return }
        baselineTissue = t
        baselineDate = Date()
        siActual = ""; si24 = false; si48 = false
        // Consume it. Without this the button stayed live after committing, so
        // it sat on screen next to "Residual gas is carried" as though nothing
        // had happened — and pressing it again re-stamped the SAME dive with a
        // fresh timestamp, silently resetting the surface interval to zero
        // while the plan on screen was unchanged.
        resultTissue = nil
        saveState()
    }

    /// Declare the diver clean again.
    func clearTissues() {
        baselineTissue = nil
        baselineDate = nil
        resultTissue = nil
        saveState()
    }

    var canCommit: Bool { resultTissue != nil }
}

// MARK: - Root

public struct ZPlannerView: View {
    @StateObject private var m = PlannerModel()
    @State private var showConfig = false
    @State private var showLog = false
    @State private var showInfo = false
    @Environment(\.scenePhase) private var scenePhase
    #if os(iOS)
    @Environment(\.horizontalSizeClass) private var hSize
    #endif

    public init() {}

    public var body: some View {
        VStack(spacing: 0) {
            topBar
            Divider()
            siRow
            Divider()
            gasRow
            Divider()
            autoRow
            Divider()
            content
        }
        .background(Color.planPaper.ignoresSafeArea())
        .foregroundColor(.primary)
        .sheet(isPresented: $showConfig) { ConfigSheet(m: m) }
        .sheet(isPresented: $showLog) { LogSheet(m: m) }
        .sheet(isPresented: $showInfo) { infoSheet }
        // The autosave is debounced by a second, so flush on the way out in case
        // the app is closed immediately after the last edit.
        .onChange(of: scenePhase) { phase in
            if phase != .active { m.saveState() }
        }
    }

    private var compact: Bool {
        #if os(iOS)
        return hSize == .compact
        #else
        return false
        #endif
    }

    @ViewBuilder private var content: some View {
        if compact {
            TabView {
                ScrollView { diveColumn.padding(12) }
                    .tabItem { Label("Dive", systemImage: "square.and.pencil") }
                ScrollView { planPane.padding(12) }
                    .tabItem { Label("Plan", systemImage: "doc.plaintext") }
            }
        } else {
            HStack(alignment: .top, spacing: 0) {
                ScrollView { diveColumn.padding(12) }.frame(width: 270)
                Divider()
                ScrollView { planPane.padding(12) }
            }
        }
    }

    // ---- top bar: Config · Log · Calculate (left) · Share/Print (right) ----
    /// No plan yet — Share and Print stay visible but inert.
    private var noPlan: Bool { m.planText.isEmpty }

    private var topBar: some View {
        HStack(spacing: 10) {
            barButton("Config", "gearshape") { showConfig = true }
            // Log is now purely a viewer — entries are recorded by Calculate.
            barButton("Log", "book") { showLog = true }
            Button(action: m.calculate) {
                Text("Calculate")
                    .font(.headline)
                    .padding(.horizontal, 18).padding(.vertical, 7)
                    .overlay(RoundedRectangle(cornerRadius: 4).stroke(Color.primary, lineWidth: 1.5))
            }
            .buttonStyle(.plain)
            .disabled(!m.canCalculate)
            .opacity(m.canCalculate ? 1 : 0.4)
            Spacer()
            // Share, Print and Info are permanent. Share and Print used to be
            // hidden until a plan existed, so the right-hand side of the bar
            // changed shape after the first Calculate; they now stay put and
            // simply dim while there is nothing to act on.
            shareButton
                .disabled(noPlan)
                .opacity(noPlan ? 0.4 : 1)
            // Print is available on every platform now; it was macOS-only, so
            // the button was missing entirely on iPhone and iPad.
            barButton("Print", "printer") { printPlan(m.planText) }
                .disabled(noPlan)
                .opacity(noPlan ? 0.4 : 1)
            barButton("Info", "info.circle") { showInfo = true }
        }
        .padding(.horizontal, 12).padding(.vertical, 8)
    }

    private var infoSheet: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("Lplanner").font(.title3.bold())
                Spacer()
                Button("Done") { showInfo = false }.keyboardShortcut(.defaultAction)
            }
            .padding(.bottom, 14)

            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    // Monochrome throughout: emphasis comes from weight and
                    // placement, never colour.
                    Text(Disclaimer.text)
                        .fontWeight(.semibold)
                        .fixedSize(horizontal: false, vertical: true)
                    Divider()
                    Text(Manual.text)
                        .font(.callout)
                        .fixedSize(horizontal: false, vertical: true)
                    Divider()
                    Text(ZPlan.version)
                        .font(.caption).foregroundColor(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .padding(20)
        .frame(minWidth: 340, maxWidth: 480, minHeight: 420)
        .background(Color.planPaper)
    }


    @ViewBuilder private var shareButton: some View {
        if #available(iOS 16.0, macOS 13.0, *) {
            ShareLink(item: m.planText) {
                VStack(spacing: 2) {
                    Image(systemName: "square.and.arrow.up")
                    Text("Share").font(.caption)
                }
                .padding(.horizontal, 8).padding(.vertical, 4)
                .overlay(RoundedRectangle(cornerRadius: 4).stroke(Color.secondary, lineWidth: 1))
            }.buttonStyle(.plain)
        } else {
            barButton("Copy", "doc.on.doc") {
                #if os(iOS)
                UIPasteboard.general.string = m.planText
                #else
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString(m.planText, forType: .string)
                #endif
            }
        }
    }

    private func barButton(_ title: String, _ icon: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(spacing: 2) {
                Image(systemName: icon)
                Text(title).font(.caption)
            }
            .padding(.horizontal, 8).padding(.vertical, 4)
            .overlay(RoundedRectangle(cornerRadius: 4).stroke(Color.secondary, lineWidth: 1))
        }.buttonStyle(.plain)
    }

    // ---- Surface Interval row ----
    private var siRow: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 12) {
                Text("Surface Interval").font(.callout)
                check("48 hr", isOn: Binding(get: { m.si48 },
                    set: { m.si48 = $0; if $0 { m.si24 = false } }))
                check("24 hr", isOn: Binding(get: { m.si24 },
                    set: { m.si24 = $0; if $0 { m.si48 = false } }))
                Text("Actual:")
                TextField(m.hasResidual ? m.elapsedText : "_:__", text: $m.siActual)
                    .textFieldStyle(.roundedBorder).frame(width: 70)
                Spacer()
            }
            // Residual loading must be visible. A schedule that silently depends
            // on an earlier dive is exactly the kind of thing a diver has to be
            // able to see and cancel.
            HStack(spacing: 10) {
                if m.hasResidual {
                    Text(m.canCalculate
                         ? "Residual gas carried — surfaced \(m.elapsedText) ago"
                         : "Residual gas carried — set a surface interval to calculate")
                        .font(.caption).fontWeight(.semibold)
                    // underline() on a View needs iOS 16 / macOS 13; this target
                    // deploys to iOS 15.6 / macOS 12.4, where it exists only on
                    // Text. Hence the explicit label rather than Button("…").
                    Button { m.clearTissues() } label: {
                        Text("Clear").font(.caption).underline()
                    }.buttonStyle(.plain)
                } else {
                    Text("No residual gas — planning clean")
                        .font(.caption).foregroundColor(.secondary)
                }
                if m.canCommit {
                    Button { m.commitDive() } label: {
                        Text("Next dive")
                            .font(.caption).underline()
                    }.buttonStyle(.plain)
                }
                Spacer()
            }
        }.padding(.horizontal, 12).padding(.vertical, 6)
    }

    // ---- Deco gases row ----
    private var gasRow: some View {
        HStack(spacing: 12) {
            Text("Deco gases").font(.callout)
            check("Yes", isOn: $m.decoGasesOn)
            TextField("50, 100", text: $m.decoGases)
                .textFieldStyle(.roundedBorder).frame(maxWidth: 140)
                .disabled(!m.decoGasesOn)
            Spacer()
        }.padding(.horizontal, 12).padding(.vertical, 6)
    }

    // ---- +3m / +5min / altGF row ----
    private var autoRow: some View {
        HStack(spacing: 16) {
            check(m.depthsMetric ? "+3m" : "+10ft", isOn: $m.plus3m)
            check("+5min", isOn: $m.plus5min)
            HStack(spacing: 4) {
                check("altGF", isOn: $m.useAltGF)
                // editable here as well as in Config — any values accepted
                TextField("", text: $m.altGfLow)
                    .textFieldStyle(.roundedBorder).frame(width: 44)
                Text("/")
                TextField("", text: $m.altGfHigh)
                    .textFieldStyle(.roundedBorder).frame(width: 44)
            }
            .opacity(m.model == "vval" ? 0.4 : 1)
            .disabled(m.model == "vval")
            Spacer()
        }.padding(.horizontal, 12).padding(.vertical, 6)
    }

    // ---- Left column: circuit, entry fields, Add >>, levels list ----
    private var diveColumn: some View {
        VStack(alignment: .leading, spacing: 10) {
            Picker("", selection: $m.circuitClosed) {
                Text("Open").tag(false); Text("Closed").tag(true)
            }.pickerStyle(.segmented)
            Text("Depth, time, mix, levels.").font(.caption)
            entryField("D:", $m.entry.d)
            entryField("T:", $m.entry.t)
            entryField("O2:", $m.entry.o2)
            entryField("He:", $m.entry.he)
            if m.circuitClosed {
                entryField("Set:", $m.entry.set)
                entryField("Sld:", $m.entry.sld)
            }
            HStack(spacing: 8) {
                Button(action: m.addEntry) {
                    Text(m.editingID == nil ? "Add >>" : "Update")
                        .padding(.horizontal, 14).padding(.vertical, 5)
                        .overlay(RoundedRectangle(cornerRadius: 4)
                            .stroke(Color.primary, lineWidth: 1))
                }.buttonStyle(.plain)
                if m.editingID != nil {
                    Button("Cancel") { m.cancelEdit() }.buttonStyle(.plain)
                }
            }
            Divider()
            Text("Tap a level to edit it.").font(.caption).foregroundColor(.secondary)
            ForEach($m.levels) { $l in
                HStack(spacing: 6) {
                    check("", isOn: $l.enabled)
                    Button { m.beginEdit(l) } label: {
                        Text(l.summary)
                            .font(.system(.body, design: .monospaced))
                            .underline(m.editingID == l.id)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .contentShape(Rectangle())
                    }.buttonStyle(.plain)
                    Button { m.move(l, up: true) }
                        label: { Image(systemName: "chevron.up").font(.caption2) }
                        .buttonStyle(.plain)
                    Button { m.move(l, up: false) }
                        label: { Image(systemName: "chevron.down").font(.caption2) }
                        .buttonStyle(.plain)
                    Button {
                        if m.editingID == l.id { m.cancelEdit() }
                        m.levels.removeAll { $0.id == l.id }
                    } label: { Image(systemName: "xmark").font(.caption) }
                        .buttonStyle(.plain)
                }
            }
        }
    }

    private var planPane: some View {
        VStack(alignment: .leading, spacing: 8) {
            if !m.notes.isEmpty {
                Text(m.notes).font(.system(.callout, design: .monospaced)).bold()
            }
            Text(m.planText.isEmpty ? "Press Calculate." : m.planText)
                .font(.system(.callout, design: .monospaced))
                .textSelection(.enabled)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private func entryField(_ label: String, _ b: Binding<String>) -> some View {
        HStack {
            Text(label).frame(width: 34, alignment: .leading)
            TextField("", text: b).textFieldStyle(.roundedBorder).frame(width: 80)
        }
    }

    private func check(_ label: String, isOn: Binding<Bool>) -> some View {
        Button { isOn.wrappedValue.toggle() } label: {
            HStack(spacing: 4) {
                Image(systemName: isOn.wrappedValue ? "checkmark.square" : "square")
                if !label.isEmpty { Text(label) }
            }
        }.buttonStyle(.plain)
    }
}

// MARK: - Config sheet (all settings in one place, with descriptions)

struct ConfigSheet: View {
    @ObservedObject var m: PlannerModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text("Config").font(.title3.bold())
                Spacer()
                Button("Done") { dismiss() }
                    .keyboardShortcut(.defaultAction)
            }
            .padding(12)
            Divider()
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    group("Units",
                          help: "Depths sets the units for depth, altitude, stop distance and END. RMVs sets the units for breathing-rate and gas-consumption figures — the two can differ.") {
                        row("Depths") { seg($m.depthsMetric, off: "Feet", on: "Meters") }
                        row("RMVs")   { seg($m.rmvMetric, off: "Cu.ft.", on: "Liters") }
                    }
                    group("Environment",
                          help: "Fresh or salt water changes the depth-to-pressure conversion. O2 Narcotic controls whether oxygen counts as narcotic when calculating equivalent narcotic depths (ENDs).") {
                        row("Water") { seg($m.saltWater, off: "Fresh", on: "Salt") }
                        row("O2 Narcotic") { seg($m.o2Narcotic, off: "No", on: "Yes") }
                    }
                    group("Model",
                          help: "ZHL16-C is the Buhlmann set used here. VVAL-18 is the U.S. Navy Thalmann EL-DCM (exponential uptake, linear elimination); gradient factors and Conservatism do not apply to it. With gradient factors enabled, Pyle deep stops are disabled — GF Low provides the deep-stop function — and Conservatism is ignored.") {
                        Picker("", selection: $m.model) {
                            Text("ZHL16-C").tag("c")
                            Text("VVAL-18").tag("vval")
                        }.pickerStyle(.segmented).labelsHidden()
                        if m.model != "vval" {
                            Toggle("Gradient factors", isOn: $m.useGF)
                            HStack(spacing: 16) {
                                row2("GF Low", $m.gfLow)
                                row2("GF High", $m.gfHigh)
                            }
                            .disabled(!m.useGF)
                            .opacity(m.useGF ? 1 : 0.4)
                        }
                    }
                    if m.model != "vval" {
                        group("Alternative gradient factors",
                              help: "A second GF pair, used instead of the main pair whenever altGF is checked on the main screen. Set these to whatever you like — any values are accepted, low and high independently, and they need not bracket the main pair. 100/100 gives the pure Buhlmann ZHL-16C ceiling; values above 100 go beyond it (less conservative than the raw model); a low GF Low with a high GF High deepens the first stop while keeping the shallow stops short. Editable here or directly beside the altGF checkbox on the main screen.") {
                            HStack(spacing: 16) {
                                row2("Alt GF Low", $m.altGfLow)
                                row2("Alt GF High", $m.altGfHigh)
                            }
                        }
                        group("NDL calculation",
                              help: "Which gradient factor decides whether a direct, no-stop ascent to the surface is still allowed. GF High is the standard behaviour for ZHL16-C. GF Low is stricter and ends the no-decompression phase earlier.") {
                            Picker("Calculate NDL by", selection: $m.ndlLow) {
                                Text("GF High (standard)").tag(false)
                                Text("GF Low").tag(true)
                            }.pickerStyle(.segmented)
                            .disabled(!gfOn)
                            .opacity(gfOn ? 1 : 0.4)
                        }
                    }
                    group("Conditions",
                          help: "Altitude of the dive site (0 for sea level; be extra conservative if you are still off-gassing from travel to altitude). Conservatism applies only when gradient factors are switched off. It (0–50 %) preloads the tissue compartments with additional inert gas — nitrogen, and helium in proportion when the profile uses trimix — weighted from the fast compartments (none) to the slow ones (the full percentage), as if a previous dive had been made. Zero is the clean-diver profile.") {
                        row2("Altitude", $m.altitude)
                        VStack(alignment: .leading, spacing: 4) {
                            Text(gfOn ? "Conservatism — not used with gradient factors"
                                      : "Conservatism: \(Int(m.conservatism)) %  (0–50 maximum)")
                            Slider(value: $m.conservatism, in: 0...50, step: 1)
                                .frame(maxWidth: 360)
                                .tint(.secondary)
                                .disabled(gfOn)
                        }
                        .opacity(gfOn ? 0.4 : 1)
                    }
                    // Stop grid stands on its own. It used to live inside Deep
                    // stops, which hid it completely whenever gradient factors
                    // were on — yet every schedule is built on this grid, GF or
                    // not, Pyle or not, and a diver who wants 6 m increments on
                    // a rebreather has nothing to do with deep stops.
                    group("Stop depths",
                          help: "Stop distance is the interval between decompression stops — 3 m is the convention, some rebreather divers prefer 6 m. Last stop is the depth of the final stop; some prefer pulling the 10 ft / 3 m stop deeper. Both apply to every schedule, whichever model, gradient factors or deep stops are in use.") {
                        HStack(spacing: 16) {
                            row2("Stop distance", $m.stopDistance)
                            row2("Last stop", $m.lastStop)
                        }
                    }
                    if !(m.useGF && m.model != "vval") {
                        group("Deep stops",
                              help: "Pyle deep stops insert short stops between the bottom and the first normal stop (mean-depth rule, re-run iteratively) to reduce microbubble formation and post-dive fatigue. Pyle stop time is the minutes spent at each generated stop (1–5). Not shown when gradient factors are enabled: GF Low takes over the deep-stop role.") {
                            Picker("", selection: $m.deepStops) {
                                Text("None").tag("n"); Text("Pyle").tag("p")
                            }.pickerStyle(.segmented).labelsHidden()
                            if m.deepStops == "p" {
                                Stepper("Pyle stop time: \(m.pyleTime) min  (1–5)",
                                        value: $m.pyleTime, in: 1...5)
                                    .frame(maxWidth: 300)
                            }
                        }
                    }
                    group("Ascent behaviour (experimental)",
                          help: "Extra slow delays the ascent to the next stop while the off-gassing gradient of any compartment — tissue inert tension minus ambient pressure, i.e. supersaturation — exceeds 1.25 bar. It only ever adds time at the deeper depth, so the schedule stays below the gradient factor regardless of the rule. Two limits keep it practical: it never applies to the final ascent to the surface, and it adds at most 5 minutes per stop. Time spent held is counted in the total decompression time. Noticeable on dives that leave a compartment strongly supersaturated at the stop.") {
                        Toggle("Extra slow ascent rule", isOn: $m.extraSlow)
                    }
                    group("Descent — range, rate",
                          help: "One range per line: depth1-depth2, rate (ft or m per minute). List shallowest range first, leave no gaps.") {
                        editor($m.descentRates, height: 52)
                    }
                    group("Ascent — range, rate (deepest first)",
                          help: "One range per line, deepest range first, no gaps. Slow shallow ascent rates are credited to the decompression and can shorten stops or remove them entirely.") {
                        editor($m.ascentRates, height: 76)
                    }
                    group("Deco Set Point (CCR) / Slide rate",
                          help: "Setpoint changes by depth range during CCR deco, one per line, e.g. 80-30, 1.4 — a setpoint of 0 switches to open circuit for that range. Only active on closed circuit — the OC/CCR chip on a phone, the Open/Closed control elsewhere (disabled for open-circuit dives). Slide rate is the PO2 burned off per minute during a Scamahorn Slide: enter a bottom setpoint like 1.2-1.6 to ride the descent PO2 spike down to the setpoint for a deco advantage.") {
                        editor($m.decoSetpoints, height: 52)
                            .disabled(!m.circuitClosed)
                            .opacity(m.circuitClosed ? 1 : 0.4)
                        row2("Slide rate (PO2/min)", $m.slideRate)
                    }
                    group("Extended stops on a deco mix switch",
                          help: "Extra minutes held at the depth where the planner switches to a deco mix, on top of whatever the model requires. Common practice: settle on the new gas, confirm the analysis and the PO2, and let the switch do some work for you. The amount is chosen by the depth of the switch, in two bands. Switches shallower than 7 m / 23 ft are not extended — the final stop is already long. The extra time off-gasses you, so it does not simply add to the total: the stops above it usually shorten.") {
                        HStack(spacing: 16) {
                            Stepper("30 m+ : \(m.extStopDeep) min",
                                    value: $m.extStopDeep, in: 0...10)
                                .frame(maxWidth: 260)
                        }
                        HStack(spacing: 16) {
                            Stepper("7–30 m : \(m.extStopShallow) min",
                                    value: $m.extStopShallow, in: 0...10)
                                .frame(maxWidth: 260)
                        }
                    }
                    group("Deco gas limits",
                          help: "The planner auto-selects the deco gas with the highest PO2 that stays within Max PO2 and Max END. Set Max PO2 to 1.6 if you want 100% O2 at the 20 ft / 6 m stop; tune it down to lower CNS exposure at the cost of longer deco.") {
                        HStack(spacing: 16) {
                            row2("Max PO2", $m.maxPO2)
                            row2("Max END", $m.maxEND)
                        }
                    }
                    group("RMV values",
                          help: "Respiratory Minute Volume for gas-consumption planning, in the RMV units above. Deco is usually lower than Bottom, since you are more at rest hanging on the line. If you don't know your RMV, measure it.") {
                        HStack(spacing: 16) {
                            row2("Bottom", $m.bottomRMV)
                            row2("Deco", $m.decoRMV)
                        }
                    }
                }
                .padding(16)
            }
        }
        .background(Color.planPaper)
        .foregroundColor(.primary)
        #if os(macOS)
        .frame(width: 620, height: 720)
        #endif
    }

    /// Gradient factors active (they override Conservatism).
    private var gfOn: Bool { (m.useGF || m.useAltGF) && m.model != "vval" }

    // ---- layout helpers: fixed label widths so nothing truncates ----
    private func group<C: View>(_ title: String, help: String,
                                @ViewBuilder _ content: () -> C) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title).font(.headline)
            content()
            Text(help)
                .font(.caption)
                .foregroundColor(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Divider()
        }
    }
    private func row<C: View>(_ label: String, @ViewBuilder _ c: () -> C) -> some View {
        HStack {
            Text(label).frame(width: 110, alignment: .leading)
            c().frame(maxWidth: 260)
            Spacer(minLength: 0)
        }
    }
    private func row2(_ label: String, _ b: Binding<String>) -> some View {
        HStack(spacing: 6) {
            Text(label).fixedSize()
            TextField("", text: b)
                .textFieldStyle(.roundedBorder)
                .frame(width: 80)
        }
    }
    private func seg(_ b: Binding<Bool>, off: String, on: String) -> some View {
        Picker("", selection: b) { Text(off).tag(false); Text(on).tag(true) }
            .pickerStyle(.segmented).labelsHidden()
    }
    private func editor(_ b: Binding<String>, height: CGFloat) -> some View {
        TextEditor(text: b)
            .font(.system(.body, design: .monospaced))
            .frame(maxWidth: 360, minHeight: height, maxHeight: height)
            .overlay(RoundedRectangle(cornerRadius: 4).stroke(Color.secondary.opacity(0.5)))
    }
}

// MARK: - Log sheet

struct LogSheet: View {
    @ObservedObject var m: PlannerModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationView {
            Group {
                if m.log.isEmpty {
                    Text("No plans logged this session.\n"
                         + "Every successful Calculate is recorded here automatically.")
                        .multilineTextAlignment(.center)
                        .foregroundColor(.secondary)
                        .padding()
                } else {
                    List {
                        ForEach(m.log) { e in
                            VStack(alignment: .leading, spacing: 4) {
                                Text(e.stamp).font(.caption).bold()
                                // Which dive and settings produced this plan.
                                Text(e.summary)
                                    .font(.caption2)
                                    .foregroundColor(.secondary)
                                    .fixedSize(horizontal: false, vertical: true)
                                Text(e.text)
                                    .font(.system(.caption, design: .monospaced))
                                    .textSelection(.enabled)
                            }
                        }
                        .onDelete { idx in
                            for i in idx.sorted(by: >) { m.log.remove(at: i) }
                            m.persistLog()
                        }
                    }
                }
            }
            .navigationTitle(m.log.isEmpty ? "Log"
                             : "Log — \(m.log.count) plan\(m.log.count == 1 ? "" : "s")")
            .toolbar {
                if !m.log.isEmpty { Button("Clear") { m.clearLog() } }
                Button("Done") { dismiss() }
            }
        }
        #if os(macOS)
        .frame(minWidth: 480, minHeight: 480)
        #endif
    }
}
