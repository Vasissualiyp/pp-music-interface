// Tests for the run spec, its invariants, and config generation.
//
// The realization invariant test is the most important one in the repository.
// If it does not hold, the zoom stage refines a different universe than the one
// the halo was found in, and nothing in the output says so.

#include "ppmi/spec.hpp"

#include <map>
#include <string>
#include <vector>

#include "ppmi_test.hpp"

using namespace ppmi;

namespace {

const char* kValidSpec = R"(
[meta]
name = test_run

[cosmology]
Omega_m = 0.3099
Omega_b = 0.0489
Omega_L = 0.6901
H0 = 67.74
sigma_8 = 0.8159
nspec = 0.9667
transfer = camb_file
transfer_file = camb.dat

[box]
boxlength = 100.0
levelmin = 8

[random]
seed_levelmin = 12345
cubesize = 32

[survey]
zstart = 50.0
levelmax = 8

[peakpatch]
ntile = 1
nbuff = 16
global_redshift = 20.0
Rsmooth_max = 20.0
ievol = 0
num_redshifts = 1
cen = 0.0,0.0,0.0

[zoom]
levelmax = 11
padding = 8
extent_factor = 3.0
region = box

[run]
ranks = 4
)";

RunSpec spec_from(const std::string& text) {
  RunSpec s;
  s.ini = Ini::parse(text);
  return s;
}

RunSpec valid() { return spec_from(kValidSpec); }

// Return the spec text with one key replaced, for the negative cases.
std::string with(const std::string& section, const std::string& key,
                 const std::string& value) {
  Ini ini = Ini::parse(kValidSpec);
  ini.set(section, key, value);
  return ini.to_string();
}

}  // namespace

// --- INI parsing -----------------------------------------------------------

TEST("ini: sections, keys and types") {
  Ini ini = Ini::parse(kValidSpec);
  CHECK(ini.has("box", "levelmin"));
  CHECK(!ini.has("box", "nonsense"));
  CHECK_EQ(ini.get_int("box", "levelmin"), 8);
  CHECK_CLOSE(ini.get_double("box", "boxlength"), 100.0, 1e-12);
  CHECK(ini.get("cosmology", "transfer") == "camb_file");
}

TEST("ini: a missing key names the section and the key") {
  Ini ini = Ini::parse(kValidSpec);
  CHECK_THROWS(ini.get("box", "missing"), SpecSyntaxError);
  CHECK(ini.get_or("box", "missing", "fallback") == "fallback");
}

TEST("ini: all three comment characters are honoured, like MUSIC's parser") {
  Ini ini = Ini::parse("[s]\na = 1 # hash\nb = 2 ; semi\nc = 3 % percent\n");
  CHECK_EQ(ini.get_int("s", "a"), 1);
  CHECK_EQ(ini.get_int("s", "b"), 2);
  CHECK_EQ(ini.get_int("s", "c"), 3);
}

TEST("ini: booleans accept MUSIC's vocabulary and reject anything else") {
  Ini ini = Ini::parse("[s]\na = yes\nb = off\nc = 1\nd = false\ne = maybe\n");
  CHECK(ini.get_bool("s", "a"));
  CHECK(!ini.get_bool("s", "b"));
  CHECK(ini.get_bool("s", "c"));
  CHECK(!ini.get_bool("s", "d"));
  CHECK_THROWS(ini.get_bool("s", "e"), SpecSyntaxError);
}

TEST("ini: a comma triple parses as a vector") {
  Ini ini = Ini::parse("[s]\ncen = 1.5,-2.5,3.0\n");
  Vec3 v = ini.get_vec3("s", "cen");
  CHECK_CLOSE(v[0], 1.5, 1e-12);
  CHECK_CLOSE(v[1], -2.5, 1e-12);
  CHECK_CLOSE(v[2], 3.0, 1e-12);
}

TEST("ini: round trips through to_string with sections and order preserved") {
  Ini a = Ini::parse(kValidSpec);
  Ini b = Ini::parse(a.to_string());
  CHECK(a.to_string() == b.to_string());
}

// --- the valid spec --------------------------------------------------------

