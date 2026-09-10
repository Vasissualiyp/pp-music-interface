// catalog.cpp - implementation of ppmi/catalog.hpp.
//
// See catalog.hpp for the full format specification, transcribed from
// peakpatch/src/merge_pkvd/merge_pkvd_module.f90.

#include "ppmi/catalog.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace ppmi {

namespace {

constexpr double kPi = 3.14159265358979323846;

// RAII wrapper so peek_header's early-return/throw paths never leak the FILE*.
struct FileGuard {
  std::FILE* f = nullptr;
  explicit FileGuard(std::FILE* file) : f(file) {}
  ~FileGuard() {
    if (f) std::fclose(f);
  }
  FileGuard(const FileGuard&) = delete;
  FileGuard& operator=(const FileGuard&) = delete;
};

}  // namespace

// ---------------------------------------------------------------------------
// Cosmology / mass
// ---------------------------------------------------------------------------

double Cosmology::rho_m() const { return kRhoCritH2 * h * h * Omega_m; }

double halo_mass(double r, const Cosmology& cosmo) {
  return (4.0 / 3.0) * kPi * r * r * r * cosmo.rho_m();
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

CatalogHeader peek_header(const std::string& path) {
  std::FILE* raw = std::fopen(path.c_str(), "rb");
  if (!raw) throw CatalogError("cannot open catalogue: " + path);
  FileGuard guard(raw);
  std::FILE* f = raw;

  if (std::fseek(f, 0, SEEK_END) != 0) {
    throw CatalogError("cannot seek to end of " + path);
  }
  long file_size_l = std::ftell(f);
  if (file_size_l < 0) {
    throw CatalogError("cannot determine size of " + path);
  }
  std::int64_t file_size = static_cast<std::int64_t>(file_size_l);
  std::rewind(f);

  if (file_size < 4) {
    throw TruncatedCatalog("catalogue " + path + " is only " +
                            std::to_string(file_size) +
                            " bytes, too short to hold even the header tag");
  }

  std::int32_t first = 0;
  if (std::fread(&first, sizeof(first), 1, f) != 1) {
    throw TruncatedCatalog("failed to read header tag from " + path);
  }

  CatalogHeader hdr;
  if (first == -1) {
    if (file_size < kHeaderSentinelBytes) {
      throw TruncatedCatalog("catalogue " + path +
                              " starts with the sentinel tag but is only " +
                              std::to_string(file_size) + " bytes, short of the " +
                              std::to_string(kHeaderSentinelBytes) +
                              "-byte sentinel header");
    }
    std::int64_t n_halos = 0;
    float rmax = 0.0f, z = 0.0f;
    if (std::fread(&n_halos, sizeof(n_halos), 1, f) != 1 ||
        std::fread(&rmax, sizeof(rmax), 1, f) != 1 ||
        std::fread(&z, sizeof(z), 1, f) != 1) {
      throw TruncatedCatalog("failed to read sentinel header from " + path);
    }
    hdr.layout = HeaderLayout::kSentinel;
    hdr.header_bytes = kHeaderSentinelBytes;
    hdr.n_halos = n_halos;
    hdr.rmax = rmax;
    hdr.boxredshift = z;
  } else {
    if (file_size < kHeaderLegacyBytes) {
      throw TruncatedCatalog("catalogue " + path + " is only " +
                              std::to_string(file_size) + " bytes, short of the " +
                              std::to_string(kHeaderLegacyBytes) +
                              "-byte legacy header");
    }
    float rmax = 0.0f, z = 0.0f;
    if (std::fread(&rmax, sizeof(rmax), 1, f) != 1 ||
        std::fread(&z, sizeof(z), 1, f) != 1) {
      throw TruncatedCatalog("failed to read legacy header from " + path);
    }
    hdr.layout = HeaderLayout::kLegacy;
    hdr.header_bytes = kHeaderLegacyBytes;
    hdr.n_halos = static_cast<std::int64_t>(first);
    hdr.rmax = rmax;
    hdr.boxredshift = z;
  }

  if (hdr.n_halos < 0) {
    throw CatalogError("catalogue " + path + " has a negative halo count (" +
                        std::to_string(hdr.n_halos) + ")");
  }

  std::int64_t body_bytes = file_size - hdr.header_bytes;
  if (body_bytes < 0) {
    throw TruncatedCatalog("catalogue " + path +
                            " is shorter than its own header claims");
  }

  if (body_bytes % 4 != 0) {
    throw TruncatedCatalog(
        "catalogue " + path + " body is " + std::to_string(body_bytes) +
        " bytes, not a whole number of float32 values -- the file is cut "
        "mid-record");
  }
  std::int64_t total_floats = body_bytes / 4;

  if (hdr.n_halos == 0) {
    if (total_floats != 0) {
      throw UnknownCatalogLayout(
          "catalogue " + path + " claims zero halos but has " +
          std::to_string(total_floats) + " leftover float32 values");
    }
    // No records to disambiguate the layout from; default to the base one.
    hdr.n_fields = kFieldsBase;
    return hdr;
  }

  if (total_floats % hdr.n_halos != 0) {
    throw TruncatedCatalog(
        "catalogue " + path + " body holds " + std::to_string(total_floats) +
        " float32 values, not a whole number of records for the " +
        std::to_string(hdr.n_halos) + " halos the header claims");
  }

  std::int64_t fields_per_halo = total_floats / hdr.n_halos;
  if (fields_per_halo == kFieldsBase) {
    hdr.n_fields = kFieldsBase;
  } else if (fields_per_halo == kFieldsShear) {
    hdr.n_fields = kFieldsShear;
  } else {
    throw UnknownCatalogLayout(
        "catalogue " + path + " has " + std::to_string(fields_per_halo) +
        " float32 fields per halo, matching neither " +
        std::to_string(kFieldsBase) + " nor " + std::to_string(kFieldsShear));
  }

  return hdr;
}

// ---------------------------------------------------------------------------
// CatalogReader
// ---------------------------------------------------------------------------

namespace {
Halo halo_from_fields(std::int64_t index, const float f[kFieldsBase]) {
  Halo h;
  h.index = index;
  h.eulerian = {f[0], f[1], f[2]};
  h.velocity = {f[3], f[4], f[5]};
  h.r = f[6];
  h.lagrangian = {f[7], f[8], f[9]};
  h.fcoll = f[10];
  return h;
}
}  // namespace

struct CatalogReader::Impl {
  std::string path;
  CatalogHeader header;
  std::FILE* file = nullptr;
  std::int64_t record_bytes = 0;

  explicit Impl(const std::string& p) : path(p) {
    header = peek_header(p);
    record_bytes = static_cast<std::int64_t>(header.n_fields) * 4;
    file = std::fopen(p.c_str(), "rb");
    if (!file) throw CatalogError("cannot open catalogue: " + p);
  }

  ~Impl() {
    if (file) std::fclose(file);
  }
};

CatalogReader::CatalogReader(const std::string& path) : impl_(new Impl(path)) {}

CatalogReader::~CatalogReader() { delete impl_; }

const CatalogHeader& CatalogReader::header() const { return impl_->header; }

std::int64_t CatalogReader::read_chunk(std::int64_t first, std::int64_t max_halos,
                                       std::vector<Halo>& out) {
  out.clear();
  if (first < 0 || max_halos <= 0 || first >= impl_->header.n_halos) return 0;

  std::int64_t n = std::min(max_halos, impl_->header.n_halos - first);
  out.reserve(static_cast<std::size_t>(n));

  std::int64_t offset = static_cast<std::int64_t>(impl_->header.header_bytes) +
                        first * impl_->record_bytes;
  if (std::fseek(impl_->file, offset, SEEK_SET) != 0) {
    throw CatalogError("seek failed while reading " + impl_->path);
  }

  std::int64_t skip_floats = impl_->header.n_fields - kFieldsBase;
  for (std::int64_t i = 0; i < n; ++i) {
    float f[kFieldsBase];
    if (std::fread(f, sizeof(float), kFieldsBase, impl_->file) !=
        static_cast<std::size_t>(kFieldsBase)) {
      throw TruncatedCatalog("unexpected end of file while reading " + impl_->path);
    }
    out.push_back(halo_from_fields(first + i, f));
    if (skip_floats > 0) {
      if (std::fseek(impl_->file, skip_floats * 4, SEEK_CUR) != 0) {
        throw CatalogError("seek failed while reading " + impl_->path);
      }
    }
  }
  return n;
}

Halo CatalogReader::read_halo(std::int64_t index) {
  if (index < 0 || index >= impl_->header.n_halos) {
    throw CatalogError("halo index " + std::to_string(index) + " out of range (" +
                        std::to_string(impl_->header.n_halos) + " halos) in " +
                        impl_->path);
  }
  std::int64_t offset = static_cast<std::int64_t>(impl_->header.header_bytes) +
                        index * impl_->record_bytes;
  if (std::fseek(impl_->file, offset, SEEK_SET) != 0) {
    throw CatalogError("seek failed while reading " + impl_->path);
  }
  float f[kFieldsBase];
  if (std::fread(f, sizeof(float), kFieldsBase, impl_->file) !=
      static_cast<std::size_t>(kFieldsBase)) {
    throw TruncatedCatalog("unexpected end of file while reading " + impl_->path);
  }
  return halo_from_fields(index, f);
}

// ---------------------------------------------------------------------------
// CatalogWriter
// ---------------------------------------------------------------------------

struct CatalogWriter::Impl {
  std::string path;
  HeaderLayout layout;
  int n_fields;
  float rmax;
  float boxredshift;
  std::FILE* file = nullptr;
  std::int64_t count = 0;
  bool finished = false;

  Impl(const std::string& p, HeaderLayout l, int nf, float rm, float z)
      : path(p), layout(l), n_fields(nf), rmax(rm), boxredshift(z) {
    file = std::fopen(p.c_str(), "wb");
    if (!file) throw CatalogError("cannot create catalogue: " + p);
    write_header(0);
  }

  ~Impl() {
    if (file) std::fclose(file);
  }

  void write_header(std::int64_t n) {
    std::fseek(file, 0, SEEK_SET);
    if (layout == HeaderLayout::kSentinel) {
      std::int32_t sentinel = -1;
      std::fwrite(&sentinel, sizeof(sentinel), 1, file);
      std::fwrite(&n, sizeof(n), 1, file);
      std::fwrite(&rmax, sizeof(rmax), 1, file);
      std::fwrite(&boxredshift, sizeof(boxredshift), 1, file);
    } else {
      std::int32_t nn = static_cast<std::int32_t>(n);
      std::fwrite(&nn, sizeof(nn), 1, file);
      std::fwrite(&rmax, sizeof(rmax), 1, file);
      std::fwrite(&boxredshift, sizeof(boxredshift), 1, file);
    }
    std::fflush(file);
  }
};

CatalogWriter::CatalogWriter(const std::string& path, HeaderLayout layout, int n_fields,
                             float rmax, float boxredshift)
    : impl_(new Impl(path, layout, n_fields, rmax, boxredshift)) {}

CatalogWriter::~CatalogWriter() {
  // ~CatalogWriter() is implicitly noexcept (destructors default to that
  // since C++11), so throwing here would call std::terminate rather than
  // raise a catchable error -- worse than the bug it would be trying to
  // report. finish()'s doc comment ("Throws if not called") is honoured by
  // finish() itself: skipping it leaves the file's header at the placeholder
  // count written by the constructor, which any reader will visibly
  // disagree with the body length it finds, surfacing the mistake through
  // peek_header's own consistency checks instead of a destructor throw.
  delete impl_;
  impl_ = nullptr;
}

void CatalogWriter::write(const Halo& halo) {
  Impl* im = impl_;
  float f[kFieldsBase] = {
      static_cast<float>(halo.eulerian[0]),  static_cast<float>(halo.eulerian[1]),
      static_cast<float>(halo.eulerian[2]),  static_cast<float>(halo.velocity[0]),
      static_cast<float>(halo.velocity[1]),  static_cast<float>(halo.velocity[2]),
      static_cast<float>(halo.r),            static_cast<float>(halo.lagrangian[0]),
      static_cast<float>(halo.lagrangian[1]), static_cast<float>(halo.lagrangian[2]),
      static_cast<float>(halo.fcoll),
  };
  std::fwrite(f, sizeof(float), kFieldsBase, im->file);

  if (im->n_fields > kFieldsBase) {
    std::vector<float> pad(static_cast<std::size_t>(im->n_fields - kFieldsBase), 0.0f);
    std::fwrite(pad.data(), sizeof(float), pad.size(), im->file);
  }
  ++im->count;
}

void CatalogWriter::finish() {
  impl_->write_header(impl_->count);
  impl_->finished = true;
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

namespace {

struct Entry {
  Halo halo;
  double mass;
};

// Distance to the nearest strictly-more-massive halo, in the Lagrangian
// frame, divided by this halo's own r. Infinity when nothing is more
// massive.
double isolation_of(const std::vector<Entry>& all, std::size_t idx) {
  double best = std::numeric_limits<double>::infinity();
  const Entry& e = all[idx];
  for (std::size_t j = 0; j < all.size(); ++j) {
    if (j == idx) continue;
    if (all[j].mass <= e.mass) continue;
    double dx = all[j].halo.lagrangian[0] - e.halo.lagrangian[0];
    double dy = all[j].halo.lagrangian[1] - e.halo.lagrangian[1];
    double dz = all[j].halo.lagrangian[2] - e.halo.lagrangian[2];
    double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    double val = (e.halo.r > 0.0) ? dist / e.halo.r
                                  : std::numeric_limits<double>::infinity();
    if (val < best) best = val;
  }
  return best;
}

// Clearance between the halo's Lagrangian patch and the nearest box face, in
// units of r. Negative when the patch already crosses a face.
double edge_of(const Entry& e, const BoxFrame& frame) {
  double half = frame.boxsize / 2.0;
  double closest_face = std::numeric_limits<double>::infinity();
  for (int ax = 0; ax < 3; ++ax) {
    double local = e.halo.lagrangian[static_cast<std::size_t>(ax)] -
                   frame.cen[static_cast<std::size_t>(ax)];
    double d_low = local + half;
    double d_high = half - local;
    closest_face = std::min({closest_face, d_low, d_high});
  }
  if (e.halo.r <= 0.0) return std::numeric_limits<double>::infinity();
  return (closest_face - e.halo.r) / e.halo.r;
}

}  // namespace

Selection select(const std::string& path, const Cosmology& cosmo,
                 const BoxFrame& frame, const Criterion& criterion,
                 bool is_lightcone) {
  if (is_lightcone) {
    throw AmbiguousRedshift(
        "select() was asked to run on a lightcone catalogue (" + path +
        ") without a redshift filter; a lightcone spans a range of "
        "redshifts and picking a halo from it needs one");
  }

  CatalogReader reader(path);
  const CatalogHeader& hdr = reader.header();

  // Pass 1: stream the whole catalogue via read_chunk, computing mass per
  // halo along the way.
  std::vector<Entry> all;
  all.reserve(static_cast<std::size_t>(std::max<std::int64_t>(hdr.n_halos, 0)));
  {
    std::vector<Halo> buf;
    const std::int64_t chunk_size = 4096;
    std::int64_t pos = 0;
    while (pos < hdr.n_halos) {
      std::int64_t got = reader.read_chunk(pos, chunk_size, buf);
      if (got == 0) break;
      for (std::int64_t i = 0; i < got; ++i) {
        const Halo& h = buf[static_cast<std::size_t>(i)];
        all.push_back({h, halo_mass(h.r, cosmo)});
      }
      pos += got;
    }
  }

  // Pass 2: build the ordered candidate list per criterion kind.
  std::vector<std::size_t> candidates;
  if (criterion.kind == CriterionKind::kMassWindow) {
    for (std::size_t i = 0; i < all.size(); ++i) {
      if (all[i].mass >= criterion.m_min && all[i].mass <= criterion.m_max) {
        candidates.push_back(i);
      }
    }
    if (criterion.closest_to > 0.0) {
      double target_log = std::log(criterion.closest_to);
      std::sort(candidates.begin(), candidates.end(),
                [&](std::size_t a, std::size_t b) {
                  double da = std::fabs(std::log(all[a].mass) - target_log);
                  double db = std::fabs(std::log(all[b].mass) - target_log);
                  return da < db;
                });
    } else {
      std::sort(candidates.begin(), candidates.end(),
                [&](std::size_t a, std::size_t b) { return all[a].mass > all[b].mass; });
    }
  } else {
    candidates.reserve(all.size());
    for (std::size_t i = 0; i < all.size(); ++i) candidates.push_back(i);
    std::sort(candidates.begin(), candidates.end(),
              [&](std::size_t a, std::size_t b) { return all[a].mass > all[b].mass; });
  }

  std::int64_t considered = 0;
  std::int64_t rejected_isolation = 0;
  std::int64_t rejected_edge = 0;
  std::int64_t pass_count = 0;

  for (std::size_t cidx : candidates) {
    ++considered;
    double iso = isolation_of(all, cidx);
    double edge = edge_of(all[cidx], frame);

    bool fail_iso = criterion.min_isolation_radii > 0.0 && iso < criterion.min_isolation_radii;
    bool fail_edge = criterion.min_edge_radii > 0.0 && edge < criterion.min_edge_radii;
    if (fail_iso) ++rejected_isolation;
    if (fail_edge) ++rejected_edge;
    if (fail_iso || fail_edge) continue;

    ++pass_count;
    if (pass_count == criterion.rank) {
      Selection sel;
      sel.halo = all[cidx].halo;
      sel.mass = all[cidx].mass;
      sel.isolation_radii = iso;
      sel.edge_radii = edge;
      sel.criterion = (criterion.kind == CriterionKind::kMassRank)
                          ? ("mass_rank=" + std::to_string(criterion.rank))
                          : ("mass_window=[" + std::to_string(criterion.m_min) + ", " +
                             std::to_string(criterion.m_max) + "]");
      return sel;
    }
  }

  throw NoHaloMatches(
      "no halo matches in " + path + ": " + std::to_string(considered) +
      " candidate(s) considered for rank " + std::to_string(criterion.rank) + ", " +
      std::to_string(rejected_isolation) + " rejected by the isolation guard, " +
      std::to_string(rejected_edge) + " rejected by the edge guard");
}

}  // namespace ppmi
