// Tests for grid pairing and the two coordinate frames.
//
// The numbers here are not invented. The grid relations come from
// config_reader.f90 lines 802-810, and the two "invalid" cases are the configs
// actually shipped in param/, which is why those tests exist: they are
// regression guards against the incorrect nmesh == 2^levelmin rule in the
// interface README.

#include "ppmi/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "ppmi_test.hpp"

using namespace ppmi;

// --- grid relations --------------------------------------------------------

TEST("grid: derived quantities match config_reader.f90") {
  PeakPatchGrid g{288, 16, 1, 100.0};
  g.validate();
  CHECK_EQ(g.nsub(), 256);            // nmesh - 2*nbuff
  CHECK_EQ(g.core_grid(), 256);       // nsub * ntile
  CHECK_EQ(g.n_ext(), 288);           // nsub*ntile + 2*nbuff
  CHECK_EQ(g.n_tiles(), (long long)1);
  CHECK_CLOSE(g.dcore_box(), 100.0, 1e-12);
  CHECK_CLOSE(g.cellsize(), 100.0 / 256.0, 1e-12);
  CHECK_CLOSE(g.buffersize(), 16.0 * 100.0 / 256.0, 1e-12);
  CHECK_CLOSE(g.dL_box(), 100.0 + 2.0 * 16.0 * 100.0 / 256.0, 1e-12);
  CHECK_EQ(g.music_levelmin(), 8);
}

TEST("grid: multiple tiles shrink the cell and the core stays whole") {
  // 512 core cells split over 2 tiles per side.
  PeakPatchGrid g{288, 16, 2, 100.0};
  CHECK_EQ(g.nsub(), 256);
  CHECK_EQ(g.core_grid(), 512);
  CHECK_EQ(g.n_ext(), 512 + 32);
  CHECK_EQ(g.n_tiles(), (long long)8);
  CHECK_CLOSE(g.dcore_box(), 50.0, 1e-12);
  CHECK_CLOSE(g.cellsize(), 50.0 / 256.0, 1e-12);
  CHECK_EQ(g.music_levelmin(), 9);
}

TEST("grid: zero buffer and one tile is the degenerate case") {
  PeakPatchGrid g{64, 0, 1, 10.0};
  CHECK_EQ(g.nsub(), 64);
  CHECK_EQ(g.core_grid(), 64);
  CHECK_EQ(g.n_ext(), 64);
  CHECK_EQ(g.music_levelmin(), 6);
}

TEST("grid: invalid geometries are rejected, not silently accepted") {
  CHECK_THROWS(PeakPatchGrid({32, 16, 1, 10.0}).validate(), GeometryError);
  CHECK_THROWS(PeakPatchGrid({0, 0, 1, 10.0}).validate(), GeometryError);
  CHECK_THROWS(PeakPatchGrid({64, 0, 0, 10.0}).validate(), GeometryError);
  CHECK_THROWS(PeakPatchGrid({64, 0, 1, 0.0}).validate(), GeometryError);
  CHECK_THROWS(PeakPatchGrid({64, -1, 1, 10.0}).validate(), GeometryError);
}

TEST("grid: a core grid that is not a power of two has no MUSIC level") {
  PeakPatchGrid g{300, 0, 1, 100.0};  // core 300
  CHECK_THROWS(g.music_levelmin(), GeometryError);
}

// --- solve_nmesh -----------------------------------------------------------

TEST("solve_nmesh: inverts the pairing rule") {
  CHECK_EQ(solve_nmesh(8, 1, 16), 288);
  CHECK_EQ(solve_nmesh(9, 2, 16), 288);
  CHECK_EQ(solve_nmesh(6, 1, 0), 64);
  CHECK_EQ(solve_nmesh(10, 8, 32), 128 + 64);
}

TEST("solve_nmesh: round trips through the grid it describes") {
  for (int levelmin : {6, 7, 8, 9, 10}) {
    for (int ntile : {1, 2, 4}) {
      for (int nbuff : {0, 8, 16}) {
        if ((1 << levelmin) % ntile != 0) continue;
        int nmesh = solve_nmesh(levelmin, ntile, nbuff);
        PeakPatchGrid g{nmesh, nbuff, ntile, 100.0};
        CHECK(check_pairing(levelmin, g));
        CHECK_EQ(g.music_levelmin(), levelmin);
      }
    }
  }
}

TEST("solve_nmesh: refuses when no integer solution exists") {
  // 2^8 = 256 is not divisible by 3.
  CHECK_THROWS(solve_nmesh(8, 3, 0), GeometryError);
  CHECK_THROWS(solve_nmesh(4, 32, 0), GeometryError);
}

