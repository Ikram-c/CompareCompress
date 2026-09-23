/**
 * @file test_sites.cpp
 * @brief URL helpers and the site database that config.yaml ships.
 */
#include "core/config.h"
#include "core/sites.h"
#include "test_util.h"
#include "util/strings.h"

using namespace cc;

namespace {

/** @brief Display name of the site detected for a URL, or "". */
std::string site_for(const std::vector<SiteInfo>& sites, const std::string& url)
{
  const int i = detect_site_from_url(sites, url);
  return i < 0 ? "" : sites[static_cast<std::size_t>(i)].name;
}

} // namespace

int main()
{
  CHECK_EQ_STR(url_host("https://pbs.twimg.com/media/abc.jpg?name=large"), "pbs.twimg.com");
  CHECK_EQ_STR(url_host("HTTP://User:pw@Example.COM:8080/x"), "example.com");
  CHECK_EQ_STR(url_host("not a url"), "");
  CHECK(looks_like_url("https://x.com/a"));
  CHECK(!looks_like_url("C:\\photos\\a.jpg"));
  CHECK_EQ_STR(file_extension("https://i.redd.it/abc.PNG?x=1"), "png");
  CHECK_EQ_STR(file_extension("C:\\photos\\holiday.JPG"), "jpg");
  CHECK_EQ_STR(file_extension("/tmp/noext"), "");
  CHECK_EQ_STR(format_bytes(1536), "1.5 KB");

  const std::vector<SiteInfo>& sites = default_config().sites;
  CHECK(!sites.empty());
  CHECK_EQ_STR(site_for(sites, "https://pbs.twimg.com/media/abc.jpg?name=large"), "X (Twitter)");
  CHECK_EQ_STR(site_for(sites, "https://x.com/user/status/1"), "X (Twitter)");
  CHECK_EQ_STR(site_for(sites, "https://scontent-lhr8-1.cdninstagram.com/v/t51.jpg"), "Instagram");
  CHECK_EQ_STR(site_for(sites, "https://scontent.xx.fbcdn.net/v/t39/photo.jpg"), "Facebook");
  CHECK_EQ_STR(site_for(sites, "https://i.redd.it/abc.png"), "Reddit");
  CHECK_EQ_STR(site_for(sites, "https://preview.redd.it/abc.png?width=640"), "Reddit");
  CHECK_EQ_STR(site_for(sites, "https://cdn.discordapp.com/attachments/1/2/a.png"), "Discord");
  CHECK_EQ_STR(site_for(sites, "https://media.discordapp.net/attachments/1/2/a.png?format=webp"), "Discord");
  CHECK_EQ_STR(site_for(sites, "https://media.licdn.com/dms/image/x.jpg"), "LinkedIn");
  CHECK_EQ_STR(site_for(sites, "https://i.pinimg.com/736x/ab/cd.jpg"), "Pinterest");
  CHECK_EQ_STR(site_for(sites, "https://cdn.bsky.app/img/feed_thumbnail/plain/did/x@jpeg"), "Bluesky");
  CHECK_EQ_STR(site_for(sites, "https://p16-sign-va.tiktokcdn.com/obj/x.jpeg"), "TikTok");
  CHECK_EQ_STR(site_for(sites, "https://i.ytimg.com/vi/abc/maxresdefault.jpg"), "YouTube");
  CHECK_EQ_STR(site_for(sites, "https://64.media.tumblr.com/abc/s2048x3072/x.jpg"), "Tumblr");
  CHECK_EQ_STR(site_for(sites, "https://live.staticflickr.com/1/2_o.jpg"), "Flickr");
  CHECK_EQ_STR(site_for(sites, "https://files.mastodon.social/media_attachments/x.png"), "Mastodon");
  CHECK_EQ_STR(site_for(sites, "https://example.org/photo.jpg"), "");
  CHECK_EQ_STR(site_for(sites, "C:\\photos\\a.jpg"), "");

  CHECK(find_site_by_name(sites, "instagram") >= 0);
  CHECK(find_site_by_name(sites, "IG") == find_site_by_name(sites, "Instagram"));
  CHECK(find_site_by_name(sites, "x") == find_site_by_name(sites, "X (Twitter)"));
  CHECK(find_site_by_name(sites, "nope-not-a-site") == kNoSite);

  /* every site has a name, aliases and a note; the fallback entry is last */
  for(const SiteInfo& s : sites)
  {
    CHECK(!s.name.empty());
    CHECK(!s.aliases.empty());
    CHECK(!s.notes.empty());
  }
  CHECK_EQ_STR(sites.back().name, "Other / custom");

  return test_summary("test_sites");
}
