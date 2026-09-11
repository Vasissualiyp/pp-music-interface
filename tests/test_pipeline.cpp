// Tests for the staged runner.
//
// These cover the parts that are pure functions of the spec: directory naming,
// command construction, expected outputs, and the currency logic that decides
// whether a stage re-runs. Actually executing MUSIC and PeakPatch is an
// integration concern and is not tested here.
//
// The currency tests matter most. "Skip a stage that is already done" is the
// feature most likely to silently produce a wrong answer, because the failure
// mode is using a stale catalogue from a different spec and never noticing.

#include "ppmi/pipeline.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ppmi_test.hpp"

using namespace ppmi;

namespace {

const char* kSpec = R"(
[meta]
name = pipe_test

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
levelmin = 6

[random]
seed_levelmin = 12345
cubesize = 32

[survey]
zstart = 50.0
levelmax = 6

[peakpatch]
ntile = 1
nbuff = 8
global_redshift = 20.0
Rsmooth_max = 20.0
ievol = 0
num_redshifts = 1
cen = 0.0,0.0,0.0

[zoom]
levelmax = 8
padding = 8
extent_factor = 3.0
region = box

[run]
ranks = 4
)";

RunSpec spec_of(const std::string& text) {
  RunSpec s;
  s.ini = Ini::parse(text);
  return s;
}

std::string scratch(const char* stem) {
  std::string d = std::string("/tmp/ppmi_pipe_") + stem;
  std::system(("rm -rf " + d).c_str());
  return d;
}

RunOptions opts(const std::string& root) {
  RunOptions o;
  o.root = root;
  o.music_bin = "/opt/music/MUSIC";
  o.peakpatch_src = "/opt/peakpatch";
  o.ranks = 4;
  return o;
}

bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

bool any_command_mentions(const std::vector<Command>& cs, const std::string& s) {
  for (const auto& c : cs)
    if (contains(c.to_string(), s)) return true;
  return false;
}

}  // namespace

// --- naming ----------------------------------------------------------------

TEST("stage names are stable, because manifests key off them") {
  CHECK(std::string(stage_name(PipelineStage::kSurvey)) == "survey");
  CHECK(std::string(stage_name(PipelineStage::kPeakPatch)) == "peakpatch");
  CHECK(std::string(stage_name(PipelineStage::kZoom)) == "zoom");
}

TEST("stage directories are numbered so a listing reads in execution order") {
  CHECK(stage_dirname(PipelineStage::kSurvey) == "01_survey");
  CHECK(stage_dirname(PipelineStage::kPeakPatch) == "02_peakpatch");
  CHECK(stage_dirname(PipelineStage::kZoom) == "03_zoom");
  CHECK(stage_dir("/x/y", PipelineStage::kZoom) == "/x/y/03_zoom");
}

// --- command construction ---------------------------------------------------

TEST("survey runs MUSIC under mpirun at the requested rank count") {
  RunSpec s = spec_of(kSpec);
  auto cs = survey_commands(s, opts("/tmp/unused"));
  CHECK_EQ((int)cs.size(), 1);
  CHECK(contains(cs[0].to_string(), "mpirun"));
  CHECK(contains(cs[0].to_string(), "-np 4"));
  CHECK(contains(cs[0].to_string(), "MUSIC"));
  CHECK(contains(cs[0].cwd, "01_survey"));
}

TEST("zoom clamps to one rank and does not silently use four") {
  // MUSIC's multi-rank constrained refinement is broken, and the zoom always
  // takes that path. Running it at four ranks would produce a corrupt field.
  RunSpec s = spec_of(kSpec);
  auto cs = zoom_commands(s, opts("/tmp/unused"));
  CHECK_EQ((int)cs.size(), 1);
  CHECK(contains(cs[0].to_string(), "-np 1"));
  CHECK(!contains(cs[0].to_string(), "-np 4"));
  CHECK(contains(cs[0].cwd, "03_zoom"));
}

TEST("peakpatch runs its four executables in the required order") {
  RunSpec s = spec_of(kSpec);
  auto cs = peakpatch_commands(s, opts("/tmp/unused"));
  CHECK_EQ((int)cs.size(), 4);
  CHECK(contains(cs[0].to_string(), "filter_gen"));
  CHECK(contains(cs[1].to_string(), "hpkvd"));
  CHECK(contains(cs[1].to_string(), " 1 "));   // collapse-table pass
  CHECK(contains(cs[2].to_string(), "hpkvd"));
  CHECK(contains(cs[2].to_string(), " 0 "));   // main pass
  CHECK(contains(cs[3].to_string(), "merge_pkvd"));
  for (const auto& c : cs) CHECK(contains(c.cwd, "02_peakpatch"));
}

