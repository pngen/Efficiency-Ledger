#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

namespace eltest {

inline int& failures() {
  static int f = 0;
  return f;
}
inline int& checks() {
  static int c = 0;
  return c;
}

}  // namespace eltest

#define CHECK(cond)                                                          \
  do {                                                                       \
    ++eltest::checks();                                                      \
    if (!(cond)) {                                                           \
      ++eltest::failures();                                                  \
      std::fprintf(stderr, "CHECK FAILED at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                        \
  } while (0)

#define CHECK_EQ(a, b)                                                       \
  do {                                                                       \
    ++eltest::checks();                                                      \
    if (!((a) == (b))) {                                                     \
      ++eltest::failures();                                                  \
      std::fprintf(stderr, "CHECK_EQ FAILED at %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
    }                                                                        \
  } while (0)

#define CHECK_STREQ(a, b)                                                    \
  do {                                                                       \
    ++eltest::checks();                                                      \
    if (std::string(a) != std::string(b)) {                                  \
      ++eltest::failures();                                                  \
      std::fprintf(stderr, "CHECK_STREQ FAILED at %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
    }                                                                        \
  } while (0)

#define TEST_MAIN_END()                                                      \
  if (eltest::failures() != 0) {                                             \
    std::fprintf(stderr, "FAILED: %d/%d checks\n", eltest::failures(), eltest::checks()); \
    return 1;                                                                \
  }                                                                          \
  std::printf("OK: %d checks passed\n", eltest::checks());                   \
  std::fflush(stdout);                                                       \
  return 0
