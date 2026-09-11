# PeakPatch-MUSIC interface (`ppmi`)

Find a dark-matter halo in a large periodic box, then regenerate that same
region at higher resolution with MUSIC and baryons: produce zoom-in initial
conditions for a specified halo.

Three pieces:

- **MUSIC** (`../music_mpi`) — generates initial conditions, MPI-parallel.
- **PeakPatch** (`../peakpatch`) — semi-analytic halo finder.
- **`ppmi`** — this repo. The orchestrator that drives both in three stages,
  exchanging files on disk.

## Architecture

The stages are separate processes exchanging files, not an in-process
hand-off. Two codes with different MPI decompositions (MUSIC's FFTW slabs,
PeakPatch's cubic tiles) and a Fortran/C++ boundary are more reliably stitched
through disk; the fields are large but the jobs are long anyway.

```
  Stage A                Stage B                 Stage C
  MUSIC survey    -->    PeakPatch        -->    MUSIC zoom
  DM only                filter_gen              baryons + DM
  levelmin=levelmax      hpkvd 1 (table)         levelmin unchanged
  writes Fvec_<name>     hpkvd 0 (peaks)         levelmax raised
                         merge_pkvd              ref_center/ref_extent
                         merged .pksc            from the halo's xlag
```

The orchestrator is one dependency-free C++17 binary. Job submission is plain
`sbatch`; Python is offline analysis only.

## Build

```
nix develop --command bash -c 'cmake -S . -B build -G Ninja && cmake --build build'
```

The PeakPatch and MUSIC binaries are built by delegation (the Makefile's `MUSIC`
target calls `music_mpi`'s own Makefile; PeakPatch is built by the pipeline into
each run directory because it bakes its grid size and run directory in at
compile time).

## Run spec

One INI file is the single source of truth; both codes' configs are generated
from it. See `param/ppmi.ini` for a working example. The spec holds cosmology
once, box once, `levelmin` once, the `[random]` block once; stages add only what
they are allowed to change.

## CLI

```
ppmi validate    <spec.ini>                      check every invariant
ppmi gen-configs <spec.ini> --stage survey|zoom  emit MUSIC/PeakPatch configs
ppmi run         <spec.ini> --music B --peakpatch-src S [--root DIR] [--dry-run]
                                                 run the whole pipeline
ppmi run         ... --stage survey|peakpatch|zoom   run one stage only
ppmi submit      <spec.ini> --music B --peakpatch-src S --dry-run
                                                 print the sbatch chain
ppmi zoom-params <cat.pksc> <spec.ini> --rank N  select a halo, emit zoom conf
ppmi catalog     info|select <cat.pksc> <spec.ini>
ppmi feasibility <spec.ini> --z Z                mass range + expected count
ppmi verify      <run-dir>                       re-hash every recorded output
```

`run` skips a stage whose manifest is current; `--force` re-runs.

## Worked example (small, ~2 minutes locally)

```
# 1. build the orchestrator (above)
# 2. survey + PeakPatch + zoom, 50 Mpc box, levelmin=6, zoom levelmax=8
./build/ppmi run param/ppmi_small.ini \
    --music  "$(pwd)/bin/MUSIC" \
    --peakpatch-src ../peakpatch \
    --root /tmp/demo
./build/ppmi verify /tmp/demo
```

`param/ppmi_small.ini` is a valid, runnable spec using the analytic Eisenstein &
Hu transfer function (no external table needed); `param/ppmi.ini` is a larger
reference spec that uses a CAMB table. `ppmi validate` reports no violations,
and the run produces `<root>/01_survey`, `02_peakpatch`, `03_zoom` with a
manifest each.

## Correctness

The pipeline is only useful if the two MUSIC runs describe the same universe.
Three invariants are enforced, not hoped for:

1. **Realization** — the survey and zoom share `levelmin`, the whole `[random]`
   block and `cubesize`. The spec holds them once; overriding them in a stage is
   a hard error.
2. **Grid pairing** — `2^levelmin == nsub·ntile`; `nmesh` is solved, not copied.
3. **Frame** — generated zoom configs disable MUSIC's coarse-grid alignment
   shift so particle positions line up with the catalogue's Lagrangian frame.

Formats, sign conventions, the density-only hand-off, and the MUSIC gotchas are
documented in [`docs/FORMATS.md`](docs/FORMATS.md). The factual baseline and
full validation history live in `../plan/`.
