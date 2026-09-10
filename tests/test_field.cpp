// Tests for the raw field layout MUSIC writes and PeakPatch reads.
//
// The layout is tile-blocked, not a single global cube. See the long comment
// at the top of include/ppmi/field.hpp: readsubbox()'s active code path reads
// at offset 4*(ibox-1)*nmesh^3 with F dimensioned nmesh^3, so the file is a
// sequence of ntile^3 per-tile blocks. Writing a global cube instead would be
// read as garbage for any ntile > 1, and would silently appear to work at
// ntile == 1 because the two layouts coincide there. These tests exercise
// ntile > 1 specifically for that reason.

#include "ppmi/field.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ppmi_test.hpp"

using namespace ppmi;

namespace {

std::string tmp(const char* stem) {
  return std::string("/tmp/ppmi_test_field_") + stem + ".bin";
}

// A global cube whose value encodes its own index, so a transposition or an
// off-by-one is visible in the value itself.
std::vector<float> ramp(int n) {
  std::vector<float> g((size_t)n * n * n);
  for (int k = 0; k < n; ++k)
    for (int j = 0; j < n; ++j)
      for (int i = 0; i < n; ++i)
        g[(size_t)i + (size_t)n * (j + (size_t)n * k)] =
            (float)(i + 100 * j + 10000 * k);
  return g;
}

}  // namespace

// --- tile indexing ---------------------------------------------------------

TEST("tile index: x varies fastest, matching make_boxes") {
  PeakPatchGrid g{40, 4, 2, 100.0};  // nsub 32, core 64, 8 tiles
  CHECK_EQ(g.n_tiles(), (long long)8);
  CHECK_EQ(tile_index(g, 1, 1, 1), (long long)1);
  CHECK_EQ(tile_index(g, 2, 1, 1), (long long)2);
  CHECK_EQ(tile_index(g, 1, 2, 1), (long long)3);
  CHECK_EQ(tile_index(g, 1, 1, 2), (long long)5);
  CHECK_EQ(tile_index(g, 2, 2, 2), (long long)8);
}

TEST("tile index: coords and index are inverse over the whole box") {
  PeakPatchGrid g{40, 4, 3, 100.0};
  for (std::int64_t ibox = 1; ibox <= g.n_tiles(); ++ibox) {
    int k1, k2, k3;
    tile_coords(g, ibox, k1, k2, k3);
    CHECK_EQ(tile_index(g, k1, k2, k3), ibox);
  }
}

TEST("tile index: out of range is an error") {
  PeakPatchGrid g{40, 4, 2, 100.0};
  CHECK_THROWS(tile_offset(g, 0), FieldError);
  CHECK_THROWS(tile_offset(g, 9), FieldError);
}

TEST("tile offset: blocks are contiguous and nmesh^3 apart") {
  PeakPatchGrid g{40, 4, 2, 100.0};
  std::int64_t block = (std::int64_t)40 * 40 * 40 * 4;
  CHECK_EQ(tile_offset(g, 1), (long long)0);
  CHECK_EQ(tile_offset(g, 2), (long long)block);
  CHECK_EQ(tile_offset(g, 8), (long long)(7 * block));
  CHECK_EQ(field_file_bytes(g), (long long)(8 * block));
}

TEST("field size: a single-tile grid is one block") {
  PeakPatchGrid g{64, 0, 1, 10.0};
  CHECK_EQ(field_file_bytes(g), (long long)64 * 64 * 64 * 4);
}

// --- tiling round trip -----------------------------------------------------

TEST("tiling: global to tiles and back is exact, single tile") {
  PeakPatchGrid g{32, 0, 1, 100.0};
  std::vector<float> in = ramp(32);
  std::string p = tmp("rt1");
  tile_from_global(g, in.data(), p);
  CHECK_EQ(field_file_bytes(g), (long long)32 * 32 * 32 * 4);

  std::vector<float> out((size_t)32 * 32 * 32, -1.0f);
  global_from_tiles(g, p, out.data());
  for (size_t i = 0; i < in.size(); ++i) CHECK_EQ(out[i], in[i]);
}

