# VVal-79 conformance review — czplan.c v1.10.0

Prompted by a 70 m / 30 min 18/45 dive with EAN50 and O2, whose stops came out
21 m 13:00, 18 m 12:20, 15 m 11:20, 12 m 10:20 — flat, and *decreasing* with
depth, which no decompression schedule should do.

Reviewed against `docs/VVAL-79_Algorithm.md` (Part I, the validated nine
-compartment specification) and USN Rev 7 Tables 9-7 and 9-9.

**Conclusion: what ships as "VVAL-18" is not the VVal-79 specification.** Five
divergences, one of which fully explains the reported behaviour.

---

## 1. The linear off-gassing rate — this is the one that matters

**Spec §6.** Off-gassing becomes linear above the crossover, and the linear rate
is *the slope-continuity value of the exponential curve at the crossover
boundary*, so the two regimes join smoothly. The exponential slope where
`Pt = P_amb + PBOVP` is

    dPt/dt = −k · (P_amb + PBOVP − P_art)

**czplan.c `tick()`** uses

    dPt/dt = −k · PBOVP            /* kn2 * pcross */

which is not the slope of anything. The spec rate varies with **depth** and with
**the gas being breathed**; czplan's varies with neither.

40-minute compartment, 21 m (69 fsw), on EAN50:

| | rate |
|---|---|
| spec, slope-continuous | 1.083 fsw/min |
| czplan, `k · Pcross` | 0.225 fsw/min |
| | **4.8× too slow** |

And across conditions the spec rate moves while czplan's cannot:

| condition | spec | czplan |
|---|---|---|
| 21 m on EAN50 | 1.083 | 0.225 |
| 18 m on EAN50 | 0.996 | 0.225 |
| 21 m on 18/45 | 1.313 | 0.225 |
| 6 m on O2 | 1.092 | 0.225 |

**This is the whole of the reported symptom.** A washout rate that is constant
in depth and gas means every 3 m of ceiling costs the same number of minutes —
hence flat stop times. It also means a gas switch cannot speed up nitrogen
elimination at all: the only thing the switch changes is whether the linear
branch is entered, not how fast it runs. On the 70 m dive the controlling
compartment (40 min) sits at pN2 1.554 bar against an inspired 1.534 bar on
EAN50, drops to exactly inspired, and then stops moving entirely — every
remaining minute of that stop is being paid for by helium alone.

### Why the no-stop limits still looked right

No-stop limits are governed by on-gassing; the crossover is never reached. Both
formulas give **identical** NDLs — verified across 25–190 fsw. The per
-compartment `Pcross` values in czplan.c were fitted against Rev 7, and the fit
succeeded on Table 9-7 while leaving the deco side wrong. `Reference/README.md`
already suspected exactly this: *"accurate where it was anchored, drifting
elsewhere."* It was.

---

## 2. Arterial inert tension

**Spec §2.**  `P_art = F_inert · (P_amb − P_H2O) − P_aCO2`,
with **P_H2O = 0.00 fsw** and **P_aCO2 = 1.50 fsw**.

**czplan.c `inspired()`.** `P_art = F_inert · (P_amb − 1.85 fsw)`, with no CO2
term.

These agree almost exactly on air, because `0.79 × 1.85 = 1.46 ≈ 1.50` — which
is why air validated and nothing looked wrong. They diverge on every other mix,
because czplan scales the correction by the inert fraction and the spec does
not. On EAN50 czplan overstates inspired nitrogen by 0.58 fsw.

## 3. Compartment set

| | compartments | half-times (min) |
|---|---|---|
| Spec Part I (validated) | 9 | 5, 10, 20, 40, 80, 120, 160, 200, 240 |
| Spec Part II (Cochran, `[WORKING]`) | 12 | 2.5, 5, 10, 20, 40, 80, 120, 160, 200, 240, 360, 480 |
| czplan.c | 12 | **1.5, 2.5, 3.5**, 5, 10, 20, 40, 80, 120, 160, 200, 240 |

czplan keeps the nine published compartments and prepends three fast ones with
MPTT0 values of 120, 115 and 108 fsw. Those three compartments and their MPTTs
appear in neither part of the specification. The shipped set matches neither the
validated nine nor the hypothesised twelve.

