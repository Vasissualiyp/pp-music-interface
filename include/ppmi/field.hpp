// field.hpp - the raw density and displacement fields MUSIC hands to PeakPatch.
//
// This is the HPC surface of the interface. A survey run at 4096^3 is 275 GB
// per field and there are four of them, so nothing here may assume a field
// fits in memory. Every routine works a tile at a time.
//
// ---------------------------------------------------------------------------
// LAYOUT - read this before writing any field, it is the easiest thing in the
// whole pipeline to get silently wrong.
// ---------------------------------------------------------------------------
//
// PeakPatch reads external fields in readsubbox(),
// peakpatch/src/hpkvd/hpkvdmodule.f90 lines 1887-1976. That routine has three
// code paths selected by a hardcoded `parallel_read` variable, and the ACTIVE
// one is `parallel_read == 0`:
//
//     offset = int(4,8)*(ibox-1)*n1**3+1
//     open(unit=30,file=filename,access='stream')
//     read(30,pos=int(offset)) F
//
// with F dimensioned (n1,n2,n3) = nmesh^3. So the file on disk is a sequence
// of ntile^3 PER-TILE BLOCKS, each nmesh^3 float32 in Fortran order, indexed
// by ibox. It is NOT one global cube.
//
// The dormant `parallel_read == 1` path instead uses mpi_type_create_subarray
// over a global next^3 cube. That is the layout peakpatch/src/modules/padICs
// produces. The two layouts coincide only when ntile == 1, because then
// next == nmesh and there is exactly one block. For ntile > 1 they differ, and
// writing a global cube would be read as garbage by the active path.
//
// So: WRITE TILE BLOCKS. Each block carries its own buffer, which means
// neighbouring blocks overlap by nbuff cells on every shared face, and cells
// near a box face wrap periodically.
//
// Block ordering follows make_boxes() in the same file: the counter increments
// with the x tile index innermost, then y, then z. For 1-based tile indices
// (k1,k2,k3),
//
//     ibox = k1 + nlx*(k2-1) + nlx*nly*(k3-1)
//
// with nlx = nly = nlz = ntile. Byte offset of block ibox is
// 4*(ibox-1)*nmesh^3, zero-based.
//
// Within a block, Fortran order: the first index varies fastest. A C++ writer
// filling a contiguous buffer must therefore write x fastest. Whether
// PeakPatch's first index corresponds to MUSIC's first index is exactly what
// task P3-T3 tests; do not assume it, and do not compensate for a mismatch
// anywhere but here.
//
// ---------------------------------------------------------------------------
// FILES AND SIGN
// ---------------------------------------------------------------------------
//
// read_external_field() at hpkvdmodule.f90 lines 1826-1883 reads four files
// for a given `filein` stem:
//
//     Fvec_<stem>   linear density contrast
//     etax_<stem>   displacement, x
//     etay_<stem>   displacement, y
//     etaz_<stem>   displacement, z
//
// and then negates the three eta fields, with the comment
// "eta = -displacement, so need to multiply by -1". So PeakPatch's on-disk eta
// is the NEGATED displacement. A writer producing MUSIC's displacement
// directly must flip the sign, or PeakPatch will displace every halo backwards.
// Task P3-T3's displacement spike pins this down; until it passes, treat the
// convention as unproven.
//
// No header, no Fortran record markers, float32, little-endian assumed.
// PeakPatch byte-swaps on read only if the host is big-endian
// (hpkvdmodule.f90 lines 1959-1972), so a writer must always emit
// little-endian regardless of host.
//
// Because nothing in that format is self-describing, every field set written
// by this code carries a sidecar (see FieldMeta) recording grid size, box
// length, level, units, byte order, sign convention and the redshift the field
// is defined at. The sidecar is the only defence against a silent convention
// drift, and P5-T2 depends on the redshift it records.

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "ppmi/geometry.hpp"

namespace ppmi {

class FieldError : public std::runtime_error {
 public:
  explicit FieldError(const std::string& what) : std::runtime_error(what) {}
};

// What the binary format cannot say about itself.
struct FieldMeta {
  int core_grid = 0;        // nsub*ntile, the unpadded global side
  int nmesh = 0;            // per-tile side including buffers
  int nbuff = 0;
  int ntile = 0;
  double boxsize = 0.0;
  int music_level = 0;      // which MUSIC level produced this
  double redshift = 0.0;    // the redshift the field is defined at
  std::string units = "Mpc";
  std::string byte_order = "little";
  // "negated_displacement" when eta files hold -displacement, PeakPatch's own
  // convention; "displacement" when they hold it unflipped.
  std::string eta_convention = "negated_displacement";
  std::string source = "";  // free text, e.g. the MUSIC commit

