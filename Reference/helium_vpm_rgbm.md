# What VPM and RGBM say about helium

Second pass, with Erik Baker's VPM-B FORTRAN implementation (`VPM.EXE`,
`VPM.SET`, `VPM.IN/OUT`), his VPM explanation, the VPM-B vs GAP-RGBM vs GF
comparison, and Wienke's RGBM appendix. Nothing applied.

The uploads contain something the earlier pass did not have: **a worked trimix
schedule from Baker's own program**, which is a real helium reference.

---

## 1. The benchmark run — 80 msw / 30 min on 15/45

`VPM.IN` / `VPM.OUT`: descent 23 msw/min, 26.5 min at 80 msw, ascent 10
msw/min, 3 m stops, EAN36 from 33 m, O2 from 6 m. Same dive through this engine
with matched rates and gas-switch depths:

| | total deco | first stop | deeper than 12 m | last two stops |
|---|---:|---:|---:|---:|
| **VPM-B (Baker FORTRAN)** | 92 min | **54 m** | 35 (38%) | 36 (39%) |
| Lplanner ZHL16-C, GF 30/85 | 116 min | 48 m | 36 (31%) | 53 (45%) |
| Lplanner ZHL16-C, no GF | 76 min | 30 m | 17 (23%) | 38 (50%) |
| **Lplanner VVAL-79** | 120 min | **33 m** | 21 (17%) | 72 (60%) |

**The first-stop depths are the whole story.** VPM-B holds its first stop at
54 m. VVAL-79 does not stop until 33 m — twenty-one metres shallower — and then
puts 60% of the decompression into the last two stops, against VPM-B's 39%.

That is the helium signature, and our model has no mechanism that produces it.

Note this is plain VPM-B with no added conservatism, unlike the MultiDeco
VPM-B/E **+2** reference used earlier. On that earlier 70 m / 18/45 dive
ZHL16-C at GF 30/85 matched to within three minutes; on this deeper, more
helium-rich dive it runs 26% long. The agreement was profile-specific, not
general — worth recording, because it was cited as evidence the Bühlmann side
is sound. It is sound on *that* profile.

## 2. Why the first stop goes deep on helium

Helium's fast kinetics cut both ways. It loads fast on the bottom and it
off-gasses fast on ascent, so a helium-loaded compartment reaches supersaturation
**early in the ascent, while still deep**. A bubble model sees that as a
free-phase risk and inserts a stop. A dissolved-gas model with a fixed ceiling
sees only whether tension exceeds an M-value, and on a mix with 45% helium the
tension falls fast enough that no ceiling is violated until much shallower.

Bühlmann with GF Low 30 partially emulates the bubble behaviour — the low
gradient factor forces a deep first stop, giving 48 m. **VVAL-79 has neither
gradient factors nor a bubble term, so nothing pulls its first stop deep at
all.**

## 3. The one number worth taking from VPM

`VPM.SET`, Baker's shipped defaults:

    Critical_Radius_N2_Microns = 0.6
    Critical_Radius_He_Microns = 0.5

VPM's initial allowable supersaturation gradient goes as

    ΔP ∝ 2γ(γc − γ) / (γc · r)

so it is **inversely proportional to the critical radius**. Helium's smaller
nuclei therefore tolerate a larger gradient:

    ΔP_He / ΔP_N2 = 0.6 / 0.5 = 1.20

**Helium is permitted about 20% more supersaturation than nitrogen.** Subsurface
carries the same asymmetry with slightly different values (0.55 / 0.45, ratio
1.22), so it is not an artefact of one implementation.

Our engine gives helium **exactly the nitrogen MPTT0** — no distinction at all.
A helium MPTT0 scaled by ~1.2 would be the minimal, testable way to introduce
the asymmetry every bubble model agrees on, and it would leave air completely
untouched, which is the property that makes it safe to try. Recorded, not
applied.

## 4. RGBM's helium number points the other way, and both are right

RGBM mass transfer coefficients across a bubble surface (µm²/sec·fsw × 10⁻⁶):
N₂ 56.9, **He 18.4** — helium roughly **3× slower**.

Our `HE_RATIO = sqrt(28/4) = 2.6458` makes helium 2.65× **faster**.

