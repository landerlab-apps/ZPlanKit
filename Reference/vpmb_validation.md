# VPM-B — port and validation against Baker's VPM.OUT

Standalone implementation: `ZPlanKit/Reference/vpmb_ref.c`. Written from Erik C.
Baker's VPM-B FORTRAN (released to the community, "DISTRIBUTE FREELY — CREDIT
THE AUTHORS"), cross-read against Subsurface's `core/deco.c`. Subroutine names
are preserved so the two can be read side by side.

Nothing has been merged into `czplan.c` yet. This file records what was
validated and the one question that is still open.

## The acceptance target

`VPM.IN` / `VPM.SET` / `VPM.OUT` from your own files — Baker's own program, run
07-01-2014:

    TRIMIX DIVE TO 80 MSW
    15/45 bottom gas, EAN36 from 33 m, O2 from 6 m
    descent 23 msw/min, 26.5 min at 80 m, ascent 10 msw/min, 3 m stops
    Critical_Radius_N2 0.6 um, Critical_Radius_He 0.5 um, Lambda 7500 fsw-min

Result: first stop 54 m, eighteen stops, 92 min of stops, run time 125 min.

## What matches

All eighteen stop depths, all eighteen stop times, and all eighteen run times,
exactly:

    54:1  51:1  48:1  45:2  42:2  39:2  36:3  33:2  30:1
    27:2  24:3  21:4  18:4  15:7  12:8   9:13  6:13  3:23

Also matching: "deepest possible decompression stop is 63.0 mswg". The start of
the decompression zone comes out 64.73 mswg against Baker's printed 64.8 — a
0.07 m difference that does not move any stop.

Implemented in full: the permeable and impermeable crushing-pressure branches,
`ONSET_OF_IMPERMEABILITY`, nuclear regeneration with the adjusted crushing
pressure, initial allowable gradients, the deco-zone bisection, Boyle's law
compensation, the critical volume algorithm, and the surface phase volume time
including its three-branch helium case.

## Two things that had to be got right

**The ascent ceiling rounds to a DEEPER stop, never a shallower one.** Baker
writes `DBLE(INT(x + 0.5))`, which reads like nearest-integer rounding, and I
briefly implemented it that way. It is wrong: a 54.62 m ceiling rounded to 54 m
is a stop shallower than the ceiling, i.e. a violation. The construct is a
ceiling, and the first stop lands at 57 m on the first pass. Getting this wrong
made the first trial schedule five minutes long and threw the whole convergence
trajectory off.

**Both cubic solvers use `A·r³ − B·r² − C = 0`.** In Boyle's law compensation
Baker passes `B = −2γ`, which under that form becomes `+2γ` — and only then does
the equation reduce to the physical Boyle relation
`(P_next + 2γ/r_new)·r_new³ = (P_first + 2γ/r_first)·r_first³`. Reading the sign
the other way gives a solver that runs and returns plausible numbers that are
quietly wrong.

## The open question: how many passes of the Critical Volume Algorithm

This is the one thing not settled, and it is worth a decision rather than a
default.

The critical volume loop relaxes the allowable gradients, recomputes the
schedule, and repeats. Each relaxation is computed from the *initial* gradients
(not compounded), so it is a fixed-point iteration on deco time:

| pass | deco phase volume time | total stop time |
|---|---:|---:|
| 1 — initial gradients | 100.5 min | — |
| 2 — one relaxation | 93.5 min | **92 min — matches VPM.OUT exactly** |
| 3 | 91.5 min | 90 min |
| 4 onward | 91.5 min | 90 min (fixed point, stable) |

Baker's documented rule is "two or more passes... until the difference is less
than or equal to one minute in any one of the compartments." Implemented
literally, that rule fires on pass 3 and yields **90 minutes**. Baker's own
binary produced **92 minutes**, which is exactly one relaxation.

I could not reconcile these. Between passes 1 and 2 the smallest change across
all sixteen compartments is 5.0 min, so the stated one-minute test cannot fire
there under any reading I could construct. `VPM.EXE` is dated 2003; the FORTRAN
listing in circulation is later. A version difference is the most likely
explanation, and it is not something this end can resolve.

The practical difference is 2 minutes on a 92-minute obligation — ours would be
the *shorter* of the two, which is the wrong direction to drift by accident.

**Decision: follow the documented rule and iterate to convergence.** The engine
runs the loop to its fixed point. This is recorded here so that anyone
comparing an Lplanner VPM-B schedule against V-Planner or MultiDeco knows
where the couple of minutes went, and that it is deliberate.

---

# Engine integration — czplan 1.18.0

VPM-B is now the third model, `Algorithm: vpm` (`use_vval == 2`), alongside
ZHL-16C and VVAL-18. It inherits the whole existing config surface: stop grid,
last stop depth, Pyle stops, extra-slow ascent, extended stops on a switch,
deco gas selection by Max PO2 and Max END, CCR setpoints, CNS/OTU, and
repetitive tissue loading.

New settings: `VpmConservatism` (0-4, default 0 = Baker nominal),
`VpmRadiusN2` (0.6 um), `VpmRadiusHe` (0.5 um).

**ZHL-16C and VVAL-18 output is byte-identical to the previous version.**
Verified by building the engine at git HEAD and diffing both models' schedules
on the same dive. VPM-B is purely additive.

Where it differs from the standalone, and why:

| | first stop | total |
|---|---:|---:|
| Baker VPM.OUT | 54 m | 92 min |
| standalone `vpmb_ref.c` | 54 m | 90 min |
| engine, fresh water, matched run time | 51 m | 89 min |

## Whole-minute stops

VPM-B stops and run times are both whole minutes, as in VPM.OUT. The engine
already held in whole-minute increments; the fractions came from a MultiDeco
convention applied afterwards, which folds the ascent leg into the stop and
rounds the *total* up — turning a 2-minute hold reached after a 0.3 min ascent
into a reported 2.7.

Baker does it the other way round: the run time is rounded up to the next whole
increment **on arrival, before the ceiling is first tested**, so travel is
absorbed before the hold begins, and the reported stop time is the difference
between consecutive whole run times. Both columns are then integral by
construction, and the schedule is not lengthened to achieve it.

That ordering matters. Testing the ceiling first and rounding afterwards costs
an extra minute at every stop that the round-up alone would have satisfied —
which is most of the deep ones.

Applied to VPM-B only. It also moved the engine *closer* to the reference,
from 86 to 89 minutes against the standalone's 90, with the middle of the
schedule now matching Baker stop for stop:

    Baker   ... 42:2 39:2 36:3 33:2 30:1 27:2 24:3 21:4 18:4 ...
    engine  ... 42:2 39:2 36:3 33:2 30:1 27:2 24:3 21:4 18:4 ...

The engine is not expected to reproduce the standalone exactly, and should not
be made to: it is running the same model inside a different dive model. Salt
water at 0.1005 bar/m against Baker's msw at 0.101325 makes an 80 m dive
genuinely shallower; deco gas is chosen by Max PO2 rather than at fixed
depths; and waypoint time is time *at depth* rather than run time.

**One difference is structural and is not a units artefact.** The engine puts
the first stop at 51 m where Baker puts it at 54 m. Our ascent loop climbs
until the ceiling stops it, which lands on the shallowest grid point still
tolerated. Baker rounds the ascent ceiling *up* to the next deeper grid point.
His is the stricter reading — a 54.6 m ceiling is not honoured by a 54 m stop —
and it also anchors Boyle compensation one stop deeper, which propagates
through the whole schedule. Left as it stands for now and logged, because
changing the first-stop rule touches every model, not just VPM.
