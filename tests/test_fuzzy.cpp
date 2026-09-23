/**
 * @file test_fuzzy.cpp
 * @brief Subsequence matcher semantics and site ranking for typical queries.
 */
#include "core/config.h"
#include "core/fuzzy.h"
#include "test_util.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace cc;

namespace {

/** @brief Ranks all known sites for a query the way the UI does and returns the top name. */
std::string top_site(const Config& cfg, const std::string& query)
{
  struct Row
  {
    int score;
    std::string name;
  };
  std::vector<Row> rows;
  for(const SiteInfo& s : cfg.sites)
  {
    const FuzzyMatch m = fuzzy_match_aliases(query, s.name, s.aliases, cfg.fuzzy);
    if(m.matched)
    {
      rows.push_back(Row{m.score, s.name});
    }
  }
  std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.score > b.score; });
  return rows.empty() ? "" : rows.front().name;
}

} // namespace

int main()
{
  const Config& cfg = default_config();
  const FuzzyConfig& f = cfg.fuzzy;

  /* basic subsequence semantics */
  CHECK(fuzzy_match("ig", "Instagram", f).matched);
  CHECK(!fuzzy_match("zq", "Instagram", f).matched);
  CHECK(fuzzy_match("", "anything", f).matched);
  CHECK(fuzzy_match("INSTA", "instagram", f).matched);      /* case-insensitive */
  CHECK(!fuzzy_match("instagramx", "instagram", f).matched); /* longer than haystack */

  /* positions are ascending and point at matching characters */
  const FuzzyMatch m = fuzzy_match("tgm", "Instagram", f);
  CHECK(m.matched);
  CHECK(m.positions.size() == 3);
  CHECK(m.positions[0] < m.positions[1] && m.positions[1] < m.positions[2]);
  CHECK(m.positions[0] == 3 && m.positions[1] == 5 && m.positions[2] == 8);

  /* an exact prefix scores the full bonuses: base + first letter + 3 sequential + substring - unmatched */
  const FuzzyMatch prefix = fuzzy_match("inst", "Instagram", f);
  CHECK(prefix.matched);
  CHECK(prefix.score == f.base_score + f.first_letter_bonus + 3 * f.sequential_bonus + f.exact_substring_bonus
                          + f.unmatched_letter_penalty * 5);

  /* prefix / word-boundary matches score higher than scattered matches */
  CHECK(fuzzy_match("face", "Facebook", f).score > fuzzy_match("face", "Interface book", f).score);
  CHECK(fuzzy_match("li", "LinkedIn", f).score > fuzzy_match("li", "Bluesky link", f).score);

  /* the formal bounds truncate instead of failing */
  const std::string long_pattern(kFuzzyMaxPatternChars + 10, 'a');
  const std::string long_hay(kFuzzyMaxHaystackChars + 10, 'a');
  const FuzzyMatch truncated = fuzzy_match(long_pattern, long_hay, f);
  CHECK(truncated.matched);
  CHECK(truncated.positions.size() == kFuzzyMaxPatternChars);

  /* the real site list */
  CHECK_EQ_STR(top_site(cfg, "insta"), "Instagram");
  CHECK_EQ_STR(top_site(cfg, "ig"), "Instagram");
  CHECK_EQ_STR(top_site(cfg, "x"), "X (Twitter)");
  CHECK_EQ_STR(top_site(cfg, "twitter"), "X (Twitter)");
  CHECK_EQ_STR(top_site(cfg, "twtr"), "X (Twitter)");
  CHECK_EQ_STR(top_site(cfg, "fb"), "Facebook");
  CHECK_EQ_STR(top_site(cfg, "yt"), "YouTube");
  CHECK_EQ_STR(top_site(cfg, "tik"), "TikTok");
  CHECK_EQ_STR(top_site(cfg, "redd"), "Reddit");
  CHECK_EQ_STR(top_site(cfg, "linked"), "LinkedIn");
  CHECK_EQ_STR(top_site(cfg, "bsky"), "Bluesky");
  CHECK_EQ_STR(top_site(cfg, "disc"), "Discord");
  CHECK_EQ_STR(top_site(cfg, "whats"), "WhatsApp");
  CHECK_EQ_STR(top_site(cfg, "tele"), "Telegram");
  CHECK_EQ_STR(top_site(cfg, "pint"), "Pinterest");
  CHECK_EQ_STR(top_site(cfg, "masto"), "Mastodon");
  CHECK_EQ_STR(top_site(cfg, "threads"), "Threads");

  return test_summary("test_fuzzy");
}
