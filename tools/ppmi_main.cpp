// ppmi - one binary, subcommands, no runtime dependencies.
//
// Job submission is deliberately NOT here. Generated sbatch scripts live under
// scripts/ and are launched the way any other cluster job is, so nothing sits
// between you and the scheduler.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ppmi/catalog.hpp"
#include "ppmi/feasibility.hpp"
#include "ppmi/field.hpp"
#include "ppmi/geometry.hpp"
#include "ppmi/pipeline.hpp"
#include "ppmi/spec.hpp"

namespace {

const char* kUsage =
    "ppmi - MUSIC to PeakPatch to MUSIC zoom-in orchestration\n"
    "\n"
    "  ppmi validate     <spec.ini>\n"
    "        Check every invariant. Exits non-zero on a hard violation.\n"
    "\n"
    "  ppmi gen-configs  <spec.ini> --stage survey|zoom [--out DIR]\n"
    "                    [--ref-center a,b,c --ref-extent a,b,c] [--dry-run]\n"
    "        Write the MUSIC .conf and PeakPatch parameters.ini for one stage.\n"
    "\n"
    "  ppmi feasibility  <spec.ini> [--z Z]\n"
    "        Resolvable halo mass range and expected abundance.\n"
    "\n"
    "  ppmi catalog info   <file.pksc> [<spec.ini>]\n"
    "  ppmi catalog select <file.pksc> <spec.ini> [--rank N]\n"
    "                      [--mass-window MIN MAX] [--closest-to M]\n"
    "                      [--min-isolation R] [--min-edge R] [--lightcone]\n"
    "\n"
    "  ppmi zoom-params  <file.pksc> <spec.ini> [--rank N]\n"
    "                    [--extent-factor F] [--out FILE]\n"
    "        Select a halo and emit the zoom-stage MUSIC config for it.\n"
    "\n"
    "  ppmi field stat   <field.bin> <spec.ini>\n"
    "  ppmi field spike  <out.bin>  <spec.ini> --index I,J,K [--amplitude A]\n"
    "        Synthesise the single-cell field the P3-T3 convention test needs.\n"
    "\n"
    "  ppmi run    <spec.ini> [--root DIR] [--music PATH] [--peakpatch-src PATH]\n"
    "                         [--ranks N] [--rank N|--mass-window A B]\n"
    "                         [--min-isolation R] [--min-edge R]\n"
    "                         [--extent-factor F] [--dry-run] [--force]\n"
    "        Run survey, peakpatch and zoom in one staged, resumable run.\n"
    "\n"
    "  ppmi verify <run-dir>\n"
    "        Re-hash every output recorded in every stage manifest under\n"
    "        <run-dir> and report any that no longer match.\n";

// --- small argv helpers ----------------------------------------------------

bool has_flag(int argc, char** argv, const char* name) {
  for (int i = 0; i < argc; ++i)
    if (std::strcmp(argv[i], name) == 0) return true;
  return false;
}

// Returns the value following `name`, or fallback when absent.
// Throws when the flag is present but its value is missing, because silently
// treating "--rank" with no number as the default is how a wrong halo gets
// selected without anyone noticing.
std::string opt(int argc, char** argv, const char* name,
                const std::string& fallback) {
  for (int i = 0; i < argc; ++i) {
    if (std::strcmp(argv[i], name) == 0) {
      if (i + 1 >= argc)
        throw std::runtime_error(std::string(name) + " needs a value");
      return argv[i + 1];
    }
  }
  return fallback;
}

ppmi::Vec3 parse_triple(const std::string& s, const char* what) {
  ppmi::Vec3 v{};
  if (std::sscanf(s.c_str(), "%lf,%lf,%lf", &v[0], &v[1], &v[2]) != 3)
    throw std::runtime_error(std::string(what) + " must be three "
                             "comma-separated numbers, got '" + s + "'");
  return v;
}

double spec_double(const ppmi::RunSpec& s, const char* sec, const char* key,
                   double fallback) {
  return s.ini.has(sec, key) ? s.ini.get_double(sec, key) : fallback;
}

// Rsmooth_max defaults to a fifth of the box, which is roughly where the
// largest collapsed structures sit. Stated rather than hidden, because it
// sets the upper end of the resolvable mass range.
double rsmooth_max(const ppmi::RunSpec& s) {
  return spec_double(s, "peakpatch", "Rsmooth_max", s.boxlength() / 5.0);
}

// --- subcommands -----------------------------------------------------------

int cmd_validate(int argc, char** argv) {
  if (argc < 1) { std::fputs(kUsage, stderr); return 2; }
  ppmi::RunSpec spec = ppmi::load_spec(argv[0]);
  std::vector<std::string> warnings = ppmi::validate(spec);

  ppmi::PeakPatchGrid g = spec.peakpatch_grid();
  std::printf("spec       %s\n", argv[0]);
  std::printf("levelmin   %d   (survey grid %d^3)\n", spec.levelmin(),
              g.core_grid());
  std::printf("boxlength  %g\n", spec.boxlength());
  std::printf("peakpatch  nmesh=%d nbuff=%d ntile=%d  -> core %d, padded %d\n",
              g.nmesh, g.nbuff, g.ntile, g.core_grid(), g.n_ext());
  std::printf("cellsize   %g\n", g.cellsize());
  std::printf("zoom       levelmax %d\n", spec.zoom_levelmax());

  for (const std::string& w : warnings)
    std::printf("warning: %s\n", w.c_str());
  std::printf("\n%zu warning(s), no invariant violations.\n", warnings.size());
  return 0;
}

int cmd_gen_configs(int argc, char** argv) {
  if (argc < 1) { std::fputs(kUsage, stderr); return 2; }
  ppmi::RunSpec spec = ppmi::load_spec(argv[0]);
  std::string stage_s = opt(argc, argv, "--stage", "");
  if (stage_s != "survey" && stage_s != "zoom")
    throw std::runtime_error("--stage must be survey or zoom");
  bool zoom = stage_s == "zoom";

  ppmi::RefRegion ref{};
  const ppmi::RefRegion* refp = nullptr;
  if (zoom) {
    std::string c = opt(argc, argv, "--ref-center", "");
    std::string e = opt(argc, argv, "--ref-extent", "");
    if (c.empty() || e.empty())
      throw std::runtime_error(
          "the zoom stage needs --ref-center and --ref-extent; derive them "
          "from a catalogue with 'ppmi zoom-params'");
    ref.center = parse_triple(c, "--ref-center");
    ref.extent = parse_triple(e, "--ref-extent");
    ppmi::check_zoom_region(ref);
    refp = &ref;
  }

  ppmi::Stage st = zoom ? ppmi::Stage::kZoom : ppmi::Stage::kSurvey;
  if (has_flag(argc, argv, "--dry-run")) {
    std::printf("%s\n", ppmi::music_conf(spec, st, refp).c_str());
    if (!zoom) std::printf("%s\n", ppmi::peakpatch_ini(spec).c_str());
    return 0;
  }

  std::string outdir = opt(argc, argv, "--out", ".");
  auto written = ppmi::write_stage_configs(spec, outdir, st, refp);
  for (const auto& kv : written)
    std::printf("wrote %-10s %s\n", kv.first.c_str(), kv.second.c_str());
  return 0;
}

int cmd_feasibility(int argc, char** argv) {
  if (argc < 1) { std::fputs(kUsage, stderr); return 2; }
  ppmi::RunSpec spec = ppmi::load_spec(argv[0]);
  double z = std::atof(
      opt(argc, argv, "--z",
          std::to_string(spec_double(spec, "peakpatch", "global_redshift", 0.0)))
          .c_str());
  ppmi::PeakPatchGrid g = spec.peakpatch_grid();
  std::printf("%s\n", ppmi::feasibility_report(
                          spec.boxlength(), spec.levelmin(), z,
                          spec.cosmology(),
                          spec_double(spec, "cosmology", "sigma_8", 0.81),
                          spec_double(spec, "cosmology", "nspec", 0.965),
                          g.ntile, g.nbuff, rsmooth_max(spec))
                          .c_str());
  return 0;
}

int cmd_catalog_info(int argc, char** argv) {
  if (argc < 1) { std::fputs(kUsage, stderr); return 2; }
  ppmi::CatalogHeader h = ppmi::peek_header(argv[0]);
  std::printf("file          %s\n", argv[0]);
  std::printf("header        %s, %d bytes\n",
              h.layout == ppmi::HeaderLayout::kSentinel ? "sentinel (64-bit)"
                                                        : "legacy (32-bit)",
              h.header_bytes);
  std::printf("halos         %lld\n", (long long)h.n_halos);
  std::printf("fields/halo   %d%s\n", h.n_fields,
              h.n_fields == ppmi::kFieldsShear ? "  (includes shear)" : "");
  std::printf("rmax          %g\n", (double)h.rmax);
  std::printf("boxredshift   %g\n", (double)h.boxredshift);

  if (argc >= 2 && h.n_halos > 0) {
    ppmi::RunSpec spec = ppmi::load_spec(argv[1]);
    ppmi::Cosmology cosmo = spec.cosmology();
    ppmi::CatalogReader r(argv[0]);
    std::vector<ppmi::Halo> chunk;
    double m_min = 1e300, m_max = 0.0;
    std::int64_t seen = 0;
    for (std::int64_t i = 0; i < h.n_halos;) {
      std::int64_t got = r.read_chunk(i, 65536, chunk);
      if (got == 0) break;
      for (std::int64_t k = 0; k < got; ++k) {
        double m = ppmi::halo_mass(chunk[k].r, cosmo);
        if (m < m_min) m_min = m;
        if (m > m_max) m_max = m;
      }
      i += got;
      seen += got;
    }
    std::printf("mass range    %.4g to %.4g M_sun  (%lld halos scanned)\n",
                m_min, m_max, (long long)seen);
  }
  return 0;
}

ppmi::Criterion criterion_from_args(int argc, char** argv) {
  ppmi::Criterion c;
  std::string window = opt(argc, argv, "--mass-window", "");
  if (!window.empty()) {
    c.kind = ppmi::CriterionKind::kMassWindow;
    c.m_min = std::atof(window.c_str());
    // The second value follows the first positionally.
    for (int i = 0; i + 2 < argc; ++i)
      if (std::strcmp(argv[i], "--mass-window") == 0)
        c.m_max = std::atof(argv[i + 2]);
    if (c.m_max <= c.m_min)
      throw std::runtime_error("--mass-window needs MIN then MAX, with MAX larger");
  } else {
    c.kind = ppmi::CriterionKind::kMassRank;
    c.rank = std::atoll(opt(argc, argv, "--rank", "1").c_str());
  }
  c.closest_to = std::atof(opt(argc, argv, "--closest-to", "0").c_str());
  c.min_isolation_radii =
      std::atof(opt(argc, argv, "--min-isolation", "0").c_str());
  c.min_edge_radii = std::atof(opt(argc, argv, "--min-edge", "0").c_str());
  return c;
}

void print_selection(const ppmi::Selection& s) {
  std::printf("halo index    %lld\n", (long long)s.halo.index);
  std::printf("mass          %.5g M_sun\n", s.mass);
  std::printf("r (Lagrangian) %g\n", s.halo.r);
  std::printf("lagrangian    %g, %g, %g\n", s.halo.lagrangian[0],
              s.halo.lagrangian[1], s.halo.lagrangian[2]);
  std::printf("eulerian      %g, %g, %g\n", s.halo.eulerian[0],
              s.halo.eulerian[1], s.halo.eulerian[2]);
  std::printf("displacement  %g, %g, %g\n",
              s.halo.eulerian[0] - s.halo.lagrangian[0],
              s.halo.eulerian[1] - s.halo.lagrangian[1],
              s.halo.eulerian[2] - s.halo.lagrangian[2]);
  std::printf("isolation     %g of its own radii to the nearest larger halo\n",
              s.isolation_radii);
  std::printf("edge clearance %g of its own radii\n", s.edge_radii);
  std::printf("criterion     %s\n", s.criterion.c_str());
}

int cmd_catalog_select(int argc, char** argv) {
  if (argc < 2) { std::fputs(kUsage, stderr); return 2; }
  ppmi::RunSpec spec = ppmi::load_spec(argv[1]);
  ppmi::Selection s =
      ppmi::select(argv[0], spec.cosmology(), spec.frame(),
                   criterion_from_args(argc, argv),
                   has_flag(argc, argv, "--lightcone"));
  print_selection(s);
  return 0;
}

int cmd_zoom_params(int argc, char** argv) {
  if (argc < 2) { std::fputs(kUsage, stderr); return 2; }
  ppmi::RunSpec spec = ppmi::load_spec(argv[1]);
  double ef = std::atof(
      opt(argc, argv, "--extent-factor",
          std::to_string(spec_double(spec, "zoom", "extent_factor", 3.0)))
          .c_str());

  ppmi::Selection s =
      ppmi::select(argv[0], spec.cosmology(), spec.frame(),
                   criterion_from_args(argc, argv),
                   has_flag(argc, argv, "--lightcone"));
  print_selection(s);
  std::printf("extent factor %g\n\n", ef);

  ppmi::RefRegion ref =
      ppmi::halo_to_ref(s.halo.lagrangian, s.halo.r, spec.frame(), ef);
  ppmi::check_zoom_region(ref);
  std::printf("ref_center = %s\n", ppmi::format_triple(ref.center).c_str());
  std::printf("ref_extent = %s\n", ppmi::format_triple(ref.extent).c_str());

  // What this zoom actually costs. A refinement region is quoted as a fraction
  // per side, which badly understates the volume: a third of the box per side
  // is a thirtieth of the box by volume, and the particle count at levelmax
  // scales with that volume. Reporting it here means the number is seen before
  // the job is submitted rather than after it fails to fit.
  double vol_frac = ref.extent[0] * ref.extent[1] * ref.extent[2];
  double n_fine = std::pow(2.0, 3.0 * spec.zoom_levelmax()) * vol_frac;
  std::printf("\nregion       %.1f%% of the box per side, %.3g%% by volume\n",
              100.0 * ref.extent[0], 100.0 * vol_frac);
  std::printf("cost         ~%.3g high-resolution particles at levelmax %d\n",
              n_fine, spec.zoom_levelmax());
  if (ref.extent[0] > 0.25) {
    std::printf(
        "\nwarning: this region spans more than a quarter of the box per side,\n"
        "         so it is closer to a resimulation than a zoom. The halo's\n"
        "         Lagrangian radius is %g in a box of %g. Consider a larger box,\n"
        "         a smaller target halo, or a lower extent factor.\n",
        s.halo.r, spec.boxlength());
  }
  std::printf("\n");

  std::string out = opt(argc, argv, "--out", "");
  std::string conf = ppmi::music_conf(spec, ppmi::Stage::kZoom, &ref);
  if (out.empty()) {
    std::printf("%s\n", conf.c_str());
  } else {
    std::FILE* f = std::fopen(out.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot write " + out);
    std::fwrite(conf.data(), 1, conf.size(), f);
    std::fclose(f);
    std::printf("wrote %s\n", out.c_str());
  }
  return 0;
}

int cmd_field(int argc, char** argv) {
  if (argc < 1) { std::fputs(kUsage, stderr); return 2; }
  std::string sub = argv[0];

  if (sub == "stat") {
    if (argc < 3) { std::fputs(kUsage, stderr); return 2; }
    ppmi::RunSpec spec = ppmi::load_spec(argv[2]);
    ppmi::FieldStats s = ppmi::field_stats(argv[1], spec.peakpatch_grid());
    std::printf("cells    %lld\n", (long long)s.n_cells);
    std::printf("mean     %.8g\n", s.mean);
    std::printf("stddev   %.8g\n", s.stddev);
    std::printf("min      %.8g\n", s.min);
    std::printf("max      %.8g\n", s.max);
    std::printf("nan      %lld\n", (long long)s.n_nan);
    std::printf("inf      %lld\n", (long long)s.n_inf);
    if (s.n_nan || s.n_inf)
      std::printf("\nA field with NaN or infinity will not produce a usable "
                  "catalogue. Fix the writer before running hpkvd.\n");
    return 0;
  }

  if (sub == "spike") {
    if (argc < 3) { std::fputs(kUsage, stderr); return 2; }
    ppmi::RunSpec spec = ppmi::load_spec(argv[2]);
    ppmi::PeakPatchGrid g = spec.peakpatch_grid();
    ppmi::Vec3 idx = parse_triple(opt(argc, argv, "--index", ""), "--index");
    double amp = std::atof(opt(argc, argv, "--amplitude", "1000").c_str());
    ppmi::write_spike_field(argv[1], g, (int)idx[0], (int)idx[1], (int)idx[2],
                            (float)amp);
    std::printf("wrote %s: %d^3 core in %lld tiles of %d^3, spike %g at "
                "global index %d,%d,%d\n",
                argv[1], g.core_grid(), (long long)g.n_tiles(), g.nmesh, amp,
                (int)idx[0], (int)idx[1], (int)idx[2]);
    return 0;
  }

  std::fputs(kUsage, stderr);
  return 2;
}

int cmd_run(int argc, char** argv) {
  if (argc < 1) { std::fputs(kUsage, stderr); return 2; }
  ppmi::RunSpec spec = ppmi::load_spec(argv[0]);

  ppmi::RunOptions o;
  o.root = opt(argc, argv, "--root", "run");
  o.music_bin = opt(argc, argv, "--music", "");
  o.peakpatch_src = opt(argc, argv, "--peakpatch-src", "");
  o.ranks = std::atoi(opt(argc, argv, "--ranks", std::to_string(spec.ranks())).c_str());
  o.dry_run = has_flag(argc, argv, "--dry-run");
  o.force = has_flag(argc, argv, "--force");
  o.extent_factor = std::atof(
      opt(argc, argv, "--extent-factor",
          std::to_string(spec_double(spec, "zoom", "extent_factor", 3.0)))
          .c_str());
  o.criterion = criterion_from_args(argc, argv);

  if (o.music_bin.empty())
    throw std::runtime_error(
        "--music PATH is required: the absolute path to the MUSIC binary");
  if (o.peakpatch_src.empty())
    throw std::runtime_error(
        "--peakpatch-src PATH is required: the PeakPatch source tree the "
        "peakpatch stage builds a private copy from");

  if (o.dry_run)
    std::printf(
        "dry run: nothing on disk is touched; showing what each stage would "
        "do\n\n");

  std::vector<ppmi::StageResult> results = ppmi::run_pipeline(spec, o);

  bool any_failed = false;
  for (const auto& r : results) {
    std::printf("%s\n", ppmi::describe(r).c_str());
    if (o.dry_run && !r.skipped && !r.manifest.command.empty()) {
      std::printf("  would run: %s\n", r.manifest.command.c_str());
    }
    if (!r.error.empty()) any_failed = true;
  }
  if (!o.dry_run) {
    std::printf("\nrun root: %s\n", o.root.c_str());
  }
  return any_failed ? 1 : 0;
}

int cmd_verify(int argc, char** argv) {
  if (argc < 1) { std::fputs(kUsage, stderr); return 2; }
  std::vector<std::string> problems = ppmi::verify_run(argv[0]);
  if (problems.empty()) {
    std::printf("%s: verified -- every recorded output matches its manifest\n",
                argv[0]);
    return 0;
  }
  for (const auto& p : problems) std::printf("%s\n", p.c_str());
  std::printf("\n%zu problem(s) found under %s\n", problems.size(), argv[0]);
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fputs(kUsage, stderr);
    return 2;
  }
  std::string cmd = argv[1];
  int rest_argc = argc - 2;
  char** rest = argv + 2;

  try {
    if (cmd == "validate") return cmd_validate(rest_argc, rest);
    if (cmd == "gen-configs") return cmd_gen_configs(rest_argc, rest);
    if (cmd == "feasibility") return cmd_feasibility(rest_argc, rest);
    if (cmd == "zoom-params") return cmd_zoom_params(rest_argc, rest);
    if (cmd == "field") return cmd_field(rest_argc, rest);
    if (cmd == "run") return cmd_run(rest_argc, rest);
    if (cmd == "verify") return cmd_verify(rest_argc, rest);
    if (cmd == "catalog") {
      if (rest_argc < 1) { std::fputs(kUsage, stderr); return 2; }
      std::string sub = rest[0];
      if (sub == "info") return cmd_catalog_info(rest_argc - 1, rest + 1);
      if (sub == "select") return cmd_catalog_select(rest_argc - 1, rest + 1);
      std::fputs(kUsage, stderr);
      return 2;
    }
    if (cmd == "-h" || cmd == "--help" || cmd == "help") {
      std::fputs(kUsage, stdout);
      return 0;
    }
    std::fprintf(stderr, "ppmi: unknown command '%s'\n\n", cmd.c_str());
    std::fputs(kUsage, stderr);
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "ppmi: %s\n", e.what());
    return 1;
  }
}