TEST("tiling: global to tiles and back is exact, many tiles with buffers") {
  // core 64 split into 4x4x4 tiles of 16, each with a 4-cell buffer.
  PeakPatchGrid g{24, 4, 4, 100.0};
  CHECK_EQ(g.nsub(), 16);
  CHECK_EQ(g.core_grid(), 64);

  std::vector<float> in = ramp(64);
  std::string p = tmp("rtN");
  tile_from_global(g, in.data(), p);

  std::vector<float> out((size_t)64 * 64 * 64, -1.0f);
  global_from_tiles(g, p, out.data());
  for (size_t i = 0; i < in.size(); ++i) CHECK_EQ(out[i], in[i]);
}

TEST("tiling: buffer cells hold the periodic image of the neighbouring tile") {
  PeakPatchGrid g{24, 4, 4, 100.0};
  std::vector<float> in = ramp(64);
  std::string p = tmp("buf");
  tile_from_global(g, in.data(), p);

  TileReader r(p, g);
  std::vector<float> block((size_t)24 * 24 * 24);
  r.read_tile(tile_index(g, 1, 1, 1), block.data());

  const int nm = 24, nb = 4, ns = 16;
  // Interior: block cell (nb+a, nb+b, nb+c) is global (a,b,c) for tile 1.
  auto at = [&](int i, int j, int k) {
    return block[(size_t)i + (size_t)nm * (j + (size_t)nm * k)];
  };
  auto glob = [&](int i, int j, int k) {
    i = ((i % 64) + 64) % 64; j = ((j % 64) + 64) % 64; k = ((k % 64) + 64) % 64;
    return in[(size_t)i + 64 * ((size_t)j + 64 * (size_t)k)];
  };
  CHECK_EQ(at(nb, nb, nb), glob(0, 0, 0));
  CHECK_EQ(at(nb + ns - 1, nb, nb), glob(ns - 1, 0, 0));
  // Low buffer wraps to the far side of the box.
  CHECK_EQ(at(0, nb, nb), glob(-nb, 0, 0));
  CHECK_EQ(at(nb, 0, nb), glob(0, -nb, 0));
  CHECK_EQ(at(nb, nb, 0), glob(0, 0, -nb));
  // High buffer reaches into the next tile.
  CHECK_EQ(at(nm - 1, nb, nb), glob(ns + nb - 1, 0, 0));
  // A corner exercises all three axes at once.
  CHECK_EQ(at(0, 0, 0), glob(-nb, -nb, -nb));
}

TEST("tiling: neighbouring tiles agree on the cells they share") {
  PeakPatchGrid g{24, 4, 4, 100.0};
  std::vector<float> in = ramp(64);
  std::string p = tmp("share");
  tile_from_global(g, in.data(), p);

  TileReader r(p, g);
  const int nm = 24, nb = 4, ns = 16;
  std::vector<float> a((size_t)nm * nm * nm), b((size_t)nm * nm * nm);
  r.read_tile(tile_index(g, 1, 1, 1), a.data());
  r.read_tile(tile_index(g, 2, 1, 1), b.data());
  auto at = [&](std::vector<float>& v, int i, int j, int k) {
    return v[(size_t)i + (size_t)nm * (j + (size_t)nm * k)];
  };
  // Tile 1's high x buffer is tile 2's low x core, cell for cell.
  for (int d = 0; d < nb; ++d)
    for (int j = nb; j < nb + ns; ++j)
      CHECK_EQ(at(a, nb + ns + d, j, nb), at(b, nb + d, j, nb));
}

// --- writer safety ---------------------------------------------------------

TEST("writer: a missing tile is an error, not a silent hole of zeros") {
  PeakPatchGrid g{24, 4, 2, 100.0};
  std::string p = tmp("hole");
  std::vector<float> block((size_t)24 * 24 * 24, 1.0f);
  TileWriter w(p, g);
  for (std::int64_t ibox = 1; ibox < g.n_tiles(); ++ibox)
    w.write_tile(ibox, block.data());
  // One tile short.
  CHECK_THROWS(w.finish(), FieldError);
}

