# ppmi::geometry — grid pairing and frames

**Files:** `include/ppmi/geometry.hpp`, `src/geometry.cpp`, `tests/test_geometry.cpp`.

**Purpose.** Two pure-geometry concerns: the rule that makes a PeakPatch lattice
correspond to a MUSIC level, and the transform between PeakPatch's
observer-centred box and MUSIC's Lagrangian unit box.

**Data flow.** `RunSpec::peakpatch_grid()` uses `solve_nmesh()`; the halo
selector and zoom-config generator use `BoxFrame::to_unit/from_unit` and
`halo_to_ref`; `write_region_points` emits a MUSIC `region_point_file`.

**Units.** Lengths are the run's length unit (Mpc/h in the reference configs);
MUSIC unit-box coordinates are dimensionless in `[0,1)`. No `h` factor.

**Conventions and gotchas.**
- Pairing is `2^levelmin == nsub·ntile`, not `nmesh == 2^levelmin`.
- A region crossing a face raises `RegionWrapsBox` (MUSIC cannot express wrap);
  `write_region_points` wraps individual points instead.
- `to_unit` is `frac((x-cen)/boxsize + 0.5)`; axis order matches (P3-T3 spike).
