/**
 * @file fuzzy.h
 * @brief Fuzzy string matching for the "specify site" box: typing "insta",
 * "ig", "twtr" or "x" should rank Instagram / X (Twitter) first.
 *
 * The scoring is a Sublime-Text-style subsequence match: every pattern
 * character must appear in order; bonuses for matches at the start, after a
 * separator or a case change, and for consecutive matches; small penalties
 * for gaps.  The weights come from the `fuzzy` section of config.yaml.
 *
 * The best alignment is found with an iterative dynamic programme over
 * (pattern index, haystack index) instead of the usual recursive search, so
 * the work is bounded by kFuzzyMaxPatternChars * kFuzzyMaxHaystackChars^2.
 */
#pragma once

#include "core/config.h"

#include <string>
#include <vector>

namespace cc {

/** @brief Pattern characters considered; longer patterns are truncated. */
constexpr std::size_t kFuzzyMaxPatternChars = 64;
/** @brief Haystack characters considered; longer haystacks are truncated. */
constexpr std::size_t kFuzzyMaxHaystackChars = 256;

/** @brief Result of a fuzzy match. */
struct FuzzyMatch
{
  bool matched = false;
  int score = 0;
  std::vector<int> positions; /**< indices in the haystack that were matched (empty for alias matches) */
};

/**
 * @brief Matches a pattern against one string.
 * @param pattern What the user typed (white space is ignored).
 * @param haystack The candidate.
 * @param cfg `fuzzy` section.
 * @return matched == true with the best score when every pattern character
 *         appears in order; an empty pattern matches everything with score 0.
 */
[[nodiscard]] FuzzyMatch fuzzy_match(const std::string& pattern, const std::string& haystack, const FuzzyConfig& cfg);

/**
 * @brief Best score over a display name and its aliases.
 * @param pattern What the user typed.
 * @param name Display name; positions are reported against it.
 * @param aliases Alternative spellings; a match on one of them has no positions.
 * @param cfg `fuzzy` section.
 */
[[nodiscard]] FuzzyMatch fuzzy_match_aliases(const std::string& pattern, const std::string& name,
                                             const std::vector<std::string>& aliases, const FuzzyConfig& cfg);

} // namespace cc
