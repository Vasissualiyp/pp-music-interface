// Tests for .pksc I/O and halo selection.
//
// The mass constant is pinned against PeakPatch's own reference reader,
// peakpatch/python/peakpatchtools/catalogue.py lines 107-133, which builds
// rho_crit from H_0 = 100h and comments "All in units of solar masses and Mpc".
// Getting the h convention quietly wrong here would shift every mass by a
// factor of h^2 and nothing downstream would notice.

#include "ppmi/catalog.hpp"

#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ppmi_test.hpp"

using namespace ppmi;

namespace {

std::string tmp(const char* stem) {
  return std::string("/tmp/ppmi_test_") + stem + ".pksc";
}

Halo make_halo(std::int64_t i, double x, double y, double z, double r) {
  Halo h;
  h.index = i;
  h.lagrangian = {x, y, z};
  // Give it a small, axis-distinguishable displacement.
  h.eulerian = {x + 0.1, y + 0.2, z + 0.3};
  h.velocity = {10.0, 20.0, 30.0};
  h.r = r;
  h.fcoll = 1.686;
  return h;
}

void write_catalog(const std::string& path, const std::vector<Halo>& halos,
                   HeaderLayout layout, int n_fields) {
  CatalogWriter w(path, layout, n_fields, 5.0f, 20.0f);
  for (const auto& h : halos) w.write(h);
  w.finish();
}

}  // namespace

// --- mass ------------------------------------------------------------------

TEST("cosmology: rho_m matches the reference reader's convention") {
  Cosmology c{0.3099, 0.6774};
  // 2.77536627e11 * h^2 * Omega_m
  CHECK_CLOSE(c.rho_m(), 3.94668e10, 1e-4);
}

TEST("mass: a one-Mpc Lagrangian radius in a Planck-like cosmology") {
  Cosmology c{0.3099, 0.6774};
  CHECK_CLOSE(halo_mass(1.0, c), 1.65302e11, 1e-4);
}

TEST("mass: scales as the cube of the radius") {
  Cosmology c{0.3099, 0.6774};
  CHECK_CLOSE(halo_mass(2.0, c), 8.0 * halo_mass(1.0, c), 1e-12);
  CHECK_CLOSE(halo_mass(0.0, c), 0.0, 1e-12);
}

// --- round trips -----------------------------------------------------------

TEST("pksc: sentinel header round trips exactly") {
  std::vector<Halo> halos{make_halo(0, 1.0, 2.0, 3.0, 1.5),
                          make_halo(1, -4.0, 5.0, -6.0, 2.5)};
  std::string p = tmp("sentinel");
  write_catalog(p, halos, HeaderLayout::kSentinel, kFieldsBase);

  CatalogHeader h = peek_header(p);
  CHECK_EQ(h.header_bytes, kHeaderSentinelBytes);
  CHECK_EQ(h.n_halos, (long long)2);
  CHECK_EQ(h.n_fields, kFieldsBase);
  CHECK(h.layout == HeaderLayout::kSentinel);

  CatalogReader r(p);
  for (std::int64_t i = 0; i < 2; ++i) {
    Halo got = r.read_halo(i);
    // Everything on disk is float32, so compare at float precision.
    CHECK_CLOSE(got.lagrangian[0], halos[i].lagrangian[0], 1e-6);
    CHECK_CLOSE(got.lagrangian[1], halos[i].lagrangian[1], 1e-6);
    CHECK_CLOSE(got.lagrangian[2], halos[i].lagrangian[2], 1e-6);
    CHECK_CLOSE(got.eulerian[0], halos[i].eulerian[0], 1e-6);
    CHECK_CLOSE(got.r, halos[i].r, 1e-6);
    CHECK_CLOSE(got.velocity[2], halos[i].velocity[2], 1e-6);
  }
}

TEST("pksc: legacy header round trips exactly") {
  std::vector<Halo> halos{make_halo(0, 1.0, 2.0, 3.0, 1.5)};
  std::string p = tmp("legacy");
  write_catalog(p, halos, HeaderLayout::kLegacy, kFieldsBase);

  CatalogHeader h = peek_header(p);
  CHECK_EQ(h.header_bytes, kHeaderLegacyBytes);
  CHECK_EQ(h.n_halos, (long long)1);
  CHECK(h.layout == HeaderLayout::kLegacy);

  CatalogReader r(p);
  Halo got = r.read_halo(0);
  CHECK_CLOSE(got.lagrangian[0], 1.0, 1e-6);
}

TEST("pksc: the shear layout is read, with the first ten fields unmoved") {
  std::vector<Halo> halos{make_halo(0, 7.0, 8.0, 9.0, 3.0)};
  std::string p = tmp("shear");
  write_catalog(p, halos, HeaderLayout::kSentinel, kFieldsShear);
  CatalogHeader h = peek_header(p);
  CHECK_EQ(h.n_fields, kFieldsShear);
  CatalogReader r(p);
  Halo got = r.read_halo(0);
  CHECK_CLOSE(got.lagrangian[0], 7.0, 1e-6);
  CHECK_CLOSE(got.r, 3.0, 1e-6);
}