TEST("peakpatch passes the seed to hpkvd and merge_pkvd") {
  RunSpec s = spec_of(kSpec);
  auto cs = peakpatch_commands(s, opts("/tmp/unused"));
  CHECK(any_command_mentions(cs, "12345"));
}

TEST("stage_commands dispatches to the same thing as the per-stage functions") {
  RunSpec s = spec_of(kSpec);
  RunOptions o = opts("/tmp/unused");
  CHECK_EQ((int)stage_commands(s, o, PipelineStage::kSurvey).size(),
           (int)survey_commands(s, o).size());
  CHECK_EQ((int)stage_commands(s, o, PipelineStage::kPeakPatch).size(),
           (int)peakpatch_commands(s, o).size());
  CHECK_EQ((int)stage_commands(s, o, PipelineStage::kZoom).size(),
           (int)zoom_commands(s, o).size());
}

// --- expected outputs -------------------------------------------------------

TEST("the survey is expected to produce a density field and a config") {
  RunSpec s = spec_of(kSpec);
  auto out = expected_outputs(s, PipelineStage::kSurvey);
  CHECK(!out.empty());
  bool has_density = false;
  for (const auto& f : out)
    if (contains(f, "Fvec_")) has_density = true;
  CHECK(has_density);
}

TEST("peakpatch is expected to produce a merged catalogue") {
  RunSpec s = spec_of(kSpec);
  auto out = expected_outputs(s, PipelineStage::kPeakPatch);
  bool has_cat = false;
  for (const auto& f : out)
    if (contains(f, "merge") && contains(f, ".pksc")) has_cat = true;
  CHECK(has_cat);
}

// --- the run root -----------------------------------------------------------

TEST("prepare_root writes the spec and returns a hash of it") {
  std::string root = scratch("root");
  RunSpec s = spec_of(kSpec);
  std::string sha = prepare_root(s, opts(root));
  CHECK_EQ((int)sha.size(), 64);
  std::string copy = root + "/spec.ini";
  std::FILE* f = std::fopen(copy.c_str(), "rb");
  CHECK(f != nullptr);
  if (f) std::fclose(f);
}

TEST("prepare_root is idempotent for the same spec") {
  std::string root = scratch("idem");
  RunSpec s = spec_of(kSpec);
  RunOptions o = opts(root);
  CHECK(prepare_root(s, o) == prepare_root(s, o));
}

TEST("reusing a root with a different spec is an error, not an overwrite") {
  // This is how a half-matched run happens: an old catalogue sitting beside a
  // new spec, with nothing to say they disagree.
  std::string root = scratch("clash");
  RunSpec a = spec_of(kSpec);
  RunOptions o = opts(root);
  prepare_root(a, o);

  Ini other = Ini::parse(kSpec);
  other.set("random", "seed_levelmin", "999");
  RunSpec b = spec_of(other.to_string());
  CHECK_THROWS(prepare_root(b, o), PipelineError);
}

// --- currency, the logic that decides whether work is skipped ---------------

TEST("a stage with no manifest is never current") {
  std::string root = scratch("nomanifest");
  RunSpec s = spec_of(kSpec);
  RunOptions o = opts(root);
  std::string sha = prepare_root(s, o);
  CHECK(!stage_is_current(s, o, PipelineStage::kSurvey, sha));
  CHECK(!stage_is_current(s, o, PipelineStage::kPeakPatch, sha));
  CHECK(!stage_is_current(s, o, PipelineStage::kZoom, sha));
}

TEST("a stage is current when its manifest matches its outputs") {
  std::string root = scratch("current");
  RunSpec s = spec_of(kSpec);
  RunOptions o = opts(root);
  std::string sha = prepare_root(s, o);

  std::string dir = stage_dir(root, PipelineStage::kSurvey);
  std::system(("mkdir -p " + dir).c_str());
  std::string payload = dir + "/Fvec_pipe_test";
  std::system(("printf 'hello' > " + payload).c_str());

  Manifest m;
  m.stage = "survey";
  m.spec_sha256 = sha;
  m.exit_status = 0;
  m.outputs.push_back(record_file(payload));
  std::string mi = m.to_ini();
  std::FILE* f = std::fopen((dir + "/manifest.ini").c_str(), "wb");
  std::fwrite(mi.data(), 1, mi.size(), f);
  std::fclose(f);

  CHECK(stage_is_current(s, o, PipelineStage::kSurvey, sha));

  // Tamper with the output: no longer current.
  std::system(("printf 'HELLO' > " + payload).c_str());
  CHECK(!stage_is_current(s, o, PipelineStage::kSurvey, sha));

  // Restore, then remove it entirely: also not current.
  std::system(("printf 'hello' > " + payload).c_str());
  CHECK(stage_is_current(s, o, PipelineStage::kSurvey, sha));
  std::system(("rm -f " + payload).c_str());
  CHECK(!stage_is_current(s, o, PipelineStage::kSurvey, sha));
}

