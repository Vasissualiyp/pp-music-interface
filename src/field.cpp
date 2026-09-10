// field.cpp - implementation of include/ppmi/field.hpp
//
// See the layout comment at the top of field.hpp before touching anything
// here: the file on disk is a sequence of per-tile blocks (Fortran order,
// x fastest), not a single global cube, and tiles overlap by nbuff cells on
// every shared face with periodic wrap at the box faces.

#include "ppmi/field.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace ppmi {

namespace {

bool host_is_big_endian() {
  const std::uint16_t probe = 1;
  return *reinterpret_cast<const std::uint8_t*>(&probe) == 0;
}

float swap_float(float f) {
  std::uint32_t u;
  std::memcpy(&u, &f, sizeof(u));
  u = ((u & 0x000000FFu) << 24) | ((u & 0x0000FF00u) << 8) |
      ((u & 0x00FF0000u) >> 8) | ((u & 0xFF000000u) >> 24);
  float out;
  std::memcpy(&out, &u, sizeof(out));
  return out;
}

// Positive modulo.
int wrap_mod(std::int64_t v, int m) {
  std::int64_t r = v % m;
  if (r < 0) r += m;
  return (int)r;
}

// Global (periodic, unpadded) coordinate along one axis for tile index k
// (1-based) and a local in-block index `local` in [0, nmesh).
int global_coord(int k, int local, int nsub, int nbuff, int core_grid) {
  std::int64_t g = (std::int64_t)(k - 1) * nsub + local - nbuff;
  return wrap_mod(g, core_grid);
}

}  // namespace

// ---------------------------------------------------------------------------
// tile indexing
// ---------------------------------------------------------------------------

std::int64_t tile_index(const PeakPatchGrid& grid, int k1, int k2, int k3) {
  std::int64_t nt = grid.ntile;
  return (std::int64_t)k1 + nt * (std::int64_t)(k2 - 1) +
         nt * nt * (std::int64_t)(k3 - 1);
}

void tile_coords(const PeakPatchGrid& grid, std::int64_t ibox, int& k1,
                 int& k2, int& k3) {
  std::int64_t nt = grid.ntile;
  std::int64_t idx0 = ibox - 1;
  k1 = (int)(idx0 % nt) + 1;
  std::int64_t rem = idx0 / nt;
  k2 = (int)(rem % nt) + 1;
  k3 = (int)(rem / nt) + 1;
}

std::int64_t tile_offset(const PeakPatchGrid& grid, std::int64_t ibox) {
  std::int64_t n = grid.n_tiles();
  if (ibox < 1 || ibox > n) {
    throw FieldError("tile_offset: ibox " + std::to_string(ibox) +
                      " out of range [1, " + std::to_string(n) + "]");
  }
  std::int64_t nmesh3 =
      (std::int64_t)grid.nmesh * grid.nmesh * grid.nmesh;
  return (ibox - 1) * nmesh3 * 4;
}

std::int64_t field_file_bytes(const PeakPatchGrid& grid) {
  std::int64_t nmesh3 =
      (std::int64_t)grid.nmesh * grid.nmesh * grid.nmesh;
  return grid.n_tiles() * nmesh3 * 4;
}

// ---------------------------------------------------------------------------
// TileWriter
// ---------------------------------------------------------------------------

struct TileWriter::Impl {
  PeakPatchGrid grid;
  std::fstream file;
  std::vector<bool> written;
  std::int64_t nmesh3 = 0;
  std::int64_t total_tiles = 0;
};

TileWriter::TileWriter(const std::string& path, const PeakPatchGrid& grid)
    : impl_(new Impl{}) {
  impl_->grid = grid;
  impl_->total_tiles = grid.n_tiles();
  impl_->nmesh3 = (std::int64_t)grid.nmesh * grid.nmesh * grid.nmesh;
  impl_->written.assign((size_t)impl_->total_tiles, false);

  // Create (or truncate) the file, then stake out its final size up front so
  // seeks past what has been written so far land inside the file, whichever
  // order tiles arrive in.
  {
    std::ofstream create(path, std::ios::binary | std::ios::trunc);
    if (!create) {
      throw FieldError("TileWriter: cannot create '" + path + "'");
    }
    std::int64_t total = field_file_bytes(grid);
    if (total > 0) {
      create.seekp(total - 1, std::ios::beg);
      char zero = 0;
      create.write(&zero, 1);
      if (!create) {
        throw FieldError("TileWriter: cannot size '" + path + "'");
      }
    }
  }

  impl_->file.open(path, std::ios::binary | std::ios::in | std::ios::out);
  if (!impl_->file) {
    throw FieldError("TileWriter: cannot reopen '" + path + "' for writing");
  }
}

