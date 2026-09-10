// manifest.cpp - implementation of include/ppmi/manifest.hpp
//
// sha256_file is a from-scratch FIPS 180-4 implementation: no OpenSSL, no
// libcrypto, nothing but the standard library, because this binary has to
// build on a bare cluster login node.
//
// to_ini/from_ini use a deliberately tiny INI-like format of their own -- not
// spec.hpp's Ini class, which is a different agent's file and whose exact
// shape is not this file's business to depend on.

#include "ppmi/manifest.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace ppmi {

namespace {

// ---------------------------------------------------------------------------
// SHA-256, FIPS 180-4.
// ---------------------------------------------------------------------------

constexpr std::array<std::uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline std::uint32_t rotr(std::uint32_t x, unsigned n) {
  return (x >> n) | (x << (32 - n));
}

class Sha256 {
 public:
  Sha256() { reset(); }

  void reset() {
    h_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    buffer_.clear();
    total_len_ = 0;
  }

  void update(const unsigned char* data, std::size_t len) {
    total_len_ += len;
    buffer_.insert(buffer_.end(), data, data + len);
    std::size_t i = 0;
    while (buffer_.size() - i >= 64) {
      process_block(&buffer_[i]);
      i += 64;
    }
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(i));
  }

  std::string hex_digest() {
    // Padding: 0x80, then zeros, then the 64-bit bit-length, big-endian, to
    // bring the total to a multiple of 64 bytes.
    std::uint64_t bit_len = total_len_ * 8;
    std::vector<unsigned char> pad;
    pad.push_back(0x80);
    std::size_t current = buffer_.size() + 1;
    std::size_t zeros = (current % 64 <= 56) ? (56 - current % 64)
                                              : (56 + 64 - current % 64);
    pad.insert(pad.end(), zeros, 0x00);
    for (int i = 7; i >= 0; --i) {
      pad.push_back(static_cast<unsigned char>((bit_len >> (8 * i)) & 0xff));
    }
    update(pad.data(), pad.size());
    // update() above drains buffer_ in 64-byte blocks; after correct padding
    // it must be exactly empty.

    std::ostringstream os;
    for (std::uint32_t word : h_) {
      char buf[9];
      std::snprintf(buf, sizeof(buf), "%08x", word);
      os << buf;
    }
    return os.str();
  }

 private:
  void process_block(const unsigned char* block) {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
             (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
             (static_cast<std::uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
      std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    std::uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];

    for (int i = 0; i < 64; ++i) {
      std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      std::uint32_t ch = (e & f) ^ ((~e) & g);
      std::uint32_t temp1 = hh + s1 + ch + kK[i] + w[i];
      std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      std::uint32_t temp2 = s0 + maj;

      hh = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += hh;
  }

  std::array<std::uint32_t, 8> h_{};
  std::vector<unsigned char> buffer_;
  std::uint64_t total_len_ = 0;
};

// ---------------------------------------------------------------------------
// A tiny INI-like format for Manifest, independent of spec.hpp's Ini class.
// One key=value per line, grouped under [inputs]/[outputs] as repeated
// "path\tbytes\tsha256" lines. Values are written verbatim; none of the
// fields a Manifest carries can contain a newline, and paths are not
// expected to contain tabs.
// ---------------------------------------------------------------------------

std::string escape_line(const std::string& s) {
  // Guard against embedded newlines corrupting the format; nothing in a
  // Manifest is expected to contain one, but better an escaped character
  // than a silently broken file.
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\n') {
      out += "\\n";
    } else if (c == '\\') {
      out += "\\\\";
    } else {
      out += c;
    }
  }
  return out;
}

std::string unescape_line(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char n = s[i + 1];
      if (n == 'n') {
        out += '\n';
        ++i;
        continue;
      }
      if (n == '\\') {
        out += '\\';
        ++i;
        continue;
      }
    }
    out += s[i];
  }
  return out;
}

void write_kv(std::ostringstream& os, const std::string& key,
             const std::string& value) {
  os << key << "=" << escape_line(value) << "\n";
}

void write_file_record(std::ostringstream& os, const FileRecord& fr) {
  os << escape_line(fr.path) << "\t" << fr.bytes << "\t" << fr.sha256 << "\n";
}

}  // namespace

std::string sha256_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    throw std::runtime_error("sha256_file: cannot open " + path);
  }
  Sha256 hasher;
  std::vector<unsigned char> buf(1 << 16);
  while (f) {
    f.read(reinterpret_cast<char*>(buf.data()),
          static_cast<std::streamsize>(buf.size()));
    std::streamsize got = f.gcount();
    if (got > 0) {
      hasher.update(buf.data(), static_cast<std::size_t>(got));
    }
  }
  return hasher.hex_digest();
}