TEST("spec: the reference spec validates and derives a consistent grid") {
  RunSpec s = valid();
  std::vector<std::string> warnings = validate(s);
  CHECK_EQ(s.levelmin(), 8);
  CHECK_CLOSE(s.boxlength(), 100.0, 1e-12);
  PeakPatchGrid g = s.peakpatch_grid();
  // nmesh is solved, not specified, so the pairing holds by construction.
  CHECK(check_pairing(8, g));
  CHECK_EQ(g.nmesh, 288);
  CHECK_EQ(g.core_grid(), 256);
}

TEST("spec: the frame comes from boxlength and cen") {
  RunSpec s = valid();
  BoxFrame f = s.frame();
  CHECK_CLOSE(f.boxsize, 100.0, 1e-12);
  CHECK_CLOSE(f.cen[0], 0.0, 1e-12);
}

TEST("spec: cosmology carries total matter, not just cold dark matter") {
  RunSpec s = valid();
  Cosmology c = s.cosmology();
  CHECK_CLOSE(c.Omega_m, 0.3099, 1e-12);
  CHECK_CLOSE(c.h, 0.6774, 1e-12);
}

// --- each rule, on its own broken spec -------------------------------------

TEST("rule: the survey must be unigrid") {
  RunSpec bad = spec_from(with("survey", "levelmax", "10"));
  CHECK_THROWS(check_survey_is_unigrid(bad), InvariantViolation);
  check_survey_is_unigrid(valid());
}

TEST("rule: the grid must pair with levelmin") {
  // 2^8 = 256 is not divisible by 3 tiles, so no nmesh exists.
  RunSpec bad = spec_from(with("peakpatch", "ntile", "3"));
  CHECK_THROWS(check_grid_pairing(bad), SpecError);
  check_grid_pairing(valid());
}

TEST("rule: a lightcone run is not a single-redshift halo search") {
  RunSpec bad = spec_from(with("peakpatch", "ievol", "1"));
  CHECK_THROWS(check_redshift_mode(bad), InvariantViolation);
  RunSpec bad2 = spec_from(with("peakpatch", "num_redshifts", "4"));
  CHECK_THROWS(check_redshift_mode(bad2), InvariantViolation);
  check_redshift_mode(valid());
}

TEST("rule: the zoom cannot refine below the survey") {
  RunSpec bad = spec_from(with("zoom", "levelmax", "7"));
  CHECK_THROWS(check_levels(bad), InvariantViolation);
  check_levels(valid());
}

TEST("rule: a stage may not override the realization keys") {
  // The whole point: levelmin, the random block and cubesize are declared once.
  Ini ini = Ini::parse(kValidSpec);
  ini.set("zoom", "levelmin", "7");
  RunSpec bad = spec_from(ini.to_string());
  CHECK_THROWS(check_realization_invariant(bad), InvariantViolation);

  Ini ini2 = Ini::parse(kValidSpec);
  ini2.set("zoom", "cubesize", "64");
  CHECK_THROWS(check_realization_invariant(spec_from(ini2.to_string())),
               InvariantViolation);

  Ini ini3 = Ini::parse(kValidSpec);
  ini3.set("survey", "seed_levelmin", "999");
  CHECK_THROWS(check_realization_invariant(spec_from(ini3.to_string())),
               InvariantViolation);

  check_realization_invariant(valid());
}

TEST("rule: the zoom region must lie inside the unit box") {
  check_zoom_region(RefRegion{{0.5, 0.5, 0.5}, {0.1, 0.1, 0.1}});
  CHECK_THROWS(check_zoom_region(RefRegion{{1.5, 0.5, 0.5}, {0.1, 0.1, 0.1}}),
               InvariantViolation);
  CHECK_THROWS(check_zoom_region(RefRegion{{0.5, 0.5, 0.5}, {0.0, 0.1, 0.1}}),
               InvariantViolation);
  CHECK_THROWS(check_zoom_region(RefRegion{{0.5, 0.5, 0.5}, {1.5, 0.1, 0.1}}),
               InvariantViolation);
}