TileWriter::~TileWriter() { delete impl_; }

void TileWriter::write_tile(std::int64_t ibox, const float* block) {
  std::int64_t offset = tile_offset(impl_->grid, ibox);  // throws if OOB

  bool swap = host_is_big_endian();
  const float* src = block;
  std::vector<float> tmp;
  if (swap) {
    tmp.resize((size_t)impl_->nmesh3);
    for (std::int64_t i = 0; i < impl_->nmesh3; ++i) {
      tmp[(size_t)i] = swap_float(block[i]);
    }
    src = tmp.data();
  }

  impl_->file.seekp(offset, std::ios::beg);
  if (!impl_->file) {
    throw FieldError("TileWriter: seek failed for tile " +
                      std::to_string(ibox));
  }
  impl_->file.write(reinterpret_cast<const char*>(src),
                     (std::streamsize)(impl_->nmesh3 * (std::int64_t)sizeof(float)));
  if (!impl_->file) {
    throw FieldError("TileWriter: write failed for tile " +
                      std::to_string(ibox) + " (short buffer or I/O error)");
  }
  impl_->file.flush();
  impl_->written[(size_t)(ibox - 1)] = true;
}

void TileWriter::finish() {
  std::int64_t missing = 0;
  for (bool w : impl_->written) {
    if (!w) ++missing;
  }
  if (missing > 0) {
    throw FieldError("TileWriter::finish: " + std::to_string(missing) +
                      " of " + std::to_string(impl_->total_tiles) +
                      " tiles were never written; the field file has a "
                      "hole and would read as zeros there");
  }
}

// ---------------------------------------------------------------------------
// TileReader
// ---------------------------------------------------------------------------

struct TileReader::Impl {
  PeakPatchGrid grid;
  std::ifstream file;
  std::int64_t nmesh3 = 0;
};

TileReader::TileReader(const std::string& path, const PeakPatchGrid& grid)
    : impl_(new Impl{}) {
  impl_->grid = grid;
  impl_->nmesh3 = (std::int64_t)grid.nmesh * grid.nmesh * grid.nmesh;
  impl_->file.open(path, std::ios::binary | std::ios::in);
  if (!impl_->file) {
    throw FieldError("TileReader: cannot open '" + path + "'");
  }
}

TileReader::~TileReader() { delete impl_; }

void TileReader::read_tile(std::int64_t ibox, float* block) {
  std::int64_t offset = tile_offset(impl_->grid, ibox);  // throws if OOB
  impl_->file.seekg(offset, std::ios::beg);
  if (!impl_->file) {
    throw FieldError("TileReader: seek failed for tile " +
                      std::to_string(ibox));
  }
  impl_->file.read(reinterpret_cast<char*>(block),
                    (std::streamsize)(impl_->nmesh3 * (std::int64_t)sizeof(float)));
  if (!impl_->file) {
    throw FieldError("TileReader: short read for tile " +
                      std::to_string(ibox) + "; file is truncated");
  }
  if (host_is_big_endian()) {
    for (std::int64_t i = 0; i < impl_->nmesh3; ++i) {
      block[i] = swap_float(block[i]);
    }
  }
}

std::int64_t TileReader::n_tiles() const { return impl_->grid.n_tiles(); }

// ---------------------------------------------------------------------------
// global <-> tiles (test / small-run convenience, whole field in memory)
// ---------------------------------------------------------------------------

