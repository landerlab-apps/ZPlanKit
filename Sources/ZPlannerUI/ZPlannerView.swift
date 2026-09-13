//
//  ZPlannerView.swift — v1.9.0
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
/// `algorithms` names the models the build actually ships, so Lplanner says
/// VVAL-79 not VVAL-18.
public struct Disclaimer {
    public static var algorithms =
        "A. A. Buhlmann's algorithm, the VVAL-79 algorithm, or the VPM-B algorithm"

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
    one allowed by Max PO2 and Max END. The new mix appears in the gas column \
    of the stop where you change on to it. If the switch depth is not a stop, \
    a GasSw row marks it instead. Config can also hold you there for a few \
    extra minutes — see Extended stops.

    SETTINGS STRIP
    On a phone the settings sit in one strip of chips above the tabs. It folds \
    to a single summary line on the Plan tab so the schedule gets the full \
    screen; the chevron opens or closes it by hand. altGF is a plain on/off \
    there — its two numbers are set in Config.

    CONFIG
    Units, water, altitude, model, gradient factors, deep stops, air breaks, \
    ascent and descent rates, deco gas limits, RMVs. Config itself is a plain \
    list of controls; every setting is explained under CONFIG SETTINGS below.

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

    DAN RECOMMENDATIONS
    Divers Alert Network guidance, which sits outside any decompression \
    model and is not enforced by this planner.

    Flying after diving. The Time to Fly figure on the plan is the model's \
    own arithmetic — the hours until your tissues tolerate a 10,000 ft \
    cabin. It is not DAN's advice and is usually far shorter. DAN \
    recommends a minimum 12-hour surface interval before flying after a \
    single no-decompression dive, 18 hours after multiple dives or several \
    days of diving, and considerably longer after any dive requiring \
    decompression stops — commonly given as at least 24 hours. Take the \
    longer figure.

    Altitude after diving. Driving over a mountain pass is the same problem \
    as flying and is easier to overlook. Apply the same intervals.

    Diving at altitude. Arriving and diving the same day means your tissues \
    still hold sea-level nitrogen, which is why Config asks whether you are \
    equilibrated. DAN's guidance is to allow time at altitude before diving \
    where you can.

    Hydration, exertion and thermal stress all affect decompression and \
    none are modelled here. Cold on the deep portion followed by warm \
    shallow stops is the worst combination for gas elimination.

    Ascent rate. Keep to the rate you planned. DAN and every training \
    agency give 9–10 m/min as the maximum for the shallow portion.

    If you feel unwell after a dive, breathe oxygen and call the DAN \
    emergency line for your region. Symptoms that appear hours later are \
    still decompression illness.
    """
}

// MARK: - Help menu

/// The Help menu is built in the App scene and the manual is presented by the
/// planner view, and on the macOS 12 deployment target a Commands block has no
/// way to reach a view's state. This is the wire between the two.
public final class HelpBus: ObservableObject {
    public static let shared = HelpBus()
    @Published public var showManual = false
    private init() {}
}

/// Replaces the stock "Lplanner Help", which opened a help book that was never
/// written and reported "Help isn't available for Lplanner". On iOS the help
/// command group does not exist, so this resolves to nothing and is harmless.
public struct ZPlannerHelpCommands: Commands {
    public init() {}
    public var body: some Commands {
        CommandGroup(replacing: .help) {
            Button("Lplanner Manual") { HelpBus.shared.showManual = true }
                .keyboardShortcut("?", modifiers: [.command])
        }
    }
}

// MARK: - Config guide

/// Every Config setting, explained. Config itself carries no prose: it is a
/// list of controls, and the explanations live here, where they can be read
/// end to end instead of a paragraph at a time between two pickers.
/// Kept word-for-word identical to `ConfigGuide` in Disclaimer.kt.
public struct ConfigGuide {
    public static let text = """
    Every setting in Config, in the order the sections appear. Nothing here changes a dive on its own: it changes how the planner computes one.

    UNITS
    Depths sets the units for depth, altitude, stop distance, END, ascent and descent rates, and the dive levels themselves. Every value already entered is converted when you switch, and the plan is then computed in those units — a 10 ft stop grid is a grid of whole feet. RMVs sets the units for breathing-rate and gas-consumption figures; it follows Depths until you set it yourself, after which it stays where you put it.

    ENVIRONMENT
    Fresh or salt water changes the depth-to-pressure conversion. O2 Narcotic controls whether oxygen counts as narcotic when calculating equivalent narcotic depths (ENDs).

    MODEL
    ZHL16-C is the Bühlmann set used here.

    VVAL-79 is the U.S. Navy Thalmann EL-DCM (exponential uptake, linear elimination) with the VVal-79 air parameter set behind the Diving Manual Revision 7 air tables. It plans AIR AND NITROX ONLY: the Navy publishes no helium parameters for it, so a dive carrying helium is refused rather than computed.

