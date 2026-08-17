# Fixing VVAL for helium coherently — the design

Engine 1.15.0. Shipped neutral; nothing changes until the two keys are set.

## Why the obvious fix failed

Lowering the helium MPTT0 does push the first stop deeper, but ruinously: 33 m
to 45 m cost 117 to 208 minutes. In a fixed-ceiling model **a lower ceiling
lengthens every stop**, and the ceiling that buys a stop at 45 m is still in
force at 3 m where it is enormously expensive.

## The parameter the model already has

Spec section 7 defines the maximum permissible tension as a projection from its
surfacing value:

    MPTT(D) = MPTT0 + a·D

and **VVal-79 sets a = 1.0 for every compartment**. Work out what that means for
the tolerated supersaturation gradient:

    tolerated tension = MPTT0 + a·D
    ambient           = 33 + D
    gradient          = (MPTT0 − 33) + (a − 1)·D

| a | gradient at 3 m | gradient at 54 m | |
|---:|---:|---:|---|
| **1.0** | 23.0 | **23.0** | VVal-79 today |
| 0.8 | 21.0 | −12.4 | |
| 0.6 | 19.0 | −47.8 | |
| 1.4 | 27.0 | 93.8 | Bühlmann-like |

**At a = 1.0 the model tolerates exactly the same supersaturation at 54 m as at
3 m.** That is the whole reason it has no deep stops: nothing about being deep
makes it stricter. It is not a missing bubble term — it is a slope of one.

And the crucial property: at D = 0 the `(a − 1)·D` term vanishes, so **changing
the slope does not touch the surfacing ceiling**. Deep stops that cost nothing
shallow — precisely what lowering MPTT0 could not deliver.

## Two knobs, and they separate cleanly

Slope alone still runs long, because deep stops on bottom gas keep loading the
slow compartments. It needs a partner that relaxes the shallow end — which is
exactly the structure of a gradient-factor pair, expressed in this model's own
vocabulary:

| | role | GF analogue |
|---|---|---|
| `a_He < 1` | strict at depth → sets the first stop | GF Low |
| `MPTT0_He × k` | relaxed at the surface → sets the total | GF High |

And the multiplier is not a free invention. VPM's own critical radii — 0.6 µm
for nitrogen, 0.5 for helium, gradient going as 1/r — say helium tolerates about
**1.2× more supersaturation than nitrogen**. Subsurface carries the same
asymmetry at 0.55/0.45. The second knob was already implied by the bubble
models.

## Measured, against Baker's VPM-B on 80 msw / 30 min, 15/45

| a_He | MPTT0_He × | first stop | total | >12 m | 6+3 m | air anchors |
|---:|---:|---:|---:|---:|---:|---|
| **VPM-B** | — | **54 m** | **92** | **38%** | **39%** | — |
| 1.00 | 1.0 | 33 m | 120 | 17% | 60% | 9.0 / 3.0 |
| 0.70 | 1.0 | 48 m | 173 | 29% | 49% | 9.0 / 3.0 |
| 0.70 | 1.2 | 42 m | 119 | 27% | 52% | 9.0 / 3.0 |
| 0.60 | 1.2 | 48 m | 147 | 32% | 47% | 9.0 / 3.0 |
| **0.60** | **1.4** | **39 m** | **84** | **31%** | **48%** | 9.0 / 3.0 |
| 0.50 | 1.6 | 39 m | 57 | **39%** | 44% | 9.0 / 3.0 |

The knobs do separate: **slope moves the first stop, the multiplier moves the
total.** At (0.60, 1.4) the schedule is 84 minutes against VPM-B's 92 with a
similar distribution. At (0.50, 1.6) the deep fraction matches VPM exactly.

**Air is untouched at every combination** — 9.0 min at 132/20 and 3.0 at the
30 fsw stop, unchanged in all fourteen runs. Both terms are mix-weighted, so
with no helium in a compartment they reduce to the nitrogen values identically.
That is a property of the construction, not of the fitting.

## What is still not reachable

No pair gives both a 54 m first stop **and** ~92 minutes. Getting the first stop
that deep costs total time, because holding deep on bottom gas keeps loading the
slow compartments — VPM tolerates that because its constraint is bubble volume,
not tissue tension. The honest position is that this reproduces VPM's *shape*
and can be tuned to its *total*, but not both at once.

## How to use it

Two profile keys, both default neutral so nothing changes unless set:

    HeSlope: 0.60      MPTT projection slope for helium, 0.2 to 1.5
    HeMptt:  1.40      helium surfacing ceiling multiplier, 0.5 to 2.5

`ZP_SLOPE_HE` and `ZP_MPTT_HE` remain as environment overrides for sweeps.

Suggested starting point for evaluation: **0.60 / 1.40**. It is a fit against one
VPM-B schedule and should be treated as such until it has been run against more
profiles — and VPM-B is itself only a reference, not a truth.
