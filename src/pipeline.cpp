// pipeline.cpp - implementation of include/ppmi/pipeline.hpp
//
// See the header for the two hard constraints this file exists to satisfy:
// PeakPatch's baked-in RUNDIR/grid size (constraint 1, handled in
// prepare_stage for PipelineStage::kPeakPatch) and the one-rank zoom
// (constraint 2, handled in zoom_commands).

#include "ppmi/pipeline.hpp"

#include "ppmi/feasibility.hpp"
#include "ppmi/field.hpp"

#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace ppmi {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Naming
// ---------------------------------------------------------------------------

const char* stage_name(PipelineStage s) {
  switch (s) {
    case PipelineStage::kSurvey: return "survey";
    case PipelineStage::kPeakPatch: return "peakpatch";
    case PipelineStage::kZoom: return "zoom";
  }
  return "unknown";
}

std::string stage_dirname(PipelineStage s) {
  switch (s) {
    case PipelineStage::kSurvey: return "01_survey";
    case PipelineStage::kPeakPatch: return "02_peakpatch";
    case PipelineStage::kZoom: return "03_zoom";
  }
  return "00_unknown";
}

std::string stage_dir(const std::string& root, PipelineStage s) {
  return root + "/" + stage_dirname(s);
}

namespace {

constexpr PipelineStage kAllStages[] = {
    PipelineStage::kSurvey, PipelineStage::kPeakPatch, PipelineStage::kZoom};

// File names used consistently between the *_commands() generators (pure)
// and prepare_stage() (which actually writes them).
constexpr const char* kSurveyConfName = "music_survey.conf";
constexpr const char* kZoomConfName = "music_zoom.conf";
constexpr const char* kPeakpatchParamsName = "parameters.ini";

std::string read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw PipelineError("could not open '" + path + "' for reading");
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

void write_file(const std::string& path, const std::string& text) {
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) throw PipelineError("could not open '" + path + "' for writing");
  f << text;
  if (!f) throw PipelineError("failed writing '" + path + "'");
}

std::string now_utc_iso() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf);
}

std::string spec_name(const RunSpec& spec) {
  return spec.ini.get_or("meta", "name", "run");
}

// The merged catalogue path the peakpatch stage is expected to produce,
// relative to its own stage directory. config_reader.f90's
// add_rundir_prefix: mergepkoutfile = RUNDIR/output/<run_name>_merge.pksc.
std::string merged_catalog_rel(const RunSpec& spec) {
  return "output/" + spec_name(spec) + "_merge.pksc." + spec.ini.get("random","seed_levelmin");
}

// ---------------------------------------------------------------------------
// Running a command without a shell
// ---------------------------------------------------------------------------

struct RunResult {
  int exit_status = 0;
  double wall_seconds = 0.0;
};

// fork()/execvp() rather than system(), so arguments never need shell
// quoting. stdout and stderr are appended to log_path.
RunResult exec_command(const Command& c, const std::string& log_path) {
  fs::create_directories(fs::path(log_path).parent_path());
  int fd = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd < 0) {
    throw PipelineError("could not open log file '" + log_path +
                        "': " + std::strerror(errno));
  }

  std::vector<char*> argv;
  argv.push_back(const_cast<char*>(c.program.c_str()));
  for (const auto& a : c.args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);

  auto t0 = std::chrono::steady_clock::now();
  pid_t pid = fork();
  if (pid < 0) {
    ::close(fd);
    throw PipelineError("fork() failed running '" + c.to_string() +
                        "': " + std::strerror(errno));
  }
  if (pid == 0) {
    // Child. Redirect stdout/stderr to the log, cd into the command's
    // working directory, then replace this process image.
    if (chdir(c.cwd.c_str()) != 0) _exit(127);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    ::close(fd);
    execvp(c.program.c_str(), argv.data());
    _exit(127);  // execvp only returns on failure
  }
  ::close(fd);

  int status = 0;
  waitpid(pid, &status, 0);
  auto t1 = std::chrono::steady_clock::now();

  RunResult r;
  r.wall_seconds = std::chrono::duration<double>(t1 - t0).count();
  r.exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return r;
}