TEST("writer: tiles may be written out of order") {
  PeakPatchGrid g{24, 4, 2, 100.0};
  std::string p = tmp("order");
  std::vector<float> block((size_t)24 * 24 * 24);
  TileWriter w(p, g);
  for (std::int64_t ibox = g.n_tiles(); ibox >= 1; --ibox) {
    for (auto& v : block) v = (float)ibox;
    w.write_tile(ibox, block.data());
  }
  w.finish();

  TileReader r(p, g);
  std::vector<float> got((size_t)24 * 24 * 24);
  r.read_tile(3, got.data());
  CHECK_EQ(got[0], 3.0f);
}

// --- the spike, which is what P3-T3 actually runs --------------------------

TEST("spike: lands at the requested global index and nowhere else") {
  PeakPatchGrid g{24, 4, 4, 100.0};  // core 64
  std::string p = tmp("spike");
  // Deliberately asymmetric so a transposition is visible.
  const int si = 8, sj = 16, sk = 32;
  write_spike_field(p, g, si, sj, sk, 1000.0f);

  std::vector<float> out((size_t)64 * 64 * 64, -1.0f);
  global_from_tiles(g, p, out.data());
  for (int k = 0; k < 64; ++k)
    for (int j = 0; j < 64; ++j)
      for (int i = 0; i < 64; ++i) {
        float v = out[(size_t)i + 64 * ((size_t)j + 64 * (size_t)k)];
        if (i == si && j == sj && k == sk) CHECK_EQ(v, 1000.0f);
        else CHECK_EQ(v, 0.0f);
      }
}

TEST("spike: appears in the buffers of neighbouring tiles too") {
  // A spike inside one tile's core must show up in any neighbour whose buffer
  // reaches it, because that is how PeakPatch sees across tile boundaries.
  PeakPatchGrid g{24, 4, 4, 100.0};
  std::string p = tmp("spikebuf");
  write_spike_field(p, g, 16, 0, 0, 7.0f);  // first cell of tile (2,1,1)

  TileReader r(p, g);
  std::vector<float> block((size_t)24 * 24 * 24);
  const int nm = 24, nb = 4, ns = 16;
  auto at = [&](int i, int j, int k) {
    return block[(size_t)i + (size_t)nm * (j + (size_t)nm * k)];
  };
  // Tile (2,1,1) holds it in its core.
  r.read_tile(tile_index(g, 2, 1, 1), block.data());
  CHECK_EQ(at(nb, nb, nb), 7.0f);
  // Tile (1,1,1) holds it in its high x buffer.
  r.read_tile(tile_index(g, 1, 1, 1), block.data());
  CHECK_EQ(at(nb + ns, nb, nb), 7.0f);
}

TEST("uniform displacement: fills one axis and leaves the others alone") {
  PeakPatchGrid g{16, 0, 1, 100.0};
  std::string px = tmp("dispx");
  write_uniform_displacement(px, g, 0, 2.5f);
  std::vector<float> out((size_t)16 * 16 * 16, -1.0f);
  global_from_tiles(g, px, out.data());
  for (float v : out) CHECK_EQ(v, 2.5f);
}

// --- statistics ------------------------------------------------------------

TEST("stats: computed over core cells only, so buffers are not double counted") {
  PeakPatchGrid g{24, 4, 4, 100.0};
  std::vector<float> in((size_t)64 * 64 * 64, 3.0f);
  std::string p = tmp("stats");
  tile_from_global(g, in.data(), p);

  FieldStats s = field_stats(p, g);
  CHECK_EQ(s.n_cells, (long long)64 * 64 * 64);
  CHECK_CLOSE(s.mean, 3.0, 1e-6);
  CHECK_CLOSE(s.stddev, 0.0, 1e-6);
  CHECK_EQ(s.min, 3.0);
  CHECK_EQ(s.max, 3.0);
  CHECK_EQ(s.n_nan, (long long)0);
  CHECK_EQ(s.n_inf, (long long)0);
}