TEST("pksc: Lagrangian and Eulerian positions do not get swapped") {
  // This is the whole point of the format. If a reader confuses the two, the
  // zoom is centred on a displaced position and the halo is off-centre by
  // several Mpc with nothing to signal it.
  std::vector<Halo> halos{make_halo(0, 0.0, 0.0, 0.0, 1.0)};
  std::string p = tmp("lagvseul");
  write_catalog(p, halos, HeaderLayout::kSentinel, kFieldsBase);
  CatalogReader r(p);
  Halo got = r.read_halo(0);
  CHECK_CLOSE(got.lagrangian[0], 0.0, 1e-6);
  CHECK_CLOSE(got.eulerian[0], 0.1, 1e-6);
  CHECK_CLOSE(got.eulerian[1], 0.2, 1e-6);
  CHECK_CLOSE(got.eulerian[2], 0.3, 1e-6);
}

TEST("pksc: read_chunk agrees with read_halo") {
  std::vector<Halo> halos;
  for (int i = 0; i < 50; ++i) halos.push_back(make_halo(i, i, -i, 2 * i, 1 + i));
  std::string p = tmp("chunk");
  write_catalog(p, halos, HeaderLayout::kSentinel, kFieldsBase);

  CatalogReader r(p);
  std::vector<Halo> chunk;
  std::int64_t got = r.read_chunk(10, 20, chunk);
  CHECK_EQ(got, (long long)20);
  CHECK_EQ((long long)chunk.size(), (long long)20);
  for (int i = 0; i < 20; ++i) CHECK_CLOSE(chunk[i].r, 1.0 + (10 + i), 1e-6);

  // Reading past the end returns what exists, not an error.
  CHECK_EQ(r.read_chunk(45, 20, chunk), (long long)5);
  CHECK_EQ(r.read_chunk(50, 20, chunk), (long long)0);
}

TEST("pksc: an empty catalogue is legal and reads as empty") {
  std::string p = tmp("empty");
  write_catalog(p, {}, HeaderLayout::kSentinel, kFieldsBase);
  CHECK_EQ(peek_header(p).n_halos, (long long)0);
}

// --- corruption ------------------------------------------------------------

TEST("pksc: a truncated file raises rather than returning garbage") {
  std::vector<Halo> halos{make_halo(0, 1, 2, 3, 1), make_halo(1, 4, 5, 6, 2)};
  std::string p = tmp("trunc");
  write_catalog(p, halos, HeaderLayout::kSentinel, kFieldsBase);
  // Chop the last record in half.
  std::FILE* f = std::fopen(p.c_str(), "rb");
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fclose(f);
  CHECK(truncate(p.c_str(), size - 22) == 0);
  CHECK_THROWS(peek_header(p), CatalogError);
}

TEST("pksc: a body matching no known field count raises") {
  std::string p = tmp("badlayout");
  // Sentinel header claiming 1 halo, followed by 7 floats: neither 11 nor 33.
  std::FILE* f = std::fopen(p.c_str(), "wb");
  std::int32_t sentinel = -1;
  std::int64_t n = 1;
  float rmax = 1.0f, z = 0.0f;
  std::fwrite(&sentinel, 4, 1, f);
  std::fwrite(&n, 8, 1, f);
  std::fwrite(&rmax, 4, 1, f);
  std::fwrite(&z, 4, 1, f);
  float junk[7] = {0};
  std::fwrite(junk, 4, 7, f);
  std::fclose(f);
  CHECK_THROWS(peek_header(p), CatalogError);
}

// --- selection -------------------------------------------------------------

namespace {

// Five halos of increasing radius, well separated along x, all far from the
// faces of a 100 Mpc box.
std::string five_halo_catalog() {
  std::vector<Halo> halos;
  double xs[5] = {-30.0, -15.0, 0.0, 15.0, 30.0};
  for (int i = 0; i < 5; ++i)
    halos.push_back(make_halo(i, xs[i], 0.0, 0.0, 1.0 + i));
  std::string p = tmp("five");
  write_catalog(p, halos, HeaderLayout::kSentinel, kFieldsBase);
  return p;
}

}  // namespace

TEST("select: mass rank 1 is the largest halo") {
  std::string p = five_halo_catalog();
  Cosmology c{0.3099, 0.6774};
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  Criterion crit;
  crit.kind = CriterionKind::kMassRank;
  crit.rank = 1;
  Selection s = select(p, c, f, crit);
  CHECK_CLOSE(s.halo.r, 5.0, 1e-6);
  CHECK_CLOSE(s.halo.lagrangian[0], 30.0, 1e-6);
  CHECK_CLOSE(s.mass, halo_mass(5.0, c), 1e-6);
  CHECK(std::isinf(s.isolation_radii));
}

