#pragma once
// Tiny, dependency-free test harness. No network, no vendored libs — works
// identically on the dev machine (MSVC) and in CI (gcc). Define tests with
// TEST_CASE(name) { ... } and assert with CHECK / CHECK_EQ / REQUIRE.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace tf {

struct TestCase {
  const char* name;
  void (*fn)();
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}

inline int& failures() {
  static int f = 0;
  return f;
}

inline int& checks() {
  static int c = 0;
  return c;
}

struct Registrar {
  Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

}  // namespace tf

#define TEST_CASE(test_name)                                       \
  static void test_name();                                         \
  static ::tf::Registrar tf_reg_##test_name(#test_name, &test_name); \
  static void test_name()

#define CHECK(cond)                                                       \
  do {                                                                    \
    ::tf::checks()++;                                                     \
    if (!(cond)) {                                                        \
      ::tf::failures()++;                                                 \
      std::printf("  FAIL %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
    }                                                                     \
  } while (0)

#define CHECK_EQ(a, b)                                                          \
  do {                                                                         \
    ::tf::checks()++;                                                          \
    auto tf_va = (a);                                                          \
    auto tf_vb = (b);                                                          \
    if (!(tf_va == tf_vb)) {                                                   \
      ::tf::failures()++;                                                      \
      std::printf("  FAIL %s:%d: CHECK_EQ(%s == %s)  [left=%lld right=%lld]\n", \
                  __FILE__, __LINE__, #a, #b,                                  \
                  static_cast<long long>(tf_va),                              \
                  static_cast<long long>(tf_vb));                             \
    }                                                                         \
  } while (0)

#define REQUIRE(cond)                                                        \
  do {                                                                       \
    ::tf::checks()++;                                                        \
    if (!(cond)) {                                                           \
      ::tf::failures()++;                                                    \
      std::printf("  FATAL %s:%d: REQUIRE(%s)\n", __FILE__, __LINE__, #cond); \
      return;                                                                \
    }                                                                        \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                 \
  do {                                                                       \
    ::tf::checks()++;                                                        \
    const double tf_da = static_cast<double>(a);                            \
    const double tf_db = static_cast<double>(b);                            \
    double tf_diff = tf_da - tf_db;                                          \
    if (tf_diff < 0) tf_diff = -tf_diff;                                     \
    if (tf_diff > static_cast<double>(eps)) {                               \
      ::tf::failures()++;                                                    \
      std::printf("  FAIL %s:%d: CHECK_NEAR(%s ~= %s)  [%f vs %f]\n",        \
                  __FILE__, __LINE__, #a, #b, tf_da, tf_db);                 \
    }                                                                       \
  } while (0)