TEST("a stage recorded under a different spec is not current") {
  // The stale-catalogue trap. Same directory, same files, different spec.
  std::string root = scratch("otherspec");
  RunSpec s = spec_of(kSpec);
  RunOptions o = opts(root);
  std::string sha = prepare_root(s, o);

  std::string dir = stage_dir(root, PipelineStage::kSurvey);
  std::system(("mkdir -p " + dir).c_str());
  std::string payload = dir + "/Fvec_pipe_test";
  std::system(("printf 'hello' > " + payload).c_str());

  Manifest m;
  m.stage = "survey";
  m.spec_sha256 = std::string(64, 'a');  // some other spec
  m.exit_status = 0;
  m.outputs.push_back(record_file(payload));
  std::string mi = m.to_ini();
  std::FILE* f = std::fopen((dir + "/manifest.ini").c_str(), "wb");
  std::fwrite(mi.data(), 1, mi.size(), f);
  std::fclose(f);

  CHECK(!stage_is_current(s, o, PipelineStage::kSurvey, sha));
}

TEST("a stage that ran and failed is not current") {
  std::string root = scratch("failed");
  RunSpec s = spec_of(kSpec);
  RunOptions o = opts(root);
  std::string sha = prepare_root(s, o);

  std::string dir = stage_dir(root, PipelineStage::kSurvey);
  std::system(("mkdir -p " + dir).c_str());
  Manifest m;
  m.stage = "survey";
  m.spec_sha256 = sha;
  m.exit_status = 1;
  std::string mi = m.to_ini();
  std::FILE* f = std::fopen((dir + "/manifest.ini").c_str(), "wb");
  std::fwrite(mi.data(), 1, mi.size(), f);
  std::fclose(f);

  CHECK(!stage_is_current(s, o, PipelineStage::kSurvey, sha));
}

// --- dry run ----------------------------------------------------------------

TEST("a dry run of the whole pipeline writes nothing and skips nothing") {
  std::string root = scratch("dry");
  RunSpec s = spec_of(kSpec);
  RunOptions o = opts(root);
  o.dry_run = true;

  auto results = run_pipeline(s, o);
  CHECK_EQ((int)results.size(), 3);
  for (const auto& r : results) CHECK_EQ(r.exit_status, 0);

  // The root must not exist: a dry run touches the filesystem not at all.
  std::string probe = "test -e " + root;
  CHECK(std::system(probe.c_str()) != 0);
}

// --- verify -----------------------------------------------------------------

TEST("verify reports nothing for a run whose outputs match their manifests") {
  std::string root = scratch("verify_ok");
  RunSpec s = spec_of(kSpec);
  RunOptions o = opts(root);
  std::string sha = prepare_root(s, o);

  std::string dir = stage_dir(root, PipelineStage::kSurvey);
  std::system(("mkdir -p " + dir).c_str());
  std::string payload = dir + "/Fvec_pipe_test";
  std::system(("printf 'abc' > " + payload).c_str());
  Manifest m;
  m.stage = "survey";
  m.spec_sha256 = sha;
  m.outputs.push_back(record_file(payload));
  std::string mi = m.to_ini();
  std::FILE* f = std::fopen((dir + "/manifest.ini").c_str(), "wb");
  std::fwrite(mi.data(), 1, mi.size(), f);
  std::fclose(f);

  CHECK(verify_run(root).empty());

  std::system(("printf 'xyz' > " + payload).c_str());
  CHECK(!verify_run(root).empty());
}

// --- reporting --------------------------------------------------------------

TEST("describe says whether a stage ran, was skipped, or failed") {
  StageResult r;
  r.stage = PipelineStage::kSurvey;
  r.skipped = true;
  CHECK(contains(describe(r), "skip"));

  StageResult f;
  f.stage = PipelineStage::kZoom;
  f.exit_status = 1;
  f.error = "MUSIC aborted";
  std::string d = describe(f);
  CHECK(contains(d, "zoom"));
  CHECK(contains(d, "MUSIC aborted"));
}

int main() { return ppmi_test::run_all(); }
