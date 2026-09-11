# ppmi::spec — run spec, invariants, config generation

**Files:** `include/ppmi/spec.hpp`, `src/spec.cpp`, `src/config.cpp`,
`tests/test_spec.cpp`.

**Purpose.** One INI spec is the single source of truth; this module parses it,
enforces the invariants, and renders MUSIC `.conf` and PeakPatch `.ini`.

**Data flow.** `load_spec` -> `validate` (throws `InvariantViolation`) ->
`music_conf` / `peakpatch_ini` / `write_stage_configs`. The survey config emits
`Fvec_<name>`; the zoom config adds `ref_center`/`ref_extent`, baryons, and
`no_shift=yes`.

**Units.** Dimensionless (box fractions), Mpc/h (`boxlength`), redshift.

**Conventions and gotchas.**
- The realization invariant: survey and zoom share `levelmin`, the whole
  `[random]` block and `cubesize`; a stage may not override them.
- Grid pairing is solved, not copied; `nmesh` follows from the spec.
- Generation is deterministic; `transfer_file` is required only for tabulated
  transfer plugins.
