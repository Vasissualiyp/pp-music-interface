# ppmi::catalog — `.pksc` catalogues

**Files:** `include/ppmi/catalog.hpp`, `src/catalog.cpp`, `tests/test_catalog.cpp`.

**Purpose.** Stream PeakPatch's merged halo catalogues (both header formats) and
supply halo selection.

**Data flow.** The pipeline reads stage B's `<run>_merge.pksc`, `select()` picks
a target by criterion, and the zoom config takes its `xlag` and `r`.

**Units.** Positions and `r` in the run's length unit (Mpc/h in reference
configs); velocities km/s; mass derived in `M_sun` as
`M = (4/3)π r³ ρ_m`, `ρ_m = 2.77536627e11 · Ω_m · h²` `M_sun/(Mpc/h)³`.

**Conventions and gotchas.**
- Header is detected from the first `int32`: `-1` sentinel (20 B, int64 count),
  otherwise legacy (12 B). Records are 11 `float32`, or 33 with shear.
- Fields 4-6 are **velocities** (km/s), not displacements, despite PeakPatch's
  Python naming them `dx,dy,dz`.
- Selection refuses a lightcone catalogue without a redshift rather than
  returning a mixed-redshift answer.
