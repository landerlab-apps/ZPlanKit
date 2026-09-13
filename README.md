# ZPlanKit v1.23.0

> **WARNING**
>
> This generated dive schedule could indirectly kill you and probably has bugs.
> The author does not warrant that it accurately reflects A. A. Buhlmann's
> algorithm or VVAL-79 algorithm. This dive schedule is experimental, and you
> use it at your own risk.


Inspired by **ZPlan v1.03** (© 1997–98 William M. Smithers) — the classic
Bühlmann ZHL-16 mixed-gas decompression planner — this is a portable C99
engine with a Swift API, building natively for **macOS** and **iOS**
(and Linux/Windows, since the core is plain C).

```
ZPlanKit/
├── Package.swift                  Swift Package Manager manifest
├── Sources/
│   ├── CZPlan/                    the engine: portable C99, zero dependencies
│   │   ├── include/czplan.h       public C API
│   │   └── czplan.c               ZH-L16C, VVAL-79, VPM-B, planner, parser, report
│   ├── ZPlanKit/ZPlanKit.swift    Swift API for macOS / iOS apps
│   ├── ZPlannerUI/                SwiftUI front end (ZPlanner-style form)
│   └── zplan-cli/main.c           command-line front end (like zplan.exe)
└── Samples/                       example profile.dat files + output
```

---

*Personal-use build: the legal disclaimer boilerplate, and as of v1.8.1 the
VVAL-79 approximation notes, have been removed from program output at the
owner's request. These were the only strings the engine wrote to the plan's
`warnings` field, so that field is now always empty. The red warnings area in
the apps remains in place and still displays profile-parse and planning errors.*

---

## How it was built and validated

This is not a port of decompiled code. The engine was written from:

1. the **published Bühlmann ZHL-16 model** (17 compartments including 1b,
   ZH-L16C a/b set, partial-pressure weighted for He/N₂ mixes);
2. **differential testing against the original `Zplan.exe` binary**, run
   under Wine as a black-box oracle across dozens of profiles.