## 4. SDR is not implemented

**Spec §6.** The desaturation rate constant is scaled by SDR = 0.7 when the
inspired oxygen fraction is at or above 0.80; otherwise SDR is 1.0.

czplan.c has no SDR term. The one place it would bite is precisely where deco
schedules are decided — the O2 stops, where it should be slowing washout by 30%.

## 5. Helium is outside the specification entirely

The VVal-79 specification is a **nitrogen** model. Part I has no helium. Part II
lists an *h-factor* only as one of the Cochran conservatism multipliers, itself
`[WORKING]`.

czplan.c invents a helium treatment: half-times scaled by `sqrt(28/4) = 2.6458`,
sharing the nitrogen MPTT0 and the same crossover pressure, and eliminating at
the full linear rate *independently and simultaneously with nitrogen*. None of
that is specified anywhere, and the double-counting of two gases each at a full
linear rate has no basis in the model.

The 70 m 18/45 dive that prompted this review is therefore entirely outside the
model's defined domain. That is not a caveat about accuracy — there is no
definition to be accurate to.

---

## Naming

The app labels this model "VVAL-18". Its parameters are the VVal-79 published
set plus three invented compartments. VVal-18 and VVal-79 are different
parameter sets: Rev 7 tables — the acceptance reference in this folder — are
VVal-79. Whatever is done with the model, the label should stop claiming a
parameter set the code does not contain.

## What this review does not establish

A corrected model was built to the specification and run against Table 9-7: it
holds the no-stop limits to within a minute from 30 to 90 fsw, then drifts short
at greater depths. On Table 9-9 (150 fsw / 20 min) neither the spec rate nor the
current rate reproduced the published 30 fsw 2:00 / 20 fsw 15:00 schedule in a
first-pass harness. So the divergences above are established, and the direction
of the error is established, but a corrected engine is **not** yet demonstrated
to reproduce the Navy tables. That work has to come before any change ships.

---

# Appendix: cross-check against MultiDeco VPM-B/E +2

Owner-supplied reference schedule, 70 m / 26 min on 18/45 with 50 and 100,
sea level, VPM-B/E +2. Run against the same dive in this engine with matched
rates (descent 23 m/min, ascent 10 m/min, 3 m stops, last stop 3 m).

## Where the time is spent

| | total deco | deeper than 12 m | last two stops (6 m + 3 m) |
|---|---|---|---|
| MultiDeco VPM-B/E +2 | 89 min | 28 min (32%) | 43 min (48%) |
| Lplanner ZHL16-C, GF 30/85 | 86 min | 28 min (32%) | 40 min (46%) |
| **Lplanner VVAL** | **103 min** | **49 min (47%)** | **30 min (29%)** |
| Lplanner ZHL16-C, no GF | 51 min | 10 min (20%) | 28 min (55%) |

**ZHL16-C with GF 30/85 lands on VPM-B/E +2 almost exactly** — 86 min against
89, and the same distribution to within two percentage points. Two unrelated
models agreeing that closely is good evidence the Buhlmann side of this engine
is sound.

**VVAL is the outlier, and not on the total.** 103 min against 89 is only 16%
long. The distribution is the problem: it puts 47% of the decompression deeper
than 12 m where both references put 32%, and only 29% in the last two stops
where both references put ~47%. It is spending time at 21 m that belongs at
6 m and 3 m.

## The gas switch is the tell

Stop time either side of the switch to EAN50 at 21 m:

| | 24 m | 21 m | |
|---|---|---|---|
| MultiDeco VPM-B/E +2 | 5:00 | 3:00 | falls to 0.60x |
| Lplanner ZHL16-C, GF 30/85 | 4:00 | 3:42 | falls to 0.93x |
| **Lplanner VVAL** | **5:18** | **14:00** | **rises 2.64x** |

(ZHL16-C without GF also rises, 1:24 to 2:42, but off a stop so short the ratio
means nothing.)

