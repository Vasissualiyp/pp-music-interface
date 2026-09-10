# Legacy parameter files (do not use for new runs)

These files are the old hand-edited shared configs that were patched between
stages with `sed` by `run_pp_music.sh`. They are kept only so legacy runs can
still be reproduced; new work must start from `param/ppmi.ini`.

Both shipped configs violate the interface's own grid pairing invariant
`2^levelmin == nsub*ntile` (see `plan/00_FINDINGS.md` section 3 and
`ppmi geometry`):

- `parameters.ini` has `nmesh = 364` with `levelmin = 9`, so
  `nsub*ntile = 364 - 2*64 = 236 != 256`.
- `debug.ini` has `nmesh = 368` with `levelmin = 6`, so
  `nsub*ntile = 368 - 2*nbuff != 64` for any sane buffer.

`parameters.ini` also sets `ievol = 1`, which makes PeakPatch produce a
lightcone rather than a single-redshift catalogue, and omits `boxsize`
entirely. Both mistakes are exactly what the `ppmi` validator now rejects.
