# ppmi::field — raw density/displacement fields

**Files:** `include/ppmi/field.hpp`, `src/field.cpp`, `tests/test_field.cpp`.

**Purpose.** The HPC surface: tile/pad MUSIC's field into the shape PeakPatch's
live reader expects, and compute statistics without loading a whole field.

**Data flow.** Stage A MUSIC writes the **unpadded** core grid of side `2^level`;
`pad_core_to_next` pads to `next = nsub·ntile + 2·nbuff` with periodic wrap and
scales by `D(0)/D(zstart)`; PeakPatch reads the result.

**Units.** Grid cells, box fractions. `D(0)/D(zstart)` is dimensionless.

**Conventions and gotchas.**
- Raw, headerless little-endian `float32`, Fortran column-major (first index
  fastest). The `TileWriter/TileReader` tile layout is for the **dead**
  tile-blocked reader; the live `RandomField_Input` reads a global cube side
  `next`, one z-slab per rank.
- Sign (for the inert eta files): on-disk eta is `-displacement` in comoving Mpc.
- The padding scale factor is load-bearing: without it the catalogue is empty.
