// ppmi_test.hpp - a deliberately tiny assert harness.
//
// No external dependency, because the orchestrator is meant to build on a bare
// cluster with nothing but a C++ compiler. Catch2 or doctest would be nicer to
// read; neither is worth an extra thing to install on Trillium.
//
// Usage:
//     TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
//     int main() { return ppmi_test::run_all(); }

#pragma once

#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace ppmi_test {

struct Case {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) {
    registry().push_back({name, std::move(fn)});
  }
};

struct Failure : std::exception {
  std::string msg;
  explicit Failure(std::string m) : msg(std::move(m)) {}
  const char* what() const noexcept override { return msg.c_str(); }
};

inline void fail(const char* file, int line, const std::string& what) {
  throw Failure(std::string(file) + ":" + std::to_string(line) + "  " + what);
}

inline bool close(double a, double b, double rtol, double atol) {
  if (std::isnan(a) || std::isnan(b)) return false;
  return std::fabs(a - b) <= atol + rtol * std::fabs(b);
}

inline int run_all() {
  int failed = 0;
  for (auto& c : registry()) {
    try {
      c.fn();
      std::printf("  ok    %s\n", c.name.c_str());
    } catch (const Failure& f) {
      std::printf("  FAIL  %s\n        %s\n", c.name.c_str(), f.what());
      ++failed;
    } catch (const std::exception& e) {
      std::printf("  FAIL  %s\n        unexpected exception: %s\n",
                  c.name.c_str(), e.what());
      ++failed;
    }
  }
  std::printf("%d/%zu passed\n", (int)registry().size() - failed,
              registry().size());
  return failed == 0 ? 0 : 1;
}

}  // namespace ppmi_test

#define PPMI_CAT2(a, b) a##b
#define PPMI_CAT(a, b) PPMI_CAT2(a, b)

#define TEST(name)                                                     \
  static void PPMI_CAT(ppmi_test_fn_, __LINE__)();                     \
  static ppmi_test::Registrar PPMI_CAT(ppmi_test_reg_, __LINE__)(      \
      name, PPMI_CAT(ppmi_test_fn_, __LINE__));                        \
  static void PPMI_CAT(ppmi_test_fn_, __LINE__)()

#define CHECK(cond) \
  do { if (!(cond)) ppmi_test::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define CHECK_EQ(a, b)                                                        \
  do {                                                                        \
    auto _a = (a); auto _b = (b);                                             \
    if (!(_a == _b))                                                          \
      ppmi_test::fail(__FILE__, __LINE__,                                     \
                      "CHECK_EQ(" #a ", " #b ") got " + std::to_string(_a) +  \
                          " want " + std::to_string(_b));                     \
  } while (0)

#define CHECK_CLOSE(a, b, rtol)                                               \
  do {                                                                        \
    double _a = (double)(a), _b = (double)(b);                                \
    if (!ppmi_test::close(_a, _b, (rtol), 0.0))                               \
      ppmi_test::fail(__FILE__, __LINE__,                                     \
                      "CHECK_CLOSE(" #a ", " #b ") got " + std::to_string(_a) +\
                          " want " + std::to_string(_b));                     \
  } while (0)

#define CHECK_THROWS(expr, ExcType)                                           \
  do {                                                                        \
    bool _caught = false;                                                     \
    try { (void)(expr); } catch (const ExcType&) { _caught = true; }          \
    catch (...) {                                                             \
      ppmi_test::fail(__FILE__, __LINE__,                                     \
                      "CHECK_THROWS(" #expr ") threw the wrong type");        \
    }                                                                         \
    if (!_caught)                                                             \
      ppmi_test::fail(__FILE__, __LINE__,                                     \
                      "CHECK_THROWS(" #expr ", " #ExcType ") did not throw"); \
  } while (0)
