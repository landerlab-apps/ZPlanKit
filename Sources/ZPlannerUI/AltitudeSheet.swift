//
//  AltitudeSheet.swift - ascent to altitude after diving.
//
import SwiftUI
import ZPlanKit

struct AltitudeSheet: View {
    @ObservedObject var m: PlannerModel
    @Environment(\.dismiss) private var dismiss
    @State private var mode: TripMode = .car
    @State private var altitude = ""
    @State private var wait = ""
    @State private var travel = ""
    @State private var stay = ""
    @State private var oxygen = ""
    @State private var oxygenLast = false
    @State private var result: AltitudeAnswer?
    @State private var error: String?

    private var ft: Double { m.depthsMetric ? 1 : 0.3048 }

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text("Altitude after diving").font(.title3.bold())
                Spacer()
                Button("Done") { dismiss() }.keyboardShortcut(.defaultAction)
            }
            .padding(12)
            Divider()
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    inputs
                    Button(action: run) {
                        Text("Calculate").font(.headline)
                            .padding(.horizontal, 18).padding(.vertical, 6)
                            .overlay(RoundedRectangle(cornerRadius: 4).stroke(Color.primary, lineWidth: 1.5))
                    }.buttonStyle(.plain)
                    if let error { Text(error).font(.caption) }
                    if let r = result { Divider(); output(r) }
                }
                .padding(16)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .background(Color.planPaper)
        .foregroundColor(.primary)
        .onAppear(perform: load)
        #if os(macOS)
        .frame(width: 580, height: 720)
        #endif
    }

    private var inputs: some View {
        VStack(alignment: .leading, spacing: 10) {
            Picker("", selection: Binding(get: { mode }, set: { v in
                mode = v
                altitude = fmt((v == .car ? 2000 : 2438) / ft)
                travel = v == .car ? "60" : "20"
                stay = v == .car ? "24" : "4"
            })) {
                Text("Car").tag(TripMode.car)
                Text("Airplane").tag(TripMode.airplane)
            }
            .pickerStyle(.segmented).labelsHidden().frame(maxWidth: 260)
            field(mode == .car ? "Altitude (\(m.depthUnit))" : "Cabin altitude (\(m.depthUnit))", $altitude)
            field("Wait after surfacing (h:mm)", $wait)
            field(mode == .car ? "Drive time (min)" : "Climb time (min)", $travel)
            field(mode == .car ? "Time at altitude (h)" : "Flight time (h)", $stay)
            field("Oxygen (min)", $oxygen)
            Picker("", selection: $oxygenLast) {
                Text("From surfacing").tag(false)
                Text("Before leaving").tag(true)
            }
            .pickerStyle(.segmented).labelsHidden().frame(maxWidth: 260)
            .disabled(number(oxygen) <= 0)
        }
    }

    private func field(_ label: String, _ b: Binding<String>) -> some View {
        HStack(spacing: 6) {
            Text(label).frame(width: 210, alignment: .leading)
            TextField("", text: b).textFieldStyle(.roundedBorder).frame(width: 80)
        }
    }

    @ViewBuilder private func output(_ r: AltitudeAnswer) -> some View {
        let w = hm(m.altitudeTrip.waitMinutes)
        VStack(alignment: .leading, spacing: 6) {
            Text("Method 1  Bühlmann ZH-L16C").bold()
            if r.method1Available {
                line("Limit", String(format: "GF %.0f%%", r.method1LimitGF * 100))
                line("GF needed leaving at \(w)",
                     pct(r.method1GFAtWait) + (r.method1OK ? "  within limit" : "  EXCEEDS limit"))
                line("Earliest departure", hm(r.method1Earliest))
                line("Earliest, air only", hm(r.method1EarliestAir))
                line("Oxygen to leave at \(w)", o2(r.method1OxygenFromSurfacing, r.method1OxygenBeforeLeaving))
                if m.altitudeTrip.oxygenMinutes > 0 {
                    line("Mask O2", String(format: "%.0f%%", m.altitudeSettings.maskO2Percent))
                    line("Oxygen cost", String(format: "CNS %.0f%%, %.0f OTU", r.oxygenCNS, r.oxygenOTU))
                }
            } else {
                Text("Not available for VVAL-79 dives.").font(.caption)
            }
            Divider()
            Text("Method 2  \(r.method2Name)").bold()
            if r.method2Available {
                line("Limit", String(format: "P(DCS) %@ %g%%",
                                     m.altitudeSettings.method2Total ? "total" : "added",
                                     m.altitudeSettings.method2LimitPercent))
                line("P(DCS) of the dive", p2(r.method2PDive))
                line("Leaving at \(w)", "total \(p2(r.method2PAtWait)), added \(p2(r.method2AddedAtWait))"
                     + (r.method2OK ? "  within limit" : "  EXCEEDS limit"))
                line("Earliest departure", hm(r.method2Earliest))
                line("Earliest, air only", hm(r.method2EarliestAir))
                line("Oxygen to leave at \(w)", o2(r.method2OxygenFromSurfacing, r.method2OxygenBeforeLeaving))
            } else {
                Text("Not available: helium, closed circuit, or a dive in the series planned without it.")
                    .font(.caption)
            }
            Divider()
            VStack(alignment: .leading, spacing: 3) {
                curveRow("leave after", "GF needed", "P(DCS) added").foregroundColor(.secondary)
                ForEach(r.curve) { row in
                    curveRow(String(format: "%.0f h", row.hours),
                             row.gf.map(pct) ?? "–", row.added.map(p2) ?? "–")
                }
            }
            Text("Model output. Neither model was fitted to altitude exposures.")
                .font(.caption).foregroundStyle(.secondary).padding(.top, 6)
        }
        .font(.system(.callout, design: .monospaced))
    }

    private func curveRow(_ a: String, _ b: String, _ c: String) -> some View {
        HStack(spacing: 0) {
            Text(a).frame(width: 110, alignment: .trailing)
            Text(b).frame(width: 110, alignment: .trailing)
            Text(c).frame(width: 130, alignment: .trailing)
        }
    }

    private func line(_ k: String, _ v: String) -> some View {
        HStack(alignment: .top) {
            Text(k).frame(width: 240, alignment: .leading)
            Text(v)
        }
    }

    private func load() {
        let t = m.altitudeTrip
        mode = t.mode
        altitude = fmt(t.altitudeMeters / ft)
        wait = hm(t.waitMinutes, short: true)
        travel = fmt(t.travelMinutes)
        stay = fmt(t.stayMinutes / 60)
        oxygen = fmt(t.oxygenMinutes)
        oxygenLast = t.oxygenBeforeLeaving
    }

    private func run() {
        guard let profile = m.lastProfile else { return }
        var t = AltitudeTrip()
        t.mode = mode
        t.altitudeMeters = number(altitude) * ft
        t.waitMinutes = minutes(wait)
        t.travelMinutes = number(travel)
        t.stayMinutes = number(stay) * 60
        t.oxygenMinutes = number(oxygen)
        t.oxygenBeforeLeaving = oxygenLast
        m.altitudeTrip = t
        do {
            result = try ZPlan.altitude(profile: profile, tissueFile: m.lastTissue, state: m.lastICM,
                                        trip: t, settings: m.altitudeSettings)
            error = nil
        } catch {
            result = nil
            self.error = error.localizedDescription
        }
    }

    private func number(_ s: String) -> Double {
        Double(s.replacingOccurrences(of: ",", with: ".").trimmingCharacters(in: .whitespaces)) ?? 0
    }

    private func minutes(_ s: String) -> Double {
        let f = s.split(separator: ":").map { number(String($0)) }
        return f.count == 2 ? f[0] * 60 + f[1] : (f.first ?? 0) * 60
    }

    private func hm(_ v: Double?, short: Bool = false) -> String {
        guard let v else { return "not within 72 h" }
        let t = Int(v.rounded())
        return short ? String(format: "%d:%02d", t / 60, t % 60)
                     : String(format: "%d h %02d min", t / 60, t % 60)
    }
    private func o2(_ a: Double?, _ b: Double?) -> String {
        "\(a.map { hm($0) } ?? "not possible") from surfacing, \(b.map { hm($0) } ?? "not possible") before leaving"
    }
    private func pct(_ g: Double) -> String { g < 0 ? "<0" : String(format: "%.0f%%", g * 100) }
    private func p2(_ p: Double) -> String { String(format: "%.2f%%", p * 100) }
    private func fmt(_ v: Double) -> String {
        v == v.rounded() ? String(Int(v)) : String(format: "%.1f", v)
    }
}
