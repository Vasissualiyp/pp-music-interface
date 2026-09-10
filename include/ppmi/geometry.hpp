// geometry.hpp - grid pairing and the coordinate frames of the two codes.
//
// Two independent things live here because both are pure geometry with no I/O:
// the rule that makes a PeakPatch lattice correspond to a MUSIC level, and the
// transform between PeakPatch's observer-centred box and MUSIC's Lagrangian
// unit box.
//
// GRID RELATIONS, transcribed from PeakPatch's own parameter evaluation,
// peakpatch/src/modules/ini_reader/config_reader.f90 lines 802-810:
//
//     nsub       = nmesh - 2*nbuff
//     next       = nsub*ntile + 2*nbuff
//     dcore_box  = boxsize / ntile
//     cellsize   = dcore_box / nsub
//     buffersize = cellsize * nbuff
//     dL_box     = dcore_box + 2*buffersize
//
// The consequence, and the reason this file exists: the global core grid spans
// nsub*ntile cells across the box, while MUSIC's survey grid spans 2^levelmin
// cells across the same box. So the correct pairing is
//
//     2^levelmin == nsub * ntile
//
// NOT the `nmesh == 2^levelmin` rule stated in the interface README, which
// holds only for a single tile with no buffer. Neither shipped example config
// satisfies either rule: param/parameters.ini has nmesh=364, nbuff=64, ntile=8
// giving a core grid of 1888 against 2^9 = 512, and param/debug.ini gives 1920
// against 2^6 = 64.
//
// COORDINATE FRAMES. PeakPatch centres its box on the observer. From
// peakpatch/src/merge_pkvd/merge_pkvd_module.f90 line 440:
//
//     xtile = (itile - 0.5) * dcore_box - boxsize/2 + cenx
//
// so positions span [cen - boxsize/2, cen + boxsize/2] per axis, with
// cenx/ceny/cenz from [lattice_parameters_hpkvd], defaulting to zero. MUSIC
// works in dimensionless Lagrangian coordinates on [0,1) measured from a box
// corner, independent of boxlength (music_mpi/src/plugins/region_generator.cc
// lines 100-144). The transform per axis is
//
//     u = frac( (x - cen)/boxsize + 0.5 )
//
// Both the origin offset and the AXIS ORDERING are assumptions until task
// P3-T3 proves them with a spike test. Nothing in either codebase documents
// whether the two grids agree on which array index is x. When P3-T3 lands,
// record the result here. If the spike comes back transposed, fix the field
// writer, never this transform: compensating here would break every other
// reader of those field files.
//
// Units. Lengths are whatever the run's parameter file uses, Mpc in the
// reference reader's convention. Nothing here inserts a factor of h.

#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace ppmi {

using Vec3 = std::array<double, 3>;

// Thrown when a geometry is not self-consistent or cannot be paired.
class GeometryError : public std::runtime_error {
 public:
  explicit GeometryError(const std::string& what) : std::runtime_error(what) {}
};

// ---------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------

// One PeakPatch lattice, as described by [box_params].
//   nmesh   cells per tile side, including the buffer on both sides
//   nbuff   buffer cells added on each side of a tile
//   ntile   tiles per box side; the box holds ntile^3 tiles
//   boxsize side length of the box, in the run's length unit
struct PeakPatchGrid {
  int nmesh = 0;
  int nbuff = 0;
  int ntile = 0;
  double boxsize = 0.0;

  // Cells per tile side excluding the buffer.
  int nsub() const;
  // PeakPatch's `next`: the padded global grid side.
  int n_ext() const;
  // Unpadded global grid side, nsub*ntile. This is what pairs with MUSIC.
  int core_grid() const;
  // Side length of one tile's core region.
  double dcore_box() const;
  // Side length of one cell. Sets the smallest resolvable halo.
  double cellsize() const;
  // Thickness of the buffer, in length units.
  double buffersize() const;
  // Side length of one tile including both buffers.
  double dL_box() const;
  // Total tiles, ntile^3. Use int64 because this indexes file offsets.
  std::int64_t n_tiles() const;

