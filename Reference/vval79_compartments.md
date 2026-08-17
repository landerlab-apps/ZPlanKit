# VVal-79 tissue compartments — as shipped in czplan 1.12.0

## The table

Half-times in minutes, MPTT0 (surfacing maximum permissible tissue tension) and
PBOVP (crossover overpressure) in fsw. Rate constant `k = ln2 / t½` per minute.

| # | τ N₂ | τ He | k N₂ | k He | MPTT0 | PBOVP | source |
|---:|---:|---:|---:|---:|---:|---:|---|
| 0 | 1.5 | 0.57 | 0.46210 | 1.22260 | 120.0 | 14.0 | **this engine — invented** |
| 1 | 2.5 | 0.94 | 0.27726 | 0.73356 | 115.0 | 14.0 | **this engine — invented** |
| 2 | 3.5 | 1.32 | 0.19804 | 0.52397 | 108.0 | 14.0 | **this engine — invented** |
| 3 | 5 | 1.89 | 0.13863 | 0.36678 | 99.3 | 14.0 | NEDU published |
| 4 | 10 | 3.78 | 0.06931 | 0.18339 | 87.7 | 14.0 | NEDU published |
| 5 | 20 | 7.56 | 0.03466 | 0.09169 | 78.0 | 14.0 | NEDU published |
| 6 | 40 | 15.12 | 0.01733 | 0.04585 | 56.0 | 14.0 | NEDU published |
| 7 | 80 | 30.24 | 0.00866 | 0.02292 | 48.5 | 14.0 | NEDU published |
| 8 | 120 | 45.36 | 0.00578 | 0.01528 | 45.5 | 14.0 | NEDU published |
| 9 | 160 | 60.47 | 0.00433 | 0.01146 | 44.5 | 14.0 | NEDU published |
| 10 | 200 | 75.59 | 0.00347 | 0.00917 | 44.0 | 14.0 | NEDU published |
| 11 | 240 | 90.71 | 0.00289 | 0.00764 | 43.5 | 14.0 | NEDU published |

**Compartments 3–11 are the published VVal-79 nine.** Compartments 0–2 are this
engine's own addition and appear in no NEDU document. Both Cochran Navy
computers — NAVY-007M (VVAL-18) and AIR III-79 (VVAL-79) — state **nine tissue
compartments** in their operator manuals. Measured: removing 0–2 changes nothing
on either air acceptance test, because they never control.

Helium half-times are derived, not published:

    τ_He = τ_N2 / sqrt(28/4) = τ_N2 / 2.6458

**PBOVP = 14 fsw is a fit, not a published value.** NEDU publishes 10.0 fsw
uniform. With the venous reference in place, 10 misses the manned-trial anchor
by two minutes and 14 reproduces it exactly. Swept 10–44 to establish that.

## What each compartment is actually doing

From instrumenting a 150 fsw / 20 min air dive:

| phase | controls | why |
|---|---|---|
| first stop, 30 fsw | **#3** (5 min) then **#4** (10 min) | fast compartments still near bottom loading |
| last stop, first ~8 min | **#4** (10 min) | falling 104 → 87.7 fsw, in the **linear** regime |
| last stop, last ~9 min | **#6** (40 min) | falling 58.5 → 56.0 fsw, exponential |
| surfacing after long shallow dives | **#11** (240 min) | MPTT0 43.5 is the lowest ceiling |

Compartments 7–10 rarely control anything on recreational or technical profiles.
Compartment 6 (40 min, MPTT0 56.0) is the one that sets the last stop, and it is
where the unexplained 150/20 residual lives.

## The MPTT0 curve

    120  115  108 | 99.3  87.7  78.0 | 56.0  48.5  45.5  44.5  44.0  43.5
    <-- invented -->                    ^^^^ the cliff

The published set drops steeply from 78.0 (20 min) to 56.0 (40 min) — a 22 fsw
step, by far the largest in the table — then flattens to within 5 fsw across the
whole slow end. That cliff is why compartment 6 dominates the shallow stops on
almost every deco dive.

