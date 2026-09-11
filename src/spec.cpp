// spec.cpp - implementation of include/ppmi/spec.hpp: the Ini parser, RunSpec
// accessors, and the validation rules.
//
// See spec.hpp for the annotated example spec and the citations behind each
// rule.

#include "ppmi/spec.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace ppmi {

// ---------------------------------------------------------------------------
// Ini
// ---------------------------------------------------------------------------

namespace {

std::string trim(const std::string& s) {
  const char* delims = " \t\r\n";
  std::string::size_type start = s.find_first_not_of(delims);
  if (start == std::string::npos) return "";
  std::string::size_type end = s.find_last_not_of(delims);
  return s.substr(start, end - start + 1);
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                  [](unsigned char c) { return std::tolower(c); });
  return s;
}

}  // namespace

struct Ini::Impl {
  std::vector<std::string> section_order;
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> data;
};

namespace {
const std::vector<std::pair<std::string, std::string>>& empty_entries() {
  static const std::vector<std::pair<std::string, std::string>> e;
  return e;
}
}  // namespace

Ini Ini::parse(const std::string& text) {
  Ini ini;
  ini.impl_ = std::make_shared<Ini::Impl>();

  std::istringstream stream(text);
  std::string line;
  std::string in_section;

  while (std::getline(stream, line)) {
    // Strip a trailing '\r' in case the file has CRLF endings.
    if (!line.empty() && line.back() == '\r') line.pop_back();

    if (line.empty()) continue;

    // Comment characters mirror MUSIC's own parser (config_file.hh): '#',
    // ';' and '%' start a comment anywhere in the line.
    std::string::size_type idx = line.find_first_of("#;%");
    if (idx != std::string::npos) line.erase(idx);

    if (line.empty()) continue;

    if (line[0] == '[') {
      std::string::size_type close = line.find(']');
      std::string name = (close == std::string::npos)
                              ? line.substr(1)
                              : line.substr(1, close - 1);
      in_section = trim(name);
      if (std::find(ini.impl_->section_order.begin(),
                     ini.impl_->section_order.end(),
                     in_section) == ini.impl_->section_order.end()) {
        ini.impl_->section_order.push_back(in_section);
        ini.impl_->data[in_section];
      }
      continue;
    }

    std::string::size_type pos_equal = line.find('=');
    std::string name = trim(line.substr(0, pos_equal));
    std::string value =
        (pos_equal == std::string::npos) ? "" : trim(line.substr(pos_equal + 1));

    if (pos_equal == std::string::npos && (!name.empty() || !value.empty())) {
      // Non-assignment line; MUSIC's parser warns and ignores it.
      continue;
    }
    if (name.empty() || value.empty()) {
      // Missing name or missing value; MUSIC's parser warns and ignores it.
      continue;
    }

    if (std::find(ini.impl_->section_order.begin(), ini.impl_->section_order.end(),
                   in_section) == ini.impl_->section_order.end()) {
      ini.impl_->section_order.push_back(in_section);
      ini.impl_->data[in_section];
    }

    auto& entries = ini.impl_->data[in_section];
    bool replaced = false;
    for (auto& kv : entries) {
      if (kv.first == name) {
        kv.second = value;
        replaced = true;
        break;
      }
    }
    if (!replaced) entries.emplace_back(name, value);
  }

  return ini;
}

Ini Ini::load(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw SpecError("could not open spec file '" + path + "'");
  }
  std::ostringstream buf;
  buf << file.rdbuf();
  return Ini::parse(buf.str());
}

bool Ini::has(const std::string& section, const std::string& key) const {
  if (!impl_) return false;
  auto it = impl_->data.find(section);
  if (it == impl_->data.end()) return false;
  for (const auto& kv : it->second) {
    if (kv.first == key) return true;
  }
  return false;
}

std::string Ini::get(const std::string& section, const std::string& key) const {
  if (impl_) {
    auto it = impl_->data.find(section);
    if (it != impl_->data.end()) {
      for (const auto& kv : it->second) {
        if (kv.first == key) return kv.second;
      }
    }
  }
  throw SpecSyntaxError("missing key '" + key + "' in section [" + section + "]");
}

std::string Ini::get_or(const std::string& section, const std::string& key,
                        const std::string& fallback) const {
  if (has(section, key)) return get(section, key);
  return fallback;
}