These are not in conflict; they are different phases. Dissolved helium diffuses
faster through tissue (Graham's law). Free-phase helium crosses a bubble
interface slower. Together they describe why helium is the more demanding gas:
it arrives quickly, supersaturates early, and once a bubble has formed it leaves
that bubble slowly.

Our model captures the first half and has no representation of the second.

## 5. Not applicable, and why

- **Critical volume algorithm** (`Crit_Volume_Parameter_Lambda = 7500`) — VPM's
  iterative constraint on integrated bubble volume across the whole profile.
  Replacing an MPTT ceiling with this is not a modification to EL-DCM.
- **Crushing pressure and regeneration** (`Regeneration_Time_Constant = 20160`
  min = 14 days) — requires a nuclei population we do not model.
- **Boyle multipliers, excitation radii** — same reason.
- **`divecomp.c`** (Waller, 1992) is a configurable Haldanean simulator, air
  only, no helium. One idea in it is relevant though: it carries **separate
  on-gassing, off-gassing and surface half-times** per compartment. That is the
  same asymmetric-kinetics family as VVal-79's SDR, arrived at independently,
  and it is a reminder that our SDR currently applies one ratio to both gases.

## Summary

| finding | status |
|---|---|
| VPM-B trimix schedule, 80 msw / 15/45 | **new acceptance reference** — 92 min, first stop 54 m |
| Helium tolerates ~20% more supersaturation (r 0.5 vs 0.6 µm) | **the one portable idea.** Minimal test: helium MPTT0 × 1.2. Leaves air unchanged |
| Helium ~3× slower across a bubble interface | Explains the deep-stop behaviour. Not portable to a dissolved-gas ceiling |
| VVAL-79 first stop 21 m shallower than VPM-B on trimix | Quantifies what the current trimix warning is warning about |
| ZHL16-C GF 30/85 matched VPM-B/E +2 on one profile, not this one | **Correction to an earlier claim.** The agreement was profile-specific |
| Critical volume, crushing pressure, regeneration | Different model |

---

# The oxygen window question, answered — engine 1.13.0

The claim to test: *"oxygen windows vary with the mix; for example with 50% at
21 m, not just 6 m. Any time O2 is near or above 1.6."* Physically correct. The
question was whether our `PVSAT` should therefore respond to inspired PO2.

**It should, but not where it looked like it should, and not at those depths.**

`PVSAT = PAMB − (PvO2 + PvCO2 + PH2O)` is the pressure left over for inert gas
in venous blood. What enters it is **venous** PO2, not the window.

Venous PO2 is buffered by haemoglobin. Metabolic extraction is about 5 mL O2/dL
of blood; dissolved oxygen has a solubility of 0.003 mL/dL/mmHg, so dissolved
oxygen alone can only carry metabolism once arterial PO2 exceeds

    5.0 / 0.003 = 1667 mmHg = 72.4 fsw = 2.19 ata

Below that, haemoglobin unloads and venous PO2 stays at its resting value:

| breathing | PaO2 (ata) | PvO2 (mmHg) | oxygen window (mmHg) |
|---|---:|---:|---:|
| air, surface | 0.21 | 46 | 114 |
| air, 40 m | 1.05 | 46 | 752 |
| **EAN50 @ 21 m** | **1.54** | **46** | **1124** |
| **O2 @ 6 m** | **1.60** | **46** | **1170** |
| CCR SP1.30 | 1.30 | 46 | 942 |
| O2 @ 18 m, chamber | 2.80 | 461 | 1667 |

**The window does open enormously — 114 mmHg on air at the surface against 1170
on oxygen at 6 m — and that benefit is already in the model.** It arrives
through the arterial term, `PA_inert = PAMB − PAO2 − PACO2 − PH2O`, which falls
as PAO2 rises and reaches zero on pure oxygen. On EAN50 at 21 m the arterial
nitrogen is already computed from FO2 0.50. Nothing was missing there.

What was missing is the regime above 2.2 ata, where venous PO2 genuinely does
track arterial. Implemented:

    PvO2 = max( PvO2_resting, PAO2 − 72.4 fsw )

Below 2.2 ata this returns the resting value exactly, so **no recreational or
technical schedule moves** — verified: 132/20 holds 9 min, 150/20 holds 3+19,
CCR 45 m/25 min holds 17 min, the 70 m trimix holds 84 min. It changes the model
only at chamber and treatment depths, where it is now right instead of assuming
a resting venous PO2 that no longer applies.

So the intuition was sound and the physics is now complete, but the numbers say
the effect at 1.6 ata is nil — and that is the useful result, because it rules
out a whole line of enquiry rather than leaving it open.

---

# Cochran's helium architecture, from the owner's InDepth article

Confirmed in the author's own words:

> *"There were several versions of the Cochran algorithm: 14, 16, and 20
> compartment models. In the case of the 20 compartment version, the algorithm
> included **fast compartments to compensate for helium gas** and added a
> **compensation for microbubbles related to ascent rate velocity**. The model
> also used the **same linear off-gassing from the Thalmann algorithm**."*

and on the Gemini, *"compartments between five and 480 minutes"* with *"added
variables for ascent rate and breathing parameters."*

That is a complete architectural specification for the trimix variant:

1. EL-DCM linear off-gassing — **we have this**, corrected in 1.12.0.
2. Fast compartments carrying helium — we have three invented fast compartments
   that never control anything. They would need helium-appropriate MPTT0 values
   to do the job described.
3. Ascent-velocity microbubble compensation — **we have nothing like this.**
   This is the term that produces a deep first stop, and its absence is why
   VVAL-79 does not stop until 33 m where VPM-B stops at 54 m.
4. Compartment range 5 to 480 min — our slowest is 240.

---

# Fast compartments for helium — built, fitted, and it does not work

Engine 1.14.0. The mechanism from the owner's InDepth article — *"fast
compartments to compensate for helium gas"* — implemented as a **separate
helium MPTT0 per compartment**, with the ceiling mix-weighted the way Buhlmann
weights a and b:

    MPTT0_eff(i) = ( pn2·MPTT0_N2(i) + phe·MPTT0_He(i) ) / (pn2 + phe)

With no helium in a compartment this is exactly the nitrogen value, so **air is
untouched by construction** — confirmed at every setting tested below.

## Attempt 1: lower the ceiling on the three fast compartments

No effect whatsoever. First stop stayed at 33 m from 120 fsw down to 41 fsw.

The reason is in the half-times. Our fast compartments are 1.5, 2.5 and 3.5 min
on nitrogen, which under `sqrt(28/4)` scaling are **0.57, 0.94 and 1.32 min on
helium**. They equilibrate within about three minutes, so during a 10 m/min
ascent they track ambient almost exactly and never accumulate enough
supersaturation to control anything. A sub-minute compartment cannot hold a
diver deep because it has nothing left to hold.

Cochran's Gemini is described as carrying compartments *"between five and 480
minutes"*. The relevant "fast" compartments are the five-minute ones, not
sub-minute ones.

## Attempt 2: scale the helium ceiling across all compartments

| He MPTT0 scale | first stop | total deco | deeper than 12 m | air anchors |
|---:|---:|---:|---:|---|
| **VPM-B target** | **54 m** | **92 min** | **38%** | — |
| 1.00 (neutral) | 33 m | 117 | 17% | 9.0 / 3+19 |
| 0.90 | 36 m | 133 | 19% | 9.0 / 3+19 |
| 0.80 | 39 m | 149 | 21% | 9.0 / 3+19 |
| 0.70 | 42 m | 170 | 22% | 9.0 / 3+19 |
| 0.60 | 42 m | 187 | 23% | 9.0 / 3+19 |
| 0.55 | 45 m | 197 | 24% | 9.0 / 3+19 |
| 0.50 | 45 m | 208 | 24% | 9.0 / 3+19 |

The air anchors never move — the mix-weighting works exactly as intended.

**But the trade is ruinous.** Buying twelve metres of first stop, 33 to 45,
costs ninety-one minutes: 117 to 208. VPM-B reaches 54 m in **92 minutes
total**, less than the neutral setting. No value of the scale gets anywhere
near it.

## Why, and what it means

In a fixed-ceiling model, lowering the ceiling lengthens **every** stop, not
just the deep ones. The ceiling that forces a stop at 45 m is still in force at
6 m and 3 m, where it is enormously expensive.

VPM's deep stops are cheap because its constraint **evolves during the ascent**:
the allowable gradient starts small, and grows as bubbles are compressed and the
critical volume is spent. Gradient factors emulate this crudely by interpolating
GF Low to GF High with depth — which is exactly why ZHL16-C at GF 30/85 reaches
48 m in 116 minutes where VVAL needs 208 to reach 45.

**So a helium-specific M-value is the wrong shape of fix.** It is not that the
parameter is unfitted; the mechanism cannot produce the behaviour. The article
says as much in passing — the Cochran model *"included more than the fast
compartment and ascent velocity to compensate for the aforementioned microbubble
formations."* More than. There is a bubble term.

What VVAL needs to behave on helium is a **depth-varying ceiling**: either a
gradient-factor-style interpolation applied to MPTT, or a genuine bubble term.
That is the next experiment, and it is a bigger change than a parameter.

## What shipped

The mix-weighted MPTT0 machinery, set **neutral** (helium MPTT0 = nitrogen
MPTT0), so no schedule changes. `ZP_MPTT_HE="v0,...,v11"` overrides it for
further fitting. The infrastructure is right even though this parameterisation
is not, and any future helium work needs exactly this weighting.