    VPM-B is the Yount/Hoffman varying permeability bubble model in Erik Baker's implementation. It limits the volume of gas released from bubble nuclei rather than the tension dissolved in tissue, which is why it puts the first stop much deeper, especially on helium mixes.

    Gradient factors and Conservatism apply to ZHL16-C only; VVAL-79 has neither, and VPM-B has its own conservatism ladder. With gradient factors enabled, Pyle deep stops are disabled — GF Low provides the deep-stop function — and Conservatism is ignored.

    VPM-B
    Conservatism 0–4 scales both critical radii: a larger nucleus is excited by a smaller gradient, so higher levels give more decompression. Level 0 is Baker's nominal VPM-B and is the setting that reproduces his published reference schedule. The critical radii are the parameter that actually differs between implementations — Baker ships 0.6 and 0.5 microns, Subsurface 0.55 and 0.45. Changing them takes you outside the validated envelope, so leave them alone unless you are deliberately comparing against another planner.

    ALTERNATIVE GRADIENT FACTORS
    A second GF pair, used instead of the main pair whenever altGF is checked on the main screen. Set these to whatever you like — any values are accepted, low and high independently, and they need not bracket the main pair. 100/100 gives the pure Bühlmann ZHL-16C ceiling; values above 100 go beyond it, which is less conservative than the raw model. A low GF Low with a high GF High deepens the first stop while keeping the shallow stops short. Editable here or directly beside the altGF checkbox on the main screen.

    NDL CALCULATION
    Which gradient factor decides whether a direct, no-stop ascent to the surface is still allowed. GF High is the standard behaviour for ZHL16-C. GF Low is stricter and ends the no-decompression phase earlier.

    CONDITIONS
    Altitude of the dive site, 0 for sea level. Above sea level the air is thinner, so the same dive carries more decompression. Equilibrated means your tissues have off-gassed their excess nitrogen to match the thinner air; the U.S. Navy Diving Manual puts that at about twelve hours at altitude. If you drove up this morning you are still carrying your sea-level nitrogen and need considerably more decompression — at 3000 m that can double the obligation, so state it honestly. Hours at altitude covers the middle: the tissues wash out at their own rates, and the slow ones are still loaded well after the fast ones have finished. Note this is equilibration, not acclimatisation — adjusting to the lower oxygen takes far longer and is not modelled here at all.

    Conservatism applies only to ZHL16-C with gradient factors switched off. It (0–50 %) preloads the tissue compartments with additional inert gas — nitrogen, and helium in proportion when the profile uses trimix — weighted from the fast compartments (none) to the slow ones (the full percentage), as if a previous dive had been made. Zero is the clean-diver profile.

    STOP DEPTHS
    Stop distance is the interval between decompression stops — 3 m is the convention, some rebreather divers prefer 6 m. Last stop is the depth of the final stop; some prefer pulling the 10 ft / 3 m stop deeper. Both apply to every schedule, whichever model, gradient factors or deep stops are in use.

    DEEP STOPS
    Pyle deep stops insert short stops between the bottom and the first normal stop (mean-depth rule, re-run iteratively) to reduce microbubble formation and post-dive fatigue. Pyle stop time is the minutes spent at each generated stop (1–5). Not shown when gradient factors are enabled: GF Low takes over the deep-stop role.

    AIR BREAKS
    A break is planned when you are breathing oxygen at the last stop depth or shallower, or when CNS reaches the warning threshold on any rich mix. Break after is the oxygen time that earns a break, Break for is its length. The oxygen clock is cumulative: it runs across stop changes and excludes travel, so "Break after 30" means thirty minutes of oxygen wherever it was breathed.

    Break gas is the mix you switch to. Left blank the planner takes the leanest mix you carry that is still breathable at that depth, which is what keeps a hypoxic back gas out of a 3 m break. No break is planned in the last few minutes before surfacing, and none is planned on closed circuit: there the answer is to lower the setpoint, and the plan says so.

    Navy: the break is gas-exchange dead time. Inert tensions freeze and the stop simply grows by the break length. This is how the US Navy Air/O2 tables were generated and it is the only treatment published work validates.

    Subsurface: the break is an ordinary gas segment, integrated on the break gas. The stop grows by whatever the model says. Physically truer, and validated by nobody.

    CNS and OTU accrue on the break gas in both modes: dead time is about inert gas only.

    TRAVEL GAS
    With Travel gas checked, a descent on a hypoxic back gas starts on the leanest mix you carry that is breathable at the surface, and changes to the back gas at the first stop increment where the back gas is safe. It costs no decompression: it only moves the first few metres onto a stage. If no carried mix is breathable at the surface the plan says so and starts on the back gas anyway.