int Ini::get_int(const std::string& section, const std::string& key) const {
  std::string v = get(section, key);
  try {
    size_t pos = 0;
    int result = std::stoi(v, &pos);
    if (pos != v.size()) throw std::invalid_argument("trailing characters");
    return result;
  } catch (const std::exception&) {
    throw SpecSyntaxError("key [" + section + "]/" + key +
                          " = '" + v + "' is not a valid integer");
  }
}

double Ini::get_double(const std::string& section, const std::string& key) const {
  std::string v = get(section, key);
  try {
    size_t pos = 0;
    double result = std::stod(v, &pos);
    if (pos != v.size()) throw std::invalid_argument("trailing characters");
    return result;
  } catch (const std::exception&) {
    throw SpecSyntaxError("key [" + section + "]/" + key +
                          " = '" + v + "' is not a valid number");
  }
}

bool Ini::get_bool(const std::string& section, const std::string& key) const {
  std::string v = lower(trim(get(section, key)));
  if (v == "true" || v == "yes" || v == "on" || v == "1") return true;
  if (v == "false" || v == "no" || v == "off" || v == "0") return false;
  throw SpecSyntaxError("key [" + section + "]/" + key + " = '" + v +
                        "' is not a valid boolean (use true/yes/on/1 or "
                        "false/no/off/0)");
}

Vec3 Ini::get_vec3(const std::string& section, const std::string& key) const {
  std::string v = get(section, key);
  std::vector<std::string> parts;
  std::string::size_type start = 0;
  while (true) {
    std::string::size_type comma = v.find(',', start);
    if (comma == std::string::npos) {
      parts.push_back(trim(v.substr(start)));
      break;
    }
    parts.push_back(trim(v.substr(start, comma - start)));
    start = comma + 1;
  }
  if (parts.size() != 3) {
    throw SpecSyntaxError("key [" + section + "]/" + key + " = '" + v +
                          "' is not a comma-separated triple");
  }
  Vec3 out;
  try {
    for (int i = 0; i < 3; ++i) {
      size_t pos = 0;
      out[i] = std::stod(parts[i], &pos);
      if (pos != parts[i].size()) throw std::invalid_argument("trailing characters");
    }
  } catch (const std::exception&) {
    throw SpecSyntaxError("key [" + section + "]/" + key + " = '" + v +
                          "' is not a valid comma-separated triple of numbers");
  }
  return out;
}

void Ini::set(const std::string& section, const std::string& key,
             const std::string& value) {
  if (!impl_) impl_ = std::make_shared<Ini::Impl>();
  if (std::find(impl_->section_order.begin(), impl_->section_order.end(), section) ==
      impl_->section_order.end()) {
    impl_->section_order.push_back(section);
    impl_->data[section];
  }
  auto& entries = impl_->data[section];
  for (auto& kv : entries) {
    if (kv.first == key) {
      kv.second = value;
      return;
    }
  }
  entries.emplace_back(key, value);
}

std::string Ini::to_string() const {
  std::ostringstream out;
  if (!impl_) return out.str();
  for (const auto& section : impl_->section_order) {
    out << "[" << section << "]\n";
    auto it = impl_->data.find(section);
    if (it != impl_->data.end()) {
      for (const auto& kv : it->second) {
        out << kv.first << " = " << kv.second << "\n";
      }
    }
    out << "\n";
  }
  return out.str();
}

const std::vector<std::string>& Ini::sections() const {
  static const std::vector<std::string> empty;
  if (!impl_) return empty;
  return impl_->section_order;
}

const std::vector<std::pair<std::string, std::string>>& Ini::entries(
    const std::string& section) const {
  if (!impl_) return empty_entries();
  auto it = impl_->data.find(section);
  if (it == impl_->data.end()) return empty_entries();
  return it->second;
}

// ---------------------------------------------------------------------------
// RunSpec
// ---------------------------------------------------------------------------

int RunSpec::levelmin() const { return ini.get_int("box", "levelmin"); }

double RunSpec::boxlength() const { return ini.get_double("box", "boxlength"); }

int RunSpec::survey_levelmax() const { return ini.get_int("survey", "levelmax"); }

int RunSpec::zoom_levelmax() const { return ini.get_int("zoom", "levelmax"); }

int RunSpec::ranks() const { return ini.get_int("run", "ranks"); }