std::string join_commands(const std::vector<Command>& cs) {
  std::string out;
  for (const auto& c : cs) {
    if (!out.empty()) out += " ; ";
    out += c.to_string();
  }
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Command::to_string
// ---------------------------------------------------------------------------

std::string Command::to_string() const {
  std::ostringstream os;
  os << program;
  for (const auto& a : args) os << " " << a;
  return os.str();
}

// ---------------------------------------------------------------------------
// Command construction - pure functions of the spec and options
// ---------------------------------------------------------------------------

std::vector<Command> survey_commands(const RunSpec& spec, const RunOptions& o) {
  (void)spec;
  Command c;
  c.cwd = stage_dir(o.root, PipelineStage::kSurvey);
  c.program = "mpirun";
  c.args = {"-np", std::to_string(o.ranks), o.music_bin,
            c.cwd + "/" + kSurveyConfName};
  return {c};
}

std::vector<Command> zoom_commands(const RunSpec& spec, const RunOptions& o) {
  (void)spec;
  Command c;
  c.cwd = stage_dir(o.root, PipelineStage::kZoom);
  c.program = "mpirun";
  // Constraint 2 (header comment): the zoom always takes MUSIC's broken
  // multi-rank constrained-refinement path, so this clamps to one rank
  // regardless of RunOptions::ranks rather than silently corrupting the
  // field at more than one rank.
  c.args = {"-np", "1", o.music_bin, c.cwd + "/" + kZoomConfName};
  return {c};
}

std::vector<Command> peakpatch_commands(const RunSpec& spec, const RunOptions& o) {
  std::string dir = stage_dir(o.root, PipelineStage::kPeakPatch);
  std::string params = dir + "/" + kPeakpatchParamsName;
  std::string seed = spec.ini.get("random", "seed_levelmin");

  std::vector<Command> cs;

  Command filt;
  filt.cwd = dir;
  filt.program = dir + "/bin/filter_gen";
  filt.args = {params};
  cs.push_back(filt);

  // hpkvd's first pass (mode 1) builds the ellipsoidal-collapse table; the
  // second (mode 0) is the actual peak search. Both take the seed and the
  // parameter file, same as merge_pkvd.
  Command h1;
  h1.cwd = dir;
  h1.program = dir + "/bin/hpkvd";
  h1.args = {"1", seed, params};
  cs.push_back(h1);

  Command h0;
  h0.cwd = dir;
  h0.program = dir + "/bin/hpkvd";
  h0.args = {"0", seed, params};
  cs.push_back(h0);

  Command mg;
  mg.cwd = dir;
  mg.program = dir + "/bin/merge_pkvd";
  mg.args = {seed, params};
  cs.push_back(mg);

  return cs;
}

std::vector<Command> stage_commands(const RunSpec& spec, const RunOptions& o,
                                    PipelineStage s) {
  switch (s) {
    case PipelineStage::kSurvey: return survey_commands(spec, o);
    case PipelineStage::kPeakPatch: return peakpatch_commands(spec, o);
    case PipelineStage::kZoom: return zoom_commands(spec, o);
  }
  return {};
}

// ---------------------------------------------------------------------------
// Expected outputs
// ---------------------------------------------------------------------------

std::vector<std::string> expected_outputs(const RunSpec& spec, PipelineStage s) {
  std::string name = spec_name(spec);
  switch (s) {
    case PipelineStage::kSurvey:
      // MUSIC's peakpatch output plugin writes the density and, since P3-T1,
      // the (currently inert, see HANDOFF issue 3) displacement fields
      // alongside it. See field.hpp's FILES AND SIGN section.
      return {"Fvec_" + name, "etax_" + name, "etay_" + name, "etaz_" + name};
    case PipelineStage::kPeakPatch:
      return {merged_catalog_rel(spec)};
    case PipelineStage::kZoom:
      return {name + "_zoom_ics.dat"};
  }
  return {};
}

// ---------------------------------------------------------------------------
// prepare_root
// ---------------------------------------------------------------------------

std::string prepare_root(const RunSpec& spec, const RunOptions& o) {
  fs::create_directories(o.root);
  std::string spec_path = o.root + "/spec.ini";
  std::string content = spec.ini.to_string();

  if (fs::exists(spec_path)) {
    std::string existing = read_file(spec_path);
    if (existing != content) {
      throw PipelineError(
          "run root '" + o.root +
          "' already holds a spec.ini that differs from this run's spec. "
          "Reusing it would pair a possibly stale catalogue with a new "
          "spec, which is exactly the failure mode manifests exist to "
          "catch. Use a different --root, or remove the existing one if "
          "you mean to replace it.");
    }
  } else {
    write_file(spec_path, content);
  }
  return sha256_file(spec_path);
}

// ---------------------------------------------------------------------------
// prepare_stage
// ---------------------------------------------------------------------------

namespace {

void prepare_survey(const RunSpec& spec, const RunOptions&, const std::string& dir) {
  validate(spec);
  std::string conf = music_conf(spec, Stage::kSurvey, nullptr);
  write_file(dir + "/" + kSurveyConfName, conf);
}

void prepare_zoom(const RunSpec& spec, const RunOptions& o, const std::string& dir) {
  validate(spec);
  std::string cat = stage_dir(o.root, PipelineStage::kPeakPatch) + "/" +
                    merged_catalog_rel(spec);
  if (!fs::exists(cat)) {
    throw StageNotReady(
        "the zoom stage needs the peakpatch stage's merged catalogue '" +
        cat + "', which does not exist yet; run the peakpatch stage first.");
  }

  // A function of the catalogue the peakpatch stage actually produced, never
  // of a stale one: select() and halo_to_ref() read it fresh every time
  // prepare_stage runs.
  Selection sel = select(cat, spec.cosmology(), spec.frame(), o.criterion,
                        /*is_lightcone=*/false);
  RefRegion ref =
      halo_to_ref(sel.halo.lagrangian, sel.halo.r, spec.frame(), o.extent_factor);
  check_zoom_region(ref);

  std::string conf = music_conf(spec, Stage::kZoom, &ref);
  write_file(dir + "/" + kZoomConfName, conf);
}

// Constraint 1 (header comment): PeakPatch bakes n1/n2/n3 and RUNDIR into the
// binary at compile time, so the only way to get a binary that looks for
// tables/ and writes output/ in THIS stage directory is to build it here.
void prepare_peakpatch(const RunSpec& spec, const RunOptions& o, const std::string& dir) {
  validate(spec);
  if (o.peakpatch_src.empty()) {
    throw PipelineError(
        "the peakpatch stage needs RunOptions::peakpatch_src (the PeakPatch "
        "source tree to build a private copy from), and it was empty.");
  }

  fs::create_directories(dir + "/bin");
  fs::create_directories(dir + "/output");
  fs::create_directories(dir + "/logfiles");

  fs::path tables_src = fs::path(o.peakpatch_src) / "tables";
  if (!fs::exists(tables_src)) {
    throw StageNotReady("no 'tables/' directory under peakpatch_src '" +
                        o.peakpatch_src + "'");
  }
  fs::path tables_dst = fs::path(dir) / "tables";
  fs::remove_all(tables_dst);
  fs::copy(tables_src, tables_dst, fs::copy_options::recursive);

  // A PRIVATE copy of the source, never the shared checkout: building here
  // is what bakes RUNDIR to this stage directory rather than to peakpatch_src
  // or to wherever the interface happens to live.
  fs::path src_src = fs::path(o.peakpatch_src) / "src";
  if (!fs::exists(src_src)) {
    throw StageNotReady("no 'src/' directory under peakpatch_src '" +
                        o.peakpatch_src + "'");
  }
  fs::path src_copy = fs::path(dir) / "src";
  fs::remove_all(src_copy);
  fs::copy(src_src, src_copy, fs::copy_options::recursive);

  std::string params_path = dir + "/" + kPeakpatchParamsName;
  write_file(params_path, peakpatch_ini(spec));

  // PeakPatch's ireadfield=1 path reads Fvec_<run_name> from RUNDIR/fielddir,
  // which defaults to RUNDIR itself (config_reader.f90's
  // add_rundir_prefix: fielddir = RUNDIR/''). RUNDIR bakes to this stage
  // directory, so the field has to live here, not in the survey directory
  // that wrote it.
  std::string name = spec_name(spec);
  std::string survey_field =
      stage_dir(o.root, PipelineStage::kSurvey) + "/Fvec_" + name;
  if (!fs::exists(survey_field)) {
    throw StageNotReady(
        "the peakpatch stage needs the survey stage's density field '" +
        survey_field + "', which does not exist yet; run the survey stage "
        "first.");
  }
  fs::create_directories(fs::path(dir) / "fields");
  // PeakPatch's live reader wants the PADDED global cube of side n_ext, not the
  // unpadded core MUSIC writes. See pad_core_to_next in field.hpp.
  // MUSIC writes the linear field at zstart. PeakPatch applies its own D(z)
  // internally and therefore expects the field extrapolated to z=0, so hand it
  // delta * D(0)/D(zstart). Without this every peak is tens of times too
  // shallow, nothing reaches delta_c = 1.686, and the run completes normally
  // having found no halos at all.
  const double zstart = spec.ini.get_double("survey", "zstart");
  const double omega_m = spec.cosmology().Omega_m;
  const double to_z0 = growth_factor(0.0, omega_m) / growth_factor(zstart, omega_m);
  pad_core_to_next(spec.peakpatch_grid(), std::string(survey_field),
                   (fs::path(dir) / "fields" / ("Fvec_" + name)).string(), to_z0);

  // HANDOFF issue 4: `make SYSTYPE=nix` on the command line is silently
  // ignored. The Makefile does `SYSTYPE := "$(SYSTYPE)"` to add quotes, but a
  // command-line-set variable cannot be overridden by a plain assignment in
  // the makefile (only `override` can do that), so SYSTYPE stays the
  // unquoted `nix` and every `ifeq ($(SYSTYPE),"nix")` guard fails to match,
  // silently. A Makefile.systype FILE containing the quoted assignment is
  // the only thing that actually takes effect, so write one into the private
  // copy rather than passing SYSTYPE on the command line.
  write_file((src_copy / "Makefile.systype").string(), "SYSTYPE=\"nix\"\n");

  std::string log_path = dir + "/logfiles/build.log";
  Command build;
  build.cwd = src_copy.string();
  build.program = "make";
  // HANDOFF issue 4's reference-BLAS override, passed defensively. This
  // checkout's hpkvd link recipe does not currently reference $(BLAS) (its
  // HomogeneousEllipsoid module has no LAPACK/BLAS calls), so this is
  // presently a no-op; it costs nothing and picks up automatically should a
  // future PeakPatch checkout reintroduce a BLAS-linked module here.
  build.args = {"filter_gen", "hpkvd", "merge_pkvd", "CONFIG_FILE=" + params_path,
               "BLAS=-shared -llapack -lblas -lm"};
  RunResult rr = exec_command(build, log_path);
  if (rr.exit_status != 0) {
    throw PipelineError("building PeakPatch in '" + dir +
                        "' failed (exit " + std::to_string(rr.exit_status) +
                        "); see " + log_path);
  }
}

}  // namespace

void prepare_stage(const RunSpec& spec, const RunOptions& o, PipelineStage s) {
  std::string dir = stage_dir(o.root, s);
  fs::create_directories(dir);
  switch (s) {
    case PipelineStage::kSurvey: prepare_survey(spec, o, dir); break;
    case PipelineStage::kPeakPatch: prepare_peakpatch(spec, o, dir); break;
    case PipelineStage::kZoom: prepare_zoom(spec, o, dir); break;
  }
}

// ---------------------------------------------------------------------------
// Currency
// ---------------------------------------------------------------------------

bool stage_is_current(const RunSpec&, const RunOptions& o, PipelineStage s,
                      const std::string& spec_sha) {
  std::string manifest_path = stage_dir(o.root, s) + "/manifest.ini";
  if (!fs::exists(manifest_path)) return false;

  Manifest m;
  try {
    m = Manifest::from_ini(read_file(manifest_path));
  } catch (const std::exception&) {
    return false;
  }

  if (m.spec_sha256 != spec_sha) return false;
  if (m.exit_status != 0) return false;

  // Manifest outputs are recorded with absolute paths (see run_stage), so no
  // root prefix is needed here.
  return verify(m, "").empty();
}

// ---------------------------------------------------------------------------
// run_stage
// ---------------------------------------------------------------------------

StageResult run_stage(const RunSpec& spec, const RunOptions& o, PipelineStage s,
                      const std::string& spec_sha) {
  StageResult res;
  res.stage = s;
  res.dir = stage_dir(o.root, s);

  if (!o.force && stage_is_current(spec, o, s, spec_sha)) {
    res.skipped = true;
    res.exit_status = 0;
    std::string mpath = res.dir + "/manifest.ini";
    if (fs::exists(mpath)) {
      try {
        res.manifest = Manifest::from_ini(read_file(mpath));
      } catch (const std::exception&) {
        // Fall through with a default-constructed manifest; the skip
        // decision itself already succeeded via stage_is_current.
      }
    }
    return res;
  }

  if (o.dry_run) {
    // Touch nothing: stage_commands() is a pure function, and prepare_stage()
    // is never called here.
    res.exit_status = 0;
    res.manifest.stage = stage_name(s);
    std::vector<Command> cmds = stage_commands(spec, o, s);
    res.manifest.command = join_commands(cmds);
    return res;
  }

  // A stage that re-runs replaces its own directory and nothing else.
  std::error_code ec;
  fs::remove_all(res.dir, ec);

  std::string started = now_utc_iso();
  auto t0 = std::chrono::steady_clock::now();

  int final_status = 0;
  std::string error_msg;
  std::string command_text;
  std::string log_path = res.dir + "/logfiles/stage.log";

  try {
    prepare_stage(spec, o, s);
    std::vector<Command> cmds = stage_commands(spec, o, s);
    command_text = join_commands(cmds);
    for (const auto& c : cmds) {
      RunResult rr = exec_command(c, log_path);
      if (rr.exit_status != 0) {
        final_status = rr.exit_status;
        error_msg = "command failed (exit " + std::to_string(rr.exit_status) +
                    "): " + c.to_string() + "; see " + log_path;
        break;
      }
    }
  } catch (const std::exception& e) {
    final_status = final_status == 0 ? 1 : final_status;
    error_msg = e.what();
  }

  auto t1 = std::chrono::steady_clock::now();
  double wall = std::chrono::duration<double>(t1 - t0).count();

  Manifest m;
  m.stage = stage_name(s);
  m.command = command_text;
  m.started_utc = started;
  m.wall_seconds = wall;
  m.exit_status = final_status;
  m.spec_sha256 = spec_sha;

  if (final_status == 0) {
    for (const auto& rel : expected_outputs(spec, s)) {
      std::string full = res.dir + "/" + rel;
      if (!fs::exists(full)) {
        final_status = 1;
        error_msg = "stage reported success but expected output '" + full +
                    "' is missing";
        break;
      }
      m.outputs.push_back(record_file(full));
    }
    m.exit_status = final_status;
  }

  // On failure, still write a manifest recording the failure, so verify()
  // can distinguish "never ran" from "ran and failed".
  fs::create_directories(res.dir);
  write_file(res.dir + "/manifest.ini", m.to_ini());

  res.exit_status = final_status;
  res.error = error_msg;
  res.wall_seconds = wall;
  res.manifest = m;
  return res;
}

// ---------------------------------------------------------------------------
// run_pipeline
// ---------------------------------------------------------------------------

std::vector<StageResult> run_pipeline(const RunSpec& spec, const RunOptions& o) {
  validate(spec);

  std::vector<StageResult> results;

  if (o.dry_run) {
    // A dry run touches the filesystem not at all, including not creating
    // the root, so prepare_root() (which writes spec.ini) is never called.
    for (PipelineStage s : kAllStages) {
      results.push_back(run_stage(spec, o, s, ""));
    }
    return results;
  }

  std::string spec_sha = prepare_root(spec, o);

  for (PipelineStage s : kAllStages) {
    StageResult r = run_stage(spec, o, s, spec_sha);
    bool failed = !r.error.empty() || (!r.skipped && r.exit_status != 0);
    results.push_back(r);
    if (failed) break;
  }
  return results;
}

// ---------------------------------------------------------------------------
// verify_run
// ---------------------------------------------------------------------------

std::vector<std::string> verify_run(const std::string& root) {
  std::vector<std::string> problems;
  for (PipelineStage s : kAllStages) {
    std::string mpath = stage_dir(root, s) + "/manifest.ini";
    if (!fs::exists(mpath)) continue;

    Manifest m;
    try {
      m = Manifest::from_ini(read_file(mpath));
    } catch (const std::exception& e) {
      problems.push_back(std::string(stage_name(s)) + ": could not read manifest (" +
                         e.what() + ")");
      continue;
    }

    for (const auto& msg : verify(m, "")) {
      problems.push_back(std::string(stage_name(s)) + ": " + msg);
    }
  }
  return problems;
}

// ---------------------------------------------------------------------------
// describe
// ---------------------------------------------------------------------------

std::string describe(const StageResult& r) {
  std::ostringstream os;
  os << stage_name(r.stage) << ": ";
  if (r.skipped) {
    os << "skip -- already current (manifest matches this spec and outputs)";
  } else if (!r.error.empty()) {
    os << "FAILED -- " << r.error;
  } else {
    os << "ok";
    if (r.wall_seconds > 0.0) {
      std::ostringstream t;
      t.precision(2);
      t << std::fixed << r.wall_seconds;
      os << " (" << t.str() << "s)";
    }
  }
  return os.str();
}

}  // namespace ppmi
