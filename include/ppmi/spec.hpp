// spec.hpp - the run specification, its invariants, and config generation.
//
// One INI file is the single source of truth for a pipeline run. Both MUSIC's
// .conf and PeakPatch's parameters.ini are generated from it. This replaces the
// previous arrangement, where one hand-edited INI served both codes and was
// patched between stages with sed (run_pp_music.sh lines 172-216).
//
// INI rather than TOML or YAML deliberately: both codes already speak it, the
// parser is fifty lines, and the orchestrator stays a dependency-free binary
// that drops onto any cluster without an environment to install.
//
// Example ppmi.ini:
//
//     [meta]
//     name = m12_z20
//
//     [cosmology]
//     Omega_m = 0.3099
//     Omega_b = 0.0489
//     Omega_L = 0.6901
//     H0 = 67.74
//     sigma_8 = 0.8159
//     nspec = 0.9667
//     transfer = camb_file
//     transfer_file = camb.dat
//
//     [box]
//     boxlength = 1248.0     ; MUSIC boxlength and PeakPatch boxsize, one number
//     levelmin = 9           ; survey grid is 2^levelmin
//
//     [random]
//     seed_levelmin = 12345
//     cubesize = 32
//
//     [survey]
//     zstart = 50.0
//     levelmax = 9           ; must equal levelmin; the survey is unigrid
//
//     [peakpatch]
//     ntile = 8
//     nbuff = 64
//     global_redshift = 20.0
//     Rsmooth_max = 34.0
//     ievol = 0
//     num_redshifts = 1
//     cen = 0.0,0.0,0.0
//
//     [zoom]
//     levelmax = 12
//     padding = 8
//     extent_factor = 3.0
//     region = box
//
//     [run]
//     ranks = 4
//
// Validation is the point of this file. Each rule is its own function so it can
// be tested in isolation and so an error message can name the rule that fired.

#pragma once

#include <map>
#include <memory>
#include <utility>
#include <stdexcept>
#include <string>
#include <vector>

#include "ppmi/catalog.hpp"
#include "ppmi/geometry.hpp"

namespace ppmi {

class SpecError : public std::runtime_error {
 public:
  explicit SpecError(const std::string& w) : std::runtime_error(w) {}
};

// The TOML is malformed, or a required key is missing. Messages name the
// section and the key, never just "missing key".
class SpecSyntaxError : public SpecError {
 public:
  explicit SpecSyntaxError(const std::string& w) : SpecError(w) {}
};

// A hard rule was broken. The run would produce a wrong answer silently.
class InvariantViolation : public SpecError {
 public:
  explicit InvariantViolation(const std::string& w) : SpecError(w) {}
};

// ---------------------------------------------------------------------------
// INI
// ---------------------------------------------------------------------------

// A parsed INI file: section name to key/value map, insertion order preserved
// for the sections and keys so that generation is deterministic.
class Ini {
 public:
  static Ini parse(const std::string& text);
  static Ini load(const std::string& path);

  bool has(const std::string& section, const std::string& key) const;
  // Throws SpecSyntaxError naming section and key when absent.
  std::string get(const std::string& section, const std::string& key) const;
  std::string get_or(const std::string& section, const std::string& key,
                     const std::string& fallback) const;
  int get_int(const std::string& section, const std::string& key) const;
  double get_double(const std::string& section, const std::string& key) const;
  bool get_bool(const std::string& section, const std::string& key) const;
  Vec3 get_vec3(const std::string& section, const std::string& key) const;

  void set(const std::string& section, const std::string& key,
           const std::string& value);

  std::string to_string() const;
  const std::vector<std::string>& sections() const;
  const std::vector<std::pair<std::string, std::string>>& entries(
      const std::string& section) const;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

// Comment characters follow MUSIC's own parser (music_mpi/src/config_file.hh):
// '#', ';' and '%' start a comment anywhere in a line. Booleans accept
// true/yes/on/1 and false/no/off/0, and anything else is an error rather than
// a silent false.

// ---------------------------------------------------------------------------
// RunSpec
// ---------------------------------------------------------------------------

struct RunSpec {
  Ini ini;
  std::string source_path;

  int levelmin() const;
  double boxlength() const;
  int survey_levelmax() const;
  int zoom_levelmax() const;
  int ranks() const;

