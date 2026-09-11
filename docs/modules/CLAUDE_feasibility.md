# ppmi::feasibility — resolution reach

**Files:** `include/ppmi/feasibility.hpp`, `src/feasibility.cpp`,
`tests/test_feasibility.cpp`, and the `ppmi feasibility` command.

**Purpose.** Decide in advance whether a box/grid/redshift can find the target
halo, and the inverse (required `levelmin` for a mass at a redshift).

**Data flow.** From `filter_gen`'s bank (smallest radius `1.65·cellsize`, largest
`Rsmooth_max`, ratio 1.15) it maps radii to masses, then a Sheth-Tormen count.

**Units.** M_sun for mass, Mpc/h for box and radii, redshift.

**Conventions and gotchas.**
- The estimate is **order of magnitude only** (factor two to five against a
  Boltzmann code); the output says so. It is enough to separate "thousands" from
  "none", not to predict a count.
- The `Cosmology` struct carries no `Omega_b`, so the transfer function uses the
  zero-baryon Eisenstein-Hu limit.