---

# Cross-check against RGBM — what is worth learning

From the RGBM appendix (Wienke, *Reduced Gradient Bubble Model in Depth*). RGBM
is a dissolved-gas-plus-free-phase model with a phase volume constraint across
the whole profile; it shares no architecture with an exponential-linear
compartmental model. Three things are still worth taking away.

## 1. The oxygen window is the same idea as PVSAT — and this is the useful one

RGBM defines an **inherent unsaturation**, υ, the oxygen window, as a function
of ambient pressure and inspired FO₂, and states that this window *"is assumed
to take up inert gas under compression-decompression."*

That is structurally the same construct as the VVal-79 venous reference we just
implemented:

    RGBM      inherent unsaturation υ(P, fO2)
    VVal-79   PVSAT = PAMB - (PvO2 + PvCO2 + PH2O) = PAMB - 4.30 fsw

Two model families with nothing else in common both insert a metabolic offset of
a few fsw between ambient pressure and the pressure against which dissolved
inert gas is judged. That is corroboration for the change made in 1.12.0 —
independent of the Navy documentation that prompted it.

**Difference worth noting:** RGBM's window varies with inspired FO₂, so it opens
wider on a rich mix. VVal-79's is a fixed 4.30 fsw regardless of gas. On O₂ at
6 m the true window is far larger than 4.30 fsw. Whether an FO₂-dependent PVSAT
would improve the model is a question we can now ask precisely, and it is
testable against the same two air anchors — air would be unaffected, which is
exactly the property that makes it safe to try.

## 2. Helium mass transport — a direct contradiction worth understanding

RGBM Table 2, mass transfer coefficients across a 50/50 lipid-aqueous bubble
surface (µm²/sec·fsw × 10⁻⁶):

| gas | D·S |
|---|---:|
| H₂ | 72.5 |
| **N₂** | **56.9** |
| O₂ | 41.3 |
| Ar | 40.7 |
| **He** | **18.4** |
| Ne | 10.1 |

Wienke's own comment: *"helium has a low mass transport coefficient, some 3
times smaller than nitrogen."* The ratio is 56.9 / 18.4 = **3.09**.

Our engine does the opposite. `HE_RATIO = sqrt(28/4) = 2.6458` makes helium
half-times **2.65× shorter** than nitrogen — helium is treated as the faster gas.

These are not the same quantity. Graham's law scaling describes diffusion of
dissolved gas through tissue; RGBM's D·S describes transport across a bubble
interface. Dissolved-phase helium genuinely is faster; free-phase helium
genuinely is slower. But it is a caution worth recording: **the assumption that
helium is uniformly the fast gas is only true in the dissolved phase**, and our
helium treatment rests entirely on that one assumption with nothing validating
it.

Nothing here tells us what τ_He should be for an EL model. It does say that
borrowing a bubble-model number would be a category error, and it explains why
bubble models produce deeper stops on trimix than dissolved-gas models do.

## 3. Not portable

- **Boyle multipliers** (ξ = 0.610 at 30 fsw rising to 1.203 at 510 fsw) —
  bubble volume scaling under pressure change. Meaningless without a bubble
  radius to scale.
- **Excitation radii** — separate ε for N₂ and He, pressure-dependent, 0.174 µm
  at 13 fsw down to 0.009 µm at 583 fsw. Requires a seed distribution we do not
  have and would not know how to fit.
- **Phase volume constraint** — RGBM's central mechanism, limiting integrated
  separated phase across the profile. Replacing an MPTT ceiling with a phase
  constraint is not a modification to EL-DCM, it is a different model.

## Summary

| RGBM element | applicable? |
|---|---|
| Oxygen window as inert-gas sink | **Yes, already effectively adopted** as PVSAT. An FO₂-dependent version is a defined, testable next question. |
| Helium slower than N₂ in free phase | Not directly. A useful warning about our sqrt(28/4) assumption. |
| Boyle multipliers | No |
| Excitation radii | No |
| Phase volume constraint | No — different model |

Nothing applied. Recorded for the record.
