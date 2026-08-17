# What the V-Planner "myths and mistakes" page gives us

Three things worth having, one of them a bug fix that was overdue.

---

## 1. Helium in deco gases — a real technique we could not even express

The page makes the case directly:

> *"At the switch onto 50%: there isn't any He in the deco mix to inspire, so the
> helium gradient has been maximised... At this switch point the N2 inspired
> component was virtually unchanged, and the off gas rate of N2 is controlled by
> ambient pressure reduction alone... the N2 pp has increased to the level
> experienced several stops deeper. This effectively brings N2 off gas rates to
> a halt."*
>
> *"Using 50/50... there is no inspired N2, so the nitrogen gradient has been
> maximised."*

**That is exactly the mechanism this project diagnosed and then mis-labelled.**
Instrumenting the 70 m dive months of work ago, the controlling compartment's
nitrogen sat at 1.554 bar against an inspired 1.534 on EAN50 and stopped moving
entirely. It was called isobaric counterdiffusion at the time. It is better
described as what the page says: on a nitrox deco gas, nitrogen washout very
nearly stops and the whole switch is spent clearing helium.

**And we could not model the alternative.** `OcDecoGas` was parsed with `atof()`,
so **`50/25` silently became `50`** — a diver who entered a trimix deco gas got a
schedule computed for a gas they were not breathing. Fixed in engine 1.17.0:
deco gases now carry a helium fraction, it is used for the tissue model, for the
END calculation, and it is reported.

### What it does to a schedule

70 m / 26 min on 18/45, ZHL16-C GF 30/85:

| deco gases | total deco | stops from 21 m |
|---|---:|---|
| 50, 100 | **93 min** | 3:42 / 3:00 / 5:00 / 7:00 / 12:00 / 13:42 / 26:00 |
| 50/25, 100 | 101 min | 3:42 / 4:00 / 7:00 / 9:00 / 13:00 / 14:42 / 27:00 |
| 50/50, 100 | 115 min | 4:42 / 6:00 / 8:00 / 12:00 / 18:00 / 15:42 / 28:00 |

Stops deeper than 21 m are identical, as they must be — the deco gas only
applies above the switch.

**The honest finding: on a dissolved-gas model a helium deco mix costs time, it
does not save it.** 93 minutes becomes 115 on 50/50. That is not a contradiction
of the page, which claims *"improved results in post dive conditions"* — a
clinical observation, not a shorter schedule. The model can now express the
technique; it cannot demonstrate the benefit, because the benefit claimed is
about which gas you are left carrying, not how long you hang.

Divers who use helium deco mixes should know our schedules will be longer, and
that this is the model working correctly rather than a penalty.

## 2. First stop at about 2 ATA off the bottom

> *"The bubble models... naturally create a first stop at around 2 ATA off the
> bottom"* — and gradient factors are *"often set to provide a first stop at
> around 2 ATA off the bottom."*

A simple, model-independent sanity rule, and a better fitting target than
chasing one VPM-B number:

| | bottom | 2 ATA rule | observed |
|---|---:|---:|---:|
| 70 m, VPM-B/E +2 | 8.10 ata | 50 m | **48 m** |
| 80 m, VPM-B (Baker) | 9.12 ata | 60 m | 54 m |
| 80 m, our ZHL16-C GF 30/85 | 9.12 ata | 60 m | 48 m |
| 80 m, our VVAL-79 neutral | 9.12 ata | 60 m | **33 m** |
| 80 m, our VVAL, HeSlope 0.60 | 9.12 ata | 60 m | **48 m** |

VVAL at neutral is 27 m shallower than the rule. At HeSlope 0.60 it lands on
48 m — the same first stop as our own Bühlmann at GF 30/85, and in the same
neighbourhood as VPM-B. That is independent support for the slope mechanism,
from a rule that knows nothing about it.

The page is also scathing about ratio-deco style *x%*-of-depth rules, noting the
80% rule *"is valid at 300 ft only"* and produces first stops nearly at the
bottom on a 100 ft dive. We do not implement any such rule and should not.

## 3. The oxygen window — Ross argues it does nothing

> *"I would suggest that the oxygen window concept does exist as described in
> the paper... but it has no practical effect on decompression rates. It does not
> significantly change the off gas rates of N2 or He."*

His argument: nitrogen and helium each seek equilibrium with their **own** partial
pressure, independently of oxygen and CO2; and summing partial pressures against
ambient is only valid for gas in the lungs, because in tissue the sum routinely
exceeds ambient — that is what supersaturation *is*. So the "more room exists"
picture does not hold.

This corroborates what engine 1.13.0 measured from the other direction: venous
PO2 is haemoglobin-buffered and does not move below 2.2 ata, so an FO2-dependent
`PVSAT` changes nothing on any real dive. Two independent routes to the same
place — there is no separate oxygen-window mechanism to add. Whatever benefit
exists is already carried by the arterial term.

## 4. IBCD, for the wording of our advisory

The page notes the term covers **four distinct injuries** — superficial/skin,
Lambertsen's tissue loadings, inner-ear DCS, and the deco-mix-selection sense —
and that conflating them causes most of the confusion. Our advisory is about the
fourth only. Worth saying so if the wording is ever revisited.

---

## Shipped in 1.17.0

Helium-carrying deco gases: `OcDecoGas: 50/25, 100`. Parsed, breathed, counted
in the tissue model and in END, and shown in the report. Air is unaffected; the
two air anchors hold at 9.0 and 3+19.
