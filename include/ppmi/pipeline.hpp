// pipeline.hpp - the staged runner: survey, PeakPatch, zoom.
//
// One spec drives three stages, each in its own directory, each leaving a
// manifest. Without manifests an old catalogue and a new spec are
// indistinguishable from a matched pair, and reproducibility becomes a matter
// of memory.
//
// Run-root layout:
//
//     <root>/spec.ini          verbatim copy of the spec, hashed into every manifest
//     <root>/01_survey/        MUSIC survey: music.conf, Fvec_*, sidecar, manifest.ini
//     <root>/02_peakpatch/     PeakPatch: parameters.ini, bin/, tables/, output/, manifest.ini
//     <root>/03_zoom/          MUSIC zoom: music.conf, the ICs, manifest.ini
//
// Nothing is ever written in place. A stage that re-runs replaces its own
// directory and nothing else.
//
// TWO CONSTRAINTS THAT SHAPE THIS DESIGN, both learned the hard way.
//
// 1. PeakPatch bakes its grid size into the binary. `n1`, `n2`, `n3` are
//    Fortran `parameter` constants substituted from the config file at compile
//    time (src/hpkvd/arrays.f90 carries a literal `N_REPLACE` placeholder), and
//    `RUNDIR` is likewise baked in, so a binary built elsewhere looks for
//    `tables/` and writes `output/` in that other place. Therefore the
//    PeakPatch stage MUST build into its own stage directory. This is not
//    avoidable by copying binaries around; see `prepare_peakpatch`.
//
// 2. The zoom stage runs at one MPI rank until MUSIC's multi-rank constrained
//    refinement is fixed (00_FINDINGS.md section 10). The survey is unaffected
//    and runs at any rank count. `RunOptions::ranks` therefore applies to the
//    survey; the zoom clamps to 1 and says so rather than silently producing a
//    corrupt field.

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "ppmi/manifest.hpp"
#include "ppmi/spec.hpp"

namespace ppmi {

class PipelineError : public std::runtime_error {
 public:
  explicit PipelineError(const std::string& w) : std::runtime_error(w) {}
};

// A stage whose inputs are missing, named so the user knows what to run first.
class StageNotReady : public PipelineError {
 public:
  explicit StageNotReady(const std::string& w) : PipelineError(w) {}
};

enum class PipelineStage { kSurvey, kPeakPatch, kZoom };

// "survey", "peakpatch", "zoom". Stable: manifests key off these.
const char* stage_name(PipelineStage s);

// "01_survey", "02_peakpatch", "03_zoom". Numbered so a directory listing
// reads in execution order.
std::string stage_dirname(PipelineStage s);

// <root>/<stage_dirname>
std::string stage_dir(const std::string& root, PipelineStage s);

struct RunOptions {
  std::string root = "run";
  // Absolute path to the MUSIC binary.
  std::string music_bin;
  // Source tree PeakPatch is built FROM. The stage builds a private copy.
  std::string peakpatch_src;
  // MPI ranks for the survey. The zoom clamps to 1; see the header comment.
  int ranks = 1;
  // Print what would run, touch nothing.
  bool dry_run = false;
  // Re-run even when the stage's manifest says it is current.
  bool force = false;
  // Halo selection for the zoom stage.
  Criterion criterion;
  double extent_factor = 3.0;
};

// One command to execute, with the directory to execute it in. Kept as data
// rather than a shell string so tests can assert on it and so nothing has to
// be quoted or escaped.
struct Command {
  std::string cwd;
  std::string program;
  std::vector<std::string> args;

  // Shell-ish rendering, for logs and for --dry-run. Not used to execute.
  std::string to_string() const;
};

// The commands each stage runs, as pure functions of the spec and options.
// These construct nothing on disk, so tests can check them without binaries.
//
// Survey: one MUSIC invocation under mpirun.
// PeakPatch: filter_gen, then hpkvd 1 (collapse table), then hpkvd 0, then
//   merge_pkvd, in that order. The first is one-time per run, the second
//   one-time per cosmology.
// Zoom: one MUSIC invocation under mpirun at one rank.
std::vector<Command> survey_commands(const RunSpec& spec, const RunOptions& o);
std::vector<Command> peakpatch_commands(const RunSpec& spec, const RunOptions& o);
std::vector<Command> zoom_commands(const RunSpec& spec, const RunOptions& o);
std::vector<Command> stage_commands(const RunSpec& spec, const RunOptions& o,
                                    PipelineStage s);

// Create <root>, write spec.ini, and return its SHA-256. Idempotent: an
// existing root with a DIFFERENT spec is an error rather than a silent
// overwrite, because that is how a half-matched run happens.
std::string prepare_root(const RunSpec& spec, const RunOptions& o);

// Create a stage directory and everything the stage needs in it.
// For PeakPatch this includes bin/, output/, logfiles/, a copy of tables/, and
// a build of the three binaries with the stage directory as RUNDIR and the
// spec's nmesh substituted. See constraint 1 in the header comment.
void prepare_stage(const RunSpec& spec, const RunOptions& o, PipelineStage s);

// Outputs a completed stage is expected to have produced, relative to its
// stage directory. Used to build the manifest and to check currency.
std::vector<std::string> expected_outputs(const RunSpec& spec, PipelineStage s);

// Is this stage complete and consistent?
// True only when: the manifest exists, its spec hash matches, every recorded
// output still exists, and every recorded hash still matches. Anything else is
// false, which means the stage re-runs.
bool stage_is_current(const RunSpec& spec, const RunOptions& o,
                      PipelineStage s, const std::string& spec_sha);

struct StageResult {
  PipelineStage stage;
  bool skipped = false;      // was current, and force was not set
  int exit_status = 0;
  double wall_seconds = 0.0;
  std::string dir;
  Manifest manifest;
  // Empty on success. On failure, what went wrong, in a form a person can act
  // on rather than a bare exit code.
  std::string error;
};

// Run one stage. Writes a manifest on success. On failure the manifest records
// the failure rather than being omitted, so a later `verify` can tell "never
// ran" from "ran and failed".
StageResult run_stage(const RunSpec& spec, const RunOptions& o,
                      PipelineStage s, const std::string& spec_sha);

// Run all three in order, stopping at the first failure. A stage that is
// current is skipped unless `force`, and the skip is reported rather than
// silent.
//
// Between PeakPatch and zoom this selects a halo from the merged catalogue and
// derives the refinement region, so the zoom config it generates is a function
// of the catalogue the previous stage actually produced.
std::vector<StageResult> run_pipeline(const RunSpec& spec, const RunOptions& o);

// Re-hash every output recorded in every manifest under <root>.
// Empty vector means the run is exactly what its manifests say it is.
std::vector<std::string> verify_run(const std::string& root);

// Human-readable one-line summary of a stage result, for the CLI.
std::string describe(const StageResult& r);

}  // namespace ppmi
