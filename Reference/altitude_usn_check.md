# Altitude — checked against the U.S. Navy Diving Manual rev 7, chapter 9

The altitude handling was rebuilt at engine 1.21.0 after the acclimatisation
assumption was found to be wrong. This records the checks against the Navy's
own tables, and one thing the manual corrected.

## What the Navy does, and why it is not what this engine does

§9-13 gives the **Cross Correction** technique. Two corrections, both ratios of
atmospheric pressure:

    sea level equivalent depth = actual depth × (P_sealevel / P_altitude)
    stop depth at altitude     = sea level stop × (P_altitude / P_sealevel)

The diver then reads a standard sea-level table at the corrected depth. Table
9-4 tabulates both so nobody has to do the arithmetic on a boat.

This engine does not use Cross Correction, and should not. It evaluates the
model directly at the ambient pressure that actually obtains at altitude, which
is what Bühlmann, VPM-B and every modern computer do. Cross Correction exists
to let a **fixed printed table** be used at altitude; it preserves the *ratio*
of absolute pressure to surface pressure rather than the absolute pressure
itself, and is deliberately conservative because of it.

So the two methods are not expected to agree stop for stop. What can be checked
is the physics underneath both.

## Check 1 — atmospheric pressure model

Table 2-19 gives atmospheric pressure at altitude. The engine uses the ISA
barometric formula, `P = P0 · (1 − 2.25577e-5·h)^5.25588`.

| altitude ft | Table 2-19 (ata) | engine (ata) | difference |
|---:|---:|---:|---:|
| 1000 | 0.964 | 0.964 | +0.04 % |
| 2000 | 0.930 | 0.930 | −0.02 % |
| 3000 | 0.896 | 0.896 | +0.03 % |
| 4000 | 0.864 | 0.864 | −0.04 % |
| 5000 | 0.832 | 0.832 | +0.01 % |
| 6000 | 0.801 | 0.801 | +0.05 % |
| 7000 | 0.772 | 0.772 | −0.05 % |
| 8000 | 0.743 | 0.743 | −0.03 % |
| 9000 | 0.715 | 0.715 | −0.03 % |
| 10000 | 0.688 | 0.688 | −0.04 % |

Worst deviation 0.05 %, which is rounding in the published table.

## Check 2 — sea level equivalent depth against Table 9-4

Applying the ratio and rounding up to the next standard table depth reproduces
**45 of 50** sampled cells exactly.

All five disagreements are in the 1000 ft column, where the Navy leaves shallow
depths uncorrected and the ratio rounds up one step — 30 fsw becomes 31.1,
which this method takes to 35 and the table leaves at 30. Erring deep is the
safe direction, and at 1000 ft the difference is immaterial anyway.

Every column from 2000 ft upward matches cell for cell.

## Check 3 — equivalent stop depths against the Table 9-4 footer

Multiplying a sea-level stop by the inverse ratio reproduces **59 of 60** cells.
The single miss is a 50 fsw stop at 2000 ft, which lands on exactly 46.5 and is
a rounding tie.

## What the manual corrected: equilibration, not acclimatisation

§9-13.4 is worth quoting, because this project had the word wrong:

> Upon ascent to altitude, two things happen. The body off-gases excess
> nitrogen to come into equilibrium with the lower partial pressure of nitrogen
> in the atmosphere. It also begins a series of complicated adjustments to the
> lower partial pressure of oxygen. The first process is called equilibration;
> the second is called acclimatization. Approximately twelve hours at altitude
> is required for equilibration. A longer period is required for full
> acclimatization.

The engine models the nitrogen process and nothing at all of the oxygen one. It
was calling the setting "acclimatised", which named the wrong process and could
have led a diver to think the planner accounted for altitude adaptation. Now
"equilibrated", everywhere, with the Navy's twelve-hour figure in the help text.

§9-13.4 also vindicates the change that prompted all this:

> If a diver begins a dive at altitude within 12 hours of arrival, the residual
> nitrogen left over from sea level must be taken into account. In effect, the
> initial dive at altitude can be considered a repetitive dive, with the first
> dive being the ascent from sea level to altitude.

That is exactly the defect that was fixed: the engine had been starting every
altitude dive with the tissues already equilibrated, and so was ignoring
residual nitrogen the Navy requires to be accounted for. Their Table 9-5 does
it by assigning a repetitive group; the per-compartment Haldane washout here is
the continuous form of the same idea.

## Where twelve hours is and is not enough

The Navy's figure is right for the Navy's model. Their air tables are
controlled by a 120-minute compartment, and at twelve hours that compartment
retains 1.6 % of the sea-level excess — equilibrated by any reasonable
standard.

Bühlmann's set runs to 635 minutes. Percentage of the sea-level excess still
present:

| half-time | 1 h | 6 h | 12 h | 24 h | 48 h |
|---:|---:|---:|---:|---:|---:|
| 27 min | 21 % | 0 % | 0 % | 0 % | 0 % |
| 109 min | 68 % | 10 % | 1 % | 0 % | 0 % |
| 239 min | 84 % | 35 % | 12 % | 2 % | 0 % |
| 635 min | 94 % | 68 % | **46 %** | 21 % | 4 % |

So at twelve hours the slowest Bühlmann compartment is still holding nearly
half its excess. For a single dive this rarely shows, because the compartments
that lead a 30–40 m dive are equilibrated long before — which is why the
computed schedule stops moving at around two hours on a 30 m dive and around
six on a long shallow one. It matters on a second or third day at altitude,
where the tissue file carries it forward.

Using `HoursAtAltitude` rather than ticking equilibrated is therefore the
honest choice for a diver who arrived yesterday.
