/**
 * @file net.cpp
 * @brief libcurl and WinHTTP back ends of fetch_url().
 */
#include "core/net.h"

#include "util/contract.h"
#include "util/strings.h"

#include <cstdio>
#include <cstring>

#if CC_HAVE_CURL
#include <curl/curl.h>
#elif CC_HAVE_WINHTTP
#include <windows.h>
#include <winhttp.h>
#endif

namespace cc {

#if CC_HAVE_CURL

namespace {

/** @brief State shared with the libcurl callbacks. */
struct CurlCtx
{
  std::vector<uint8_t>* out = nullptr;
  uint64_t max_bytes = 0;
  const FetchProgress* progress = nullptr;
  bool too_big = false;
  bool cancelled = false;
  uint64_t total = 0;
};

/** @brief libcurl write callback: appends to the buffer, enforcing the cap. */
size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  CurlCtx* c = static_cast<CurlCtx*>(userdata);
  CC_REQUIRE(c != nullptr && c->out != nullptr && ptr != nullptr, return 0);
  const size_t n = size * nmemb;
  if(c->max_bytes != 0 && c->out->size() + n > c->max_bytes)
  {
    c->too_big = true;
    return 0; /* abort transfer */
  }
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(ptr);
  c->out->insert(c->out->end(), bytes, bytes + n);
  if(c->progress != nullptr && *c->progress && !(*c->progress)(static_cast<uint64_t>(c->out->size()), c->total))
  {
    c->cancelled = true;
    return 0;
  }
  return n;
}

/** @brief libcurl progress callback: records the total and aborts oversized transfers early. */
int xferinfo_cb(void* userdata, curl_off_t dltotal, curl_off_t /*dlnow*/, curl_off_t /*ultotal*/, curl_off_t /*ulnow*/)
{
  CurlCtx* c = static_cast<CurlCtx*>(userdata);
  CC_REQUIRE(c != nullptr, return 1);
  if(dltotal > 0)
  {
    c->total = static_cast<uint64_t>(dltotal);
  }
  if(c->max_bytes != 0 && dltotal > 0 && static_cast<uint64_t>(dltotal) > c->max_bytes)
  {
    c->too_big = true;
    return 1;
  }
  return 0;
}

/** @brief Process-wide libcurl initialisation, torn down at exit. */
struct CurlGlobal
{
  CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobal() { curl_global_cleanup(); }
  CurlGlobal(const CurlGlobal&) = delete;
  CurlGlobal& operator=(const CurlGlobal&) = delete;
};

/** @brief Applies every option of the request; returns false when one is refused. */
bool configure_curl(CURL* curl, const std::string& url, const NetworkConfig& cfg, curl_slist* headers, CurlCtx& ctx, char* errbuf)
{
  bool ok = true;
  ok = ok && curl_easy_setopt(curl, CURLOPT_URL, url.c_str()) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_USERAGENT, cfg.user_agent.c_str()) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_MAXREDIRS, cfg.max_redirects) == CURLE_OK;
#if CURL_AT_LEAST_VERSION(7, 85, 0)
  ok = ok && curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https") == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https") == CURLE_OK;
#else
  ok = ok && curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS) == CURLE_OK;
#endif
  ok = ok && curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "") == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, cfg.connect_timeout_s) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, cfg.low_speed_limit_bytes_per_s) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, cfg.low_speed_time_s) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_cb) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf) == CURLE_OK;
  ok = ok && curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK;
  return ok;
}

/** @brief Reads status, content type and effective URL after the transfer. */
void collect_curl_info(CURL* curl, FetchResult& r)
{
  const CURLcode status_rc = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
  CC_ENSURE(status_rc == CURLE_OK);
  char* ct = nullptr;
  if(curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct) == CURLE_OK && ct != nullptr)
  {
    r.content_type = ct;
  }
  char* eff = nullptr;
  if(curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff) == CURLE_OK && eff != nullptr)
  {
    r.final_url = eff;
  }
}

} // namespace

bool net_available() { return true; }