PeakPatchGrid RunSpec::peakpatch_grid() const {
  int ntile = ini.get_int("peakpatch", "ntile");
  int nbuff = ini.get_int("peakpatch", "nbuff");
  int lmin = levelmin();

  PeakPatchGrid grid;
  grid.nmesh = solve_nmesh(lmin, ntile, nbuff);
  grid.nbuff = nbuff;
  grid.ntile = ntile;
  grid.boxsize = boxlength();
  grid.validate();
  return grid;
}

BoxFrame RunSpec::frame() const {
  BoxFrame f;
  f.boxsize = boxlength();
  f.cen = ini.get_vec3("peakpatch", "cen");
  return f;
}

Cosmology RunSpec::cosmology() const {
  Cosmology c;
  c.Omega_m = ini.get_double("cosmology", "Omega_m");
  c.h = ini.get_double("cosmology", "H0") / 100.0;
  return c;
}

RunSpec load_spec(const std::string& path) {
  RunSpec s;
  s.ini = Ini::load(path);
  s.source_path = path;
  return s;
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

void check_realization_invariant(const RunSpec& spec) {
  // The keys that must be declared exactly once, in [box] and [random], and
  // never shadowed by a stage section. If a stage overrode any of these, the
  // survey and zoom runs would build white noise from different inputs and
  // silently become different universes (music_mpi/src/random.cc lines
  // 1898-2036).
  std::vector<std::string> frozen_keys;
  frozen_keys.push_back("levelmin");
  for (const auto& kv : spec.ini.entries("random")) {
    frozen_keys.push_back(kv.first);
  }

  static const char* const kStages[] = {"survey", "zoom"};
  for (const char* stage : kStages) {
    for (const auto& kv : spec.ini.entries(stage)) {
      for (const auto& frozen : frozen_keys) {
        if (kv.first == frozen) {
          throw InvariantViolation(
              std::string("[") + stage + "]/" + kv.first +
              " overrides a realization key that must be declared once, in "
              "[box] or [random]. The survey and zoom stages share a white "
              "noise realization only when levelmin, the whole [random] "
              "block, and cubesize are identical between them "
              "(music_mpi/src/random.cc builds white noise as a "
              "coarse-to-fine cascade); overriding '" +
              kv.first + "' in [" + stage +
              "] would make the two stages refine different universes.");
        }
      }
    }
  }
}

void check_grid_pairing(const RunSpec& spec) {
  try {
    PeakPatchGrid grid = spec.peakpatch_grid();
    if (!check_pairing(spec.levelmin(), grid)) {
      throw InvariantViolation(describe_mismatch(spec.levelmin(), grid));
    }
  } catch (const GeometryError& e) {
    throw InvariantViolation(std::string("grid pairing: ") + e.what());
  }
}

void check_survey_is_unigrid(const RunSpec& spec) {
  int lmin = spec.levelmin();
  int survey_lmax = spec.survey_levelmax();
  if (survey_lmax != lmin) {
    throw InvariantViolation(
        "survey.levelmax (" + std::to_string(survey_lmax) +
        ") must equal box.levelmin (" + std::to_string(lmin) +
        "); a zoomed survey would hand PeakPatch a field that is not "
        "uniform, which its tiling cannot represent.");
  }
}

void check_redshift_mode(const RunSpec& spec) {
  int ievol = spec.ini.get_int("peakpatch", "ievol");
  int num_redshifts = spec.ini.get_int("peakpatch", "num_redshifts");
  if (ievol != 0) {
    throw InvariantViolation(
        "[peakpatch]/ievol = " + std::to_string(ievol) +
        "; a single-redshift halo search needs ievol=0. With ievol=1 "
        "PeakPatch produces a lightcone and 'the largest halo at z' stops "
        "being well defined without a distance filter.");
  }
  if (num_redshifts != 1) {
    throw InvariantViolation(
        "[peakpatch]/num_redshifts = " + std::to_string(num_redshifts) +
        "; a single-redshift halo search needs num_redshifts=1.");
  }
}

void check_levels(const RunSpec& spec) {
  int lmin = spec.levelmin();
  int zoom_lmax = spec.zoom_levelmax();
  if (zoom_lmax < lmin) {
    throw InvariantViolation(
        "zoom.levelmax (" + std::to_string(zoom_lmax) +
        ") must be >= box.levelmin (" + std::to_string(lmin) +
        "); the zoom cannot refine below the resolution the survey was "
        "built at.");
  }
  if (lmin < 1 || zoom_lmax > 20) {
    throw InvariantViolation(
        "levelmin (" + std::to_string(lmin) + ") and zoom.levelmax (" +
        std::to_string(zoom_lmax) +
        ") must be within MUSIC's usable range [1,20].");
  }
}

void check_zoom_region(const RefRegion& region) {
  static const char* axis_names[3] = {"x", "y", "z"};
  for (int i = 0; i < 3; ++i) {
    if (region.center[i] < 0.0 || region.center[i] >= 1.0) {
      throw InvariantViolation(
          std::string("ref_center.") + axis_names[i] + " = " +
          std::to_string(region.center[i]) +
          " must lie in [0,1); MUSIC's Lagrangian frame is the unit box.");
    }
    if (region.extent[i] <= 0.0 || region.extent[i] > 1.0) {
      throw InvariantViolation(
          std::string("ref_extent.") + axis_names[i] + " = " +
          std::to_string(region.extent[i]) +
          " must lie in (0,1]; zero or negative extent refines nothing, and "
          "an extent past 1 exceeds the box.");
    }
  }
  for (int i = 0; i < 3; ++i) {
    double lo = region.center[i] - region.extent[i] / 2.0;
    double hi = region.center[i] + region.extent[i] / 2.0;
    if (lo < 0.0 || hi > 1.0) {
      throw InvariantViolation(
          std::string("the zoom region on axis ") + axis_names[i] +
          " reaches outside the unit box (center=" +
          std::to_string(region.center[i]) +
          ", extent=" + std::to_string(region.extent[i]) +
          "); MUSIC's box region generator cannot express a wrapping "
          "region.");
    }
  }
}

std::vector<std::string> check_mpi_overflow(const RunSpec& spec) {
  std::vector<std::string> warnings;
  int ranks = spec.ranks();
  if (ranks <= 0) return warnings;

  const double kLimit = 2147483647.0;  // INT_MAX, as music_mpi/src/poisson.cc

  struct StageLevel {
    const char* name;
    int levelmax;
  };
  StageLevel stages[] = {{"survey", spec.survey_levelmax()},
                         {"zoom", spec.zoom_levelmax()}};

  for (const auto& stage : stages) {
    const int n = stage.levelmax;
    if (n < 0 || n > 40) continue;
    const double G = std::ldexp(1.0, n);  // 2^levelmax
    // MUSIC's FFTW-MPI slab is an integer division of the global grid; use
    // the same integer slab size so the advisory matches its runtime guard.
    const double local = std::floor(G / ranks);
    const double value = local * local * (G / 2.0 + 1.0);
    if (value > kLimit) {
      // Mirror music_mpi/src/poisson.cc lines 556-564: find the minimum rank
      // count by doubling and testing the integer-division message count.
      long long min_p = 1;
      const long long Gll = 1LL << n;
      while (min_p < Gll) {
        min_p *= 2;
        const long long slab = Gll / min_p;
        if (static_cast<double>(slab) * static_cast<double>(slab) *
                static_cast<double>(Gll / 2 + 1) <=
            kLimit) {
          break;
        }
      }
      warnings.push_back(
          std::string("MPI overflow: the ") + stage.name + " stage grid G=" +
          std::to_string(static_cast<long long>(G)) + " on " +
          std::to_string(ranks) +
          " ranks exceeds MUSIC's 32-bit FFTW-MPI transpose message count "
          "((G/P)^2*(G/2+1) >= 2^31, music_mpi/src/poisson.cc lines "
          "540-579); use at least " +
          std::to_string(min_p) + " ranks.");
    }
  }
  return warnings;
}

std::vector<std::string> validate(const RunSpec& spec) {
  // Syntax-level access happens naturally inside every check below (missing
  // keys throw SpecSyntaxError). Order after that: grid pairing first (the
  // most fundamental structural fact), then the realization invariant, then
  // the remaining rules, so the user sees the most fundamental problem
  // first rather than a downstream symptom.
  check_grid_pairing(spec);
  check_realization_invariant(spec);
  check_survey_is_unigrid(spec);
  check_levels(spec);
  check_redshift_mode(spec);
  return check_mpi_overflow(spec);
}

}  // namespace ppmi
