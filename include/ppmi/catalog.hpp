// catalog.hpp - PeakPatch merged halo catalogues (.pksc) and halo selection.
//
// FORMAT, from peakpatch/src/merge_pkvd/merge_pkvd_module.f90.
//
// Header. Two variants exist and a reader must handle both, because the two
// branches being merged write different ones.
//
//   sentinel (origin/nate, line 597), 20 bytes:
//       int32   -1            marks the 64-bit format
//       int64   nout          halo count
//       float32 rmax
//       float32 boxredshift
//
//   legacy (origin/vasdev, line 581), 12 bytes:
//       int32   nout
//       float32 rmax
//       float32 boxredshift
//
// Detection is entirely the first int32: -1 means sentinel, anything else is a
// legacy halo count. Catalogues from WebSky-scale runs exceed 2^31 halos,
// which is why the sentinel format exists, so every count and offset in this
// file is int64.
//
// Record. n_fields consecutive float32 per halo, written at lines 568-569.
// n_fields is 11 when the run had ioutshear=0 and 33 otherwise, per lines
// 557-561. The first eleven, in order:
//
//   0-2    x, y, z            Eulerian position, after 1LPT + 2LPT displacement
//   3-5    vx, vy, vz         peculiar velocity, km/s
//   6      r                  Lagrangian top-hat radius of the collapsing patch
//   7-9    xlag, ylag, zlag   Lagrangian position, unperturbed
//   10     fcoll              linear collapse overdensity at the peak
//
// Fields 7-9 are why this pipeline works at all: they give the zoom centre with
// no reverse-displacement machinery. merge_pkvd saves them before applying the
// displacement, at lines 467-475.
//
// Fields 3-5 are named dx,dy,dz in PeakPatch's own Python reader, which is
// misleading. merge_pkvd has already converted them from displacements into
// velocities in km/s at lines 490-492.
//
// MASS is derived, not stored. The reference implementation is
// peakpatch/python/peakpatchtools/catalogue.py lines 107-133: rho_crit from
// H_0 = 100h, rho_m = rho_crit*Omega_m, M = 4/3 pi r^3 rho_m, with that file's
// own comment "All in units of solar masses and Mpc". This header matches that
// convention exactly and inserts no factors of h of its own.

#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "ppmi/geometry.hpp"

namespace ppmi {

// Critical density for H_0 = 100 km/s/Mpc, in M_sun/Mpc^3. Multiply by h^2.
inline constexpr double kRhoCritH2 = 2.77536627e11;

inline constexpr int kHeaderSentinelBytes = 20;
inline constexpr int kHeaderLegacyBytes = 12;
inline constexpr int kFieldsBase = 11;
inline constexpr int kFieldsShear = 33;

class CatalogError : public std::runtime_error {
 public:
  explicit CatalogError(const std::string& what) : std::runtime_error(what) {}
};

// File size is not consistent with its header and any known record length.
class TruncatedCatalog : public CatalogError {
 public:
  explicit TruncatedCatalog(const std::string& w) : CatalogError(w) {}
};

// Header parsed, but the body length matches neither 11 nor 33 fields.
class UnknownCatalogLayout : public CatalogError {
 public:
  explicit UnknownCatalogLayout(const std::string& w) : CatalogError(w) {}
};

enum class HeaderLayout { kSentinel, kLegacy };

// The two numbers needed to turn a Lagrangian radius into a mass.
struct Cosmology {
  double Omega_m = 0.0;  // total matter, PeakPatch's Omx + OmB
  double h = 0.0;        // Hubble parameter in units of 100 km/s/Mpc

  // Mean matter density in M_sun/Mpc^3.
  double rho_m() const;
};

struct CatalogHeader {
  std::int64_t n_halos = 0;
  float rmax = 0.0f;
  float boxredshift = 0.0f;
  HeaderLayout layout = HeaderLayout::kSentinel;
  int header_bytes = 0;
  int n_fields = 0;
};

// One halo, as the selection layer sees it.
struct Halo {
  std::int64_t index = -1;
  Vec3 eulerian{};
  Vec3 velocity{};
  double r = 0.0;
  Vec3 lagrangian{};
  double fcoll = 0.0;
};

// Read only the header. Throws TruncatedCatalog if the file is shorter than
// the header it claims, or if the body length divides by neither field count.
CatalogHeader peek_header(const std::string& path);

// Mass of one halo, 4/3 pi r^3 rho_m, in solar masses.
double halo_mass(double r, const Cosmology& cosmo);

// Streaming reader. Catalogues can hold billions of halos, so the default
// access pattern is a scan, not a load. read_halo() seeks; read_chunk() is the
// efficient path for a full pass.
class CatalogReader {
 public:
  explicit CatalogReader(const std::string& path);
  ~CatalogReader();