  // The PeakPatch grid this spec implies. nmesh is SOLVED from levelmin, ntile
  // and nbuff rather than specified, so it cannot disagree with MUSIC by
  // construction.
  PeakPatchGrid peakpatch_grid() const;
  BoxFrame frame() const;
  Cosmology cosmology() const;
};

RunSpec load_spec(const std::string& path);

// ---------------------------------------------------------------------------
// Rules, each independently testable
// ---------------------------------------------------------------------------

// The rule the whole pipeline rests on. MUSIC builds white noise as a
// coarse-to-fine cascade (music_mpi/src/random.cc lines 1898-2036), so the
// survey and zoom stages share a realization only when levelmin, the whole
// [random] block and cubesize are identical. Because this spec holds them once
// and both stages read them, the check is that neither stage OVERRIDES them.
// Throws InvariantViolation naming the offending key and explaining that the
// two stages would otherwise be different universes.
void check_realization_invariant(const RunSpec& spec);

// 2^levelmin == nsub*ntile. Throws InvariantViolation carrying the message
// from describe_mismatch().
void check_grid_pairing(const RunSpec& spec);

// The survey stage must have levelmax == levelmin. A zoomed survey would hand
// PeakPatch a field that is not uniform, which its tiling cannot represent.
void check_survey_is_unigrid(const RunSpec& spec);

// A single-redshift halo search needs ievol=0 and num_redshifts=1. With
// ievol=1 PeakPatch produces a lightcone and "the largest halo at z=20" stops
// being well defined without a distance filter. The shipped
// param/parameters.ini has ievol=1 with global_redshift=0, which is exactly
// this mistake.
void check_redshift_mode(const RunSpec& spec);

// zoom.levelmax >= levelmin, and both within MUSIC's usable range.
void check_levels(const RunSpec& spec);

// ref_center in [0,1), ref_extent in (0,1], region inside the box.
void check_zoom_region(const RefRegion& region);

// MUSIC's FFTW-MPI transpose uses a 32-bit message count.
// music_mpi/src/poisson.cc lines 540-579 aborts when
// local_n0*local_n1*(nz/2+1) exceeds INT_MAX, because exceeding it silently
// corrupts the FFT rather than failing. For a grid G^3 on P ranks the
// requirement is (G/P)^2 * (G/2+1) < 2^31.
//
// Returns warnings rather than throwing, and reports the minimum safe rank
// count when the check fails, so a run can be fixed at submission time instead
// of after an hour of compute.
std::vector<std::string> check_mpi_overflow(const RunSpec& spec);

// Run every rule. Returns warnings; throws InvariantViolation on hard errors.
// Order matters for the quality of the message: syntax-level things first,
// then the grid pairing, then the realization invariant, so the user sees the
// most fundamental problem rather than a downstream symptom.
std::vector<std::string> validate(const RunSpec& spec);

// ---------------------------------------------------------------------------
// Config generation
// ---------------------------------------------------------------------------
//
// Key names are not invented here: MUSIC's are those its parser actually
// reads, catalogued in plan/00_FINDINGS.md section 2, and PeakPatch's are those
// config_reader.f90 accepts.
//
// Generation must be deterministic. The same spec produces byte-identical
// output every time, including key order, so that a diff between two generated
// configs shows only real differences and a manifest hash means something.

enum class Stage { kSurvey, kZoom };

// MUSIC config keys stage C must inherit unchanged from stage A. Writing any
// of these differently between the two stages breaks the realization
// invariant.
extern const char* const kFrozenKeys[];

// Render a MUSIC config as text.
// The survey stage emits levelmin == levelmax, baryons = no, and the peakpatch
// output plugin. The zoom stage emits the raised levelmax, baryons = yes,
// use_2LPT, the three calculate_* keys, the region keys, and a Gadget-family
// output plugin. Both stages emit an identical [random] block; that is not a
// convenience, it is the invariant.
std::string music_conf(const RunSpec& spec, Stage stage,
                       const RefRegion* ref = nullptr);

// Render a PeakPatch parameter file as text. nmesh is solved rather than
// copied, ireadfield is 1 since the field comes from stage A, and boxsize is
// written explicitly. The shipped example omits boxsize entirely and silently
// takes a default.
std::string peakpatch_ini(const RunSpec& spec);

// Validate, render and write the configs for one stage. Validation runs before
// anything is written, so an invalid spec never leaves a half-configured
// directory behind. Returns role to path.
// Parse a MUSIC config back into {section: {key: value}}. Needed by the
// manifest verifier and by tests. Mirrors music_mpi/src/config_file.hh:
// [section] headers, key = value, and '#', ';' or '%' starting a comment
// anywhere in a line.
std::map<std::string, std::map<std::string, std::string>> parse_music_conf(
    const std::string& text);

std::map<std::string, std::string> write_stage_configs(
    const RunSpec& spec, const std::string& outdir, Stage stage,
    const RefRegion* ref = nullptr);

}  // namespace ppmi
