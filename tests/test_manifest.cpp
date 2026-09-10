// Tests for manifest.cpp: SHA-256 known vectors, INI round-trip, and verify().
//
// Not wired into CMakeLists.txt (out of scope for this task); build and run
// manually, e.g.:
//   g++ -std=c++17 -Wall -Wextra -Iinclude -Itests src/manifest.cpp
//       tests/test_manifest.cpp -o /tmp/test_manifest && /tmp/test_manifest
//
// CHECK_EQ in ppmi_test.hpp formats mismatches with std::to_string, which has
// no std::string overload, so string comparisons here use plain CHECK(a==b).

#include "ppmi/manifest.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "ppmi_test.hpp"

using namespace ppmi;

namespace {
std::string write_temp_file(const std::string& name, const std::string& content) {
  std::string path = "/tmp/ppmi_manifest_test_" + name;
  std::ofstream f(path, std::ios::binary);
  f << content;
  f.close();
  return path;
}
}  // namespace

TEST("sha256: known vector, empty string") {
  std::string path = write_temp_file("empty", "");
  CHECK(sha256_file(path) ==
       "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST("sha256: known vector, abc") {
  std::string path = write_temp_file("abc", "abc");
  CHECK(sha256_file(path) ==
       "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST("sha256: a message spanning multiple 64-byte blocks") {
  // Known vector for "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq".
  std::string msg =
      "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  std::string path = write_temp_file("multiblock", msg);
  CHECK(sha256_file(path) ==
       "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST("sha256: a message long enough to need two padding blocks") {
  // NIST CAVP vector: SHA-256("a" * 1000000).
  std::string msg(1000000, 'a');
  std::string path = write_temp_file("million_a", msg);
  CHECK(sha256_file(path) ==
       "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST("record_file: bytes and sha256 match") {
  std::string path = write_temp_file("record", "abc");
  FileRecord fr = record_file(path);
  CHECK_EQ(fr.bytes, (std::int64_t)3);
  CHECK(fr.sha256 ==
       "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST("ini round trip: scalar fields") {
  Manifest m;
  m.stage = "hpkvd";
  m.command = "./bin/hpkvd parameters.ini";
  m.started_utc = "2026-09-10T12:00:00Z";
  m.wall_seconds = 123.456;
  m.exit_status = 0;
  m.music_commit = "deadbeef";
  m.peakpatch_commit = "cafef00d";
  m.interface_commit = "0badc0de";
  m.spec_sha256 = "abc123";

  std::string ini = m.to_ini();
  Manifest m2 = Manifest::from_ini(ini);

  CHECK(m2.stage == m.stage);
  CHECK(m2.command == m.command);
  CHECK(m2.started_utc == m.started_utc);
  CHECK_CLOSE(m2.wall_seconds, m.wall_seconds, 1e-9);
  CHECK_EQ(m2.exit_status, m.exit_status);
  CHECK(m2.music_commit == m.music_commit);
  CHECK(m2.peakpatch_commit == m.peakpatch_commit);
  CHECK(m2.interface_commit == m.interface_commit);
  CHECK(m2.spec_sha256 == m.spec_sha256);
}

TEST("ini round trip: inputs and outputs") {
  Manifest m;
  m.stage = "merge";
  FileRecord in1{"parameters.ini", 42, "aaaa"};
  FileRecord out1{"halos/merged.pksc", 123456789, "bbbb"};
  FileRecord out2{"halos/merged.pksc.log", 17, "cccc"};
  m.inputs.push_back(in1);
  m.outputs.push_back(out1);
  m.outputs.push_back(out2);

  std::string ini = m.to_ini();
  Manifest m2 = Manifest::from_ini(ini);

  CHECK_EQ((int)m2.inputs.size(), 1);
  CHECK_EQ((int)m2.outputs.size(), 2);
  CHECK(m2.inputs[0].path == in1.path);
  CHECK_EQ(m2.inputs[0].bytes, in1.bytes);
  CHECK(m2.inputs[0].sha256 == in1.sha256);
  CHECK(m2.outputs[0].path == out1.path);
  CHECK(m2.outputs[1].path == out2.path);
  CHECK_EQ(m2.outputs[1].bytes, out2.bytes);
}

TEST("verify: matches a real file with correct hash, catches a tampered one") {
  Manifest m;
  m.stage = "test";
  std::string path = write_temp_file("verify_target", "hello world");
  FileRecord fr = record_file(path);
  // record_file stores the absolute temp path; verify() joins root + "/" +
  // path when root is non-empty, so use an empty root and an absolute path.
  m.outputs.push_back(fr);

  std::vector<std::string> mism = verify(m, "");
  CHECK_EQ((int)mism.size(), 0);

  // Now tamper with the file's recorded hash and confirm verify() catches it.
  Manifest bad = m;
  bad.outputs[0].sha256 =
      "0000000000000000000000000000000000000000000000000000000000000000";
  std::vector<std::string> mism2 = verify(bad, "");
  CHECK_EQ((int)mism2.size(), 1);
}

TEST("verify: missing output is reported") {
  Manifest m;
  m.stage = "test";
  FileRecord fr;
  fr.path = "/tmp/ppmi_manifest_test_this_file_does_not_exist_12345";
  fr.bytes = 0;
  fr.sha256 = "deadbeef";
  m.outputs.push_back(fr);
  std::vector<std::string> mism = verify(m, "");
  CHECK_EQ((int)mism.size(), 1);
}

int main() { return ppmi_test::run_all(); }