FileRecord record_file(const std::string& path) {
  FileRecord fr;
  fr.path = path;
  fr.sha256 = sha256_file(path);
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    throw std::runtime_error("record_file: cannot open " + path);
  }
  fr.bytes = static_cast<std::int64_t>(f.tellg());
  return fr;
}

std::string Manifest::to_ini() const {
  std::ostringstream os;
  os << "[manifest]\n";
  write_kv(os, "stage", stage);
  write_kv(os, "command", command);
  write_kv(os, "started_utc", started_utc);
  os << "wall_seconds=" << wall_seconds << "\n";
  os << "exit_status=" << exit_status << "\n";
  write_kv(os, "music_commit", music_commit);
  write_kv(os, "peakpatch_commit", peakpatch_commit);
  write_kv(os, "interface_commit", interface_commit);
  write_kv(os, "spec_sha256", spec_sha256);

  os << "[inputs]\n";
  for (const auto& fr : inputs) write_file_record(os, fr);

  os << "[outputs]\n";
  for (const auto& fr : outputs) write_file_record(os, fr);

  return os.str();
}

namespace {

std::vector<std::string> split_tabs(const std::string& line) {
  std::vector<std::string> parts;
  std::string cur;
  for (char c : line) {
    if (c == '\t') {
      parts.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  parts.push_back(cur);
  return parts;
}

}  // namespace

Manifest Manifest::from_ini(const std::string& text) {
  Manifest m;
  std::istringstream is(text);
  std::string line;
  enum class Section { kNone, kManifest, kInputs, kOutputs } section =
      Section::kNone;

  while (std::getline(is, line)) {
    if (line.empty()) continue;
    if (line.front() == '[' && line.back() == ']') {
      std::string name = line.substr(1, line.size() - 2);
      if (name == "manifest") {
        section = Section::kManifest;
      } else if (name == "inputs") {
        section = Section::kInputs;
      } else if (name == "outputs") {
        section = Section::kOutputs;
      } else {
        section = Section::kNone;
      }
      continue;
    }

    if (section == Section::kManifest) {
      std::size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string key = line.substr(0, eq);
      std::string value = unescape_line(line.substr(eq + 1));
      if (key == "stage") {
        m.stage = value;
      } else if (key == "command") {
        m.command = value;
      } else if (key == "started_utc") {
        m.started_utc = value;
      } else if (key == "wall_seconds") {
        m.wall_seconds = std::stod(value);
      } else if (key == "exit_status") {
        m.exit_status = std::stoi(value);
      } else if (key == "music_commit") {
        m.music_commit = value;
      } else if (key == "peakpatch_commit") {
        m.peakpatch_commit = value;
      } else if (key == "interface_commit") {
        m.interface_commit = value;
      } else if (key == "spec_sha256") {
        m.spec_sha256 = value;
      }
    } else if (section == Section::kInputs || section == Section::kOutputs) {
      std::vector<std::string> parts = split_tabs(line);
      if (parts.size() != 3) continue;
      FileRecord fr;
      fr.path = unescape_line(parts[0]);
      fr.bytes = std::stoll(parts[1]);
      fr.sha256 = parts[2];
      if (section == Section::kInputs) {
        m.inputs.push_back(fr);
      } else {
        m.outputs.push_back(fr);
      }
    }
  }

  return m;
}

std::vector<std::string> verify(const Manifest& m, const std::string& root) {
  std::vector<std::string> mismatches;
  for (const auto& fr : m.outputs) {
    std::string full = root.empty() ? fr.path : (root + "/" + fr.path);
    std::ifstream check(full, std::ios::binary);
    if (!check) {
      mismatches.push_back(fr.path + ": missing (expected sha256 " +
                           fr.sha256 + ")");
      continue;
    }
    check.close();

    std::string actual_sha;
    try {
      actual_sha = sha256_file(full);
    } catch (const std::exception& e) {
      mismatches.push_back(fr.path + ": could not hash (" +
                           std::string(e.what()) + ")");
      continue;
    }

    if (actual_sha != fr.sha256) {
      mismatches.push_back(fr.path + ": sha256 mismatch, expected " +
                           fr.sha256 + " got " + actual_sha);
      continue;
    }

    std::ifstream sz(full, std::ios::binary | std::ios::ate);
    std::int64_t actual_bytes = sz ? static_cast<std::int64_t>(sz.tellg()) : -1;
    if (actual_bytes != fr.bytes) {
      mismatches.push_back(fr.path + ": size mismatch, expected " +
                           std::to_string(fr.bytes) + " got " +
                           std::to_string(actual_bytes));
    }
  }
  return mismatches;
}

}  // namespace ppmi