TEST("select: mass rank counts down from the largest") {
  std::string p = five_halo_catalog();
  Cosmology c{0.3099, 0.6774};
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  Criterion crit;
  crit.rank = 3;
  CHECK_CLOSE(select(p, c, f, crit).halo.r, 3.0, 1e-6);
  crit.rank = 5;
  CHECK_CLOSE(select(p, c, f, crit).halo.r, 1.0, 1e-6);
}

TEST("select: a rank past the end of the catalogue raises") {
  std::string p = five_halo_catalog();
  Cosmology c{0.3099, 0.6774};
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  Criterion crit;
  crit.rank = 6;
  CHECK_THROWS(select(p, c, f, crit), NoHaloMatches);
}

TEST("select: a mass window picks the most massive inside it") {
  std::string p = five_halo_catalog();
  Cosmology c{0.3099, 0.6774};
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  Criterion crit;
  crit.kind = CriterionKind::kMassWindow;
  crit.m_min = halo_mass(1.5, c);
  crit.m_max = halo_mass(3.5, c);
  Selection s = select(p, c, f, crit);
  CHECK_CLOSE(s.halo.r, 3.0, 1e-6);
}

TEST("select: closest_to picks by log-space distance in mass") {
  std::string p = five_halo_catalog();
  Cosmology c{0.3099, 0.6774};
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  Criterion crit;
  crit.kind = CriterionKind::kMassWindow;
  crit.m_min = 0.0;
  crit.m_max = 1e30;
  crit.closest_to = halo_mass(3.9, c);
  CHECK_CLOSE(select(p, c, f, crit).halo.r, 4.0, 1e-6);
}

TEST("select: an empty mass window raises rather than returning anything") {
  std::string p = five_halo_catalog();
  Cosmology c{0.3099, 0.6774};
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  Criterion crit;
  crit.kind = CriterionKind::kMassWindow;
  crit.m_min = 1e20;
  crit.m_max = 1e21;
  CHECK_THROWS(select(p, c, f, crit), NoHaloMatches);
}

TEST("select: isolation is measured in the Lagrangian frame") {
  // A small halo sitting right next to a much larger one is contaminated.
  std::vector<Halo> halos;
  halos.push_back(make_halo(0, 0.0, 0.0, 0.0, 5.0));   // big
  halos.push_back(make_halo(1, 6.0, 0.0, 0.0, 1.0));   // small, 6 Mpc away
  halos.push_back(make_halo(2, -30.0, 0.0, 0.0, 2.0)); // small, isolated
  std::string p = tmp("isolation");
  write_catalog(p, halos, HeaderLayout::kSentinel, kFieldsBase);

  Cosmology c{0.3099, 0.6774};
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};

  // Rank 2 by mass is the r=2 halo, 30 Mpc from the big one, so 15 of its own
  // radii away.
  Criterion crit;
  crit.rank = 2;
  Selection s = select(p, c, f, crit);
  CHECK_CLOSE(s.halo.r, 2.0, 1e-6);
  CHECK_CLOSE(s.isolation_radii, 15.0, 1e-4);

  // Rank 3 is the r=1 halo, only 6 of its own radii from the big one.
  crit.rank = 3;
  CHECK_CLOSE(select(p, c, f, crit).isolation_radii, 6.0, 1e-4);

  // Demanding 10 radii of isolation skips it and lands back on the r=2 halo.
  crit.rank = 1;
  crit.min_isolation_radii = 10.0;
  Selection iso = select(p, c, f, crit);
  CHECK_CLOSE(iso.halo.r, 5.0, 1e-6);  // the biggest is still isolated
}

TEST("select: the edge guard rejects halos whose patch leaves the box") {
  std::vector<Halo> halos;
  halos.push_back(make_halo(0, 48.0, 0.0, 0.0, 3.0));  // patch crosses +x face
  halos.push_back(make_halo(1, 0.0, 0.0, 0.0, 2.0));   // safely central
  std::string p = tmp("edge");
  write_catalog(p, halos, HeaderLayout::kSentinel, kFieldsBase);

  Cosmology c{0.3099, 0.6774};
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  Criterion crit;
  crit.rank = 1;
  // Without the guard, the largest halo is the one at the edge.
  CHECK_CLOSE(select(p, c, f, crit).halo.r, 3.0, 1e-6);
  // With it, selection steps to the next candidate.
  crit.min_edge_radii = 1.0;
  CHECK_CLOSE(select(p, c, f, crit).halo.r, 2.0, 1e-6);
}

TEST("select: refuses a lightcone catalogue without a redshift") {
  std::string p = five_halo_catalog();
  Cosmology c{0.3099, 0.6774};
  BoxFrame f{100.0, {0.0, 0.0, 0.0}};
  Criterion crit;
  CHECK_THROWS(select(p, c, f, crit, /*is_lightcone=*/true), AmbiguousRedshift);
}

int main() { return ppmi_test::run_all(); }
