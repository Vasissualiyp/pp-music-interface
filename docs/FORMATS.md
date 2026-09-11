# Formats and conventions

Everything the two codes exchange, with the file/line citations that pin it.
Written so the next person does not have to rediscover `00_FINDINGS.md`
sections 1-3, 7-10 from source. Where a long-standing belief was wrong, the
correction is stated explicitly.

## 1. Merged halo catalogue (`.pksc`)

Written by `merge_pkvd` (`peakpatch/src/merge_pkvd/merge_pkvd_module.f90`).

### Header

Two variants; a reader branches on the first `int32`:

| first `int32` | layout | header |
|---|---|---|
| `-1` | sentinel / 64-bit (nate) | `int32 -1`, `int64 nout`, `float32 rmax`, `float32 boxredshift` (20 B) |
| anything else | legacy / 32-bit (vasdev) | `int32 nout`, `float32 rmax`, `float32 boxredshift` (12 B) |

Both exist because the two branches evolved separately; the sentinel form exists
because WebSky-scale runs exceed `2^31` halos. `ppmi::CatalogReader` handles
both and infers the record width from the file size.

### Records

`nout` records, each `outnum` little-endian `float32`:

| # | field | units | meaning |
|---|---|---|---|
| 1-3 | `x,y,z` | comoving Mpc/h (run's length unit) | **Eulerian** position, after 1LPT+2LPT |
| 4-6 | `vx,vy,vz` | km/s | peculiar velocity (not displacement, despite PeakPatch's Python naming them `dx,dy,dz`) |
| 7 | `r` | Mpc/h | Lagrangian top-hat radius of the collapsing patch |
| 8-10 | `xlag,ylag,zlag` | Mpc/h | **Lagrangian** position, saved before displacement |
| 11 | `Fcollv` | dimensionless | linear collapse overdensity at the peak |

`outnum` is 11 when `ioutshear=0`, else 33 (fields 12-33 are the shear
eigen-decomposition; the first ten are unmoved).

Mass is derived: `M = (4/3)π r³ ρ_m`, `ρ_m = 2.77536627e11 · Ω_m · h²`
`M_sun/(Mpc/h)³`. Worked value for pinning tests: `r=1`, `Ω_m=0.3099`,
`h=0.6774` → `M = 1.653e11 M_sun`.

### Coordinate origin

PeakPatch's box is observer-centred:
`x ∈ [cen - boxsize/2, cen + boxsize/2]`, `cen` from
`[lattice_parameters_hpkvd]`. MUSIC works in the unit box `[0,1)` from a corner.
The transform (proven empirically by the P3-T3 spike test, `ppmi::BoxFrame`) is
per axis

```
u = frac( (x - cen) / boxsize + 0.5 )        # PeakPatch -> MUSIC
x = (u - 0.5) * boxsize + cen                # MUSIC -> PeakPatch
```

Axis order matches (a permuted spike stayed un-transposed), and the writer's
Fortran column-major order was confirmed.

## 2. Field files (`Fvec_`, `etax_`, `etay_`, `etaz_`)

Raw, headerless `float32`, Fortran column-major (first index varies fastest),
little-endian. No record markers.

### What PeakPatch's reader actually uses

**Corrected from `00_FINDINGS.md` §3.** `read_external_field`
(`hpkvdmodule.f90:1826-1883`), which reads all four files and negates the eta
fields, is **dead code** — it is never called in the base, either branch, or the
merge. The live `ireadfield=1` path is `RandomField_Input(-1, deltag)`
(`RandomField.f90:1176-1222`): it reads **only `Fvec_<stem>`** and PeakPatch
computes 1LPT/2LPT itself from that density. So the survey hand-off is
**density-only**; the eta files are correct but inert. (MUSIC's 1LPT from the
same density equals PeakPatch's internal 1LPT, so the physics is unchanged.)

### Layout the live reader expects

`RandomField_Input` reads a plain global cube of side
`n = nsub·ntile + 2·nbuff` in Fortran order, each rank taking a z-slab
(offset `n·n·local_z_start`). This is **not** the tile-blocked layout that the
dead `readsubbox` describes; the two coincide only at `ntile==1`. The tile
layout in `include/ppmi/field.hpp` belongs to the dead path.

MUSIC's `output_peakpatch` plugin writes the **unpadded** core grid of side
`2^level` (task P3-T1). The interface pads it to `next = nsub·ntile + 2·nbuff`
with periodic wraparound and scales it by `D(0)/D(zstart)`, because MUSIC writes
the field at `zstart` while PeakPatch applies its own `Dlinear(z)` and expects
the field at `z=0`. That scale factor is the one silent-but-fatal bug found in
Phase 4: without it every peak is ~40× too shallow and the catalogue is empty.

### Sign and units of the eta files (for future use)

Decided once so it cannot drift, though currently inert: the eta files hold the
**negated** displacement in comoving Mpc. PeakPatch's dead reader negates them
again (`eta = -displacement`) and uses the result as the positive displacement
`x_euler = x_lag + etax·D` (`hpkvdmodule.f90:988`). MUSIC's grid stores a
displacement (the gadget plugin adds it to the cell position), so the writer
emits `-displacement · boxlength`.

## 3. Sidecar and manifest

- `<base>.fields.json` (written by the MUSIC plugin): grid, level, boxlength,
  zstart, dtype, byte order, array order, sign convention. The binary format is
  not self-describing, so this is the only defence against convention drift.
- `<stage>/manifest.ini` (written by `ppmi`): the command, environment, git
  commits, input/output paths with sizes and SHA-256, wall time and exit status.
  `ppmi verify <root>` re-hashes every recorded output.

## 4. Reproducibility and the three MUSIC gotchas

1. **Realization invariant.** White noise is a coarse-to-fine cascade, so the
   survey and zoom share a realization only when `levelmin`, the whole
   `[random]` block and `cubesize` are identical. `ppmi` enforces this and the
   spec holds them once. (`00_FINDINGS.md` §2.)
2. **Seed placement.** For `levelmin < levelmax`, MUSIC must declare the seed at
   `levelmin` and build finer levels by constrained refinement. A seed *below*
   `levelmin` under MPI used to abort; the pipeline never emits one.
3. **Domain alignment shift.** With `levelmin < levelmax` MUSIC shifts the box so
   the refined region is centred on the coarse grid (`mesh.hh:1719-1741`,
   "Domain shifted by ..."), which offsets zoom particles from the catalogue
   `xlag`. Generated zoom configs set `[setup] no_shift = yes` so the output
   frame equals the catalogue frame.

Two other MUSIC bugs found and fixed on this branch are worth knowing because
they were silent: `store_rnd` freeing RNG cubes the refinement still needed
(levelmin<levelmax abort) and a negative-index wrap in `random_numbers::operator()`
(heap corruption for off-centre regions). See `plan/PHASE3_STATUS.md` and
`plan/VALIDATION.md`.

## 5. Grid geometry

From `config_reader.f90:802-810`:

```
nsub      = nmesh - 2*nbuff
next      = nsub*ntile + 2*nbuff
dcore_box = boxsize / ntile
cellsize  = dcore_box / nsub
```

The pairs-with-MUSIC rule is **`2^levelmin == nsub·ntile`**, not the
`nmesh == 2^levelmin` stated in the old README. `ppmi` solves `nmesh` from
`levelmin`, `ntile`, `nbuff` so the two cannot disagree.
