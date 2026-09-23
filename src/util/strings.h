/**
 * @file strings.h
 * @brief Small string helpers shared by the core and the UI.
 *
 * All functions are pure and allocate only their result; none of them throws.
 */
#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace cc {

/** @brief Bytes in one kibibyte. */
constexpr uint64_t kKiB = 1024;
/** @brief Bytes in one mebibyte. */
constexpr uint64_t kMiB = kKiB * 1024;
/** @brief Bytes in one gibibyte. */
constexpr uint64_t kGiB = kMiB * 1024;

/**
 * @brief Lower-cases a string in the C locale.
 * @param s Input (copied).
 * @return The lower-cased copy.
 */
[[nodiscard]] inline std::string to_lower(std::string s)
{
  for(char& c : s)
  {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

/**
 * @brief Removes leading and trailing white space.
 * @param s Input.
 * @return The trimmed copy.
 */
[[nodiscard]] inline std::string trim(const std::string& s)
{
  std::size_t a = 0;
  std::size_t b = s.size();
  while(a < b && std::isspace(static_cast<unsigned char>(s[a])) != 0)
  {
    ++a;
  }
  while(b > a && std::isspace(static_cast<unsigned char>(s[b - 1])) != 0)
  {
    --b;
  }
  return s.substr(a, b - a);
}

/** @brief True when `s` begins with `p`. */
[[nodiscard]] inline bool starts_with(const std::string& s, const std::string& p)
{
  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

/** @brief True when `s` ends with `p`. */
[[nodiscard]] inline bool ends_with(const std::string& s, const std::string& p)
{
  return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

/**
 * @brief Splits on a separator character; empty fields are kept.
 * @param s Input.
 * @param sep Separator.
 * @return At least one element.
 */
[[nodiscard]] inline std::vector<std::string> split(const std::string& s, char sep)
{
  std::vector<std::string> out;
  std::string current;
  for(char c : s)
  {
    if(c == sep)
    {
      out.push_back(current);
      current.clear();
    }
    else
    {
      current.push_back(c);
    }
  }
  out.push_back(current);
  return out;
}

/**
 * @brief Formats a byte count for people: "1.2 MB", "834 KB", "12 B".
 * @param n Byte count.
 * @return The formatted text.
 */
[[nodiscard]] inline std::string format_bytes(uint64_t n)
{
  char buf[64];
  if(n >= kGiB)
  {
    std::snprintf(buf, sizeof buf, "%.2f GB", static_cast<double>(n) / static_cast<double>(kGiB));
  }
  else if(n >= kMiB)
  {
    std::snprintf(buf, sizeof buf, "%.2f MB", static_cast<double>(n) / static_cast<double>(kMiB));
  }
  else if(n >= kKiB)
  {
    std::snprintf(buf, sizeof buf, "%.1f KB", static_cast<double>(n) / static_cast<double>(kKiB));
  }
  else
  {
    std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(n));
  }
  return buf;
}

/**
 * @brief Extension of a path or URL, lower-case and without the dot.
 * @param path A file path or URL (query and fragment are ignored).
 * @return The extension, or "" when there is none.
 */
[[nodiscard]] inline std::string file_extension(const std::string& path)
{
  std::string p = path;
  const std::size_t query = p.find_first_of("?#");
  if(query != std::string::npos)
  {
    p.resize(query);
  }
  const std::size_t slash = p.find_last_of("/\\");
  const std::size_t dot = p.find_last_of('.');
  if(dot == std::string::npos || (slash != std::string::npos && dot < slash))
  {
    return "";
  }
  return to_lower(p.substr(dot + 1));
}

/** @brief The last path component. */
[[nodiscard]] inline std::string file_name(const std::string& path)
{
  const std::size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

/** @brief True when the trimmed text starts with http:// or https://. */
[[nodiscard]] inline bool looks_like_url(const std::string& s)
{
  const std::string t = to_lower(trim(s));
  return starts_with(t, "http://") || starts_with(t, "https://");
}

/**
 * @brief Host part of a URL, lower-case, without port or credentials.
 * @param url The URL.
 * @return The host, or "" when `url` has no scheme.
 */
[[nodiscard]] inline std::string url_host(const std::string& url)
{
  const std::string u = trim(url);
  const std::size_t scheme = u.find("://");
  if(scheme == std::string::npos)
  {
    return "";
  }
  const std::size_t start = scheme + 3;
  const std::size_t end = u.find_first_of("/?#", start);
  std::string authority = u.substr(start, end == std::string::npos ? std::string::npos : end - start);
  const std::size_t at = authority.find('@');
  if(at != std::string::npos)
  {
    authority = authority.substr(at + 1);
  }
  const std::size_t colon = authority.find(':');
  if(colon != std::string::npos)
  {
    authority.resize(colon);
  }
  return to_lower(authority);
}

} // namespace cc