FetchResult fetch_url(const std::string& url, const NetworkConfig& cfg, std::vector<uint8_t>& out, uint64_t max_bytes,
                      const FetchProgress& progress)
{
  static const CurlGlobal global;
  FetchResult r;
  out.clear();
  CC_REQUIRE(!url.empty(), r.error = "empty URL"; return r);
  CURL* curl = curl_easy_init();
  if(curl == nullptr)
  {
    r.error = "curl init failed";
    return r;
  }
  CurlCtx ctx;
  ctx.out = &out;
  ctx.max_bytes = max_bytes;
  ctx.progress = &progress;
  char errbuf[CURL_ERROR_SIZE] = {0};
  curl_slist* headers = curl_slist_append(nullptr, ("Accept: " + cfg.accept).c_str());
  if(headers == nullptr || !configure_curl(curl, url, cfg, headers, ctx, errbuf))
  {
    r.error = "curl set-up failed";
  }
  else
  {
    const CURLcode rc = curl_easy_perform(curl);
    collect_curl_info(curl, r);
    if(ctx.too_big)
    {
      r.error = "download exceeds the size limit of " + format_bytes(max_bytes);
    }
    else if(ctx.cancelled)
    {
      r.error = "cancelled";
    }
    else if(rc != CURLE_OK)
    {
      r.error = std::string(errbuf[0] != '\0' ? errbuf : curl_easy_strerror(rc))
                + (r.status != 0 ? " (HTTP " + std::to_string(r.status) + ")" : "");
    }
    else
    {
      r.ok = true;
    }
  }
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return r;
}

#elif CC_HAVE_WINHTTP

namespace {

/** @brief Longest host name accepted by the URL parser. */
constexpr DWORD kMaxHostChars = 256;
/** @brief Longest path accepted by the URL parser. */
constexpr DWORD kMaxPathChars = 4096;
/** @brief Longest Content-Type header read back. */
constexpr DWORD kMaxContentTypeChars = 256;
/** @brief First HTTP status that counts as an error. */
constexpr DWORD kFirstErrorStatus = 400;

/** @brief RAII wrapper closing a WinHTTP handle. */
class Handle
{
public:
  explicit Handle(HINTERNET h) : h_(h) {}
  ~Handle()
  {
    if(h_ != nullptr)
    {
      WinHttpCloseHandle(h_);
    }
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  [[nodiscard]] HINTERNET get() const { return h_; }
  [[nodiscard]] bool ok() const { return h_ != nullptr; }

private:
  HINTERNET h_;
};

std::wstring widen(const std::string& s)
{
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n > 0 ? n - 1 : 0), L'\0');
  if(n > 1)
  {
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
  }
  return w;
}

std::string narrow(const std::wstring& w)
{
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
  if(n > 1)
  {
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
  }
  return s;
}

std::string win_error(DWORD e)
{
  char buf[64];
  std::snprintf(buf, sizeof buf, "WinHTTP error %lu", static_cast<unsigned long>(e));
  return buf;
}

/** @brief Pieces of a cracked URL. */
struct UrlParts
{
  std::wstring host;
  std::wstring path; /**< path plus query */
  INTERNET_PORT port = 0;
  bool https = false;
};

/** @brief Splits a URL with WinHttpCrackUrl. */
bool crack_url(const std::string& url, UrlParts& parts)
{
  const std::wstring wurl = widen(url);
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof uc;
  wchar_t host[kMaxHostChars] = {0};
  wchar_t path[kMaxPathChars] = {0};
  uc.lpszHostName = host;
  uc.dwHostNameLength = kMaxHostChars;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = kMaxPathChars;
  uc.dwSchemeLength = static_cast<DWORD>(-1);
  uc.dwExtraInfoLength = static_cast<DWORD>(-1);
  if(!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
  {
    return false;
  }
  parts.host = host;
  parts.path = path;
  if(uc.lpszExtraInfo != nullptr && uc.dwExtraInfoLength != 0)
  {
    parts.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
  }
  parts.port = uc.nPort;
  parts.https = uc.nScheme == INTERNET_SCHEME_HTTPS;
  return true;
}

/** @brief Opens the session with time-outs, TLS 1.2+ and automatic decompression. */
HINTERNET open_session(const NetworkConfig& cfg)
{
  HINTERNET session = WinHttpOpen(widen(cfg.user_agent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if(session == nullptr)
  {
    return nullptr;
  }
  const BOOL timeouts = WinHttpSetTimeouts(session, cfg.winhttp.resolve_timeout_ms, cfg.winhttp.connect_timeout_ms,
                                           cfg.winhttp.send_timeout_ms, cfg.winhttp.receive_timeout_ms);
  CC_ENSURE(timeouts != FALSE);
  DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
  protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
  WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
  DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
  WinHttpSetOption(session, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof decompression);
  return session;
}

/** @brief Reads status, content type, final URL and content length. */
void read_headers(HINTERNET request, FetchResult& r, uint64_t& total)
{
  DWORD status = 0;
  DWORD size = sizeof status;
  WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                      WINHTTP_NO_HEADER_INDEX);
  r.status = static_cast<long>(status);
  wchar_t ctbuf[kMaxContentTypeChars] = {0};
  size = sizeof ctbuf;
  if(WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, ctbuf, &size, WINHTTP_NO_HEADER_INDEX))
  {
    r.content_type = narrow(ctbuf);
  }
  wchar_t urlbuf[kMaxPathChars] = {0};
  size = sizeof urlbuf;
  if(WinHttpQueryOption(request, WINHTTP_OPTION_URL, urlbuf, &size))
  {
    r.final_url = narrow(urlbuf);
  }
  DWORD clen = 0;
  size = sizeof clen;
  total = 0;
  if(WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &clen, &size,
                         WINHTTP_NO_HEADER_INDEX))
  {
    total = clen;
  }
}

