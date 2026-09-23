/**
 * @file net.h
 * @brief Fetches a URL into memory: libcurl where available, WinHTTP on
 * Windows otherwise.
 *
 * Follows redirects, enforces a byte cap and reports the final URL and
 * content type so the caller can sniff the payload.  With vcpkg's Schannel
 * build of curl on Windows no CA bundle is needed.
 */
#pragma once

#include "core/config.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cc {

/** @brief Outcome of fetch_url(). */
struct FetchResult
{
  bool ok = false;
  std::string error;
  std::string final_url;    /**< after redirects */
  std::string content_type; /**< Content-Type header, may be empty */
  long status = 0;          /**< HTTP status, 0 when the request never got that far */
};

/**
 * @brief Progress callback: `progress(received_bytes, total_bytes_or_0)`; return false to cancel.
 *
 * Power of 10 note: a std::function is used because libcurl and WinHTTP are
 * callback-driven C APIs; the callback only ever runs on the calling thread.
 */
using FetchProgress = std::function<bool(uint64_t, uint64_t)>;

/** @brief True when this build can fetch URLs at all. */
[[nodiscard]] bool net_available();

/**
 * @brief Downloads a URL.
 * @param url http:// or https:// URL.
 * @param cfg `network` section (user agent, time-outs, redirects).
 * @param out Receives the body (cleared first).
 * @param max_bytes Abort when the body would exceed this; 0 = no cap.
 * @param progress Optional progress callback.
 */
[[nodiscard]] FetchResult fetch_url(const std::string& url, const NetworkConfig& cfg, std::vector<uint8_t>& out, uint64_t max_bytes,
                                    const FetchProgress& progress = nullptr);

} // namespace cc