void tile_from_global(const PeakPatchGrid& grid, const float* global,
                      const std::string& out_path) {
  const int nmesh = grid.nmesh;
  const int nbuff = grid.nbuff;
  const int nsub = grid.nsub();
  const int core = grid.core_grid();
  const std::int64_t nmesh3 = (std::int64_t)nmesh * nmesh * nmesh;

  TileWriter w(out_path, grid);
  std::vector<float> block((size_t)nmesh3);

  for (std::int64_t ibox = 1; ibox <= grid.n_tiles(); ++ibox) {
    int k1, k2, k3;
    tile_coords(grid, ibox, k1, k2, k3);
    for (int lz = 0; lz < nmesh; ++lz) {
      int gz = global_coord(k3, lz, nsub, nbuff, core);
      std::int64_t z_term = (std::int64_t)gz * core * core;
      std::int64_t out_z = (std::int64_t)lz * nmesh * nmesh;
      for (int ly = 0; ly < nmesh; ++ly) {
        int gy = global_coord(k2, ly, nsub, nbuff, core);
        std::int64_t row_base = z_term + (std::int64_t)gy * core;
        std::int64_t out_base = out_z + (std::int64_t)ly * nmesh;
        for (int lx = 0; lx < nmesh; ++lx) {
          int gx = global_coord(k1, lx, nsub, nbuff, core);
          block[(size_t)(out_base + lx)] = global[(size_t)(row_base + gx)];
        }
      }
    }
    w.write_tile(ibox, block.data());
  }
  w.finish();
}

