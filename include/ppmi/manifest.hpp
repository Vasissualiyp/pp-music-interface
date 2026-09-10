// manifest.hpp - provenance, so an old catalogue and a new spec are
// distinguishable from a matched pair.
//
// Each stage runs in its own directory and leaves a manifest recording the
// command, the git commit of all three repos, every input and output with its
// size and SHA-256, the wall time and the exit status. Without this,
// reproducibility is a matter of memory.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ppmi {

struct FileRecord {
  std::string path;
  std::int64_t bytes = 0;
  std::string sha256;
};

struct Manifest {
  std::string stage;
  std::string command;
  std::string started_utc;
  double wall_seconds = 0.0;
  int exit_status = 0;
  std::string music_commit;
  std::string peakpatch_commit;
  std::string interface_commit;
  std::string spec_sha256;
  std::vector<FileRecord> inputs;
  std::vector<FileRecord> outputs;

  std::string to_ini() const;
  static Manifest from_ini(const std::string& text);
};

std::string sha256_file(const std::string& path);
FileRecord record_file(const std::string& path);

// Re-hash every recorded output and report mismatches. Empty vector means the
// stage's outputs are exactly what the manifest says they are.
std::vector<std::string> verify(const Manifest& m, const std::string& root);

}  // namespace ppmi
