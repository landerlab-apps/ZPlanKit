# Isobaric counterdiffusion — which criterion, and why

Engine 1.16.0. Advisory only, **off by default**, enabled with `IcdWarn: 0.5`.

## Two rival criteria

**Rule of fifths.** Limit the rise in nitrogen *fraction* to one fifth of the
fall in helium *fraction*. Derives from Steve Burton's argument that nitrogen is
about five times more soluble in tissue than helium, so the comparison should be
made in absolute quantities of dissolved gas.

**Partial-pressure rise.** Flag when the rise in an inspired inert *partial
pressure* at a switch exceeds a threshold. This is what V-Planner ships, with a
default of 0.5 ata.

## Why we use the second

Everything else in this engine — and in every diffusion-based decompression
model — works in partial pressures, because partial pressure is what sets the
diffusion gradient. Solubility determines how much gas a tissue holds at a given
tension; it does not determine which way the gas moves. Mixing an
absolute-quantity criterion into a partial-pressure model is inconsistent.

The rule of fifths also has no empirical evaluation behind it, and it **forbids
18/45 to EAN50** — a switch that is standard practice for a great many technical
divers. A criterion that fires on the most common gas plan in the sport is not
discriminating; it is just noisy, and a warning that always fires is a warning
nobody reads.

Robert Helling makes the same argument in the Subsurface context, with a neat
disproof of the naive "in-moving gas clogs out-moving gas" picture: imagine
switching between two mixes identical except that N-14 is replaced by N-15.
There is counterdiffusion, and there are precisely zero physiological
consequences, because the two are chemically identical.

## What the criterion actually says

Rise in inspired inert partial pressure at the switch:

| switch | depth | Δ pN₂ | Δ pHe | at 0.5 ata |
|---|---:|---:|---:|---|
| **18/45 → EAN50** | **21 m** | **+0.41** | −1.41 | **quiet** |
| 18/45 → EAN50 | 30 m | +0.53 | −1.82 | flag |
| 21/35 → EAN50 | 21 m | +0.19 | −1.10 | quiet |
| 15/45 → EAN36 | 33 m | +1.05 | −1.96 | flag |
| 10/70 → EAN50 | 21 m | +0.94 | −2.20 | flag |
| 12/60 → EAN32 | 33 m | +1.74 | −2.61 | flag |
| 18/45 → O₂ | 6 m | −0.60 | −0.73 | quiet |

It is quiet on the standard 18/45 → EAN50 at 21 m and speaks up on the
helium-rich switches where the nitrogen jump is genuinely large — and on the
*same* 18/45 → EAN50 done nine metres deeper, which is the depth dependence the
fraction-based rule cannot express at all.

The 15/45 → EAN36 at 33 m in Baker's own `VPM.OUT` benchmark flags at +1.05.
Worth knowing rather than worth forbidding, which is exactly the intended tone.

## Correcting an earlier claim in this project

An earlier note in `vval79_conformance.md` treated the 18/45 → EAN50 switch as
an isobaric-counterdiffusion problem and used it to explain a schedule anomaly.
That was wrong twice over: the anomaly turned out to be the linear-rate defect
fixed in 1.12.0, and by the criterion adopted here that switch does not even
flag at the depth it is normally made. The owner said so at the time.

## Implementation

In `select_deco_source()`, the single funnel every gas change passes through —
`do_gas_switch()` is only one of its four callers, which is why an earlier
placement there never fired at all.

Open circuit only. On a rebreather the inspired inert tension is set by the
setpoint and the diluent together, and a diluent change at constant setpoint
does not produce the same step.

    IcdWarn: 0.5      threshold in ata, 0 disables. Default 0.
