# ZPlanKit v1.9.2

> **WARNING**
>
> This generated dive schedule could indirectly kill you and probably has bugs.
> The author does not warrant that it accurately reflects A. A. Buhlmann's
> algorithm or VVAL-18 algorithm. This dive schedule is experimental, and you
> use it at your own risk.


A clean-room reimplementation of **ZPlan v1.03** (© 1997–98 William M. Smithers) —
the classic Bühlmann ZHL-16 mixed-gas decompression planner — as a portable
C99 engine with a Swift API, building natively for **macOS** and **iOS**
(and Linux/Windows, since the core is plain C).

```
ZPlanKit/
├── Package.swift                  Swift Package Manager manifest
├── Sources/
│   ├── CZPlan/                    the engine: portable C99, zero dependencies
│   │   ├── include/czplan.h       public C API
│   │   └── czplan.c               ZHL-16 B/C model, planner, parser, report
│   ├── ZPlanKit/ZPlanKit.swift    Swift API for macOS / iOS apps
│   ├── ZPlannerUI/                SwiftUI front end (ZPlanner-style form)
│   └── zplan-cli/main.c           command-line front end (like zplan.exe)
└── Samples/                       example profile.dat files + output
```

---

*Personal-use build: the legal disclaimer boilerplate, and as of v1.8.1 the
VVAL-18 approximation notes, have been removed from program output at the
owner's request. These were the only strings the engine wrote to the plan's
`warnings` field, so that field is now always empty. The red warnings area in
the apps remains in place and still displays profile-parse and planning errors.*

---

## How it was built and validated

This is not a port of decompiled code. The engine was written from:

1. the **published Bühlmann ZHL-16 model** (17 compartments including 1b,
   ZHL-16B and ZHL-16C a/b sets, partial-pressure weighted for He/N₂ mixes);
2. ZPlan's own documentation (`Readme.txt`, the annotated `profile.dat`);
3. the original **`O2.cfg`** NOAA CNS% / REPEX OTU table, embedded verbatim;
4. **differential testing against the original `Zplan.exe` binary**, run
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
(40 m / 20 min on air, fresh water, ZHL-16B, conservatism 20 %, Pyle stops 1:00):

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
  near the threshold (repetitive test: 9.5 h vs 5.0 h). The original's own
  Readme (§13) recommends simply waiting 24 h — so does this one.
* **WKPP deep-stop mode ('w')** is approximated (Pyle mean-depth placement,
  1:00 stops). The true WKPP algorithm was never published in full.
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
* O₂ tracking: the original `O2.cfg` (NOAA CNS%, REPEX OTU) embedded,
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

`Sources/ZPlannerUI/ZPlannerView.swift` recreates the original ZPlanner
Delphi form in SwiftUI for macOS and iOS: the same radio groups
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

## VVAL-18 model (new in v1.2.0)

`Model: vval18` selects a U.S. Navy Thalmann Exponential-Linear model
(EL-DCM): exponential gas uptake, linear elimination at rate k·P_cross for
compartments supersaturated beyond their crossover, Workman-style
MPTT(D) = MPTT₀ + D tolerance limits, 33 fsw/atm and 1.85 fsw alveolar
water vapour per USN convention. Parameters follow the owner's
`parameters.py`: the nine NEDU VVAL-79 compartments (NEDU TR 12-01,
Table 3) extended with three fast compartments; displayed name "VVAL-18"
per owner request. **Validated:** computed no-decompression limits match
the published USN Rev. 7 air table at 60/80/130 fsw exactly (63/39/13 min)
and 100 fsw within one minute (26 vs 25). **Caveats, from the parameter
file itself:** the three fast-compartment MPTTs and all crossover
pressures are [WORKING] values pending fitting against real dive logs, so
deco-stop *lengths* (unlike NDLs) should not be expected to match Navy
tables; helium half-times are an unvalidated √(28/4) scaling (a warning is
emitted); gradient factors do not apply and are ignored with a note.
Conservatism % applies normally. `RmvMetric: y/n` now sets RMV units
independently of depth units.

## Version history
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
Clean-room reimplementation; no code was decompiled or copied from the
original binaries. Original ZPlan concept and file formats by
William M. Smithers (1997–98). Bühlmann ZHL-16 per A. A. Bühlmann,
*Tauchmedizin* (1995); CNS table per NOAA; OTU per R. W. Hamilton's REPEX.