A richer gas should shorten the stop that follows it. In VVAL the stop nearly
triples. That is the linear-rate defect in one line: when the washout rate is
`k · PBOVP`, it does not depend on the gas being breathed, so switching to
EAN50 cannot accelerate nitrogen elimination at all. The only thing the switch
can do is change *whether* the linear branch is entered.

The same defect explains the distribution. A rate that is also independent of
depth cannot know that shallow stops are more productive than deep ones, so the
schedule has no reason to shift time shallow — which is exactly what the two
reference models do and this one does not.

---

# The fix — engine v1.11.0

## What changed

**1. The linear off-gassing rate is now the spec form.**

    was:  dPt/dt = -k * PBOVP
    now:  dPt/dt = -k * (P_amb + PBOVP - P_art)      clamped at 0

applied to both nitrogen and helium.

**2. PBOVP is 10.0 fsw for every compartment**, per spec section 9. The
per-compartment fitted values (23, 17, 13, 11, 8...) existed only to compensate
for the wrong rate law. With the rate corrected, the published uniform value
reproduces the manned-trial anchor exactly and the fitted set no longer does.

**3. VVAL now warns when helium is in the mix.** See below.

Nothing else was touched. The arterial-tension and compartment-count
divergences (sections 2 and 3 above) were implemented and measured: on air they
change nothing, because the two arterial formulas coincide there. They are left
alone rather than changed without evidence.

## Acceptance

| test | reference | before | after |
|---|---|---|---|
| 132 fsw / 20 min, 20 fsw stop | **9:00** (NEDU manned trial) | 9:00 | **9:00** |
| Table 9-7 no-stop limits, 30–190 fsw | — | mean 0.65 min | mean 1.10 min |
| 150 fsw / 20 min | 30 fsw 2:00, 20 fsw 15:00 | 3:00 / 20:00 | 3:00 / 20:00 |
| ZHL16-C output | must not move | — | **byte-identical** |

The no-stop drift is 2–3 minutes short at 130–190 fsw and every error except
one is on the conservative side. The manned-trial anchor — the only
schedule here validated on actual divers — is held exactly.

## The reported dive, before and after

70 m / 26 min on 18/45 with EAN50 and O2:

| | before | after | MultiDeco VPM-B/E +2 |
|---|---|---|---|
| 24 m | 5:18 | 2:18 | 5:00 |
| 21 m (switch to EAN50) | **14:00** | **1:00** | 3:00 |
| 6 m | 10:00 | 15:00 | 18:00 |
| 3 m | 20:18 | 25:18 | 25:00 |
| total | 101 min | 69 min | 89 min |

The pathology is gone: stop times now increase as the diver gets shallower, and
the stop after the gas switch is shorter than the one before it rather than
2.6x longer. The 3 m stop lands within 20 seconds of VPM-B.

## Still open

**The 20 fsw stop on 150 fsw / 20 min is 20 minutes against Table 9-9's 15, and
the fix did not move it.** That stop sits in the *exponential* regime —
supersaturation at 20 fsw barely reaches PBOVP — so the linear rate law has no
effect on it. This is a second, independent defect and it has not been found.

**Trimix is now 22% shorter than VPM-B/E +2, not 16% longer.** 69 minutes
against 89. The direction of the error has reversed, which is the less
forgiving direction.

## Why VVAL now warns on helium

There is no U.S. Navy EL-DCM helium table to validate against, and this is not a
gap in our research:

- The Surface-Supplied He-O2 table in the Diving Manual Rev 7 Change A is an
  edited version of a **1939** table, not model-derived. NEDU states that its
  longer and deeper schedules "will incur unacceptably high risks of DCS".
- NEDU's replacement work used a *different* model — a probabilistic
  linear-exponential multi-gas (LEM) fit to the N2He_2016 dataset — and
  concluded by recommending the existing **MK 16 MOD 1 He-O2 table** instead.

So the helium treatment in this engine — sqrt(28/4) half-time scaling, sharing
the nitrogen MPTT and crossover — is an extrapolation with nothing to check it
against, and it now produces schedules materially shorter than two independent
references. The model says so on every trimix plan and points at ZHL16-C, which
this folder shows reproduces VPM-B/E +2 to within three minutes.