// --- the shipped configs are wrong, and must stay caught -------------------

TEST("shipped param/parameters.ini fails the pairing rule") {
  // nmesh=364, nbuff=64, ntile=8 with levelmin=9.
  PeakPatchGrid g{364, 64, 8, 1248.0};
  CHECK_EQ(g.nsub(), 236);
  CHECK_EQ(g.core_grid(), 1888);
  CHECK(!check_pairing(9, g));
  CHECK(!describe_mismatch(9, g).empty());
  // 1888 is not a power of two either, so it pairs with no level at all.
  CHECK_THROWS(g.music_levelmin(), GeometryError);
}

TEST("shipped param/debug.ini fails the pairing rule") {
  // nmesh=368, nbuff=64, ntile=8 with levelmin=6.
  PeakPatchGrid g{368, 64, 8, 10.0};
  CHECK_EQ(g.nsub(), 240);
  CHECK_EQ(g.core_grid(), 1920);
  CHECK(!check_pairing(6, g));
  CHECK_THROWS(g.music_levelmin(), GeometryError);
}

TEST("describe_mismatch is empty exactly when the pairing holds") {
  PeakPatchGrid good{288, 16, 1, 100.0};
  CHECK(describe_mismatch(8, good).empty());
  CHECK(!describe_mismatch(9, good).empty());
}

// --- coordinate frames -----------------------------------------------------

TEST("frame: the observer sits at the centre of MUSIC's unit box") {
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  Vec3 u = f.to_unit({0.0, 0.0, 0.0});
  CHECK_CLOSE(u[0], 0.5, 1e-12);
  CHECK_CLOSE(u[1], 0.5, 1e-12);
  CHECK_CLOSE(u[2], 0.5, 1e-12);
}

TEST("frame: the low corner maps to the origin of the unit box") {
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  Vec3 u = f.to_unit({-50.0, -50.0, -50.0});
  CHECK_CLOSE(u[0], 0.0, 1e-12);
  CHECK_CLOSE(u[1], 0.0, 1e-12);
  CHECK_CLOSE(u[2], 0.0, 1e-12);
}

TEST("frame: axes are independent, so a transposition would show") {
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  Vec3 u = f.to_unit({25.0, 0.0, -25.0});
  CHECK_CLOSE(u[0], 0.75, 1e-12);
  CHECK_CLOSE(u[1], 0.50, 1e-12);
  CHECK_CLOSE(u[2], 0.25, 1e-12);
}

TEST("frame: the high face wraps to zero, not to one") {
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  Vec3 u = f.to_unit({50.0, 50.0, 50.0});
  CHECK(u[0] >= 0.0 && u[0] < 1.0);
  CHECK_CLOSE(u[0], 0.0, 1e-9);
}

TEST("frame: a non-zero observer position shifts the whole box") {
  BoxFrame f{100.0, {10.0, -20.0, 5.0}};
  Vec3 u = f.to_unit({10.0, -20.0, 5.0});
  CHECK_CLOSE(u[0], 0.5, 1e-12);
  CHECK_CLOSE(u[1], 0.5, 1e-12);
  CHECK_CLOSE(u[2], 0.5, 1e-12);
}

TEST("frame: to_unit and from_unit are inverse") {
  BoxFrame f{1248.0, {0.0, 0.0, 0.0}};
  Vec3 x{123.4, -456.7, 89.0};
  Vec3 back = f.from_unit(f.to_unit(x));
  for (int i = 0; i < 3; ++i) CHECK_CLOSE(back[i], x[i], 1e-10);
}

TEST("frame: length_to_unit is a plain box fraction") {
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  CHECK_CLOSE(f.length_to_unit(10.0), 0.1, 1e-12);
  CHECK_CLOSE(f.length_to_unit(100.0), 1.0, 1e-12);
}

// --- halo to refinement region --------------------------------------------

TEST("halo_to_ref: centre is the Lagrangian position, extent scales with r") {
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  RefRegion ref = halo_to_ref({0.0, 0.0, 0.0}, 1.0, f, 3.0);
  CHECK_CLOSE(ref.center[0], 0.5, 1e-12);
  CHECK_CLOSE(ref.center[1], 0.5, 1e-12);
  CHECK_CLOSE(ref.center[2], 0.5, 1e-12);
  // Full width is 2*r*extent_factor = 6 Mpc in a 100 Mpc box.
  for (int i = 0; i < 3; ++i) CHECK_CLOSE(ref.extent[i], 0.06, 1e-12);
}

