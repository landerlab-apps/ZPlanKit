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
