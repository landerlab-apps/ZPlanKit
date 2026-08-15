# CCR inspired-gas cross-check — ZHL-16C

Checked 15 August 2026 against the two references named in the project
instructions. All three implementations are algebraically identical.

## Lplanner — `czplan.c`, `inspired()`

    pa      = ambient - PH2O                 PH2O = 0.0627 bar
    po2     = min(setpoint, pa)
    pinert  = pa - po2
    pin2    = pinert * fN2dil / (1 - fO2dil)
    pihe    = pinert * fHedil / (1 - fO2dil)

## Subsurface — `core/deco.cpp` + `core/gas.cpp`

`add_segment()` calls `fill_pressures(pressure - WV_PRESSURE, ...)` with
`WV_PRESSURE 0.0627`, so its "ambient" is already alveolar. Then:

    o2 = po2                                  (o2 = amb, inert = 0 if po2 >= amb)
    he = (amb - po2) * fHe / (1 - fO2)
    n2 =  amb - po2 - he

`n2` expands to `(amb - po2) * fN2 / (1 - fO2)`. Same expression, same water
vapour constant, same clamp against the alveolar rather than the raw ambient
pressure.

## Abysner — `BuhlmannUtilities.kt`, `ccrSchreinerInputs()`

    inspired = fInert * (ambient - setpoint) / (1 - fO2dil)

with the caller adding water vapour to the setpoint, which is the same thing as
subtracting it from ambient. Identical.

Abysner derives water vapour from temperature via the Antoine equation instead
of fixing it at 0.0627 bar; at 37 C the two agree. Its near-surface clamp is
against raw ambient rather than alveolar, a difference confined to the last
~0.6 m and only to whether inert reaches exactly zero.

## Conclusion

No change required to the closed-circuit model. Recorded here so this does not
get re-derived a third time.