The Bühlmann coefficients were additionally **cross-verified against
Subsurface's `core/deco.cpp`** (github.com/subsurface/subsurface): the
N₂/He half-times, a/b values, the mix-weighted a/b combination, and the
tolerance equation `P_tol = (P_tissue − a)·b` match exactly. Further
background: Erik Baker's *Understanding M-values*, decompression.org, and
the open-source [AbysnerApp](https://github.com/NeoTechni/Abysner-mirror)
planner are good independent references.

### Validation results (this engine vs. original Zplan.exe v1.03)

Reference dive — the bundled `Samples/profile.dat`
(40 m / 20 min on air, fresh water, ZH-L16C, conservatism 20 %, Pyle stops 1:00):

| Quantity            | Original v1.03 | ZPlanKit v1.0.0 |
|---------------------|---------------:|----------------:|
| Deep stop 1         | 24 m — 1:00    | 24 m — 1:00 ✓   |
| Deep stop 2         | 15 m — 1:00    | 15 m — 1:00 ✓   |
| Stop at 6 m         | 2:00           | 4:00 (deeper-weighted) |
| Stop at 3 m         | 15:06          | 12:54           |
| **Total deco**      | **19 min**     | **19 min ✓**    |
| **Run time**        | **49 min**     | **49 min ✓**    |
| CNS                 | 8.0 %          | 8.0 % ✓         |
| OTU                 | 23             | 23 ✓            |
| Time to fly         | 1.5 h          | 1.5 h ✓         |
| Gas consumed        | 2750.5 L       | 2632.2 L (−4.3 %)|

Trimix check (55 m / 20 min on 18/30, salt, deco EAN32/50/O₂, max PO₂ 1.6):

| Quantity        | Original           | ZPlanKit            |
|-----------------|--------------------|---------------------|
| Stops           | 12/9/6/3 m: 2,3,5,9 min | identical ✓    |
| Gas switches    | EAN50 @ 12 m, O₂ @ 6 m | identical ✓     |
| PPO₂ / END      | 1.096/1.596… END 4/2/0 | identical ✓     |
| Total deco / RT | 19 / 46 min        | 19 / 48 min         |
| CNS / OTU       | 25.5 % / 57        | 27.9 % / 59         |
| Gas total       | 3468.8 L           | 3486.4 L (+0.5 %)   |

Also verified: imperial units + ZHL-16C (130 ft dive: stops within 1 min,
gas within 0.7 %), altitude diving (2000 m: equal totals), conservatism
sweep 0–30 % (totals within 1–2 min at every setting, always on the
conservative side), and **repetitive dives**: tissue files written by either
engine load correctly in the other (values agree within ~3 %, ZPlanKit's
slightly higher = slightly more conservative).

### Known, deliberate differences

* **Stop-time distribution in deep-stop mode.** The original "expands" only
  its final stop after inserting deep stops (its 6 m stop stays fixed even
  when tissue loading has grown, briefly exceeding its own ceiling on the
  way up). ZPlanKit instead holds every stop as long as the model requires
  — so some minutes shift from the 3 m stop to deeper stops. Totals match;
  the distribution is deeper, which is the conservative direction.
* **CNS carry-over on repetitive dives.** ZPlanKit implements the 90-minute
  CNS half-time between dives exactly as ZPlan's own Readme (§11)
  documents. The original binary appears not to add the carried CNS into
  the displayed total; ZPlanKit does.
* **Time to fly** is advisory. Small tissue differences swing it by hours
  near the threshold (repetitive test: 9.5 h vs 5.0 h). For any decompression
  dive, DAN's guidance is to wait 24 hours before flying — take that over the
  computed figure.
* **Scamahorn Slide** is parsed but planned at the fixed setpoint (a
  warning is emitted). Closed-circuit constant-setpoint planning works.
* CNS above PO₂ ≈ 1.6 interpolates the table's steep knee slightly more
  conservatively than the original.

### Engine internals (matched to the original by black-box testing)

* Pressures: salt ≈ 10.07 m/atm, fresh ≈ 10.34 m/atm (physical densities);
  displayed PPO₂ = fO₂ × ambient ATM, no alveolar water-vapour subtraction.
* Conservatism %: pads **on-gassing time** at every integration step —
  ZPlan's documented "additional time at each calculation waypoint for
  purposes of determining tissue loading".
* Stop-leaving criterion: **predictive look-ahead** (Readme §10) — a stop
  may be left when the planned ascent at the user's rates, with off-gassing
  credited en route, never crosses the ceiling. Slow shallow ascent rates
  therefore shorten or eliminate stops, as the original advertises.
* Deep stops: iterative Pyle mean-depth insertion with re-runs (the first
  normal stop drifts between iterations, as in the original), plus a legacy
  final-stop minimum of `deep-stop time × (1 + 5·conservatism)` so
  schedules are never shorter than the reference engine's.
* Deco gas auto-selection on stop arrival: highest PO₂ ≤ max (with the
  original's "few inches" rounding grace) and END ≤ max. Deco RMV engages
  on arrival at the first stop.
* O₂ tracking: the published NOAA CNS% and REPEX OTU tables, built in,
  linear interpolation, defined in ATM.

---

## Building

### macOS command line
```bash
cd ZPlanKit
swift build -c release
.build/release/zplan Samples/profile.dat          # plan a dive
.build/release/zplan -s Samples/profile.dat       # …and save tissue.dat
```
(Or with no Swift toolchain: `cc -O2 -ISources/CZPlan/include
Sources/CZPlan/czplan.c Sources/zplan-cli/main.c -lm -o zplan`.)

### macOS / iOS app (Xcode)
Add the package (File ▸ Add Package Dependencies ▸ Add Local…), then:

```swift
import ZPlanKit

let profile = try String(contentsOf: profileURL)
let plan = try ZPlan.plan(profile: profile)          // fresh diver
print(plan.reportText)                               // classic text plan

for line in plan.lines where line.kind == .normStop {
    print(line.depthMeters, line.stopSeconds / 60)
}

// repetitive dive: persist plan.tissueState.tissueFileText, later:
let dive2 = try ZPlan.plan(profile: profile2,
                           tissueFile: savedTissueText)
```

`profile.dat`, `tissue.dat` and the report format are compatible with the
original ZPlan, so existing files keep working.

Note: all validation above was performed on the **C core** (which is what
computes every number). The Swift wrapper is a thin marshalling layer and
was not compiled in the validation environment — build it once in Xcode
and re-run `Samples/profile.dat` to confirm it reproduces `Samples/plan.out`.

### UI styling note
Per the project brand: white background, black text; graphics in blue,
yellow and green; **all warnings and the legal notice in red**. The
`DivePlan.warnings` string and the WARNING block of `reportText` are the
strings that must be shown in red and may not be suppressed.

---

## Gradient factors (new in v1.1.0)

Erik Baker's gradient factors are available as an alternative to ZPlan's
Conservatism mechanism. In `profile.dat`:

```
GradientFactors: 30, 85        # GF-low, GF-high (percent; fractions also accepted)
# or:  UseGF: y  +  GFLow: 30  +  GFHigh: 85
```

When enabled, GF **replaces** Conservatism (the pad is disabled) and the
report header shows e.g. `Buhlmann ZHL-16b + gradient factors 30/85`.
Implementation: the tolerated ambient pressure per compartment becomes
`(P_t − a·GF) / (GF/b − GF + 1)`, with GF sloping linearly from GF-low at
the first (deepest) stop to GF-high at the surface, anchored at the first
ceiling-limited depth — the standard Baker formulation as used by
Subsurface and modern dive computers. Verified: `GF 100/100` reproduces the
pure-Bühlmann (Conservatism 0) schedule exactly; `GF 30/80` produces the
classic staircase of short deep stops growing toward the surface.

## The ZPlanner-style UI (new in v1.1.0)

`Sources/ZPlannerUI/ZPlannerView.swift`, inspired by the original ZPlanner
Delphi form, in SwiftUI for macOS and iOS: the same radio groups
(units / water / ZHL-16 b-c / deep stops), settings group boxes, the
depth-time-mix levels grid, deco-gas list, a green **Calculate** button,
and the plan in a monospaced memo — plus a depth-profile sketch (blue
line, green stop markers, yellow gas switches) and a Gradient-factors /
Conservatism selector. Styling follows the project brand: white
background, black text, blue/yellow/green graphics, red warnings.

To run it, create an App target in Xcode (macOS or iOS), add this package,
and set the root view:

```swift
import SwiftUI
import ZPlannerUI

@main struct ZPlannerApp: App {
    var body: some Scene { WindowGroup { ZPlannerView() } }
}
```

Repetitive dives: switch on "Use tissues from previous plan" — the tissue
state of the last calculated plan is carried into the next one with the
surface interval you enter.

## VVAL-79 model

`Model: vval79` selects the U.S. Navy Thalmann Exponential-Linear model
(EL-DCM): exponential gas uptake, linear elimination at rate k·P_cross for
compartments supersaturated beyond their crossover, Workman-style
MPTT(D) = MPTT₀ + D tolerance limits, and the USN 33 fsw/atm convention.
Any spelling beginning `vval` is accepted, case-insensitively; an
unrecognised model name falls back to Bühlmann and says so.

**Air, nitrox and oxygen only.** The U.S. Navy publishes no helium
parameters for this model, so rather than invent them the planner refuses a
dive carrying helium and explains why. The earlier √(28/4) Graham's-law
helium scaling was removed in v1.32.0: NEDU's own fitted helium data
contradicts it.

Parameters are the published nine-compartment VVAL-79 set (NEDU TR 12-01,
Table 3; see also ADA561928): N₂ half-times 5, 10, 20, 40, 80, 120, 160,
200 and 240 min, MPTT₀ 99.3, 87.7, 78.0, 56.0, 48.5, 45.5, 44.5, 44.0 and
43.5 fsw, slope 1.0 fsw/fsw, SDR 0.70, crossover overpressure 10 fsw.

**Validated:** computed no-decompression limits reproduce *USN Diving
Manual Rev. 7, Table 9-7* within ±1 min at 16 of 17 depths, six of them
exactly. The 12-compartment set this replaced gave 21 min at 120 fsw where
the manual says 15.

Gradient factors do not apply to this model and are ignored with a note;
Conservatism % applies normally. `RmvMetric: y/n` sets RMV units
independently of depth units.

## Version history
* **v1.35.0** (2026-09-13) — **One oxygen clock for both air-break modes.**
  Subsurface mode previously reset the clock at every stop, as Subsurface
  itself does, so on a 70 m trimix dive the first break came after 49 minutes
  of continuous oxygen. Both modes now carry the clock across stop changes and
  exclude travel, per NEDU TR 07-09, so "break after 30" means thirty minutes
  of cumulative oxygen wherever it was breathed. The modes still differ in how
  the break is integrated: Navy freezes inert exchange for its length,
  Subsurface runs it as an ordinary segment on the break gas. Regression over
  62 stored configurations: two changed, both Subsurface mode, both only in
  break position; totals unmoved.
  Config in the apps no longer carries explanatory text. Every setting is
  described in a single guide, reached from Help ▸ Lplanner Manual on macOS
  and the Info button elsewhere; the macOS Help menu previously answered
  "Help isn't available for Lplanner". The Info panel now labels the build
  "AI-assisted" beside the version.
* **v1.34.0** (2026-09-13) — **VVAL-18 becomes VVAL-79, air and nitrox only.**
  The model now carries the published nine-compartment VVAL-79 parameter set
  and reproduces USN Rev. 7 Table 9-7 within ±1 min at 16 of 17 depths. A dive
  carrying helium is refused rather than computed, with an explanation.
  ZHL-16B removed entirely: the Bühlmann model is ZH-L16C only and the
  `UseBValues` key is gone rather than merely ignored. Model names are parsed
  case-insensitively — `Model: VVAL18` used to select Bühlmann silently — and
  an unknown name now produces a notice instead of a silent substitution.
  A stop that cleared in exactly zero time printed "-0 minutes".
* **v1.33.0** (2026-09-13) — XVal-He-9 removed after evaluation. Implementing
  it showed that no published source states how two inert gases share one
  Thalmann compartment, and NEDU answered trimix empirically rather than
  analytically (TR 15-04). The model lineup is settled at three: ZH-L16C with
  gradient factors, VPM-B, and VVAL-79.
* **v1.30.0–v1.32.0** (2026-09-13) — VVAL restored to the published
  nine-compartment set; the fabricated helium MPTT table and per-gas slope,
  which were copies of the nitrogen ones, removed with it.
* **v1.26.0–v1.29.0** (2026-09-11) — **Air breaks and travel gas.** A break is
  planned when oxygen is breathed at the last stop depth or shallower, or when
  CNS reaches the warning threshold, in two modes (Navy dead time, Subsurface
  modelled) with configurable period, length and break gas; the automatic
  choice is the leanest carried mix still breathable at that depth, which keeps
  a hypoxic back gas out of a 3 m break. No break is planned in the last few
  minutes before surfacing, and none on closed circuit, where the plan advises
  lowering the setpoint instead. Travel gas starts a descent on a hypoxic back
  gas with the leanest carried mix breathable at the surface and changes over
  at the first safe stop increment, at no cost in decompression. CNS advisory
  warning at 80% on both circuits.
* **v1.23.0** (2026-09-08) — **The experimental extra-slow ascent rule is
  removed**, along with its `ExtraSlow` toggle, the `extra_slow` config field
  and the engine branch. `ExtraSlow:` is still accepted in a `profile.dat` and
  ignored, so existing files do not raise "unknown key".
  It was removed because it never did anything. Two independent reasons, both
  measured:
  (1) `offgas_gradient_at()` took the depth being ascended to and discarded it
  (`(void)target_m;`), measuring supersaturation at the depth the diver was
  already at. Since tissue tensions do not depend on depth, the two readings
  differ by exactly the ambient-pressure gap, (d_current − d_target) × 0.0993
  bar/m — 0.30 bar on a 3 m or 10 ft grid, or 24 % of the 1.25 bar threshold.
  On the `multideco_230ft` reference dive that is the difference between 0
  holds and 4: peak gradient 1.165 bar as coded, 1.266 bar as documented.
  (2) More fundamentally, an absolute 1.25 bar trigger is incompatible with
  gradient factors, whose entire purpose is to cap supersaturation below the
  M-value. Measured peak gradients: 0.43 bar (30 m/60 min air, GF 30/85),
  0.67 (40 m/20 min air, 30/85), 0.93 (70 m/25 min 18/45, 45/85), 1.01
  (100 m/20 min 10/70, 30/80), 1.17 (230 ft/26 min 18/45, 45/85). The rule
  only ever engaged at GF 100/100 — the raw Bühlmann ceiling. Fixing (1) would
  not have changed that.
  Two further defects went with it: the 5-minute cap was a local in `travel()`,
  which recurses at a gas switch, so a leg crossing a MOD had a fresh budget on
  each side; and the v1.8.2 note claiming the rule "holds depth at the stop"
  was wrong — traced on a 120 m dive it held across the whole leg (36.0 m down
  to 33.2 m), a crawl, while the plan charged all of it to the stop line.
  **No schedule changes.** Every bundled sample — `profile.dat`,
  `multideco_230ft.dat`, `trimix55.dat`, `vval79_100ft.dat` — is byte-identical
  to v1.21.0 output, which is the point: with the recommended settings the rule
  had never altered a plan.
* **v1.22.0** (2026-09-07) — Imperial units fixed across all three front ends
  (macOS/iOS, Android, F-Droid), from a tester report that Imperial "was not
  working well". Two defects, both in the UI layer; the engine's arithmetic was
  never at fault.
  (1) **Switching units converted nothing.** Every Config value was held as a
  raw string and passed through verbatim, and every shipped default was a metric
  number, so selecting Feet reinterpreted them as feet: a 3 m stop grid and 3 m
  last stop became 3 ft, `MaxEND` 40 m became 40 ft, descent 15 m/min became
  15 ft/min, and the ascent bands (70-30, 30-12, 12-0) left a 131 ft dive with
  **no defined ascent rate at all above 70 ft**. The same 40 m / 20 min dive ran
  45 min in Meters and 54 min in Feet, on stops of 51/33/27/18/15 ft. Flipping a
  units control now converts altitude, stop distance, last stop, END, the
  descent and ascent tables, the deco setpoint depths, the RMVs and the entered
  dive levels, rounding to whole units so the round trip is lossless
  (3 m -> 10 ft -> 3 m) and lands on the conventional imperial values.
  (2) **The gas report could not be put into Imperial.** The engine has always
  supported Cu.Ft. — `rmv_metric = -1` means "follow UseMetric" — but both UIs
  emitted an explicit `RmvMetric:` line unconditionally, so that default was
  dead code: Feet gave depths in feet and gas in litres. Worse, flipping the
  RMVs control without fixing the numbers read `Rmv: 19` as 19 cu.ft/min
  (≈538 L/min), reporting 2383 Cu.Ft. for a 40 m/20 min dive. The RMV units now
  follow the depth units until the diver sets them explicitly, and `RmvMetric:`
  is written only when they have.
  Also: the parser is now two-pass, so `UseMetric` and `RmvMetric` are found
  before any scaling is applied — previously a value placed above `UseMetric` in
  a hand-written `profile.dat` was scaled with the imperial default, silently
  (the old ZPlan docs work around this by requiring `UseMetric` to be line one).
  Every Config field now shows its unit, and the extended-stop bands read
  100 ft+ / 23–100 ft in Imperial.
  **Validated:** the metric reference plan is byte-identical to v1.21.0;
  Meters and Feet forms of the same dive now agree (44 vs 43 min run, gas within
  0.5 %, stops on whole feet: 60/40/30/20/10); `UseMetric` first vs last in the
  file now gives identical output. New cross-check `Samples/multideco_230ft.dat`
  (230 ft / 18-45 / GF 45/85, MultiDeco's own settings) reproduces MultiDeco's
  first stop, both gas switches and 8 of its 12 stop times exactly, the other
  four one minute longer each — run time 110 min vs 104, conservative.
  *(The version history below jumps from v1.9.5 to here: releases v1.10–v1.21
  were not recorded in this file.)*
* **v1.9.5** (2026-08-14) — The gas-switch row is marked `GasSw` rather than
  `Gas`. The left column is an event column, so the marker should read as an
  event; five characters keeps it the same width as `DStop` and nothing else in
  the layout moves.

* **v1.9.4** (2026-08-14) — Consumption is reported per gas as bottom + ascent,
  and the total open-circuit line is gone. Mixes are named as they are in the
  plan table (Air, EAN50, O2, TMX 18/45) rather than "of 21.0% consumed".

  Only a back gas is split. It is the one carried down, so its ascent share is
  what must still be in the cylinder when the bottom phase ends — the number
  that matters for planning. A deco gas is breathed on the way up only, so its
  total is the whole story; zero bottom consumption is what identifies it.

* **v1.9.3** (2026-08-14) — Gas switch depths snap down to the stop grid. The
  raw MOD is a number like 22.0 m for EAN50 at 1.6; divers switch on the grid
  and take the 21 m rung rather than argue about the last 0.6 m. Oxygen at 1.6
  works out at 6.0 m and stays there. Snapping downward also guarantees the
  switch is never deeper than the mix permits. `StopDistance` drives the grid,
  so a CCR diver working in 6 m increments — a multiple of 3, chosen because a
  rebreather cannot ascend quickly with the loop and counterlungs expanding —
  gets switches on 6 m rungs without any extra setting.

* **v1.9.2** (2026-08-14) — Gas switches happen at the mix's maximum operating
  depth, mid-water, rather than at whatever stop comes next. `travel()` now
  splits an ascent leg at any MOD it crosses, switches there and continues,
  recursing so several mixes come on line during one ascent. With Pyle stops on,
  EAN50 at 1.6 previously waited until the 18 m deep stop; it now goes on the
  bottle on the way past, which is where the oxygen window opens.

  The MOD test also moved from alveolar to ambient pressure. Alveolar belongs in
  the tissue model; MOD is an ambient convention, and using alveolar permitted a
  switch about 2 m deeper than the stated limit and printed a PO2 above the
  configured maximum. Across 24 gas and limit combinations the displayed PO2 now
  never exceeds the limit set.

* **v1.9.1** (2026-08-14) — Pyle deep stops now use the deco gas when one is
  permitted at that depth. The deep-stop branch travelled, held and continued
  without ever calling `select_deco_source()`, so every deep stop was breathed
  on back gas regardless of depth: with EAN50 at MaxPO2 1.6 (MOD 21.6 m) the
  18 m Pyle stop stayed on air, throwing away the oxygen window on precisely
  the stops where the gradient is largest. The switch now happens on arrival,
  so the hold itself and every stop above it are computed on the new mix — the
  9 m stop fell from 4:31 to 3:13 on the test profile. Stops deeper than the
  mix's MOD correctly stay on back gas, and an extended stop configured for the
  band applies at a deep-stop switch as well.

* **v1.9.0** (2026-08-14) — Extended stops on a deco mix switch. `ExtStopDeep`
  and `ExtStopShallow` (0-10 min each) hold the diver at the depth where the
  planner switches to a deco gas, chosen by band: 30 m or deeper, and 7 m up to
  30 m. Switches shallower than 7 m are not extended. The hold goes through the
  stop's minimum time rather than being added afterwards, so it off-gasses the
  diver and the stops above usually shorten — 5 min at a 21 m switch added only
  2 min to total deco on the test profile. Counted as decompression time.

* **v1.8.3** (2026-08-14) — Deco gases now switch on the way up, not only at
  stops. `select_deco_source()` was called in exactly one place — where a stop
  was required — so a gas whose maximum operating depth was deeper than the
  first stop never got picked up. With EAN50 at MaxPO2 1.6 (MOD 21.6 m) the
  diver stayed on bottom gas past 21 m and only switched at the 9 m stop. The
  check now runs at every stop-grid depth passed during the ascent.
  Switches are recorded as a new `ZP_LINE_GASSWITCH` plan line and shown in the
  report, because a switch at a pass-through depth produced no line at all and
  so was invisible even once it was happening.

* **v1.8.2** (2026-08-14) — Two report fixes found on an iPad CCR plan.
  Closed-circuit rows printed the diluent as well as the setpoint
  ("CC 21/0 SP 1.50"), 15 characters in an 11-column field, which pushed PO2 and
  EAD out of alignment and wrapped the row; they now print "CC SP1.50". The
  diluent does not set inspired PO2 on closed circuit and its inert content is
  already visible in EAD.
  The extra-slow hold is now charged to the stop where it happens rather than to
  the following ascent leg: a 3 m ascent was reading "6:01" because up to five
  minutes of hold at the deeper stop landed in the travel line. Same schedule,
  same total deco — the time is now shown where the diver actually spends it.

* **v1.8.1** (2026-08-14) — Suppressed the two VVAL-18 notes in the plan output
  ("gradient factors do not apply to VVAL-18; ignored" and "VVAL-18 helium
  halftimes are an unvalidated approximation"), at the owner's request. The
  caveats themselves are unchanged and remain documented here and in `czplan.h`.
  These were the engine's only `warnings` strings, so `zp_result.warnings` is now
  always empty; the apps' red warning area still shows parse/planning errors.
  No effect on any schedule.

* **v1.8.0** (2026-08-14) — Fixed the extra-slow ascent rule. The off-gassing
  gradient was measured as tissue tension minus *inspired* inert pressure; on an
  oxygen decompression gas that term is zero, so the quantity collapsed to raw
  tissue tension and stayed permanently above the 1.25 bar threshold (at 6 m the
  ambient pressure alone is 1.6 bar). The rule could never clear and always spent
  its whole delay budget, stretching a 6 m ascent to over 16 minutes. It now
  measures supersaturation (tension minus ambient), which falls as the diver
  off-gasses. The per-segment cap is restored to 5 minutes (it had been raised to
  15 to mask the defect) and the `ZP_ESMODE` experiment hook is removed.
  Hold time is now added to TOTAL DECO TIME — previously only stop time counted,
  so enabling the rule made the reported deco *fall* (8 min to 4 min) while the
  diver stayed under water 11 minutes longer and CNS rose from 16.1% to 29.1%.
  Total deco time is now invariant to the rule. Also: no-stop dives no longer
  print "TOTAL DECO TIME: -0 minutes".
  **All three apps must be rebuilt** — iOS, macOS and Android share this engine.

* **v1.7.0** (2026-08-11) — Calibrated against MultiDeco and TechDeco on a
  shared reference dive (70 m, 23 min, TMX 18/45, EAN50+O2, GF 45/85, salt):
  three corrections, all in the conservative direction.
  (1) Ascent criterion switched to the standard instantaneous ceiling used
  by MultiDeco/Subsurface/TechDeco; ZPlan's predictive rule, which credited
  off-gassing during the ascent itself, made schedules ~5 min short and
  first stops too shallow (kept behind AscentCredit: y).
  (2) Buhlmann now uses the standard 16 compartments, exactly matching
  Subsurface; the optional 1b compartment is opt-in via Compartment1b: y.
  (3) Stop times are rounded up to whole minutes with the inbound ascent leg
  counted into the stop, as both reference planners do (Buhlmann only —
  VVAL-18 keeps the USN convention of counting travel separately).
  Result: run time 86 min vs 85 in both references, with the shallow stops
  (15:4, 12:5, 9:8, 6:10, 3:18) matching MultiDeco exactly. VVAL-18
  crossovers re-fitted after the compartment change: NEDU 132 fsw/20 min
  back to exactly 9 min, Rev.7 40 fsw/240 min exact at 75.
* **v1.6.0** (2026-08-08) — Full coefficient re-verification against fresh
  clones of Subsurface and Abysner: every ZHL-16C N2/He half-time and a/b
  value matches Subsurface digit-for-digit. Root cause of over-conservative
  schedules found and fixed: the Buhlmann path now subtracts 0.0627 bar
  alveolar water vapour (Buhlmann Rq=1.0 convention, as Subsurface and
  MultiDeco do) from inspired gas and initial saturation — the old ZPlan
  binary used zero, inflating inert loading ~6%. Reference dive 45 m/20 min
  GF 45/85 with EAN50+O2: 42 -> 40 min with the classic 12/9/6/3 structure.
  (VVAL-18 is unaffected: it keeps the USN 1.85 fsw convention, and the
  NEDU 132 fsw fit still returns exactly 9 min.) Extra-slow ascent rule
  finalised per owner definition: the off-gassing gradient is tissue
  tension relative to ambient pressure, projected at the depth the ascent
  is heading to; while any compartment exceeds 1.25 bar the ascent leg is
  delayed in place (stops are preserved or absorbed by the crawl), bounded
  at 15 min per leg, never applied leaving the bottom. "Trimix" is now
  "TMX" in all output.
* **v1.5.1** (2026-08-07) — Alternative gradient factors are editable to
  any values: free-text low and high, in Config and now also inline beside
  the altGF checkbox on the main screen. GF parsing no longer folds values
  near 100 oddly — "100, 100" is the pure ZHL-16C ceiling and values above
  100 are accepted (less conservative than the raw model). altGF now
  activates gradient factors on its own, without the main GF toggle.
* **v1.5.0** (2026-08-07) — ZHL-16B removed: the Buhlmann model is now
  ZHL-16C only (UseBValues is accepted and ignored; Model: accepts zhl16c
  or vval18). Conservatism now applies only when gradient factors are off,
  in both engine and UI. Fixed: the Alternative GF pair and the NDL
  GF-High/GF-Low selector were never rendered in v1.4 (nested behind the GF
  toggle) — both are now their own Config groups, making the main-screen
  altGF checkbox usable. Extra-slow ascent rule corrected: it was holding
  ~36 min at the O2 stop because pure O2 makes the inspired inert pressure
  zero, so the gradient never cleared; it now skips the final ascent to the
  surface and adds at most 5 min per stop (trimix sample: 56 -> 69 min,
  time shifted deeper, instead of 91). Plan table gains PO2 and EAD
  columns; a footer line reports the deco zone start (first ceiling-limited
  stop) and the breathing-gas density at the deepest point (g/l, 0 C).
  Levels can now be edited: tap a level to load it into the entry fields,
  Update to commit, plus reorder and delete controls.
* **v1.4.0** (2026-08-07) — Coefficients re-verified against Subsurface
  (ZHL-16C exact match) and Abysner (half-times/b/He exact; their base
  a-table is the ZHL-16A set). VVAL-18 crossover pressures FITTED to U.S.
  Navy references: 132 fsw/20 min reproduces the NEDU manned-trial
  algorithm output exactly (9:00 @ 20 fsw); Rev. 7 Table 9-9 rows at
  40 fsw/170-240 min within 1 min; NDLs unchanged (63/39/26/13 vs
  63/39/25/13). Re-fit hook: ZP_PCROSS env var. Conservatism redefined
  (0-50 %): inert-gas preload weighted fast-to-slow (N2+He split by the
  deepest waypoint's mix), replacing the time-padding mechanism — plans
  with conservatism > 0 differ from v1.3 by design. Gradient factors now
  disable Pyle deep stops (GF-Low takes that role). WKPP option removed
  ('w' in profiles maps to Pyle). Scamahorn Slide implemented (setpoint
  "1.2-1.6" + SlideRate). Deco setpoints ignored on open-circuit dives.
  New plan layout: symbol column (descent/ascent arrows, em-dash stops,
  DStop) with depth/stop/run/gas columns; short inter-stop ascents without
  a gas switch fold into the following stop. Experimental Extra-slow
  ascent rule (holds while any compartment's off-gassing gradient vs
  inspired inert exceeds 1.25 bar). NdlGF setting (GF-High standard /
  GF-Low). UI: Alternative GF pair, +3m/+10ft, +5min and altGF checkboxes
  on the main screen.
* **v1.3.0** (2026-08-07) — Config sheet rebuilt as a fixed-width plain
  layout (fixes unreadable/truncated rendering in macOS sheets) with a
  description under every setting group, adapted from the original
  profile.dat documentation; Calculate moved to the left beside Config and
  Log; Share button (send the plan to Notes, Mail, or any app — includes
  Print on iOS) and a dedicated Print button on macOS.
* **v1.2.0** (2026-08-06) — VVAL-18 model (Thalmann EL-DCM, VVAL-79
  parameter set per NEDU TR 12-01 + owner's parameters.py); USN Rev. 7 NDL
  validation; UI rebuilt monochrome per owner spec: no product names, no
  graphics, Config/Log/Calculate top bar, one Config sheet (Depths,
  RMVs, Water, O2 Narcotic, Model incl. VVAL-18, Conditions, Pyle 1–5,
  rates, CCR deco setpoints/slide), Surface-Interval and Deco-gases rows,
  Open/Closed levels column — single tab on iPhone, side-by-side on Mac;
  independent depth/RMV units; neutral report header.
* **v1.1.0** (2026-07-18) — Baker gradient factors (GF-low/high) as an
  alternative to Conservatism; ZPlanner-style SwiftUI front end
  (`ZPlannerUI`) for macOS/iOS; legal disclaimer removed from program
  output (personal-use build); zero-length stop lines suppressed.
* **v1.0.0** (2026-07-18) — first release. Engine feature-complete for OC
  and constant-setpoint CC; validated against Zplan.exe v1.03 under Wine
  (see tables above); coefficients cross-checked against Subsurface.

## License / provenance

Copyright (C) 2026 Carlos Lander. Free software under the **GNU General Public
License, version 3** — see [LICENSE](LICENSE), and [NOTICE](NOTICE) for the
full attribution and the warranty disclaimer.

Copyleft is the deliberate choice here rather than a permissive licence. A
diver ought to be able to read the model that produced the schedule he is
about to breathe, and GPL keeps that true of anything built from this.

Written from published sources. No code was decompiled or copied from any
original binary. Inspired by ZPlan; the concept and file formats are
William M. Smithers' (1997–98). Bühlmann ZHL-16 per A. A. Bühlmann,
*Tauchmedizin* (1995); CNS table per NOAA; OTU per R. W. Hamilton's REPEX.
VPM-B per D. E. Yount and D. C. Hoffman, in the implementation E. C. Baker
released to the diving community to be distributed freely with credit to the
authors. M-values per R. D. Workman.
