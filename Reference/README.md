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
