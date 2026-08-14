# Reference data for model validation

Extracted from *U.S. Navy Diving Manual, Revision 7* (Volume 2, Chapter 9).
Rev 7 has been in service since 2017 and its air tables are computed with the
Thalmann EL-DCM using the **VVal-79** parameter set — so these tables are the
authoritative acceptance target for the VVAL-79 model.

| File | Source | Use |
|---|---|---|
| `usn_rev7_table9-7_ndl.txt` | Rev 7 Table 9-7, p.9-63 | No-stop limits, 10–190 fsw |

Table 9-9 (Air Decompression Table, Rev 7 p.9-65 onward) holds the stop
schedules and is the second acceptance target. Descent 75 fpm, ascent 30 fpm,
last water stop 20 fsw (Rev 7 section 9-6.5).

## Measured state at engine v1.8.1 (before VVAL-79 exists)

No-stop limits, air, sea level, conservatism 0, USN rates, last stop 20 fsw:

| Depth | USN Rev 7 | ZHL16-C | VVAL-18 (app) |
|------:|----------:|--------:|--------------:|
|  60 ft |  63 | 57  (-6) | 62  (-1) |
|  70 ft |  48 | 39  (-9) | 47  (-1) |
|  80 ft |  39 | 28 (-11) | 38  (-1) |
|  90 ft |  33 | 21 (-12) | 32  (-1) |
| 100 ft |  25 | 17  (-8) | 25   (0) |
| 110 ft |  20 | 13  (-7) | 19  (-1) |
| 130 ft |  12 |  9  (-3) | 12   (0) |
| 150 ft |   8 |  6  (-2) |  8   (0) |

The app's VVAL-18 tracks the Rev 7 no-stop limits to within one minute at every
depth tested. That is consistent with the note in czplan.c that its crossover
pressures were fitted against Rev 7 Table 9-9 — the fit reproduces no-stop
limits well.

It does *not* follow that the deco schedules match: on 45 m / 20 min the same
model gives 52:00 runtime against 57:51 from a Cochran VVAL-18 reference, with
the first stop at 9 m instead of 6 m. A curve fit anchored at a few points
behaves exactly like this — accurate where it was anchored, drifting elsewhere.

So VVAL-79 should be judged on both tables: it must hold the no-stop accuracy
that already exists, and close the schedule gap.

## Correction: schedule comparison, done properly

An earlier comparison against a Cochran NSW III (VVAL-18) printout suggested this
engine ran ~6 min short and was "aggressive". That conclusion was wrong. It
compared totals across two differences at once: the Cochran terminates at 3 m
while the USN tables terminate at 20 fsw, and the Cochran uses VVal-18
parameters while Rev 7 tables are VVal-79.

Compared against Rev 7 Table 9-9 under Navy conventions
(150 fsw / 20 min air, descent 75 fpm, ascent 30 fpm, stops every 10 fsw,
last water stop 20 fsw per section 9-6.5, conservatism 0):

| | 1st stop | 30 fsw | 20 fsw | To 1st stop | Total ascent |
|---|---|---|---|---|---|
| USN Rev 7 (VVal-79) | 30 fsw | 2:00 | 15:00 | 4:00 | 21:40 |
| App VVAL-18         | 30 fsw | 3:00 | 20:20 | 4:00 | ~24:40 |
| App ZHL16-C         | 30 fsw | 1:00 | 25:00 | 4:00 | ~28:00 |

Findings:

* First stop depth and time-to-first-stop match the table exactly. The ceiling
  calculation is not the problem, and the "first stop 9 m instead of 6 m" noted
  earlier was an artefact of using a 3 m last stop, not a defect.
* The engine is *more* conservative than the Rev 7 table, not less — the
  opposite of the earlier claim.
* The residual gap is concentrated in the shallowest stop: 20:20 against 15:00
  at 20 fsw. The deep stop is within a minute.

The shallow-stop gap is where VVAL-79 should be judged: the published M0 values
and the 360- and 480-minute compartments, both absent from the current fitted
set, govern exactly that part of the ascent.

Note also: AscentCredit changes nothing on these profiles. With a 10 fsw stop
grid at 30 fpm each ascent leg lasts 20 seconds, too short for the predictive
rule to diverge from the instantaneous ceiling.

## Provenance of the VVAL parameters, and a document that is wrong

The engine's VVAL parameters come from the owner's `parameters.py`
(`/Volumes/KingstonData/DiveProfileAnalyzerVVALV0/parameters.txt`), which
reverse-engineers the **Cochran Navy NSW III** implementation. Its structure is
deliberate:

* Twelve compartments, because the Cochran uses twelve tissues — evidenced by
  its `TWELVETISSUES` protocol command, the C0Form-C10Form dialogs, and the
  string "Twelve Tissue Halftimes Computations" in `AnalystM.exe`.
* Nine of those are the NEDU half-times 5, 10, 20, 40, 80, 120, 160, 200, 240.
* Their surfacing MPTTs (99.3, 87.7, 78.0, 56.0, 48.5, 45.5, 44.5, 44.0, 43.5)
  are cited `[NEDU]` to TR 12-01 Table 3 — the **VVAL-79** set.
* MPTT slope is 1.0 fsw/fsw for every compartment, per VVAL-79.
* Three faster compartments (1.5/2.5/3.5 min) and all crossover pressures are
  marked `[WORKING]` — placeholders pending fitting against Cochran dive logs.

Note this means the model the app labels **VVAL-18 is built on VVAL-79 MPTTs**.
The label does not describe its contents, which is also why it tracks the Rev 7
tables (themselves VVal-79 derived) so closely.

### The Thalmann_DC_Implementation.pdf parameter set does not work

That document specifies 9 compartments at 5/10/20/40/80/160/240/360/480 min,
M0 of 78/78/78/58/52/51/50/49/48, slopes 1.2-1.8, global PBOVP and SDR 0.67 —
all cited to the same NEDU TR 12-01 Table 3. It contradicts `parameters.py`.

Resolved empirically by patching the engine with each set and computing
no-stop limits against Rev 7 Table 9-7:

| Depth | USN Rev 7 | parameters.py (12c) | PDF set (9c) |
|------:|----------:|--------------------:|-------------:|
|  60 ft |  63 | 62 (-1) | 70 (+7)  |
|  70 ft |  48 | 47 (-1) | 53 (+5)  |
|  80 ft |  39 | 38 (-1) | 15 (-24) |
|  90 ft |  33 | 32 (-1) | 10 (-23) |
| 100 ft |  25 | 25  (0) |  7 (-18) |
| 110 ft |  20 | 19 (-1) |  6 (-14) |
| 130 ft |  12 | 12  (0) |  3  (-9) |
| 150 ft |   8 |  8  (0) |  2  (-6) |

`parameters.py` reproduces the Navy table to within one minute at every depth.
The PDF's set misses by up to 24 minutes, erratically in both directions.

**Do not use Thalmann_DC_Implementation.pdf as an implementation spec.** It
appears to be a secondary summary that conflates VVal-18M and VVal-79 values.
An earlier plan to "implement VVal-79 per that document" would have replaced a
working parameter set with a broken one.

### Actual remaining work

1. Crossover pressures: `czplan.c` uses 23/17/13/11/8/8/8/8 (refitted 2026-08-07),
   `parameters.py` has 20/15/12/10 with slow compartments infinite. Both `[WORKING]`.
2. The three fast compartments' MPTTs are `[WORKING]` extrapolations.
3. The 20 fsw stop runs 20:20 against the table's 15:00.