  // Throws GeometryError if nmesh, ntile or boxsize is not positive, if nbuff
  // is negative, or if nsub() would be below 1.
  void validate() const;

  // The MUSIC levelmin this grid pairs with.
  // Throws GeometryError if core_grid() is not an exact power of two, naming
  // the value that failed.
  int music_levelmin() const;
};

// The nmesh that makes a PeakPatch grid pair with a MUSIC levelmin.
// Solves nsub*ntile == 2^levelmin for nmesh = nsub + 2*nbuff.
// Throws GeometryError if 2^levelmin is not divisible by ntile, naming both
// numbers, since no integer solution exists in that case.
int solve_nmesh(int levelmin, int ntile, int nbuff);

// True when grid.core_grid() == 2^levelmin.
bool check_pairing(int levelmin, const PeakPatchGrid& grid);

// One line explaining why a pairing fails, for error messages. Empty string
// when the pairing holds. Otherwise names the core grid, the MUSIC grid, and
// the nmesh that would fix it when one exists.
std::string describe_mismatch(int levelmin, const PeakPatchGrid& grid);

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

// The mapping between one PeakPatch box and MUSIC's unit box.
//   boxsize must equal MUSIC's boxlength
//   cen is PeakPatch's cenx/ceny/cenz, almost always the origin
struct BoxFrame {
  double boxsize = 0.0;
  Vec3 cen = {0.0, 0.0, 0.0};

  // PeakPatch coordinates to MUSIC unit-box coordinates, wrapped into [0,1).
  Vec3 to_unit(const Vec3& x) const;
  // MUSIC unit-box coordinates back to PeakPatch coordinates. Inverse of
  // to_unit up to periodic wrapping.
  Vec3 from_unit(const Vec3& u) const;
  // A length in box units to a fraction of the box.
  double length_to_unit(double length) const;
};

// Thrown when a refinement region crosses a box face. MUSIC's box region
// generator cannot express a wrapping region, so this is a hard error rather
// than something to clamp. The caller's options are a different halo or a
// shifted configuration. The message must name the axis and how far past the
// edge the region reaches.
class RegionWrapsBox : public GeometryError {
 public:
  explicit RegionWrapsBox(const std::string& what) : GeometryError(what) {}
};

// MUSIC ref_center and ref_extent for one halo.
// The centre is the halo's Lagrangian position mapped into the unit box. The
// extent is 2*r*extent_factor as a box fraction, on all three axes.
// extent_factor trades contamination against cost; two to four is the usual
// range and three is a starting point, not a result.
// Throws RegionWrapsBox if the region crosses a face.
struct RefRegion {
  Vec3 center;
  Vec3 extent;
};
RefRegion halo_to_ref(const Vec3& xlag, double r, const BoxFrame& frame,
                      double extent_factor = 3.0);

// Format a 3-vector the way MUSIC's config parser reads it. MUSIC parses these
// with sscanf(..., "%lf,%lf,%lf", ...), so: three comma-separated decimals, no
// spaces, no brackets, and enough significant figures that a round trip
// through strtod is lossless at double precision.
std::string format_triple(const Vec3& v);

// Write a halo's Lagrangian patch as a MUSIC point file, for the
// [setup] region_point_file key consumed by region=ellipsoid and
// region=convex_hull (music_mpi/src/plugins/point_file_reader.hh: whitespace
// separated, three columns, coordinates in the unit box [0,1)). The patch is
// sampled as `n_points` Fibonacci-sphere points of radius r about xlag. Each
// point wraps through the periodic box independently, so unlike halo_to_ref
// this does not throw when the patch crosses a face.
void write_region_points(const std::string& path, const Vec3& xlag, double r,
                         const BoxFrame& frame, int n_points = 64);

}  // namespace ppmi