void global_from_tiles(const PeakPatchGrid& grid, const std::string& in_path,
                       float* global) {
  const int nmesh = grid.nmesh;
  const int nbuff = grid.nbuff;
  const int nsub = grid.nsub();
  const int core = grid.core_grid();
  const std::int64_t nmesh3 = (std::int64_t)nmesh * nmesh * nmesh;

  TileReader r(in_path, grid);
  std::vector<float> block((size_t)nmesh3);

  for (std::int64_t ibox = 1; ibox <= grid.n_tiles(); ++ibox) {
    int k1, k2, k3;
    tile_coords(grid, ibox, k1, k2, k3);
    r.read_tile(ibox, block.data());
    for (int lz = 0; lz < nsub; ++lz) {
      int gz = (k3 - 1) * nsub + lz;
      std::int64_t z_term = (std::int64_t)gz * core * core;
      std::int64_t in_z = (std::int64_t)(lz + nbuff) * nmesh * nmesh;
      for (int ly = 0; ly < nsub; ++ly) {
        int gy = (k2 - 1) * nsub + ly;
        std::int64_t row_base = z_term + (std::int64_t)gy * core;
        std::int64_t in_base = in_z + (std::int64_t)(ly + nbuff) * nmesh + nbuff;
        for (int lx = 0; lx < nsub; ++lx) {
          int gx = (k1 - 1) * nsub + lx;
          global[(size_t)(row_base + gx)] = block[(size_t)(in_base + lx)];
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// stats
// ---------------------------------------------------------------------------

FieldStats field_stats(const std::string& path, const PeakPatchGrid& grid) {
  const int nmesh = grid.nmesh;
  const int nbuff = grid.nbuff;
  const int nsub = grid.nsub();
  const std::int64_t nmesh3 = (std::int64_t)nmesh * nmesh * nmesh;

  TileReader r(path, grid);
  std::vector<float> block((size_t)nmesh3);

  FieldStats s;
  double sum = 0.0, sumsq = 0.0;
  std::int64_t n_finite = 0;
  double minv = 0.0, maxv = 0.0;
  bool have_finite = false;

  for (std::int64_t ibox = 1; ibox <= grid.n_tiles(); ++ibox) {
    r.read_tile(ibox, block.data());
    for (int lz = nbuff; lz < nbuff + nsub; ++lz) {
      std::int64_t z_base = (std::int64_t)lz * nmesh * nmesh;
      for (int ly = nbuff; ly < nbuff + nsub; ++ly) {
        std::int64_t base = z_base + (std::int64_t)ly * nmesh;
        for (int lx = nbuff; lx < nbuff + nsub; ++lx) {
          float v = block[(size_t)(base + lx)];
          ++s.n_cells;
          if (std::isnan(v)) {
            ++s.n_nan;
            continue;
          }
          if (std::isinf(v)) {
            ++s.n_inf;
            continue;
          }
          double dv = (double)v;
          sum += dv;
          sumsq += dv * dv;
          if (!have_finite) {
            minv = maxv = dv;
            have_finite = true;
          } else {
            if (dv < minv) minv = dv;
            if (dv > maxv) maxv = dv;
          }
          ++n_finite;
        }
      }
    }
  }

  if (n_finite > 0) {
    s.mean = sum / (double)n_finite;
    double var = sumsq / (double)n_finite - s.mean * s.mean;
    if (var < 0.0) var = 0.0;  // guard against float round-off
    s.stddev = std::sqrt(var);
    s.min = minv;
    s.max = maxv;
  }
  return s;
}

// ---------------------------------------------------------------------------
// synthetic fields
// ---------------------------------------------------------------------------

void write_spike_field(const std::string& path, const PeakPatchGrid& grid,
                       int i, int j, int k, float amplitude) {
  const int nmesh = grid.nmesh;
  const int nbuff = grid.nbuff;
  const int nsub = grid.nsub();
  const int core = grid.core_grid();
  const std::int64_t nmesh3 = (std::int64_t)nmesh * nmesh * nmesh;

  int si = wrap_mod(i, core);
  int sj = wrap_mod(j, core);
  int sk = wrap_mod(k, core);

  TileWriter w(path, grid);
  std::vector<float> block((size_t)nmesh3);

  for (std::int64_t ibox = 1; ibox <= grid.n_tiles(); ++ibox) {
    int k1, k2, k3;
    tile_coords(grid, ibox, k1, k2, k3);
    std::fill(block.begin(), block.end(), 0.0f);
    for (int lz = 0; lz < nmesh; ++lz) {
      int gz = global_coord(k3, lz, nsub, nbuff, core);
      if (gz != sk) continue;
      std::int64_t out_z = (std::int64_t)lz * nmesh * nmesh;
      for (int ly = 0; ly < nmesh; ++ly) {
        int gy = global_coord(k2, ly, nsub, nbuff, core);
        if (gy != sj) continue;
        std::int64_t out_base = out_z + (std::int64_t)ly * nmesh;
        for (int lx = 0; lx < nmesh; ++lx) {
          int gx = global_coord(k1, lx, nsub, nbuff, core);
          if (gx == si) {
            block[(size_t)(out_base + lx)] = amplitude;
          }
        }
      }
    }
    w.write_tile(ibox, block.data());
  }
  w.finish();
}

void write_uniform_displacement(const std::string& path,
                                const PeakPatchGrid& grid, int /*axis*/,
                                float value) {
  const std::int64_t nmesh3 =
      (std::int64_t)grid.nmesh * grid.nmesh * grid.nmesh;

  TileWriter w(path, grid);
  std::vector<float> block((size_t)nmesh3, value);
  for (std::int64_t ibox = 1; ibox <= grid.n_tiles(); ++ibox) {
    w.write_tile(ibox, block.data());
  }
  w.finish();
}

// ---------------------------------------------------------------------------
// FieldMeta sidecar
// ---------------------------------------------------------------------------

namespace {

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

}  // namespace

std::string FieldMeta::to_ini() const {
  std::ostringstream os;
  os << std::setprecision(17);
  os << "core_grid=" << core_grid << "\n";
  os << "nmesh=" << nmesh << "\n";
  os << "nbuff=" << nbuff << "\n";
  os << "ntile=" << ntile << "\n";
  os << "boxsize=" << boxsize << "\n";
  os << "music_level=" << music_level << "\n";
  os << "redshift=" << redshift << "\n";
  os << "units=" << units << "\n";
  os << "byte_order=" << byte_order << "\n";
  os << "eta_convention=" << eta_convention << "\n";
  os << "source=" << source << "\n";
  return os.str();
}

FieldMeta FieldMeta::from_ini(const std::string& text) {
  FieldMeta m;
  std::istringstream is(text);
  std::string line;
  while (std::getline(is, line)) {
    std::string t = trim(line);
    if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[') continue;
    size_t eq = t.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(t.substr(0, eq));
    std::string val = trim(t.substr(eq + 1));
    if (key == "core_grid") m.core_grid = std::stoi(val);
    else if (key == "nmesh") m.nmesh = std::stoi(val);
    else if (key == "nbuff") m.nbuff = std::stoi(val);
    else if (key == "ntile") m.ntile = std::stoi(val);
    else if (key == "boxsize") m.boxsize = std::stod(val);
    else if (key == "music_level") m.music_level = std::stoi(val);
    else if (key == "redshift") m.redshift = std::stod(val);
    else if (key == "units") m.units = val;
    else if (key == "byte_order") m.byte_order = val;
    else if (key == "eta_convention") m.eta_convention = val;
    else if (key == "source") m.source = val;
  }
  return m;
}

}  // namespace ppmi
