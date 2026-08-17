# The oxygen window — where it acts, and why the argument dissolves

Three positions in this project's sources appear to contradict each other:

- **Brian / GUE** — the oxygen window enhances inert gas elimination.
- **Ross (V-Planner)** — *"it has no practical effect on decompression rates."*
- **Wienke (RGBM)** — inherent unsaturation is a real term, and it *"takes up
  inert gas under compression-decompression."*

They are all correct. They are describing **different phases**.

---

## Dalton's law binds a gas phase. It does not bind a liquid.

This is the whole of it.

**In a gas phase** — an alveolus, or a bubble — the partial pressures *must* sum
to the pressure of that phase. That is not a modelling choice, it is what a gas
phase is. Consume oxygen and return carbon dioxide at a lower partial pressure
(most CO2 travels as bicarbonate, not as dissolved gas), and the arithmetic has
to balance somehow: either the pocket shrinks, or something else in it rises.

**In blood or tissue** there is no such constraint. Each dissolved gas carries
its own tension, set by its own diffusion history. The sum is an arithmetic
quantity with no physical force behind it, and it exceeds ambient routinely —
**that is what supersaturation is.** A diver at a 6 m stop has a nitrogen tension
well above the whole ambient pressure and nothing whatever happens, because
there is no gas phase for Dalton to bind.

So Ross's objection is exactly right as stated: summing partial pressures and
comparing them to ambient is only meaningful **in the lung** — or, he does not
say this, in a bubble. It cannot be a driving force for dissolved gas, because
dissolved gas moves down **its own** partial pressure gradient and is indifferent
to what else is in solution beside it.

## So where does the window act? On bubbles.

Consider a bubble already present in tissue. Its contents must sum to the phase
pressure, ambient plus the surface-tension term:

    p_inert(bubble) = P_amb + 2γ/r − (PvO2 + PvCO2 + PH2O)

The metabolic gases are not negotiable — they equilibrate with the surrounding
tissue, where oxygen has been consumed and carbon dioxide carried away as
bicarbonate. Whatever is left of the phase pressure is **the only room inert gas
has inside that bubble.**

Using VVal-79's own constants, PvO2 2.00 fsw + PvCO2 2.30 + PH2O 0.00, the
metabolic gases claim **4.30 fsw — 99 mmHg** of every bubble. Inert gas in the
surrounding tissue diffuses into the bubble only while its tension exceeds that
remaining room; below it, the bubble gives gas back and shrinks.

**That is the oxygen window doing real work — resolving bubbles, not accelerating
dissolved washout.** And it is why breathing oxygen at the last stop is worth
more than the dissolved-gas gradient alone suggests: it is not only that inspired
inert falls to zero, it is that any bubble already present is being squeezed from
the inside.

## Which means our own model already contains it

    p_inert(bubble) = P_amb + 2γ/r − (PvO2 + PvCO2 + PH2O)
                    = PVSAT + a surface-tension allowance

**That is `PVSAT + PBOVP`** — the quantity engine 1.12.0 introduced as the
crossover boundary, and the quantity 1.16.0 fitted PBOVP against.

The Thalmann crossover is not an arbitrary threshold. `PVSAT` is the oxygen
window written in venous-tension form, and `PBOVP` is the overpressure a nucleus
tolerates before the phase becomes self-sustaining. Above that boundary the
model switches to linear kinetics — which is exactly the regime where a free
phase is present and washout is no longer a simple exponential.

So EL-DCM has a bubble term after all. It is disguised as a change of kinetics
rather than a bubble radius, but the boundary it uses is the oxygen window plus a
surface-tension allowance, and that is not a coincidence.

## What this settles for the project

**1. Engine 1.13.0's measurement was right, and now it has a reason.**
An FO2-dependent `PVSAT` changed nothing on any real dive. Correct: venous
oxygen is haemoglobin-buffered below 2.2 ata, so the room a bubble has does not
change with the mix. The benefit of a rich mix reaches a dissolved-gas model
through the arterial term, and that is the only place it can.

**2. Ross is right about dissolved-gas models specifically.** In Bühlmann or
EL-DCM the window cannot accelerate dissolved washout, because those models
track tensions, not phases. Anyone claiming a "window bonus" on a GF schedule is
claiming something the model does not contain.

**3. But the window is not optional in a bubble model.** If VPM-B is added here,
inherent unsaturation must come with it — it is one of the terms setting whether
a bubble grows or shrinks. Wienke is not adding a nicety; he is applying Dalton
where Dalton applies.

**4. And it explains the deco-gas result.** Engine 1.17.0 measured helium deco
mixes as *costing* time — 93 min on EAN50 against 115 on 50/50. A dissolved-gas
model can only see the extra helium being delivered. The claimed benefit,
reported as better post-dive condition, is a free-phase effect: which gas is
left in the bubbles, and how fast the window can squeeze them. Our model cannot
show it because our model has no bubbles. That is a limitation to state, not a
verdict on the technique.
