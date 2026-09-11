# ppmi — orchestrator architecture

`ppmi` drives MUSIC and PeakPatch as three staged processes exchanging files.
One dependency-free C++17 binary; `include/ppmi/` headers carry the contracts,
`src/` implements them, `tools/ppmi_main.cpp` is the CLI, `tests/` pin behaviour.
No Python on the run path. Formats and cross-code conventions live in
`docs/FORMATS.md`; the factual baseline is `../plan/`.

Data flow: `spec.ini` -> `music_conf()`/`peakpatch_ini()` -> stage A survey
(MUSIC writes `Fvec_`) -> stage B PeakPatch (reads the padded, D-scaled field,
writes `<run>_merge.pksc`) -> halo selection -> stage C zoom (`music_conf` with a
`RefRegion`) -> Gadget ICs. Each stage leaves a `manifest.ini`.

## Modules

| Module | Header / impl | One line | Units |
|---|---|---|---|
| geometry | `geometry.hpp` / `geometry.cpp` | grid pairing and coordinate frames | Mpc (run's length), unit box `[0,1)` |
| catalog | `catalog.hpp` / `catalog.cpp` | `.pksc` read/write/select | Mpc, Mpc/h, km/s, M_sun |
| field | `field.hpp` / `field.cpp` | raw field tiling, padding, stats | float32, no unit; box fraction |
| spec | `spec.hpp` / `spec.cpp`, `config.cpp` | run spec, invariants, config generation | dimensionless (box fractions, Mpc) |
| feasibility | `feasibility.hpp` / `feasibility.cpp` | resolvable mass range + expected count | M_sun, Mpc, redshift |
| manifest | `manifest.hpp` / `manifest.cpp` | provenance and `verify` | bytes, seconds |
| pipeline | `pipeline.hpp` / `pipeline.cpp` | staged runner and commands | Mpc, seconds |
| cli | `tools/ppmi_main.cpp` | subcommand dispatch | — |

Per-module notes: `docs/modules/CLAUDE_<module>.md`.

**Cross-cutting conventions.** All lengths are the run's length unit (Mpc/h in
the reference configs); no code inserts an `h` factor. Fields are little-endian
`float32` in Fortran column-major order. Coordinates between the codes use
`u = frac((x - cen)/boxsize + 0.5)`. The survey→PeakPatch hand-off is
**density-only** (`ireadfield=1` computes LPT internally). Generated zoom configs
set `no_shift=yes`.
