# Findings from the uploaded Thalmann / Cochran source documents

Read before starting the VVal-79 + helium work. Three of these change the plan.

Documents: *Thalmann Algorithm Decompression Table Generation* (the NEDU
EL-DCA software design report — by far the most useful), NEDU TR 03-12
(a443070, Thalmann, *Suitability of the USN MK 15 (VVAL18) Algorithm for Air
Diving*), VVAL 18 briefing slides, Cochran operator manuals for NAVY-007M,
NAVY AIR III-79 and EMC-20H with Helium, plus MT92, NOAA and RGBM material.

---

## 1. The crossover is measured against VENOUS saturation, not ambient

`UPDT7`, the gas-exchange routine:

    PAN2  = MAX( PAMB - (PAO2 + PACO2 + PH2O), 0.0 )
    PVSAT = PAMB - (PVO2 + PVCO2 + PH2O)

and the linear/exponential test is

    P(i) > PVSAT + PBOVP

With the VVal-79 constants (PvO2 2.00, PvCO2 2.30, PH2O 0.00) this puts

    PVSAT = PAMB - 4.30 fsw
    crossover at PAMB + 5.70 fsw, NOT PAMB + 10.0 fsw

**Neither czplan.c nor vval18_planner.py has this.** Both test supersaturation
against plain ambient. Compartments therefore enter the linear regime later
than they should, and the linear rate — which is the exponential slope at the
crossover boundary, `k*(PVSAT + PBOVP - PAN2)` — is computed from the wrong
reference and comes out `k * 4.30 fsw` too fast.

Measured in czplan with the venous reference added: 132 fsw / 20 min goes 9 to
11 min, 150 fsw / 20 min goes 23 to 24. Both **longer**. On its own this moves
our numbers away from the tables, which is the clue for the next item.

## 2. LST_DOMode — this is the 20 fsw stop we could not move

The last stop has three defined treatments:

- **0** — allow instantaneous ascent to surface from a depth equal to one stop
  increment (i.e. from 10 fsw).
- **1** — allow instantaneous ascent to surface from the last allowed stop
  depth. *"resultant last stop times are longer than those obtained with
  LST_DOMode = 0."*
- **2** — account for gas exchange during the entire ascent from the last stop
  to the surface. *"shorter than those obtained with LST_DOMode = 0 or 1."*

czplan requires the ceiling to reach 0 while still at the last stop, which is
**mode 1 — the longest of the three**. That is why the 20 fsw stop sat at 20
minutes against Table 9-9's 15 through every parameter change I tried: it is not
a parameter, it is the ascent criterion.

vval18_planner.py walks the ascent, which is closest to **mode 2**, and gives
14 — short, in the other direction. USN publishes 15. The three sit exactly
where the document says they should.

## 3. Two more table-generation switches we do not implement

- **TTIS** — when true, travel time to each stop except the first is *included
  in the tabulated stop time*. Published Navy stop times are therefore not the
  same quantity as ours unless this matches.
- **Round Time Up** — computed stop time is rounded up to the next whole
  minute for output.
- **SRF_CNTRLT_MODE** — selects which compartment controls surfacing: the
  slowest one over its M-value on arrival at the last stop (0), or the one
  closest to its M-value on leaving it (1).

Any comparison against Table 9-9 is only meaningful once these are matched.

## 4. Arterial tension, including the rebreather path

    open circuit    PAO2 = (PAMB - PH2O) * (1 - FN2) - AMBAO2
    constant PO2    PAO2 = PO2 * SURFP_FSW * (1 - PH2O/PAMB) - AMBAO2
    then            PAN2 = MAX( PAMB - (PAO2 + PACO2 + PH2O), 0.0 )

With AMBAO2 = 0 the open-circuit form reduces to the spec's
`F_inert*(PAMB - PH2O) - PACO2`, so the Python is right for OC. The
constant-PO2 form is a distinct branch we do not have, and it matters for the
CCR side of this model.

## 5. Compartment counts — confirmed from the Cochran manuals

| device | manual states |
|---|---|
| Cochran NAVY-007M | Algorithm NAVY VVAL-18, **Tissue Compartments 9** |
| Cochran NAVY AIR III-79 | Algorithm NAVY VVAL-79, **Tissue Compartments 9** |
| Cochran EMC-20H with Helium | **20 tissues loading** |

So the Navy air computers are nine-compartment, matching the published set, and
**the trimix device is twenty**. The twelve-compartment "Cochran-12" set in
vval18_planner.py — nine plus three fast ones — is a scaffold, not the real
architecture, and it is labelled `validated=False` accordingly.

