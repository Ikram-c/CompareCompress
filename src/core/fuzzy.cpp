/**
 * @file fuzzy.cpp
 * @brief Iterative dynamic-programming subsequence matcher.
 */
#include "core/fuzzy.h"

#include "util/contract.h"
#include "util/strings.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>

namespace cc {

namespace {

/** @brief Score of an impossible alignment. */
constexpr int kNoScore = std::numeric_limits<int>::min();

/** @brief Characters after which a match earns the separator bonus. */
bool is_separator(char c)
{
  return c == ' ' || c == '-' || c == '_' || c == '.' || c == '/' || c == '(' || c == ')' || c == ',';
}

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

/** @brief Bonus for matching haystack position j, independent of the other positions. */
int position_bonus(const std::string& hay, std::size_t j, const FuzzyConfig& cfg)
{
  if(j == 0)
  {
    return cfg.first_letter_bonus;
  }
  const char prev = hay[j - 1];
  const char cur = hay[j];
  if(is_separator(prev))
  {
    return cfg.separator_bonus;
  }
  if(std::islower(static_cast<unsigned char>(prev)) != 0 && std::isupper(static_cast<unsigned char>(cur)) != 0)
  {
    return cfg.camel_bonus;
  }
  return 0;
}

/** @brief Penalty for skipping the first j haystack characters before the first match. */
int leading_penalty(std::size_t j, const FuzzyConfig& cfg)
{
  return std::max(cfg.max_leading_letter_penalty, cfg.leading_letter_penalty * static_cast<int>(j));
}

/** @brief Removes white space and truncates to the formal bound. */
std::string clean_pattern(const std::string& pattern)
{
  std::string pat;
  for(std::size_t i = 0; i < pattern.size() && pat.size() < kFuzzyMaxPatternChars; ++i)
  {
    if(std::isspace(static_cast<unsigned char>(pattern[i])) == 0)
    {
      pat.push_back(pattern[i]);
    }
  }
  return pat;
}

/**
 * @brief Best score for matching the previous pattern character before position j.
 * @param prev Score row of the previous pattern character.
 * @param j Haystack position the current character is matched at.
 * @param cfg Gap penalties.
 * @param parent Receives the position of that previous match, or -1.
 * @return The best predecessor score including the gap penalty, or kNoScore.
 */
int best_predecessor(const std::vector<int>& prev, std::size_t j, const FuzzyConfig& cfg, int& parent)
{
  int base = kNoScore;
  parent = -1;
  for(std::size_t k = 0; k < j; ++k)
  {
    if(prev[k] == kNoScore)
    {
      continue;
    }
    const int candidate = prev[k] + (k + 1 == j ? cfg.sequential_bonus : 0);
    if(candidate > base)
    {
      base = candidate;
      parent = static_cast<int>(k);
    }
  }
  return base;
}

/**
 * @brief Fills the row for pattern character `pc` from the previous row.
 * @param hay The (lower-cased) haystack.
 * @param pc The pattern character.
 * @param first True for the first pattern character (no previous row).
 * @param prev Score row of the previous pattern character.
 * @param cur Receives the score row for `pc`.
 * @param parents Receives one back pointer per haystack position.
 * @param cfg Scoring parameters.
 */
void fill_row(const std::string& hay, char pc, bool first, const std::vector<int>& prev, std::vector<int>& cur, int* parents,
              const FuzzyConfig& cfg)
{
  const std::size_t n = hay.size();
  for(std::size_t j = 0; j < n; ++j)
  {
    cur[j] = kNoScore;
    parents[j] = -1;
    if(lower(hay[j]) != pc)
    {
      continue;
    }
    int parent = -1;
    const int base = first ? leading_penalty(j, cfg) : best_predecessor(prev, j, cfg, parent);
    if(base != kNoScore)
    {
      cur[j] = base + position_bonus(hay, j, cfg);
      parents[j] = parent;
    }
  }
}

/**
 * @brief Runs the dynamic programme.
 * @param pat The (lower-cased) pattern.
 * @param hay The (lower-cased) haystack.
 * @param cfg Scoring parameters.
 * @param positions Receives the best alignment.
 * @return The alignment score without the length-dependent terms, or kNoScore.
 */
int best_alignment(const std::string& pat, const std::string& hay, const FuzzyConfig& cfg, std::vector<int>& positions)
{
  const std::size_t m = pat.size();
  const std::size_t n = hay.size();
  std::vector<int> prev(n, kNoScore);
  std::vector<int> cur(n, kNoScore);
  std::vector<int> parents(m * n, -1);
  for(std::size_t i = 0; i < m; ++i)
  {
    fill_row(hay, lower(pat[i]), i == 0, prev, cur, &parents[i * n], cfg);
    std::swap(prev, cur);
  }
  int best = kNoScore;
  int best_j = -1;
  for(std::size_t j = 0; j < n; ++j)
  {
    if(prev[j] > best)
    {
      best = prev[j];
      best_j = static_cast<int>(j);
    }
  }
  if(best == kNoScore)
  {
    return kNoScore;
  }
  positions.assign(m, 0);
  int j = best_j;
  for(std::size_t i = m; i-- > 0 && j >= 0;)
  {
    positions[i] = j;
    j = parents[i * n + static_cast<std::size_t>(j)];
  }
  return best;
}

} // namespace

FuzzyMatch fuzzy_match(const std::string& pattern, const std::string& haystack, const FuzzyConfig& cfg)
{
  FuzzyMatch m;
  const std::string pat = clean_pattern(pattern);
  if(pat.empty())
  {
    m.matched = true;
    m.score = 0;
    return m;
  }
  const std::string hay = haystack.substr(0, kFuzzyMaxHaystackChars);
  if(pat.size() > hay.size())
  {
    return m;
  }
  std::vector<int> positions;
  const int alignment = best_alignment(pat, hay, cfg, positions);
  if(alignment == kNoScore)
  {
    return m;
  }
  CC_ENSURE(positions.size() == pat.size());
  m.matched = true;
  m.positions = positions;
  const int unmatched = static_cast<int>(hay.size()) - static_cast<int>(pat.size());
  m.score = cfg.base_score + alignment + cfg.unmatched_letter_penalty * unmatched;
  if(to_lower(hay).find(to_lower(pat)) != std::string::npos)
  {
    m.score += cfg.exact_substring_bonus;
  }
  return m;
}

FuzzyMatch fuzzy_match_aliases(const std::string& pattern, const std::string& name, const std::vector<std::string>& aliases,
                               const FuzzyConfig& cfg)
{
  FuzzyMatch best = fuzzy_match(pattern, name, cfg);
  for(const std::string& alias : aliases)
  {
    if(alias.empty())
    {
      continue;
    }
    const FuzzyMatch m = fuzzy_match(pattern, alias, cfg);
    if(m.matched && (!best.matched || m.score > best.score))
    {
      /* alias matches are reported against the display name (no positions) */
      best.matched = true;
      best.score = m.score;
      best.positions.clear();
    }
  }
  return best;
}

} // namespace cc