TEST("halo_to_ref: extent_factor is linear") {
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  RefRegion a = halo_to_ref({0.0, 0.0, 0.0}, 1.0, f, 2.0);
  RefRegion b = halo_to_ref({0.0, 0.0, 0.0}, 1.0, f, 4.0);
  CHECK_CLOSE(b.extent[0], 2.0 * a.extent[0], 1e-12);
}

TEST("halo_to_ref: a region crossing a face is refused, never clamped") {
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  // Halo 0.1 Mpc from the low x face, patch half-width 3 Mpc.
  CHECK_THROWS(halo_to_ref({-49.9, 0.0, 0.0}, 1.0, f, 3.0), RegionWrapsBox);
  // And at the high face.
  CHECK_THROWS(halo_to_ref({49.9, 0.0, 0.0}, 1.0, f, 3.0), RegionWrapsBox);
  // A region that is entirely inside is fine, even close to the face.
  RefRegion ok = halo_to_ref({-45.0, 0.0, 0.0}, 1.0, f, 3.0);
  CHECK(ok.center[0] > 0.0);
}

TEST("halo_to_ref: a region larger than the box is refused") {
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  CHECK_THROWS(halo_to_ref({0.0, 0.0, 0.0}, 30.0, f, 3.0), RegionWrapsBox);
}

// --- formatting ------------------------------------------------------------

TEST("format_triple: matches what MUSIC's sscanf accepts") {
  std::string s = format_triple({0.5, 0.25, 0.125});
  // Comma separated, no spaces, no brackets.
  CHECK(s.find(' ') == std::string::npos);
  CHECK(s.find('[') == std::string::npos);
  CHECK_EQ((int)std::count(s.begin(), s.end(), ','), 2);
  double a = 0, b = 0, c = 0;
  CHECK_EQ(std::sscanf(s.c_str(), "%lf,%lf,%lf", &a, &b, &c), 3);
  CHECK_CLOSE(a, 0.5, 1e-15);
  CHECK_CLOSE(b, 0.25, 1e-15);
  CHECK_CLOSE(c, 0.125, 1e-15);
}

TEST("format_triple: a round trip is lossless at double precision") {
  Vec3 v{0.1234567890123456, 0.9876543210987654, 0.5555555555555555};
  std::string s = format_triple(v);
  double a = 0, b = 0, c = 0;
  std::sscanf(s.c_str(), "%lf,%lf,%lf", &a, &b, &c);
  CHECK_CLOSE(a, v[0], 1e-15);
  CHECK_CLOSE(b, v[1], 1e-15);
  CHECK_CLOSE(c, v[2], 1e-15);
}

TEST("region points: a halo patch writes a valid MUSIC point file") {
  BoxFrame frame;
  frame.boxsize = 100.0;
  Vec3 xlag = {10.0, -20.0, 5.0};
  const double r = 3.0;
  const int np = 64;
  std::string p = "/tmp/ppmi_test_region_points.txt";
  write_region_points(p, xlag, r, frame, np);

  std::ifstream f(p);
  CHECK((bool)f);
  int n = 0;
  Vec3 sum{0.0, 0.0, 0.0};
  double x = 0, y = 0, z = 0;
  while (f >> x >> y >> z) {
    CHECK(x >= 0.0 && x < 1.0);
    CHECK(y >= 0.0 && y < 1.0);
    CHECK(z >= 0.0 && z < 1.0);
    sum[0] += x; sum[1] += y; sum[2] += z;
    ++n;
  }
  CHECK_EQ(n, np);
  Vec3 expect = frame.to_unit(xlag);
  CHECK_CLOSE(sum[0] / n, expect[0], 0.02);
  CHECK_CLOSE(sum[1] / n, expect[1], 0.02);
  CHECK_CLOSE(sum[2] / n, expect[2], 0.02);
  std::remove(p.c_str());
}

TEST("region points: a patch crossing a face wraps instead of throwing") {
  BoxFrame frame;
  frame.boxsize = 100.0;
  Vec3 xlag = {49.0, 0.0, 0.0};  // within r of the +x face
  std::string p = "/tmp/ppmi_test_region_points_wrap.txt";
  write_region_points(p, xlag, 3.0, frame, 32);  // must not throw
  std::ifstream f(p);
  int n = 0;
  double x = 0, y = 0, z = 0;
  while (f >> x >> y >> z) {
    CHECK(x >= 0.0 && x < 1.0);
    ++n;
  }
  CHECK_EQ(n, 32);
  std::remove(p.c_str());
}

int main() { return ppmi_test::run_all(); }