The AIR III-79 slide also resolves the naming: Cochran ships *"VVAL-18 Mod 79"*
on the rebreather/surface-supplied unit and *"VVAL-79"* on the open-circuit air
unit. The muddle is upstream of us.

## 6. Possible helium acceptance references

The Navy has none for EL-DCM (established previously: the Rev 7 He-O2 table is
an edited 1939 table). But the uploads include two candidate outside references:

- **MT92** — French professional tables, Comex lineage, helium-capable.
- **Cochran EMC-20H** — a 20-tissue trimix implementation whose schedules could
  be compared against, if real output from the device is available.

Neither is EL-DCM, so neither validates the model. Both would serve as sanity
bounds in the way MultiDeco VPM-B/E +2 already does.

---

## What this changes

The order of work I proposed needs revising. Chasing parameter sets was the
wrong first move: **three of the four discrepancies are in the ascent and
gas-exchange machinery, not in the tissue table.** Correct sequence:

1. Implement the venous PVSAT reference (item 1).
2. Implement LST_DOMode, defaulting to whichever mode reproduces Table 9-9 with
   items 1, 3 and 4 in place (item 2).
3. Implement TTIS and Round-Time-Up so our stop times are the same *quantity*
   as the published ones (item 3).
4. Add the constant-PO2 arterial branch (item 4).
5. Only then revisit tissue sets, and treat 20 compartments — not 12 — as the
   target architecture for helium.

---

# Both corrections implemented and measured — neither closes the gap

Built on czplan v1.11.0 and measured against the two air anchors. Correct
implementation of LST_DOMode 0 travels the last stop up to one increment,
crediting gas exchange during that travel, then requires surfacing to be
tolerated there. (My first attempt tested "ceiling <= one increment", which is
far more lenient and gave 3 min against the manned trial's 9 — wrong reading.)

| | 132/20 @ 20 fsw | 150/20 |
|---|---|---|
| target | **9** (NEDU manned) | **2 + 15 = 17** |
| czplan v1.11.0 | 9 | 3 + 20 = 23 |
| + LST_DOMode 0 | 9 | 3 + 19 = 22 |
| + venous PVSAT | 11 | 4 + 20 = 24 |
| + both | 11 | 4 + 19 = 23 |

**LST_DOMode 0 is worth one minute, not five.** It holds the manned anchor, so
it is safe, but it does not explain the 150/20 excess.

**The venous PVSAT reference makes things worse, and breaks the manned anchor**
(9 to 11). That is informative rather than disqualifying: PBOVP and the venous
offset are coupled, and the crossover values in this engine were fitted with no
venous term. Applying one without refitting the other is not a valid test of
either. It should not ship in this state.

So the hypothesis in the section above — that these two accounted for the
residual — is **wrong**. Measured, not argued.

## What is still unaccounted for

150 fsw / 20 min remains 22-23 minutes against a published 17, and the excess
is not in the last stop alone: our first stop is 30 fsw for 3 minutes where the
table has 2. Remaining candidates, in the order I would test them:

1. **TTIS.** Published Navy stop times include travel time to each stop except
   the first. If this engine reports time *at* the stop, every tabulated
   comparison is offset by roughly the inter-stop travel time — about 20 s per
   10 fsw at 30 fpm, which over five stops is most of the discrepancy.
2. **Round Time Up**, applied per stop, compounds with the above.
3. **Depth conversion.** This engine works in metres with a bar-per-metre
   constant; the tables are computed in fsw with 33 fsw = 1 atm exactly. A 0.5%
   error in ambient pressure is small per step and cumulative over a schedule.
4. **First stop selection** (FRSP7) and **SRF_CNTRLT_MODE**.

Nothing further has been shipped. czplan remains at v1.11.0 with the linear
rate fix only — that one is independently confirmed by vval18_planner.py and it
resolved the reported pathology.

---

# Note on the other uploads

- **NEDU TR 03-12** (a443070, Thalmann): the origin report for VVAL-18 on air —
  risk analysis against the NMRI LE1 probabilistic model, concluding VVAL-18
  gave risks not significantly higher, and mostly lower, than the standard air
  tables. Context rather than parameters.
- **VVAL 18 briefing slides**: the case for replacing the standard air tables —
  DCS incidence by decompression stress, the 150 fsw risk comparison.
- **RGBM** (Wienke, appendix): bubble-phase model with a phase-volume
  constraint. Unrelated architecture; useful only as background.
- **Probabilistic gas and bubble dynamics models**: scanned images with no OCR
  layer, so not machine-readable here. Not read.
- **MT92**: French professional tables, Comex lineage, helium-capable —
  a possible outside sanity bound for a helium model, not a validation of one.
- **NOAA Diving Manual**: oxygen exposure limits and general reference.
