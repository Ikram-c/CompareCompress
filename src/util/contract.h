/**
 * @file contract.h
 * @brief Precondition and postcondition checks with an explicit recovery action.
 *
 * The Power of 10 asks for at least two assertions per function and for every
 * failed assertion to trigger a recovery instead of undefined behaviour.
 * CC_REQUIRE evaluates a side-effect-free condition, records a violation when
 * it fails and then runs the recovery statement supplied by the caller
 * (usually `return <error value>`).  CC_ENSURE records a violation and
 * carries on, for postconditions that must never fire but whose failure is
 * harmless to continue past.  Both stay enabled in release builds.
 */
#pragma once

namespace cc {

/**
 * @brief Records a contract violation on stderr and in a global counter.
 * @param condition Source text of the failed condition.
 * @param file Source file of the check.
 * @param line Source line of the check.
 */
void contract_violation(const char* condition, const char* file, int line);

/** @brief Number of violations recorded since the process started. */
long contract_violation_count();

} // namespace cc

/** @brief Checks a precondition; on failure records it and runs `recovery`. */
#define CC_REQUIRE(condition, recovery)                                                                                \
  do                                                                                                                   \
  {                                                                                                                    \
    if(!(condition))                                                                                                   \
    {                                                                                                                  \
      ::cc::contract_violation(#condition, __FILE__, __LINE__);                                                        \
      recovery;                                                                                                        \
    }                                                                                                                  \
  } while(false)

/** @brief Checks a postcondition or invariant; on failure records it and continues. */
#define CC_ENSURE(condition)                                                                                           \
  do                                                                                                                   \
  {                                                                                                                    \
    if(!(condition)) ::cc::contract_violation(#condition, __FILE__, __LINE__);                                         \
  } while(false)