/** @brief Reads the body in chunks until the server is done, the cap is hit or the caller cancels. */
void read_body(HINTERNET request, const NetworkConfig& cfg, std::vector<uint8_t>& out, uint64_t max_bytes, uint64_t total,
               const FetchProgress& progress, FetchResult& r)
{
  std::vector<uint8_t> chunk(cfg.read_chunk_bytes);
  const uint64_t max_reads = max_bytes != 0 ? max_bytes / cfg.read_chunk_bytes + 2 : static_cast<uint64_t>(1) << 32;
  for(uint64_t reads = 0; reads <= max_reads; ++reads)
  {
    DWORD avail = 0;
    if(!WinHttpQueryDataAvailable(request, &avail))
    {
      r.error = win_error(GetLastError());
      return;
    }
    if(avail == 0)
    {
      r.ok = true;
      return;
    }
    const DWORD want = avail > chunk.size() ? static_cast<DWORD>(chunk.size()) : avail;
    DWORD got = 0;
    if(!WinHttpReadData(request, chunk.data(), want, &got))
    {
      r.error = win_error(GetLastError());
      return;
    }
    if(max_bytes != 0 && out.size() + got > max_bytes)
    {
      r.error = "download exceeds the size limit of " + format_bytes(max_bytes);
      return;
    }
    out.insert(out.end(), chunk.begin(), chunk.begin() + got);
    if(progress && !progress(static_cast<uint64_t>(out.size()), total))
    {
      r.error = "cancelled";
      return;
    }
  }
  r.error = "download exceeds the size limit of " + format_bytes(max_bytes);
}

} // namespace

bool net_available() { return true; }

FetchResult fetch_url(const std::string& url, const NetworkConfig& cfg, std::vector<uint8_t>& out, uint64_t max_bytes,
                      const FetchProgress& progress)
{
  FetchResult r;
  out.clear();
  CC_REQUIRE(!url.empty(), r.error = "empty URL"; return r);
  CC_REQUIRE(cfg.read_chunk_bytes > 0, r.error = "internal: bad chunk size"; return r);
  UrlParts parts;
  if(!crack_url(url, parts))
  {
    r.error = "invalid URL";
    return r;
  }
  const Handle session(open_session(cfg));
  if(!session.ok())
  {
    r.error = win_error(GetLastError());
    return r;
  }
  const Handle connect(WinHttpConnect(session.get(), parts.host.c_str(), parts.port, 0));
  if(!connect.ok())
  {
    r.error = win_error(GetLastError());
    return r;
  }
  const Handle request(WinHttpOpenRequest(connect.get(), L"GET", parts.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, parts.https ? WINHTTP_FLAG_SECURE : 0));
  if(!request.ok())
  {
    r.error = win_error(GetLastError());
    return r;
  }
  const std::wstring accept = L"Accept: " + widen(cfg.accept);
  WinHttpAddRequestHeaders(request.get(), accept.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
  const bool sent = WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                    && WinHttpReceiveResponse(request.get(), nullptr);
  if(!sent)
  {
    r.error = win_error(GetLastError());
    return r;
  }
  uint64_t total = 0;
  read_headers(request.get(), r, total);
  if(r.status >= static_cast<long>(kFirstErrorStatus))
  {
    r.error = "HTTP " + std::to_string(r.status);
  }
  else if(max_bytes != 0 && total > max_bytes)
  {
    r.error = "download exceeds the size limit of " + format_bytes(max_bytes);
  }
  else
  {
    read_body(request.get(), cfg, out, max_bytes, total, progress, r);
  }
  if(r.final_url.empty())
  {
    r.final_url = url;
  }
  return r;
}

#else

bool net_available() { return false; }

FetchResult fetch_url(const std::string& /*url*/, const NetworkConfig& /*cfg*/, std::vector<uint8_t>& out, uint64_t /*max_bytes*/,
                      const FetchProgress& /*progress*/)
{
  out.clear();
  FetchResult r;
  r.error = "URL fetching is not available in this build (libcurl was not found at configure time)";
  return r;
}

#endif

} // namespace cc
