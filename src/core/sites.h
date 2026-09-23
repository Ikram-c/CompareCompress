/**
 * @file sites.h
 * @brief Lookups over the site database.
 *
 * The database itself (names, search aliases, CDN host suffixes and short
 * "what this site usually does" notes) is the `sites` list of config.yaml;
 * see SiteInfo in config.h.  The notes are approximate and change over time:
 * the whole point of the application is to measure the real thing.
 */
#pragma once

#include "core/config.h"

#include <string>
#include <vector>

namespace cc {

/** @brief Returned by the lookups when nothing matches. */
constexpr int kNoSite = -1;

/**
 * @brief Finds the site that serves a URL, by host suffix.
 *
 * A host matches a suffix `s` when it equals `s` or ends with `.s`; the
 * longest matching suffix wins.
 * @param sites The database.
 * @param url Any URL.
 * @return Index into `sites`, or kNoSite.
 */
[[nodiscard]] int detect_site_from_url(const std::vector<SiteInfo>& sites, const std::string& url);

/**
 * @brief Exact, case-insensitive match on a display name or alias.
 * @param sites The database.
 * @param name Name or alias.
 * @return Index into `sites`, or kNoSite.
 */
[[nodiscard]] int find_site_by_name(const std::vector<SiteInfo>& sites, const std::string& name);

} // namespace cc
