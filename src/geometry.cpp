// geometry.cpp - implementation of include/ppmi/geometry.hpp
//
// See the header for the derivations and file:line citations this follows.

#include "ppmi/geometry.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>

namespace ppmi {

namespace {

// Wrap a value into [0,1). floor-based fractional part; guarded against the
// case where floating-point rounding lands exactly on 1.0 (or, in principle,
// a hair below 0.0).
double wrap01(double v) {
  double w = v - std::floor(v);
  if (w >= 1.0) w -= 1.0;
  if (w < 0.0) w += 1.0;
  return w;
}

}  // namespace

// ---------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------

int PeakPatchGrid::nsub() const { return nmesh - 2 * nbuff; }

int PeakPatchGrid::core_grid() const { return nsub() * ntile; }

int PeakPatchGrid::n_ext() const { return core_grid() + 2 * nbuff; }

double PeakPatchGrid::dcore_box() const { return boxsize / ntile; }

double PeakPatchGrid::cellsize() const { return dcore_box() / nsub(); }

double PeakPatchGrid::buffersize() const { return cellsize() * nbuff; }

double PeakPatchGrid::dL_box() const { return dcore_box() + 2.0 * buffersize(); }

std::int64_t PeakPatchGrid::n_tiles() const {
  std::int64_t t = ntile;
  return t * t * t;
}

void PeakPatchGrid::validate() const {
  if (nmesh <= 0) {
    throw GeometryError("nmesh must be positive, got " + std::to_string(nmesh));
  }
  if (ntile <= 0) {
    throw GeometryError("ntile must be positive, got " + std::to_string(ntile));
  }
  if (boxsize <= 0.0) {
    throw GeometryError("boxsize must be positive, got " + std::to_string(boxsize));
  }
  if (nbuff < 0) {
    throw GeometryError("nbuff must not be negative, got " + std::to_string(nbuff));
  }
  if (nsub() < 1) {
    throw GeometryError(
        "nsub = nmesh - 2*nbuff must be at least 1, got " + std::to_string(nsub()) +
        " (nmesh=" + std::to_string(nmesh) + ", nbuff=" + std::to_string(nbuff) + ")");
  }
}

int PeakPatchGrid::music_levelmin() const {
  int cg = core_grid();
  if (cg <= 0) {
    throw GeometryError("core grid must be positive, got " + std::to_string(cg));
  }
  int level = 0;
  int v = cg;
  while (v > 1) {
    if (v % 2 != 0) {
      throw GeometryError("core grid " + std::to_string(cg) +
                           " is not a power of two, so it has no MUSIC levelmin");
    }
    v /= 2;
    ++level;
  }
  return level;
}

int solve_nmesh(int levelmin, int ntile, int nbuff) {
  std::int64_t target = std::int64_t(1) << levelmin;
  if (target % ntile != 0) {
    throw GeometryError("2^" + std::to_string(levelmin) + " (" + std::to_string(target) +
                         ") is not divisible by ntile " + std::to_string(ntile) +
                         "; no integer nmesh solves the pairing rule");
  }
  std::int64_t nsub = target / ntile;
  return static_cast<int>(nsub) + 2 * nbuff;
}

bool check_pairing(int levelmin, const PeakPatchGrid& grid) {
  std::int64_t target = std::int64_t(1) << levelmin;
  return static_cast<std::int64_t>(grid.core_grid()) == target;
}

std::string describe_mismatch(int levelmin, const PeakPatchGrid& grid) {
  if (check_pairing(levelmin, grid)) return "";

  std::int64_t target = std::int64_t(1) << levelmin;
  std::string msg = "core grid " + std::to_string(grid.core_grid()) +
                     " does not match the MUSIC grid for levelmin " +
                     std::to_string(levelmin) + " (2^" + std::to_string(levelmin) + " = " +
                     std::to_string(target) + ")";

  if (grid.ntile > 0 && target % grid.ntile == 0) {
    std::int64_t nsub = target / grid.ntile;
    int nmesh_fix = static_cast<int>(nsub) + 2 * grid.nbuff;
    msg += "; nmesh=" + std::to_string(nmesh_fix) + " (with ntile=" +
           std::to_string(grid.ntile) + ", nbuff=" + std::to_string(grid.nbuff) +
           ") would fix it";
  } else {
    msg += "; no nmesh fixes this with ntile=" + std::to_string(grid.ntile) +
           ", since 2^" + std::to_string(levelmin) + " is not divisible by it";
  }
  return msg;
}

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

Vec3 BoxFrame::to_unit(const Vec3& x) const {
  Vec3 u;
  for (int i = 0; i < 3; ++i) {
    u[i] = wrap01((x[i] - cen[i]) / boxsize + 0.5);
  }
  return u;
}

Vec3 BoxFrame::from_unit(const Vec3& u) const {
  Vec3 x;
  for (int i = 0; i < 3; ++i) {
    x[i] = (u[i] - 0.5) * boxsize + cen[i];
  }
  return x;
}

double BoxFrame::length_to_unit(double length) const { return length / boxsize; }

RefRegion halo_to_ref(const Vec3& xlag, double r, const BoxFrame& frame,
                       double extent_factor) {
  static const char* axis_names[3] = {"x", "y", "z"};

  double full_extent = frame.length_to_unit(2.0 * r * extent_factor);
  double half = full_extent / 2.0;

  Vec3 center;
  for (int i = 0; i < 3; ++i) {
    double raw = (xlag[i] - frame.cen[i]) / frame.boxsize + 0.5;
    double low = raw - half;
    double high = raw + half;
    if (low < 0.0) {
      double overrun = -low * frame.boxsize;
      char buf[256];
      std::snprintf(buf, sizeof(buf),
                     "halo_to_ref: region crosses the low %s face of the box by "
                     "%.6g length units",
                     axis_names[i], overrun);
      throw RegionWrapsBox(buf);
    }
    if (high > 1.0) {
      double overrun = (high - 1.0) * frame.boxsize;
      char buf[256];
      std::snprintf(buf, sizeof(buf),
                     "halo_to_ref: region crosses the high %s face of the box by "
                     "%.6g length units",
                     axis_names[i], overrun);
      throw RegionWrapsBox(buf);
    }
    center[i] = wrap01(raw);
  }

  return RefRegion{center, Vec3{full_extent, full_extent, full_extent}};
}

namespace {

// Shortest decimal form that still reads back bit-for-bit through strtod.
// %.17g is always lossless but prints 0.06 as 0.059999999999999998, which
// makes generated configs unpleasant to read and impossible to diff usefully.
// Walking the precision up from 1 and stopping at the first exact round trip
// gives the short form when one exists and full precision when it does not.
std::string shortest_roundtrip(double x) {
  char buf[64];
  for (int prec = 1; prec < 17; ++prec) {
    std::snprintf(buf, sizeof(buf), "%.*g", prec, x);
    if (std::strtod(buf, nullptr) == x) return std::string(buf);
  }
  std::snprintf(buf, sizeof(buf), "%.17g", x);
  return std::string(buf);
}

}  // namespace

std::string format_triple(const Vec3& v) {
  return shortest_roundtrip(v[0]) + "," + shortest_roundtrip(v[1]) + "," +
         shortest_roundtrip(v[2]);
}

void write_region_points(const std::string& path, const Vec3& xlag, double r,
                         const BoxFrame& frame, int n_points) {
  if (n_points < 4)
    throw GeometryError(
        "write_region_points: an ellipsoid needs at least four points");
  if (r <= 0.0)
    throw GeometryError("write_region_points: radius must be positive");

  std::ofstream f(path);
  if (!f)
    throw GeometryError("write_region_points: cannot open '" + path + "'");
  f << std::setprecision(17);

  // Fibonacci sphere: near-uniform points, deterministic, no RNG.
  const double golden_angle = 3.14159265358979323846 * (3.0 - std::sqrt(5.0));
  for (int i = 0; i < n_points; ++i) {
    const double z = 1.0 - 2.0 * (i + 0.5) / n_points;
    const double rad = std::sqrt(std::max(0.0, 1.0 - z * z));
    const double th = golden_angle * i;
    Vec3 p = {xlag[0] + r * std::cos(th) * rad, xlag[1] + r * z,
              xlag[2] + r * std::sin(th) * rad};
    Vec3 u = frame.to_unit(p);
    f << u[0] << " " << u[1] << " " << u[2] << "\n";
  }
}

}  // namespace ppmi
