/**
 * @file contract.cpp
 * @brief Implementation of the contract-violation sink.
 */
#include "util/contract.h"

#include <atomic>
#include <cstdio>

namespace cc {

namespace {

/** @brief Process-wide count of recorded violations. */
std::atomic<long> g_violations{0};

} // namespace

void contract_violation(const char* condition, const char* file, int line)
{
  g_violations.fetch_add(1);
  std::fprintf(stderr, "contract violation: %s (%s:%d)\n", condition ? condition : "?", file ? file : "?", line);
}

long contract_violation_count() { return g_violations.load(); }

} // namespace cc
