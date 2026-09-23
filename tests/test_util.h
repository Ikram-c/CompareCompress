/**
 * @file test_util.h
 * @brief Minimal test helpers (no framework dependency).
 *
 * Every test is a plain `main()` that runs CHECK macros and returns
 * test_summary(); ctest treats a non-zero exit code as a failure.
 */
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

static int g_failures = 0;
static int g_checks = 0;

/** @brief Records a failure unless `cond` holds. */
#define CHECK(cond)                                                                                                    \
  do                                                                                                                   \
  {                                                                                                                    \
    ++g_checks;                                                                                                        \
    if(!(cond))                                                                                                        \
    {                                                                                                                  \
      ++g_failures;                                                                                                    \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                             \
    }                                                                                                                  \
  } while(0)

/** @brief Records a failure unless |a - b| <= tol. */
#define CHECK_NEAR(a, b, tol)                                                                                          \
  do                                                                                                                   \
  {                                                                                                                    \
    ++g_checks;                                                                                                        \
    const double check_a = static_cast<double>(a);                                                                     \
    const double check_b = static_cast<double>(b);                                                                     \
    if(std::fabs(check_a - check_b) > static_cast<double>(tol))                                                         \
    {                                                                                                                  \
      ++g_failures;                                                                                                    \
      std::fprintf(stderr, "FAIL %s:%d: %s = %g, expected %g (tol %g)\n", __FILE__, __LINE__, #a, check_a, check_b,   \
                   static_cast<double>(tol));                                                                          \
    }                                                                                                                  \
  } while(0)

/** @brief Records a failure unless the two strings are equal. */
#define CHECK_EQ_STR(a, b)                                                                                             \
  do                                                                                                                   \
  {                                                                                                                    \
    ++g_checks;                                                                                                        \
    const std::string check_a = (a);                                                                                   \
    const std::string check_b = (b);                                                                                   \
    if(check_a != check_b)                                                                                             \
    {                                                                                                                  \
      ++g_failures;                                                                                                    \
      std::fprintf(stderr, "FAIL %s:%d: %s = '%s', expected '%s'\n", __FILE__, __LINE__, #a, check_a.c_str(),          \
                   check_b.c_str());                                                                                   \
    }                                                                                                                  \
  } while(0)

/**
 * @brief Prints the tally and converts it to an exit code.
 * @param name Test name for the report line.
 */
static int test_summary(const char* name)
{
  std::printf("%s: %d checks, %d failures\n", name, g_checks, g_failures);
  return g_failures != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
