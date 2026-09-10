// config.cpp - implementation of include/ppmi/spec.hpp: MUSIC and PeakPatch
// config generation, plus the MUSIC config parser used to read them back.
//
// Key names are not invented here. MUSIC's are those its parser actually
// reads (music_mpi/src/*.cc, catalogued in plan/00_FINDINGS.md section 2);
// PeakPatch's are those config_reader.f90 accepts. See the citations inline
// below for each key that isn't obvious from the shipped example configs.

#include "ppmi/spec.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ppmi {

// MUSIC config keys stage C must inherit unchanged from stage A. "levelmin"
// and "cubesize" are literal MUSIC keys (music_mpi/src/random.cc line 1573);
// every "seed[N]" per-level key (sprintf'd at random.cc line 1640) is frozen
// too, but its exact name depends on levelmin, so it can't be spelled out as
// a fixed string here - "seed" stands for that whole family.
const char* const kFrozenKeys[] = {"levelmin", "cubesize", "seed", nullptr};

namespace {

// Split "a,b,c" into three trimmed tokens, preserving the caller's original
// text for each component rather than reparsing and reformatting doubles.
std::array<std::string, 3> split_triple_raw(const std::string& v) {
  std::array<std::string, 3> parts;
  std::string::size_type start = 0;
  int i = 0;
  while (i < 3) {
    std::string::size_type comma = v.find(',', start);
    std::string field = (comma == std::string::npos) ? v.substr(start)
                                                       : v.substr(start, comma - start);
    // trim
    std::string::size_type a = field.find_first_not_of(" \t\r\n");
    std::string::size_type b = field.find_last_not_of(" \t\r\n");
    parts[i] = (a == std::string::npos) ? "" : field.substr(a, b - a + 1);
    ++i;
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return parts;
}

}  // namespace

// ---------------------------------------------------------------------------
// MUSIC config
// ---------------------------------------------------------------------------

std::string music_conf(const RunSpec& spec, Stage stage, const RefRegion* ref) {
  if (stage == Stage::kZoom) {
    if (ref == nullptr) {
      throw SpecError(
          "music_conf: Stage::kZoom requires a RefRegion naming the halo to "
          "refine around; a zoom with no target region is meaningless.");
    }
    check_zoom_region(*ref);
  }

  const int levelmin = spec.levelmin();
  const int levelmax =
      (stage == Stage::kSurvey) ? spec.survey_levelmax() : spec.zoom_levelmax();

  Ini out = Ini::parse("");

  // [setup] - music_mpi/src/main.cc lines 127-185, 494-527, 578-584.
  out.set("setup", "boxlength", spec.ini.get("box", "boxlength"));
  out.set("setup", "zstart", spec.ini.get("survey", "zstart"));
  out.set("setup", "region",
          stage == Stage::kSurvey ? std::string("box") : spec.ini.get("zoom", "region"));
  out.set("setup", "levelmin", std::to_string(levelmin));
  out.set("setup", "levelmax", std::to_string(levelmax));
  if (stage == Stage::kZoom) {
    // region_generator.cc lines 80-135: padding is required (no default)
    // once levelmin != levelmax, and ref_center/ref_extent locate the region
    // in MUSIC's dimensionless Lagrangian unit box.
    out.set("setup", "padding", spec.ini.get("zoom", "padding"));
    out.set("setup", "ref_center", format_triple(ref->center));
    out.set("setup", "ref_extent", format_triple(ref->extent));
  }
  out.set("setup", "align_top", "no");
  out.set("setup", "baryons", stage == Stage::kSurvey ? "no" : "yes");
  out.set("setup", "use_2LPT", stage == Stage::kSurvey ? "no" : "yes");
  out.set("setup", "use_LLA", "yes");
  // The survey now exports its density *and* displacement fields for
  // PeakPatch (P3-T1). The 1LPT branch gates the Poisson solve on
  // calculate_potential (main.cc:780) and reads its result to build
  // displacements (main.cc:807), so a survey with displacements but no
  // potential would write three files of zeros. calculate_velocities stays
  // off for the survey because PeakPatch does not consume velocities.
  out.set("setup", "calculate_potential", stage == Stage::kSurvey ? "yes" : "no");
  out.set("setup", "calculate_displacements", "yes");
  out.set("setup", "calculate_velocities", stage == Stage::kSurvey ? "no" : "yes");

  // [cosmology] - general.hh lines 127-138, transfer_function.cc line 43.
  out.set("cosmology", "Omega_m", spec.ini.get("cosmology", "Omega_m"));
  out.set("cosmology", "Omega_b", spec.ini.get("cosmology", "Omega_b"));
  out.set("cosmology", "Omega_L", spec.ini.get("cosmology", "Omega_L"));
  out.set("cosmology", "H0", spec.ini.get("cosmology", "H0"));
  out.set("cosmology", "sigma_8", spec.ini.get("cosmology", "sigma_8"));
  out.set("cosmology", "nspec", spec.ini.get("cosmology", "nspec"));
  out.set("cosmology", "transfer", spec.ini.get("cosmology", "transfer"));
  out.set("cosmology", "transfer_file", spec.ini.get("cosmology", "transfer_file"));

  // [random] - random.cc lines 1573-1640. This block must come out
  // byte-identical between the survey and zoom stages: that is the
  // realization invariant, enforced here by construction because both calls
  // read the same [box]/[random] section of the same spec.
  out.set("random", "cubesize", spec.ini.get("random", "cubesize"));
  out.set("random", "seed[" + std::to_string(levelmin) + "]",
          spec.ini.get("random", "seed_levelmin"));

  const std::string name = spec.ini.get_or("meta", "name", "run");
  if (stage == Stage::kSurvey) {
    // The peakpatch output plugin writes Fvec_<base>, etax/etay/etaz_<base>
    // and <base>.fields.json. PeakPatch's reader looks for exactly
    // 'Fvec_'//filein, and filein defaults to its run_name, which is <name>.
    // [output] filename must be the Fvec_ path so the plugin can derive its
    // siblings; output.hh line 77 requires the key unconditionally.
    out.set("output", "format", "peakpatch");
    out.set("output", "filename", "Fvec_" + name);
    out.set("output", "peakpatch_level", std::to_string(levelmax));
  } else {
    // output_gadget2.cc line 1553; gadget_usekpc/gadget_coarsetype as used
    // by the shipped param/parameters.ini [output] block.
    out.set("output", "format", "gadget2");
    out.set("output", "filename", name + "_zoom_ics.dat");
    out.set("output", "gadget_usekpc", "yes");
    out.set("output", "gadget_coarsetype", "2");
  }

  return out.to_string();
}

// ---------------------------------------------------------------------------
// PeakPatch config
// ---------------------------------------------------------------------------

std::string peakpatch_ini(const RunSpec& spec) {
  // Solves nmesh; throws GeometryError if the grid has no integer pairing.
  PeakPatchGrid grid = spec.peakpatch_grid();

  Ini out = Ini::parse("");

  const std::string name = spec.ini.get_or("meta", "name", "run");
  out.set("peak_patch_main", "run_name", name);

  // [box_params] - config_reader.f90 case 'boxsize'/'nmesh'/'nbuff'/'ntile'
  // (around lines 413-422). boxsize is written explicitly; the shipped
  // param/parameters.ini omits it and silently takes a default.
  out.set("box_params", "nmesh", std::to_string(grid.nmesh));
  out.set("box_params", "nbuff", std::to_string(grid.nbuff));
  out.set("box_params", "ntile", std::to_string(grid.ntile));
  out.set("box_params", "boxsize", spec.ini.get("box", "boxlength"));

  // [cosmology] - config_reader.f90 case 'Omega_m'/'Omega_b'/'Omega_L'/
  // 'nspec'/'H0'/'sigma_8' (lines 626-636); PeakPatch accepts these exact
  // aliases, the same names MUSIC's [cosmology] section uses.
  out.set("cosmology", "Omega_m", spec.ini.get("cosmology", "Omega_m"));
  out.set("cosmology", "Omega_b", spec.ini.get("cosmology", "Omega_b"));
  out.set("cosmology", "Omega_L", spec.ini.get("cosmology", "Omega_L"));
  out.set("cosmology", "H0", spec.ini.get("cosmology", "H0"));
  out.set("cosmology", "sigma_8", spec.ini.get("cosmology", "sigma_8"));
  out.set("cosmology", "nspec", spec.ini.get("cosmology", "nspec"));

  // [redshifts] - config_reader.f90 case 'ievol'/'num_redshifts'/
  // 'global_redshift' (lines 469-475). check_redshift_mode already enforces
  // ievol=0, num_redshifts=1 for a well-defined single-redshift search.
  out.set("redshifts", "ievol", spec.ini.get("peakpatch", "ievol"));
  out.set("redshifts", "num_redshifts", spec.ini.get("peakpatch", "num_redshifts"));
  out.set("redshifts", "global_redshift", spec.ini.get("peakpatch", "global_redshift"));

  // [ellipsoidal_collapse] - config_reader.f90 case 'Rsmooth_max'.
  out.set("ellipsoidal_collapse", "Rsmooth_max", spec.ini.get("peakpatch", "Rsmooth_max"));

  // [peak_displacement] - config_reader.f90 case 'ireadfield'. 1 means "read
  // from disk"; the field was already generated by the survey (stage A)
  // MUSIC run, so PeakPatch must not generate its own.
  out.set("peak_displacement", "ireadfield", "1");

  // [lattice_parameters_hpkvd] - config_reader.f90 case 'cenx'/'ceny'/'cenz'.
  std::array<std::string, 3> cen = split_triple_raw(spec.ini.get("peakpatch", "cen"));
  out.set("lattice_parameters_hpkvd", "cenx", cen[0]);
  out.set("lattice_parameters_hpkvd", "ceny", cen[1]);
  out.set("lattice_parameters_hpkvd", "cenz", cen[2]);

  return out.to_string();
}

// ---------------------------------------------------------------------------
// Parsing generated configs back
// ---------------------------------------------------------------------------

std::map<std::string, std::map<std::string, std::string>> parse_music_conf(
    const std::string& text) {
  Ini ini = Ini::parse(text);
  std::map<std::string, std::map<std::string, std::string>> out;
  for (const auto& section : ini.sections()) {
    std::map<std::string, std::string> kv;
    for (const auto& pair : ini.entries(section)) {
      kv[pair.first] = pair.second;
    }
    out[section] = std::move(kv);
  }
  return out;
}

// ---------------------------------------------------------------------------
// Writing stage configs
// ---------------------------------------------------------------------------

namespace {

void write_file(const std::string& path, const std::string& text) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    throw SpecError("write_stage_configs: could not open '" + path + "' for writing");
  }
  f << text;
  if (!f) {
    throw SpecError("write_stage_configs: failed writing '" + path + "'");
  }
}

}  // namespace

std::map<std::string, std::string> write_stage_configs(const RunSpec& spec,
                                                        const std::string& outdir,
                                                        Stage stage,
                                                        const RefRegion* ref) {
  // Validation runs before anything is written, so an invalid spec never
  // leaves a half-configured directory behind.
  validate(spec);
  if (stage == Stage::kZoom && ref != nullptr) {
    check_zoom_region(*ref);
  }

  std::string music_text = music_conf(spec, stage, ref);

  std::filesystem::create_directories(outdir);

  std::map<std::string, std::string> result;

  std::string music_path =
      outdir + "/music_" + (stage == Stage::kSurvey ? "survey" : "zoom") + ".conf";
  write_file(music_path, music_text);
  result["music_conf"] = music_path;

  // The PeakPatch config only makes sense once the survey (stage A) field
  // exists for it to read, so it is emitted alongside the survey stage.
  if (stage == Stage::kSurvey) {
    std::string pp_text = peakpatch_ini(spec);
    std::string pp_path = outdir + "/peakpatch.ini";
    write_file(pp_path, pp_text);
    result["peakpatch_ini"] = pp_path;
  }

  return result;
}

}  // namespace ppmi