TEST("rule: the MPI overflow guard warns and names a safe rank count") {
  // (G/P)^2 * (G/2+1) < INT_MAX. At G=4096, P=4 gives 1024^2 * 2049 > INT_MAX,
  // and MUSIC's own guard (poisson.cc:556-564) doubles ranks with integer slab
  // division, so the minimum safe count is 8, not the continuous ~5.
  Ini ini = Ini::parse(kValidSpec);
  ini.set("box", "levelmin", "12");
  ini.set("survey", "levelmax", "12");
  ini.set("peakpatch", "ntile", "1");
  ini.set("run", "ranks", "4");
  std::vector<std::string> w = check_mpi_overflow(spec_from(ini.to_string()));
  CHECK(!w.empty());
  bool names_eight = false;
  for (const std::string& s : w) {
    if (s.find("at least 8 ranks") != std::string::npos) names_eight = true;
  }
  CHECK(names_eight);

  // Small grids are always safe.
  CHECK(check_mpi_overflow(valid()).empty());
}

// --- generation ------------------------------------------------------------

TEST("generate: the survey config is unigrid, dark matter only, peakpatch out") {
  RunSpec s = valid();
  std::string conf = music_conf(s, Stage::kSurvey);
  auto sec = parse_music_conf(conf);
  CHECK(sec["setup"]["levelmin"] == "8");
  CHECK(sec["setup"]["levelmax"] == "8");
  CHECK(sec["output"]["format"] == "peakpatch");
}

TEST("generate: the zoom config raises levelmax and turns on baryons") {
  RunSpec s = valid();
  RefRegion ref{{0.5, 0.5, 0.5}, {0.06, 0.06, 0.06}};
  std::string conf = music_conf(s, Stage::kZoom, &ref);
  auto sec = parse_music_conf(conf);
  CHECK(sec["setup"]["levelmin"] == "8");
  CHECK(sec["setup"]["levelmax"] == "11");
  CHECK(sec["setup"].count("ref_center") == 1);
  CHECK(sec["setup"].count("ref_extent") == 1);
  // The zoom output frame must equal the catalogue's Lagrangian frame: MUSIC's
  // coarse-grid alignment shift is disabled (see src/config.cpp).
  CHECK(sec["setup"]["no_shift"] == "yes");
}

TEST("generate: the two stages emit an identical random block") {
  // This is the realization invariant, enforced at the point of writing.
  RunSpec s = valid();
  RefRegion ref{{0.5, 0.5, 0.5}, {0.06, 0.06, 0.06}};
  auto survey = parse_music_conf(music_conf(s, Stage::kSurvey));
  auto zoom = parse_music_conf(music_conf(s, Stage::kZoom, &ref));
  CHECK(survey["random"] == zoom["random"]);
  CHECK(survey["setup"]["levelmin"] == zoom["setup"]["levelmin"]);
}

TEST("generate: output is byte-identical across repeated calls") {
  RunSpec s = valid();
  CHECK(music_conf(s, Stage::kSurvey) == music_conf(s, Stage::kSurvey));
  CHECK(peakpatch_ini(s) == peakpatch_ini(s));
}

TEST("generate: the PeakPatch config writes a solved nmesh and an explicit boxsize") {
  RunSpec s = valid();
  Ini pp = Ini::parse(peakpatch_ini(s));
  CHECK_EQ(pp.get_int("box_params", "nmesh"), 288);
  CHECK_EQ(pp.get_int("box_params", "nbuff"), 16);
  CHECK_EQ(pp.get_int("box_params", "ntile"), 1);
  // The shipped example omits boxsize entirely and takes a silent default.
  CHECK(pp.has("box_params", "boxsize"));
  CHECK_CLOSE(pp.get_double("box_params", "boxsize"), 100.0, 1e-12);
  // The field comes from stage A, so PeakPatch must read it rather than
  // generate its own realization.
  CHECK_EQ(pp.get_int("peak_displacement", "ireadfield"), 1);
}

TEST("generate: a zoom config with no region is refused") {
  RunSpec s = valid();
  CHECK_THROWS(music_conf(s, Stage::kZoom, nullptr), SpecError);
}

int main() { return ppmi_test::run_all(); }
