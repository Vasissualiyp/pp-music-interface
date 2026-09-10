// Tests for the filter bank and the mass range it implies.
//
// Numbers come from peakpatch/src/filter_generator/filter_gen.f90 lines 56-96.

#include "ppmi/feasibility.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

#include "ppmi_test.hpp"

using namespace ppmi;

TEST("filter bank: starts at 1.65 cells and climbs by 1.15") {
  std::vector<double> r = filter_bank(0.5, 20.0);
  CHECK(r.size() > 2);
  CHECK_CLOSE(r[0], 1.65 * 0.5, 1e-12);
  CHECK_CLOSE(r[1] / r[0], 1.15, 1e-12);
  for (size_t i = 1; i < r.size(); ++i) CHECK(r[i] > r[i - 1]);
}

TEST("filter bank: the last radius reaches Rsmooth_max") {
  std::vector<double> r = filter_bank(0.5, 20.0);
  CHECK_CLOSE(r.back(), 20.0, 1e-9);
}

TEST("filter bank: an unreachable Rsmooth_max is an error, not an empty bank") {
  // Smallest radius is 1.65*1.0 = 1.65, above the requested maximum.
  CHECK_THROWS(filter_bank(1.0, 1.0), std::invalid_argument);
}

TEST("filter bank: a finer grid gives more scales over the same range") {
  CHECK(filter_bank(0.25, 20.0).size() > filter_bank(0.5, 20.0).size());
}

TEST("mass range: the ends match halo_mass at the end radii") {
  PeakPatchGrid g{288, 16, 1, 100.0};  // cellsize 100/256
  Cosmology c{0.3099, 0.6774};
  MassRange mr = mass_range(g, c, 20.0);
  CHECK_CLOSE(mr.cellsize, 100.0 / 256.0, 1e-12);
  CHECK_CLOSE(mr.m_min, halo_mass(1.65 * mr.cellsize, c), 1e-9);
  CHECK_CLOSE(mr.m_max, halo_mass(20.0, c), 1e-9);
  CHECK_EQ(mr.n_filters, (int)filter_bank(mr.cellsize, 20.0).size());
  CHECK(mr.m_max > mr.m_min);
}

TEST("mass range: doubling the resolution drops the minimum mass eightfold") {
  Cosmology c{0.3099, 0.6774};
  PeakPatchGrid coarse{288, 16, 1, 100.0};   // core 256
  PeakPatchGrid fine{544, 16, 1, 100.0};     // core 512
  CHECK_EQ(fine.core_grid(), 512);
  MassRange a = mass_range(coarse, c, 20.0);
  MassRange b = mass_range(fine, c, 20.0);
  CHECK_CLOSE(a.m_min / b.m_min, 8.0, 1e-6);
}

TEST("required_levelmin: resolves the mass it was asked for") {
  Cosmology c{0.3099, 0.6774};
  double target = 1.0e10;
  int lm = required_levelmin(100.0, target, c, 1, 0);
  PeakPatchGrid g{1 << lm, 0, 1, 100.0};
  MassRange mr = mass_range(g, c, 20.0);
  CHECK(mr.m_min <= target);
  // And one level coarser would not have.
  PeakPatchGrid coarser{1 << (lm - 1), 0, 1, 100.0};
  CHECK(mass_range(coarser, c, 20.0).m_min > target);
}

TEST("required_levelmin: respects the tile count") {
  Cosmology c{0.3099, 0.6774};
  int lm = required_levelmin(1000.0, 1.0e11, c, 8, 32);
  CHECK((1 << lm) % 8 == 0);
}

TEST("expected halo count: falls with mass and with redshift") {
  Cosmology c{0.3099, 0.6774};
  double n_low = expected_halo_count(1000.0, 1e13, 0.0, c, 0.8159, 0.9667);
  double n_high = expected_halo_count(1000.0, 1e15, 0.0, c, 0.8159, 0.9667);
  CHECK(n_low > n_high);
  double n_z20 = expected_halo_count(1000.0, 1e13, 20.0, c, 0.8159, 0.9667);
  CHECK(n_low > n_z20);
  CHECK(n_z20 >= 0.0);
}

TEST("expected halo count: scales with volume") {
  Cosmology c{0.3099, 0.6774};
  double a = expected_halo_count(100.0, 1e12, 0.0, c, 0.8159, 0.9667);
  double b = expected_halo_count(200.0, 1e12, 0.0, c, 0.8159, 0.9667);
  CHECK_CLOSE(b / a, 8.0, 1e-6);
}

TEST("report: mentions the verdict, the mass range and the cell size") {
  Cosmology c{0.3099, 0.6774};
  std::string s = feasibility_report(100.0, 8, 20.0, c, 0.8159, 0.9667, 1, 16, 20.0);
  CHECK(!s.empty());
  CHECK(s.find("cell") != std::string::npos);
}

int main() { return ppmi_test::run_all(); }