  // Serialise to and from a small INI sidecar written beside the field files.
  std::string to_ini() const;
  static FieldMeta from_ini(const std::string& text);
};

// Byte offset of a tile block, zero-based. ibox is 1-based to match Fortran.
// Throws FieldError if ibox is outside [1, ntile^3].
std::int64_t tile_offset(const PeakPatchGrid& grid, std::int64_t ibox);

// Convert 1-based tile indices to ibox, and back. x innermost, per make_boxes.
std::int64_t tile_index(const PeakPatchGrid& grid, int k1, int k2, int k3);
void tile_coords(const PeakPatchGrid& grid, std::int64_t ibox, int& k1, int& k2,
                 int& k3);

// Total size in bytes a complete field file must have.
std::int64_t field_file_bytes(const PeakPatchGrid& grid);

// Streaming writer. Opens the file, then accepts tile blocks in any order and
// seeks to the right offset for each. Always emits little-endian float32.
class TileWriter {
 public:
  TileWriter(const std::string& path, const PeakPatchGrid& grid);
  ~TileWriter();

  // block must hold exactly nmesh^3 floats in Fortran order.
  // Throws FieldError on a short buffer or a write failure.
  void write_tile(std::int64_t ibox, const float* block);

  // Throws FieldError if any tile was never written, naming how many are
  // missing. A field file with a hole in it reads as zeros and produces a
  // plausible-looking but wrong catalogue, so this check is not optional.
  void finish();

 private:
  struct Impl;
  Impl* impl_;
};

// Streaming reader, the mirror of TileWriter.
class TileReader {
 public:
  TileReader(const std::string& path, const PeakPatchGrid& grid);
  ~TileReader();

  // Fills block with nmesh^3 floats, byte-swapping if the host is big-endian.
  void read_tile(std::int64_t ibox, float* block);

  std::int64_t n_tiles() const;

 private:
  struct Impl;
  Impl* impl_;
};

// Build the tile-blocked representation of a global unpadded cube.
// `global` is core_grid^3 floats in Fortran order and must fit in memory, so
// this is for tests and small runs only; the production path builds each tile
// directly from MUSIC's distributed grid. Periodic wrapping supplies the
// buffer cells.
void tile_from_global(const PeakPatchGrid& grid, const float* global,
                      const std::string& out_path);

// The inverse: reconstruct the global unpadded cube by taking each tile's core
// region. Round-tripping through tile_from_global must be exact.
void global_from_tiles(const PeakPatchGrid& grid, const std::string& in_path,
                       float* global);

// Field statistics, computed a tile at a time over core regions only so that
// buffer overlap is not double counted.
struct FieldStats {
  double mean = 0.0;
  double stddev = 0.0;
  double min = 0.0;
  double max = 0.0;
  std::int64_t n_nan = 0;
  std::int64_t n_inf = 0;
  std::int64_t n_cells = 0;
};
FieldStats field_stats(const std::string& path, const PeakPatchGrid& grid);

// Pad an unpadded core field into the layout PeakPatch's LIVE reader wants.
//
// IMPORTANT, and a correction to the tile-blocked layout described above.
// readsubbox(), which reads ntile^3 per-tile blocks, belongs to
// read_external_field(), and that routine is DEAD CODE: it is defined and never
// called (00_FINDINGS.md section 10). The live ireadfield=1 path is
// RandomField_Input (RandomField.f90:1202-1222), and it reads a plain GLOBAL
// CUBE of side n = nsub*ntile + 2*nbuff, in Fortran order, each MPI rank taking
// its own z-slab at byte offset 4*n*n*local_z_start:
//
//     read(33,pos=offset) (((delta(i,j,k),i=1,n),j=1,n),k=1,local_nz)
//
// The two layouts coincide when ntile == 1, because then next == nmesh and the
// tile-blocked file is a single nmesh^3 block. That is why the tile-blocked
// tests pass and why this distinction went unnoticed. For ntile > 1 they differ
// and only this one is read.
//
// Reads core_grid^3 float32 from in_path and writes n_ext^3 float32 to
// out_path, filling the nbuff-cell border by periodic wraparound. This is what
// PeakPatch's own padICs utility does; doing it here avoids depending on that
// binary being built.
// `scale` multiplies every value on the way through. The pipeline uses it to
// carry MUSIC's field, written at zstart, to the z=0 linear amplitude
// PeakPatch expects: PeakPatch applies its own D(z) internally, so handing it
// a z=50 field makes every peak ~40x too shallow and it finds no halos at all.
void pad_core_to_next(const PeakPatchGrid& grid, const std::string& in_path,
                      const std::string& out_path, double scale = 1.0);

// Synthesise a field that is zero everywhere except one cell, for the P3-T3
// spike test. Indices are zero-based in the global unpadded cube. Deliberately
// asymmetric indices make a transposition detectable.
void write_spike_field(const std::string& path, const PeakPatchGrid& grid,
                       int i, int j, int k, float amplitude);

// Synthesise a field holding a uniform displacement along one axis, for the
// sign half of the P3-T3 test. axis is 0, 1 or 2.
void write_uniform_displacement(const std::string& path,
                                const PeakPatchGrid& grid, int axis,
                                float value);

}  // namespace ppmi
