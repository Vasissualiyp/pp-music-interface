# ppmi::pipeline — staged runner

**Files:** `include/ppmi/pipeline.hpp`, `src/pipeline.cpp`,
`tools/ppmi_main.cpp`, `tests/test_pipeline.cpp`.

**Purpose.** Drive stages A/B/C in order, each in its own directory and with a
manifest; expose per-stage commands so `run --stage` and `submit` can reuse them.

**Data flow.** `prepare_root` copies and hashes the spec; `prepare_stage` builds
the stage (PeakPatch builds into its own stage directory because its grid size
and RUNDIR are compile-time); `run_stage` executes, manifests, and checks
currency; `run_pipeline` chains them and selects the halo between B and C.

**Units.** Mpc/h in commands, seconds for timing.

**Conventions and gotchas.**
- PeakPatch must build in the stage directory (constraint 1); the zoom runs at
  one rank (constraint 2).
- `--stage` runs exactly one stage; this is what generated sbatch scripts call,
  because stage C needs stage B's catalogue.
- Empty catalogue after stage B is a hard error, not something to pass on.