  const CatalogHeader& header() const;

  // Fills `out` with up to `max_halos` halos starting at `first`, and returns
  // how many were read. Reads only the first 11 fields; shear fields are
  // skipped, which is what makes a 33-field catalogue cheap to scan.
  std::int64_t read_chunk(std::int64_t first, std::int64_t max_halos,
                          std::vector<Halo>& out);

  Halo read_halo(std::int64_t index);

 private:
  struct Impl;
  Impl* impl_;
};

// Writer, used by tests and by tools that subset a catalogue.
class CatalogWriter {
 public:
  CatalogWriter(const std::string& path, HeaderLayout layout, int n_fields,
                float rmax, float boxredshift);
  ~CatalogWriter();

  void write(const Halo& halo);
  // Rewrites the header with the final count. Throws if not called.
  void finish();

 private:
  struct Impl;
  Impl* impl_;
};

// Round-tripping a catalogue through CatalogWriter then CatalogReader must
// reproduce every field bit for bit, since everything on disk is float32.

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------
//
// On redshift. A static-box PeakPatch run already contains only what has
// collapsed by global_redshift, because the peak-finding threshold fsc_of_z is
// evaluated at that redshift (peakpatch/src/hpkvd/peakvoidsubs.f90 lines
// 25-51). So "the largest halo at z=20" is a RUN CONFIGURATION, not a filter,
// and selection does not filter on redshift. A lightcone run (ievol=1) is
// different: its catalogue spans a range of redshifts, and selecting from it
// without saying which is a bug, so select() refuses to do it silently.

class SelectionError : public std::runtime_error {
 public:
  explicit SelectionError(const std::string& w) : std::runtime_error(w) {}
};

// No halo satisfied the criterion. The message must say how many candidates
// were rejected and by which guard; "no halo matches" on a full catalogue is
// otherwise baffling.
class NoHaloMatches : public SelectionError {
 public:
  explicit NoHaloMatches(const std::string& w) : SelectionError(w) {}
};

// Selection was attempted on a lightcone catalogue with no redshift filter.
class AmbiguousRedshift : public SelectionError {
 public:
  explicit AmbiguousRedshift(const std::string& w) : SelectionError(w) {}
};

// One chosen halo, with the context needed to judge whether it is a good
// target rather than merely the answer to the question asked.
struct Selection {
  Halo halo;
  double mass = 0.0;
  // Distance to the nearest more massive halo, in units of this halo's own r,
  // measured between Lagrangian positions because that is the frame the zoom
  // region lives in. Infinity when this is the most massive halo. A target
  // sitting inside a larger structure's Lagrangian patch will be contaminated,
  // so this matters more than it looks.
  double isolation_radii = std::numeric_limits<double>::infinity();
  // Clearance between this halo's Lagrangian patch and the nearest box face,
  // in units of r. Negative when the patch already crosses a face, which
  // MUSIC's box region generator cannot express.
  double edge_radii = 0.0;
  std::string criterion;
};

enum class CriterionKind { kMassRank, kMassWindow };

struct Criterion {
  CriterionKind kind = CriterionKind::kMassRank;
  std::int64_t rank = 1;      // 1 is the largest
  double m_min = 0.0;
  double m_max = 0.0;
  // When positive, pick the halo whose mass is nearest this value in log
  // space, which is the sensible metric across three decades of halo mass.
  double closest_to = 0.0;
  // Reject candidates whose nearest more massive neighbour is closer than
  // this, stepping to the next candidate. Zero means no requirement.
  double min_isolation_radii = 0.0;
  // Reject candidates whose Lagrangian patch comes closer than this to a face.
  double min_edge_radii = 0.0;
};

// Run a criterion over a catalogue. Streams; does not load the whole file.
// Throws AmbiguousRedshift when is_lightcone is true, NoHaloMatches when every
// candidate is rejected.
Selection select(const std::string& path, const Cosmology& cosmo,
                 const BoxFrame& frame, const Criterion& criterion,
                 bool is_lightcone = false);

}  // namespace ppmi
