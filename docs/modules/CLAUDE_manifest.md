# ppmi::manifest — provenance and verify

**Files:** `include/ppmi/manifest.hpp`, `src/manifest.cpp`,
`tests/test_manifest.cpp`.

**Purpose.** After each stage, record what ran so an old catalogue and a new spec
are not mistaken for a matched pair. Backs `ppmi verify`.

**Data flow.** `run_stage` writes `<stage>/manifest.ini` (command, environment,
git commits, input/output paths with sizes and SHA-256, wall time, exit status);
`verify_run` re-hashes every recorded output.

**Units.** Sizes in bytes, wall time in seconds; paths absolute.

**Conventions and gotchas.**
- A failed stage records the failure rather than omitting the manifest, so
  "never ran" and "ran and failed" are distinguishable.
- Outputs are re-hashed, not trusted, which is what makes resume safe.
