/**
 * @file sites.cpp
 * @brief Host-suffix and name lookups over the site database.
 */
#include "core/sites.h"

#include "util/strings.h"

namespace cc {

int detect_site_from_url(const std::vector<SiteInfo>& sites, const std::string& url)
{
  const std::string host = url_host(url);
  if(host.empty())
  {
    return kNoSite;
  }
  int best = kNoSite;
  std::size_t best_len = 0;
  for(std::size_t i = 0; i < sites.size(); ++i)
  {
    for(const std::string& suffix : sites[i].hosts)
    {
      const bool hit = !suffix.empty() && (host == suffix || ends_with(host, "." + suffix));
      if(hit && suffix.size() > best_len)
      {
        best = static_cast<int>(i);
        best_len = suffix.size();
      }
    }
  }
  return best;
}

int find_site_by_name(const std::vector<SiteInfo>& sites, const std::string& name)
{
  const std::string q = to_lower(trim(name));
  if(q.empty())
  {
    return kNoSite;
  }
  for(std::size_t i = 0; i < sites.size(); ++i)
  {
    if(to_lower(sites[i].name) == q)
    {
      return static_cast<int>(i);
    }
    for(const std::string& alias : sites[i].aliases)
    {
      if(to_lower(alias) == q)
      {
        return static_cast<int>(i);
      }
    }
  }
  return kNoSite;
}

} // namespace cc
