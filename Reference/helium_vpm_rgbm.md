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