    DESCENT AND ASCENT RATES
    One range per line: depth1-depth2, rate, in your depth units. List descent ranges shallowest first and ascent ranges deepest first, and leave no gaps — a depth not covered by any range has no rate to travel at. The deepest ascent range must reach at least your deepest level, or the ascent from the bottom has no defined rate. Slow shallow ascent rates are credited to the decompression and can shorten stops or remove them entirely.

    DECO SET POINT (CCR) AND SLIDE RATE
    Setpoint changes by depth range during CCR deco, one per line, e.g. 80-30, 1.4 — a setpoint of 0 switches to open circuit for that range. Only active on closed circuit: the OC/CCR chip on a phone, the Open/Closed control elsewhere. Slide rate is the PO2 burned off per minute during a Scamahorn Slide: enter a bottom setpoint like 1.2-1.6 to ride the descent PO2 spike down to the setpoint for a deco advantage.

    EXTENDED STOPS ON A DECO MIX SWITCH
    Extra minutes held at the depth where the planner switches to a deco mix, on top of whatever the model requires. Common practice: settle on the new gas, confirm the analysis and the PO2, and let the switch do some work for you. The amount is chosen by the depth of the switch, in two bands. Switches shallower than 7 m / 23 ft are not extended — the final stop is already long. The extra time off-gasses you, so it does not simply add to the total: the stops above it usually shorten.

    DECO GAS LIMITS
    The planner auto-selects the deco gas with the highest PO2 that stays within Max PO2 and Max END. Set Max PO2 to 1.6 if you want 100% O2 at the 20 ft / 6 m stop; tune it down to lower CNS exposure at the cost of longer deco. At 1.55 oxygen is held to the 3 m stop instead, which lengthens the schedule and lowers the CNS total.