TEST("stats: a zero-mean ramp has the variance it should") {
  PeakPatchGrid g{16, 0, 1, 100.0};
  const int n = 16;
  std::vector<float> in((size_t)n * n * n);
  double sum = 0.0;
  for (size_t i = 0; i < in.size(); ++i) { in[i] = (float)(i % 7) - 3.0f; sum += in[i]; }
  double mean = sum / in.size();
  double var = 0.0;
  for (float v : in) var += (v - mean) * (v - mean);
  var /= in.size();

  std::string p = tmp("ramp");
  tile_from_global(g, in.data(), p);
  FieldStats s = field_stats(p, g);
  CHECK_CLOSE(s.mean, mean, 1e-5);
  CHECK_CLOSE(s.stddev, std::sqrt(var), 1e-5);
}

TEST("stats: NaN and infinity are counted, not propagated into the mean") {
  PeakPatchGrid g{8, 0, 1, 10.0};
  std::vector<float> in((size_t)8 * 8 * 8, 1.0f);
  in[0] = std::nanf("");
  in[1] = INFINITY;
  std::string p = tmp("nan");
  tile_from_global(g, in.data(), p);
  FieldStats s = field_stats(p, g);
  CHECK_EQ(s.n_nan, (long long)1);
  CHECK_EQ(s.n_inf, (long long)1);
  CHECK(!std::isnan(s.mean));
  CHECK_CLOSE(s.mean, 1.0, 1e-6);
}

// --- sidecar ---------------------------------------------------------------

TEST("meta: the sidecar round trips, including the sign convention") {
  FieldMeta m;
  m.core_grid = 512; m.nmesh = 288; m.nbuff = 16; m.ntile = 2;
  m.boxsize = 1248.0; m.music_level = 9; m.redshift = 50.0;
  m.eta_convention = "negated_displacement";
  m.source = "music_mpi d49f3d2";
  FieldMeta back = FieldMeta::from_ini(m.to_ini());
  CHECK_EQ(back.core_grid, 512);
  CHECK_EQ(back.nmesh, 288);
  CHECK_EQ(back.ntile, 2);
  CHECK_CLOSE(back.boxsize, 1248.0, 1e-12);
  CHECK_CLOSE(back.redshift, 50.0, 1e-12);
  CHECK(back.eta_convention == "negated_displacement");
  CHECK(back.byte_order == "little");
}

// --- P3-T1: the raw unpadded core field MUSIC writes ------------------------

// MUSIC's output_peakpatch plugin writes the unpadded core grid, one
// nmesh^3 float32 block in Fortran order, with nbuff=0 and ntile=1. This is
// the input that P3-T2 pads into PeakPatch's buffered tiling. The mechanical
// acceptance checks (exact size, no NaN/inf, zero-mean density, positive
// spread) are pinned here on a synthetic field; the integration run against a
// real MUSIC survey is recorded in plan/PHASE3_STATUS.md.
TEST("P3-T1: a raw core field is one clean block with finite, zero-mean stats") {
  const int n = 16;
  PeakPatchGrid g{n, 0, 1, 100.0};  // nbuff=0, ntile=1 -> nmesh == core == n
  std::string p = tmp("p3t1_raw");

  std::vector<float> cube((size_t)n * n * n);
  for (int k = 0; k < n; ++k)
    for (int j = 0; j < n; ++j)
      for (int i = 0; i < n; ++i)
        cube[(size_t)i + (size_t)n * (j + (size_t)n * k)] =
            (float)(i + j + k) - 1.5f * (n - 1);

  TileWriter w(p, g);
  w.write_tile(1, cube.data());
  w.finish();

  CHECK_EQ(field_file_bytes(g),
           (long long)((size_t)n * n * n * sizeof(float)));
  FieldStats s = field_stats(p, g);
  CHECK_EQ(s.n_cells, (long long)((size_t)n * n * n));
  CHECK_EQ(s.n_nan, (long long)0);
  CHECK_EQ(s.n_inf, (long long)0);
  CHECK_CLOSE(s.mean, 0.0, 1e-6);
  CHECK(s.stddev > 0.0);
  std::remove(p.c_str());
}

int main() { return ppmi_test::run_all(); }