    RMV VALUES
    Respiratory Minute Volume for gas-consumption planning, in the RMV units above. Deco is usually lower than Bottom, since you are more at rest hanging on the line. If you don't know your RMV, measure it.
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
    /// Set once the diver picks the RMV units themselves. Optional so that a
    /// state.json written by an earlier build still decodes — a non-optional
    /// addition would throw, and the loader's `?? PlannerState()` fallback
    /// would silently discard every saved setting and dive level.
    var rmvMetricOverride: Bool? = nil
    var model = "c"
    var useGF = false, gfLow = "30", gfHigh = "85", altGfLow = "90", altGfHigh = "90"
    var ndlLow = false
    var altitude = "0", conservatism = 10.0
    var altitudeEquilibrated = false, hoursAtAltitude = "0"
    var deepStops = "p", pyleTime = 1, stopDistance = "3", lastStop = "3"
    var descentRates = "0-100, 15"
    var ascentRates = "70-30, 18\n30-12, 9\n12-0, 3"
    var decoSetpoints = "", slideRate = "0.1", maxPO2 = "1.6", maxEND = "40"
    var bottomRMV = "19", decoRMV = "14"
    var extStopShallow = 0, extStopDeep = 0
    var airBreaksOn = false, airBreakMode = "navy"   // navy / subsurface
    var breakAfter = "30", breakFor = "5", breakGas = ""
    var si48 = false, si24 = false, siActual = ""
    var decoGasesOn = true, decoGases = "50"
    var circuitClosed = false, plus3m = false, plus5min = false, useAltGF = false
    var travelGas = false
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
    /// True once the diver has set the RMV units explicitly. Until then the RMV
    /// units follow the depth units and no `RmvMetric:` line is written, which
    /// lets the engine's own "follow UseMetric" default (rmv_metric = -1) apply.
    @Published var rmvMetricOverride = false
    @Published var saltWater = true             // Water: Fresh / Salt
    @Published var o2Narcotic = false           // O2 Narcotic: No / Yes
    @Published var model = "c"                  // "c" ZHL16-C, "vval" VVAL-79, "vpm" VPM-B
    // VPM-B settings. Conservatism 0-4 scales both critical radii; 0 is
    // Baker's nominal VPM-B, which is what reproduces his published VPM.OUT.
    @Published var vpmConservatism = 0.0
    @Published var vpmRadiusN2 = "0.6"
    @Published var vpmRadiusHe = "0.5"
    @Published var useGF = false
    @Published var gfLow = "30"
    @Published var gfHigh = "85"
    @Published var altGfLow = "90"
    @Published var altGfHigh = "90"
    @Published var ndlLow = false
    @Published var altitude = "0"
    /// Above sea level only. False plus hoursAtAltitude 0 is the diver who
    /// drove up this morning - the conservative default, and the common case.
    @Published var altitudeEquilibrated = false
    @Published var hoursAtAltitude = "0"
    @Published var conservatism = 10.0          // 0-100 %
    @Published var deepStops = "p"              // n / p
    /* Oxygen ("air") breaks. Off by default, as in Subsurface. When on, the
     * mode decides what the break does to the decompression, not how long it
     * is: Navy freezes inert gas exchange for the break (AB_DEAD, how the USN
     * Air/O2 tables were built), Subsurface integrates it as an ordinary gas
     * segment. CNS and OTU run on the break gas either way. */
    @Published var airBreaksOn = false
    @Published var airBreakMode = "navy"        // navy / subsurface
    @Published var breakAfter = "30"            // minutes on the rich mix
    @Published var breakFor = "5"               // minutes on the break gas
    @Published var breakGas = ""                // blank = automatic
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
    /// Descend on the leanest carried mix breathable at the surface when the
    /// back gas is hypoxic there, switching at the first safe stop increment.
    @Published var travelGas = false
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
        // Absent in states written before v1.22: infer an override from the
        // saved pair, so a diver who had deliberately mixed units keeps them.
        rmvMetricOverride = s.rmvMetricOverride ?? (s.rmvMetric != s.depthsMetric)
        saltWater = s.saltWater; o2Narcotic = s.o2Narcotic
        model = s.model
        useGF = s.useGF; gfLow = s.gfLow; gfHigh = s.gfHigh
        altGfLow = s.altGfLow; altGfHigh = s.altGfHigh
        ndlLow = s.ndlLow
        altitude = s.altitude; conservatism = s.conservatism
        altitudeEquilibrated = s.altitudeEquilibrated
        hoursAtAltitude = s.hoursAtAltitude
        deepStops = s.deepStops; pyleTime = s.pyleTime
        airBreaksOn = s.airBreaksOn; airBreakMode = s.airBreakMode
        breakAfter = s.breakAfter; breakFor = s.breakFor; breakGas = s.breakGas
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
        travelGas = s.travelGas
        levels = s.levels
        baselineTissue = s.baselineTissue
        baselineDate = s.baselineDate
    }

    private var snapshot: PlannerState {
        var s = PlannerState()
        s.depthsMetric = depthsMetric; s.rmvMetric = rmvMetric
        s.rmvMetricOverride = rmvMetricOverride
        s.saltWater = saltWater; s.o2Narcotic = o2Narcotic
        s.model = model
        s.useGF = useGF; s.gfLow = gfLow; s.gfHigh = gfHigh
        s.altGfLow = altGfLow; s.altGfHigh = altGfHigh
        s.ndlLow = ndlLow
        s.altitude = altitude; s.conservatism = conservatism
        s.altitudeEquilibrated = altitudeEquilibrated
        s.hoursAtAltitude = hoursAtAltitude
        s.deepStops = deepStops; s.pyleTime = pyleTime
        s.airBreaksOn = airBreaksOn; s.airBreakMode = airBreakMode
        s.breakAfter = breakAfter; s.breakFor = breakFor; s.breakGas = breakGas
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
        s.travelGas = travelGas
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
        // UseMetric first, and RmvMetric immediately after it when the diver has
        // pinned the gas units: the engine applies its unit scaling as each key
        // is read, so both flags have to precede any value they govern.
        // Omitting RmvMetric is deliberate, not an oversight — it is what makes
        // the engine's "gas units follow depth units" default apply.
        var p = "UseMetric: \(depthsMetric ? "y" : "n")\n"
        if rmvMetricOverride { p += "RmvMetric: \(rmvMetric ? "y" : "n")\n" }
        p += """
        SaltWater: \(saltWater ? "y" : "n")
        Model: \(model == "vval" ? "vval79" : model == "vpm" ? "vpm" : "zhl16c")
        Altitude: \(altitude)
        AltitudeEquil: \(altitudeEquilibrated ? "y" : "n")
        HoursAtAltitude: \(hoursAtAltitude)
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
        AirBreaks: \(airBreaksOn ? airBreakMode : "n")
        O2Period: \(breakAfter)
        AirBreakTime: \(breakFor)
        BreakGas: \(breakGas)
        TravelGas: \(travelGas ? "y" : "n")
        """
        if model == "vpm" {
            p += "\nVpmConservatism: \(Int(vpmConservatism))"
            p += "\nVpmRadiusN2: \(vpmRadiusN2)"
            p += "\nVpmRadiusHe: \(vpmRadiusHe)"
        }
        if (useGF || useAltGF) && model == "c" {
            let lo = useAltGF ? altGfLow : gfLow
            let hi = useAltGF ? altGfHigh : gfHigh
            p += "\nGradientFactors: \(lo), \(hi)"
        }
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

    // MARK: - Units
    //
    // Settings are held in whatever units the diver is working in, and the
    // engine is asked to compute in those same units — a 10 ft stop grid is a
    // grid of whole feet, not of 3.048 m. Flipping a units control therefore
    // has to rewrite every value that carries a dimension. Before v1.22 it
    // rewrote none of them, so switching to Feet reinterpreted the metric
    // defaults as feet: a 3 m last stop silently became 3 ft, and the ascent
    // bands (70-30, 30-12, 12-0) left a 131 ft dive with no defined rate at
    // all above 70 ft.

    private static let ftPerM = 3.280839895013123
    private static let litresPerCuFt = 28.316846592

    /// Depth unit in force, for labels.
    var depthUnit: String { depthsMetric ? "m" : "ft" }
    /// RMV / gas-volume unit in force, for labels.
    var rmvUnit: String { rmvMetric ? "L/min" : "cu.ft/min" }

    /// Switch the depth unit system, converting every depth-dimensioned value.
    ///
    /// Named `changeDepthUnits` rather than `setDepthsMetric` to match the
    /// Kotlin port, where the latter collides with the JVM setter that the
    /// `depthsMetric` property already generates.
    ///
    /// Rounding is to whole units, which is what makes the round trip stable:
    /// 3 m -> 10 ft -> 3 m, 40 m -> 131 ft -> 40 m. It also lands on the
    /// conventional imperial values divers expect (a 10 ft stop grid, not 9.8).
    func changeDepthUnits(_ metric: Bool) {
        guard metric != depthsMetric else { return }
        let f = metric ? 1.0 / Self.ftPerM : Self.ftPerM
        altitude      = scale(altitude, f)
        stopDistance  = scale(stopDistance, f)
        lastStop      = scale(lastStop, f)
        maxEND        = scale(maxEND, f)
        descentRates  = scaleLines(descentRates, f, skip: [])
        ascentRates   = scaleLines(ascentRates, f, skip: [])
        // "80-30, 1.4": the depths convert, the setpoint must not.
        decoSetpoints = scaleLines(decoSetpoints, f, skip: [2])
        levels = levels.map { var l = $0; l.d = scale(l.d, f); return l }
        entry.d = scale(entry.d, f)
        depthsMetric = metric
        // Gas units follow depth units unless the diver has said otherwise.
        if !rmvMetricOverride { applyRmvUnits(metric) }
    }

    /// Switch the RMV / gas-volume unit system. Marks the choice as explicit,
    /// which pins it against later depth-unit changes and makes the planner
    /// write an `RmvMetric:` line to say so.
    func changeRmvUnits(_ metric: Bool) {
        rmvMetricOverride = true
        applyRmvUnits(metric)
    }

    private func applyRmvUnits(_ metric: Bool) {
        guard metric != rmvMetric else { return }
        // 19 L/min <-> 0.67 cu.ft/min. Two decimals imperial, whole litres
        // metric: a cubic foot is coarse enough that 0.1 would lose 3 L/min.
        if metric {
            bottomRMV = scale(bottomRMV, Self.litresPerCuFt, dp: 0)
            decoRMV   = scale(decoRMV,   Self.litresPerCuFt, dp: 0)
        } else {
            bottomRMV = scale(bottomRMV, 1.0 / Self.litresPerCuFt, dp: 2)
            decoRMV   = scale(decoRMV,   1.0 / Self.litresPerCuFt, dp: 2)
        }
        rmvMetric = metric
    }

    /// Scale one numeric field. Anything unparseable is left exactly as typed —
    /// a half-finished entry must never be silently rewritten to something else.
    private func scale(_ s: String, _ f: Double, dp: Int = 0) -> String {
        let t = s.trimmingCharacters(in: .whitespaces)
        guard !t.isEmpty, let v = Double(t) else { return s }
        return round(v * f, dp)
    }

    private func round(_ v: Double, _ dp: Int) -> String {
        let p = pow(10.0, Double(dp))
        let r = (v * p).rounded() / p
        return dp == 0 ? String(Int(r)) : String(format: "%.\(dp)f", r)
    }

    /// Scale every number in a multi-line list, per line, skipping the
    /// positions named in `skip` (0-based within the line).
    private func scaleLines(_ s: String, _ f: Double, skip: Set<Int>) -> String {
        s.split(separator: "\n", omittingEmptySubsequences: false)
         .map { line -> String in
             var out = "", num = "", idx = 0
             func flush() {
                 guard !num.isEmpty else { return }
                 if !skip.contains(idx), let v = Double(num) { out += round(v * f, 0) }
                 else { out += num }
                 idx += 1; num = ""
             }
             for ch in line {
                 if ch.isNumber || ch == "." { num.append(ch) }
                 else { flush(); out.append(ch) }
             }
             flush()
             return out
         }
         .joined(separator: "\n")
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
            // NOT r.warnings. zp_report() already appends the engine's
            // warnings to the end of the plan, so copying them here printed
            // every one of them twice — once above the schedule and once
            // inside it. The notes line is for the reasons there is NO plan:
            // an unstated surface interval, no enabled levels, an engine
            // refusal. Those all leave planText empty, so nothing else can
            // carry them.
            notes = ""
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
            modelText = "VVAL-79"
        } else if model == "vpm" {
            modelText = "VPM-B +\(Int(vpmConservatism))"
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
        if extStopShallow > 0 || extStopDeep > 0 {
            extras.append("ext stops \(extStopDeep)/\(extStopShallow) min")
        }
        if airBreaksOn {
            extras.append("air breaks \(airBreakMode == "navy" ? "Navy" : "Subsurface") \(breakAfter)/\(breakFor)")
        }
        if plus3m { extras.append(depthsMetric ? "+3m" : "+10ft") }
        if plus5min { extras.append("+5min") }
        if travelGas { extras.append("travel gas") }
        if repetitive { extras.append("SI \(surfaceInterval)") }

        return ([dive, modelText] + extras).joined(separator: " · ")
    }

    /// Gradient factors active — they override Conservatism.
    var gfOn: Bool { (useGF || useAltGF) && model == "c" }

    /// Conservatism preloads the tissues; it applies to the Buhlmann model
    /// only, and only when gradient factors are off. VPM-B has its own
    /// conservatism ladder and ignores this one.
    var consOn: Bool { model == "c" && !gfOn }

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
    /// Where contributions go. Shown in the macOS build only — see infoSheet.
    static let paypalAddress = "landercarlos@hotmail.com"

    /// PayPal.Me handle, without the leading "paypal.me/". Empty = no link, and
    /// the Info panel then shows the address alone.
    ///
    /// This replaces the `/donate/` endpoint, which PayPal refuses outright in
    /// some countries — "Donations aren't supported in this organization's
    /// country" — regardless of the parameters passed. PayPal.Me is a plain
    /// payment link, not a donation flow, so that restriction does not apply;
    /// it needs only a personal account, and the sender types the amount
    /// themselves. Claim one free at paypal.me. Note it cannot be changed or
    /// deleted afterwards, so pick the handle deliberately.
    static let paypalHandle = "carloselander"

    /// One tap to a payable page, because a plain address asks the reader to
    /// open PayPal, find "send money" and retype it — three chances to give up.
    static var paypalURL: URL? {
        paypalHandle.isEmpty ? nil
                             : URL(string: "https://paypal.me/\(paypalHandle)")
    }
    @StateObject private var m = PlannerModel()
    @State private var showConfig = false
    @State private var showLog = false
    @State private var showInfo = false
    /// Driven by the Help menu on the Mac; never set anywhere else.
    @ObservedObject private var help = HelpBus.shared
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
        .sheet(isPresented: $help.showManual) { manualSheet }
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
                    // The manual is a menu item on the Mac, where anyone looks
                    // for one first. Repeating it inside Info would put two
                    // copies of the same text on one platform.
                    #if os(macOS)
                    Text("The manual, including an explanation of every Config "
                       + "setting, is under Help \u{25B8} Lplanner Manual.")
                        .font(.callout)
                        .fixedSize(horizontal: false, vertical: true)
                    #else
                    Text(Manual.text)
                        .font(.callout)
                        .fixedSize(horizontal: false, vertical: true)
                    Divider()
                    Text(ConfigGuide.text)
                        .font(.callout)
                        .fixedSize(horizontal: false, vertical: true)
                    #endif
                    Divider()
                    // macOS only, deliberately. An iOS build may not ask for or
                    // link to donations outside the App Store — App Review
                    // guideline 3.2.2(vi) treats it as circumventing in-app
                    // purchase, and it is a rejection every time. The same block
                    // appears in the F-Droid Android build, which has no such
                    // restriction, and is absent from the Play build.
                    #if os(macOS)
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Support the developer")
                            .fontWeight(.semibold)
                        Text("Lplanner is free and has no adverts, no tracking "
                           + "and no subscription. If it has been useful to you, "
                           + "you can send the developer a contribution:")
                            .font(.callout)
                            .fixedSize(horizontal: false, vertical: true)
                        if let url = Self.paypalURL {
                            Link("Send a contribution with PayPal", destination: url)
                                .font(.callout.weight(.semibold))
                            // The address stays visible under the link: some
                            // people will not follow a payment link from inside
                            // an app, and should not have to hunt for another
                            // way to do it.
                            Text("or send to \(Self.paypalAddress)")
                                .font(.caption)
                                .foregroundColor(.secondary)
                                .textSelection(.enabled)
                        } else {
                            Text("PayPal, to \(Self.paypalAddress)")
                                .font(.system(.callout, design: .monospaced))
                                .textSelection(.enabled)
                        }
                    }
                    Divider()
                    #endif
                    // Engine version AND the build this came from. The engine
                    // number alone was ambiguous once builds went out to
                    // testers: "I'm on 1.10.0" identifies the maths, not the
                    // app, so a bug report could not be tied to a build.
                    Text(buildStamp)
                        .font(.caption).foregroundColor(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .padding(20)
        .frame(minWidth: 340, maxWidth: 480, minHeight: 420)
        .background(Color.planPaper)
    }


    /// The manual: how to drive the planner, then every Config setting. Opened
    /// from Help on the Mac. Selectable, because people quote it in reports.
    private var manualSheet: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("Lplanner Manual").font(.title3.bold())
                Spacer()
                Button("Done") { help.showManual = false }
                    .keyboardShortcut(.defaultAction)
            }
            .padding(.bottom, 14)

            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    Text(Manual.text)
                        .font(.callout)
                        .fixedSize(horizontal: false, vertical: true)
                    Divider()
                    Text(ConfigGuide.text)
                        .font(.callout)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .textSelection(.enabled)
            }
        }
        .padding(20)
        .frame(minWidth: 420, maxWidth: 620, minHeight: 520)
        .background(Color.planPaper)
    }

    /// e.g. "1.1 (7) · engine 1.10.0 · AI-assisted" — what a tester should quote in a report.
    private var buildStamp: String {
        let info = Bundle.main.infoDictionary
        let short = info?["CFBundleShortVersionString"] as? String ?? "?"
        let build = info?["CFBundleVersion"] as? String ?? "?"
        return "\(short) (\(build)) · engine \(ZPlan.version) · AI-assisted"
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
            check("travel gas", isOn: $m.travelGas)
            check("air breaks", isOn: $m.airBreaksOn)
            HStack(spacing: 4) {
                check("altGF", isOn: $m.useAltGF)
                // editable here as well as in Config — any values accepted
                TextField("", text: $m.altGfLow)
                    .textFieldStyle(.roundedBorder).frame(width: 44)
                Text("/")
                TextField("", text: $m.altGfHigh)
                    .textFieldStyle(.roundedBorder).frame(width: 44)
            }
            .opacity(m.model == "c" ? 1 : 0.4)
            .disabled(m.model != "c")
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
                    group("Units") {
                        // The conversion is deferred by one runloop turn on
                        // purpose. A Picker calls its binding's setter while
                        // SwiftUI is still evaluating this view, and these
                        // setters publish changes to a dozen @Published
                        // properties — doing that mid-update draws "Publishing
                        // changes from within view updates is not allowed",
                        // which SwiftUI documents as undefined behaviour.
                        row("Depths") {
                            seg(Binding(get: { m.depthsMetric },
                                        set: { v in DispatchQueue.main.async { m.changeDepthUnits(v) } }),
                                off: "Feet", on: "Meters")
                        }
                        row("RMVs") {
                            seg(Binding(get: { m.rmvMetric },
                                        set: { v in DispatchQueue.main.async { m.changeRmvUnits(v) } }),
                                off: "Cu.ft.", on: "Liters")
                        }
                    }
                    group("Environment") {
                        row("Water") { seg($m.saltWater, off: "Fresh", on: "Salt") }
                        row("O2 Narcotic") { seg($m.o2Narcotic, off: "No", on: "Yes") }
                    }
                    group("Model") {
                        Picker("", selection: $m.model) {
                            Text("ZHL16-C").tag("c")
                            Text("VVAL-79").tag("vval")
                            Text("VPM-B").tag("vpm")
                        }.pickerStyle(.segmented).labelsHidden()
                        if m.model == "c" {
                            Toggle("Gradient factors", isOn: $m.useGF)
                            HStack(spacing: 16) {
                                row2("GF Low", $m.gfLow)
                                row2("GF High", $m.gfHigh)
                            }
                            .disabled(!m.useGF)
                            .opacity(m.useGF ? 1 : 0.4)
                        }
                    }
                    if m.model == "vpm" {
                        group("VPM-B") {
                            VStack(alignment: .leading, spacing: 4) {
                                Text("Conservatism: +\(Int(m.vpmConservatism))   (0 = nominal VPM-B)")
                                Slider(value: $m.vpmConservatism, in: 0...4, step: 1)
                                    .frame(maxWidth: 360)
                                    .tint(.secondary)
                            }
                            HStack(spacing: 16) {
                                row2("Radius N2 (um)", $m.vpmRadiusN2)
                                row2("Radius He (um)", $m.vpmRadiusHe)
                            }
                        }
                    }
                    if m.model == "c" {
                        group("Alternative gradient factors") {
                            HStack(spacing: 16) {
                                row2("Alt GF Low", $m.altGfLow)
                                row2("Alt GF High", $m.altGfHigh)
                            }
                        }
                        group("NDL calculation") {
                            Picker("Calculate NDL by", selection: $m.ndlLow) {
                                Text("GF High (standard)").tag(false)
                                Text("GF Low").tag(true)
                            }.pickerStyle(.segmented)
                            .disabled(!gfOn)
                            .opacity(gfOn ? 1 : 0.4)
                        }
                    }
                    group("Conditions") {
                        row2("Altitude (\(m.depthUnit))", $m.altitude)
                        // Only shown above sea level, where the two references
                        // differ. At 0 m equilibrated and just-arrived are the
                        // same tissue loading and the control would be noise.
                        if (Double(m.altitude) ?? 0) > 0 {
                            Toggle("Diver equilibrated at this altitude (about 12 h)",
                                   isOn: $m.altitudeEquilibrated)
                            if !m.altitudeEquilibrated {
                                row2("Hours at altitude", $m.hoursAtAltitude)
                                Text("0 = arrived just now, carrying sea-level nitrogen.")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                        VStack(alignment: .leading, spacing: 4) {
                            Text(m.model == "vpm"
                                   ? "Conservatism — VPM-B uses its own, above"
                                   : m.model == "vval"
                                   ? "Conservatism — not used by VVAL-79"
                                   : gfOn
                                   ? "Conservatism — not used with gradient factors"
                                   : "Conservatism: \(Int(m.conservatism)) %  (0–50 maximum)")
                            Slider(value: $m.conservatism, in: 0...50, step: 1)
                                .frame(maxWidth: 360)
                                .tint(.secondary)
                                .disabled(!m.consOn)
                        }
                        .opacity(m.consOn ? 1 : 0.4)
                    }
                    // Stop grid stands on its own. It used to live inside Deep
                    // stops, which hid it completely whenever gradient factors
                    // were on — yet every schedule is built on this grid, GF or
                    // not, Pyle or not, and a diver who wants 6 m increments on
                    // a rebreather has nothing to do with deep stops.
                    group("Stop depths") {
                        HStack(spacing: 16) {
                            row2("Stop distance (\(m.depthUnit))", $m.stopDistance)
                            row2("Last stop (\(m.depthUnit))", $m.lastStop)
                        }
                    }
                    if !(m.useGF && m.model == "c") {
                        group("Deep stops") {
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
                    group("Air breaks") {
                        Toggle("Plan air breaks", isOn: $m.airBreaksOn)
                        if m.airBreaksOn {
                            Picker("", selection: $m.airBreakMode) {
                                Text("Navy").tag("navy")
                                Text("Subsurface").tag("subsurface")
                            }.pickerStyle(.segmented).labelsHidden()
                            HStack(spacing: 16) {
                                row2("Break after (min)", $m.breakAfter)
                                row2("Break for (min)", $m.breakFor)
                            }
                            row2("Break gas", $m.breakGas)
                        }
                    }
                    group("Descent — range, rate (\(m.depthUnit)/min)") {
                        editor($m.descentRates, height: 52)
                    }
                    group("Ascent — range, rate (\(m.depthUnit)/min, deepest first)") {
                        editor($m.ascentRates, height: 76)
                    }
                    group("Deco Set Point (CCR) / Slide rate") {
                        editor($m.decoSetpoints, height: 52)
                            .disabled(!m.circuitClosed)
                            .opacity(m.circuitClosed ? 1 : 0.4)
                        row2("Slide rate (PO2/min)", $m.slideRate)
                    }
                    group("Extended stops on a deco mix switch") {
                        HStack(spacing: 16) {
                            Stepper(m.depthsMetric ? "30 m+ : \(m.extStopDeep) min"
                                                   : "100 ft+ : \(m.extStopDeep) min",
                                    value: $m.extStopDeep, in: 0...10)
                                .frame(maxWidth: 260)
                        }
                        HStack(spacing: 16) {
                            Stepper(m.depthsMetric ? "7–30 m : \(m.extStopShallow) min"
                                                   : "23–100 ft : \(m.extStopShallow) min",
                                    value: $m.extStopShallow, in: 0...10)
                                .frame(maxWidth: 260)
                        }
                    }
                    group("Deco gas limits") {
                        HStack(spacing: 16) {
                            row2("Max PO2", $m.maxPO2)
                            row2("Max END (\(m.depthUnit))", $m.maxEND)
                        }
                    }
                    group("RMV values") {
                        HStack(spacing: 16) {
                            row2("Bottom (\(m.rmvUnit))", $m.bottomRMV)
                            row2("Deco (\(m.rmvUnit))", $m.decoRMV)
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
    private var gfOn: Bool { (m.useGF || m.useAltGF) && m.model == "c" }

    // ---- layout helpers: fixed label widths so nothing truncates ----
    /// Config is a plain list of controls. The explanation of every setting
    /// lives in the manual instead - Help > Lplanner Manual on the Mac, the
    /// Info button elsewhere - so the sheet stays short enough to find things
    /// in and the text stays long enough to be worth reading.
    private func group<C: View>(_ title: String,
                                @ViewBuilder _ content: () -> C) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title).font(.headline)
            content()
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
