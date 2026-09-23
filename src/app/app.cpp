/**
 * @file app.cpp
 * @brief Loading, comparing, seeking and the per-frame skeleton.
 */
#include "app/app.h"

#include "app/ui_style.h"
#include "core/net.h"
#include "core/process.h"
#include "core/sites.h"
#include "platform/dialogs.h"
#include "util/contract.h"
#include "util/strings.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace cc {

/* ---- small helpers -------------------------------------------------------- */

Settings settings_from_config(const Config& cfg)
{
  Settings s;
  s.video = cfg.limits.video;
  s.max_download_bytes = cfg.limits.max_download_bytes;
  s.max_image_bytes = cfg.limits.max_image_bytes;
  s.ui_scale = cfg.app.ui_scale;
  s.power_saver = cfg.app.power_saver;
  s.show_loupe = cfg.app.show_loupe;
  s.loupe_zoom = cfg.view.loupe.zoom;
  s.show_fps = cfg.app.show_fps;
  s.compare_at_site_resolution = cfg.app.compare_at_site_resolution;
  s.auto_visualize = cfg.app.auto_visualize;
  return s;
}

HeatSettings heat_settings_from_config(const HeatMapConfig& cfg)
{
  HeatSettings h;
  h.metric = cfg.metric;
  h.cmap = cfg.colormap;
  h.after_mode = cfg.after_mode;
  h.overlay_base = cfg.overlay_base;
  h.intensity = cfg.intensity;
  h.proportional = cfg.proportional;
  h.auto_scale = cfg.auto_scale;
  h.manual_max = cfg.manual_max;
  h.gamma = cfg.gamma;
  h.threshold = cfg.threshold;
  h.block_view = cfg.block_view;
  h.log_histogram = cfg.log_histogram;
  return h;
}

Image make_thumbnail(const Image& img, int max_px)
{
  if(!img.valid() || max_px <= 0)
  {
    return Image{};
  }
  const int tw = std::min(img.w, max_px);
  const int th = std::max(1, static_cast<int>(std::lround(static_cast<double>(img.h) * tw / std::max(1, img.w))));
  return resample(img, tw, th);
}

std::string MediaSource::format_line(float custom_table_threshold_pct) const
{
  if(!loaded() && !is_video())
  {
    return "";
  }
  std::string s;
  if(is_video())
  {
    s = video.summary();
  }
  else
  {
    s = std::string(file_kind_name(kind)) + " " + std::to_string(native_w) + "x" + std::to_string(native_h);
    if(kind == FileKind::JPEG && jpeg.ok)
    {
      s += ", " + jpeg.summary(custom_table_threshold_pct);
    }
  }
  if(bytes_size != 0)
  {
    s += ", " + format_bytes(bytes_size);
  }
  return s;
}

std::string Slot::site_label(const std::vector<SiteInfo>& sites) const
{
  if(site_index >= 0 && site_index < static_cast<int>(sites.size()))
  {
    return sites[static_cast<std::size_t>(site_index)].name;
  }
  const std::string q = trim(site_query);
  return q.empty() ? title : q;
}

bool Slot::has_site_name() const { return site_index >= 0 || !trim(site_query).empty(); }

const char* layout_name(Layout l)
{
  switch(l)
  {
    case Layout::Loupe: return "Loupe (single)";
    case Layout::LeftRight: return "Before/After Left/Right";
    case Layout::LeftRightSplit: return "Before/After Left/Right Split";
    case Layout::TopBottom: return "Before/After Top/Bottom";
    case Layout::TopBottomSplit: return "Before/After Top/Bottom Split";
    case Layout::Survey: return "Survey (all sites)";
    case Layout::COUNT: break;
  }
  return "?";
}

namespace {

/** @brief Monotonic seconds. */
double now_seconds()
{
  using namespace std::chrono;
  return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

/** @brief Modulus used to keep temp-file names short. */
constexpr long kTempNameModulus = 1000000;
/** @brief Smallest colour-scale maximum, so an all-zero map never divides by zero. */
constexpr float kMinHeatScale = 1.0e-6f;
/** @brief Progress fractions reported by the jobs. */
constexpr float kProgressDownloadStart = 0.1f;
constexpr float kProgressDownloadShare = 0.5f; /**< the download is half of a URL load */
constexpr float kProgressDecoding = 0.3f;
constexpr float kProgressResampling = 0.1f;
constexpr float kProgressStatistics = 0.3f;
constexpr float kProgressHeatMap = 0.7f;

} // namespace

/* ---- construction --------------------------------------------------------- */

App::App(GLFWwindow* window, const Config& config, float dpi_scale)
    : config_(config), window_(window), dpi_scale_(dpi_scale), settings_(settings_from_config(config)),
      heat_(heat_settings_from_config(config.heatmap)), sample_count_(config.ffmpeg.sample_points)
{
  CC_REQUIRE(window != nullptr, return);
  original_.id = next_slot_id_++;
  original_.is_original = true;
  original_.title = "Original";
  add_site_slot();
  add_site_slot();
}

App::~App()
{
  worker_.cancel_all();
  /* GL objects must go while the context is still current */
  renderer_.release(original_.thumb);
  for(Slot& s : sites_)
  {
    renderer_.release(s.thumb);
  }
  for(std::unique_ptr<Comparison>& c : comps_)
  {
    release_comparison(*c);
  }
  renderer_.shutdown();
  for(const std::string& f : temp_files_)
  {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(f), ec);
  }
}

bool App::init(std::string& err)
{
  renderer_ok_ = renderer_.init(err);
  if(!renderer_ok_)
  {
    renderer_error_ = err;
  }
  else
  {
    renderer_.set_colormap(heat_.cmap);
  }
  ffmpeg_ = locate_ffmpeg(config_.ffmpeg, config_.process, config_.acceleration.video_backend);
  dialogs_ = locate_dialogs(config_.ffmpeg, config_.process);
  last_interaction_ = now_seconds();
  return renderer_ok_;
}

bool App::wants_fast_frames() const
{
  if(!settings_.power_saver)
  {
    return true;
  }
  if(worker_.busy() || pending_visualize_ || view_.dragging_split || view_.panning)
  {
    return true;
  }
  if(now_seconds() - last_interaction_ < config_.app.frame_timing.idle_after_interaction_s)
  {
    return true;
  }
  return ImGui::GetTime() < toast_until_;
}

void App::toast(const std::string& message, double seconds)
{
  toast_ = message;
  toast_until_ = ImGui::GetTime() + seconds;
}

void App::touch() { last_interaction_ = now_seconds(); }

std::string App::temp_path(const std::string& ext)
{
  std::error_code ec;
  const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "compresscompare";
  std::filesystem::create_directories(dir, ec);
  CC_ENSURE(static_cast<int>(temp_files_.size()) < config_.limits.max_temp_files);
  static int counter = 0;
  char name[64];
  const long stamp = static_cast<long>(std::chrono::steady_clock::now().time_since_epoch().count() % kTempNameModulus);
  std::snprintf(name, sizeof name, "cc_%ld_%d.%s", stamp, counter++, ext.empty() ? "bin" : ext.c_str());
  const std::string p = (dir / name).u8string();
  temp_files_.push_back(p);
  return p;
}

/* ---- slot bookkeeping ----------------------------------------------------- */

Slot* App::slot_by_id(int id)
{
  if(original_.id == id)
  {
    return &original_;
  }
  for(Slot& s : sites_)
  {
    if(s.id == id)
    {
      return &s;
    }
  }
  return nullptr;
}

Slot* App::next_empty_slot(const Slot* exclude)
{
  if(!original_.media.loaded() && !original_.loading && &original_ != exclude)
  {
    return &original_;
  }
  for(Slot& s : sites_)
  {
    if(!s.media.loaded() && !s.loading && &s != exclude)
    {
      return &s;
    }
  }
  if(static_cast<int>(sites_.size()) >= config_.limits.max_site_slots)
  {
    return nullptr;
  }
  add_site_slot();
  return &sites_.back();
}

Slot* App::active_site()
{
  if(sites_.empty())
  {
    return nullptr;
  }
  active_site_ = std::clamp(active_site_, 0, static_cast<int>(sites_.size()) - 1);
  return &sites_[static_cast<std::size_t>(active_site_)];
}

Comparison* App::comparison_for(int slot_id)
{
  for(const std::unique_ptr<Comparison>& c : comps_)
  {
    if(c->slot_id == slot_id)
    {
      return c.get();
    }
  }
  return nullptr;
}

Comparison& App::comparison_for_slot(int slot_id)
{
  Comparison* existing = comparison_for(slot_id);
  if(existing != nullptr)
  {
    return *existing;
  }
  comps_.push_back(std::make_unique<Comparison>());
  comps_.back()->slot_id = slot_id;
  return *comps_.back();
}

Comparison* App::active_comparison()
{
  const Slot* s = active_site();
  return s != nullptr ? comparison_for(s->id) : nullptr;
}

void App::add_site_slot()
{
  CC_REQUIRE(static_cast<int>(sites_.size()) < config_.limits.max_site_slots, return);
  Slot s;
  s.id = next_slot_id_++;
  s.title = "Site " + std::to_string(sites_.size() + 1);
  sites_.push_back(std::move(s));
}

void App::release_comparison(Comparison& comp)
{
  renderer_.release(comp.tex_before);
  renderer_.release(comp.tex_after);
  renderer_.release(comp.tex_heat);
}

void App::remove_site_slot(int index)
{
  if(index < 0 || index >= static_cast<int>(sites_.size()))
  {
    return;
  }
  Slot& s = sites_[static_cast<std::size_t>(index)];
  renderer_.release(s.thumb);
  for(auto it = comps_.begin(); it != comps_.end();)
  {
    if((*it)->slot_id == s.id)
    {
      release_comparison(**it);
      it = comps_.erase(it);
    }
    else
    {
      ++it;
    }
  }
  sites_.erase(sites_.begin() + index);
  for(std::size_t i = 0; i < sites_.size(); ++i)
  {
    sites_[i].title = "Site " + std::to_string(i + 1);
  }
  active_site_ = std::clamp(active_site_, 0, std::max(0, static_cast<int>(sites_.size()) - 1));
}

void App::clear_slot(Slot& slot)
{
  renderer_.release(slot.thumb);
  slot.media = MediaSource{};
  slot.status.clear();
  slot.error = false;
  slot.url_input.clear();
  Comparison* c = comparison_for(slot.id);
  if(c != nullptr)
  {
    release_comparison(*c);
    *c = Comparison{};
    c->slot_id = slot.id;
  }
  if(slot.is_original)
  {
    invalidate_comparisons();
  }
}

/* ---- loading -------------------------------------------------------------- */

namespace {

/** @brief Everything a load job needs, copied so the job never touches App state. */
struct LoadRequest
{
  std::vector<uint8_t> bytes; /**< already-downloaded payload (URL case) or empty */
  std::string path;           /**< local file (file case) */
  std::string name;
  std::string url;
  std::string content_type;
  bool from_url = false;
  double frame_time = 0.0;
  Settings settings;
  LimitsConfig limits;
  FfmpegTools ffmpeg;
  std::string temp_video_path; /**< where to store a downloaded video for ffmpeg */
  std::string temp_still_path; /**< where to store data ffmpeg must decode */
};

/**
 * @brief Reads a local file: the header for sniffing, and the whole file for stills.
 * @return false with `error` set.
 */
bool read_local(const LoadRequest& rq, const std::string& ext, MediaSource& m, std::vector<uint8_t>& bytes, std::string& error)
{
  m.path = rq.path;
  uint64_t size = 0;
  if(!file_size(rq.path, size))
  {
    error = "cannot open '" + rq.path + "'";
    return false;
  }
  m.bytes_size = size;
  /* Only the header is needed to sniff; videos are never fully read. */
  std::vector<uint8_t> head;
  std::string err;
  if(!read_file_head(rq.path, rq.limits.sniff_head_bytes, head, err))
  {
    error = err;
    return false;
  }
  m.kind = sniff_bytes(head.data(), head.size(), ext);
  if(m.kind == FileKind::Unknown && !head.empty())
  {
    m.kind = FileKind::Other; /* let the decoders decide (BMP/GIF variants, odd headers) */
  }
  if(m.kind == FileKind::Video)
  {
    return true;
  }
  if(size > rq.settings.max_image_bytes)
  {
    error = "image file is " + format_bytes(size) + ", above the " + format_bytes(rq.settings.max_image_bytes) + " limit";
    return false;
  }
  if(head.size() == size)
  {
    bytes = std::move(head);
    return true;
  }
  if(!read_file(rq.path, bytes, err))
  {
    error = err;
    return false;
  }
  return true;
}

/**
 * @brief Classifies a downloaded payload and stores videos where ffmpeg can read them.
 * @return false with `error` set.
 */
bool store_download(const LoadRequest& rq, const std::string& ext, MediaSource& m, std::vector<uint8_t>& bytes, std::string& error)
{
  m.bytes_size = bytes.size();
  m.kind = sniff_bytes(bytes.data(), bytes.size(), ext);
  if(m.kind == FileKind::Unknown && to_lower(rq.content_type).find("video/") != std::string::npos)
  {
    m.kind = FileKind::Video;
  }
  if(m.kind != FileKind::Video)
  {
    return true;
  }
  std::string err;
  if(!write_file(rq.temp_video_path, bytes.data(), bytes.size(), err))
  {
    error = "cannot write a temporary file for the video: " + err;
    return false;
  }
  m.path = rq.temp_video_path;
  m.temp_file = true;
  bytes.clear();
  return true;
}

/** @brief Probes a video and pulls the requested frame. */
bool load_video(const LoadRequest& rq, MediaSource& m, std::string& error)
{
  m.video = probe_video(rq.ffmpeg, m.path, rq.settings.video);
  if(!m.video.ok)
  {
    error = m.video.error;
    return false;
  }
  m.native_w = m.video.width;
  m.native_h = m.video.height;
  std::string err;
  if(!extract_frame(rq.ffmpeg, m.video, rq.frame_time, m.image, err))
  {
    error = err;
    return false;
  }
  m.frame_time = std::clamp(rq.frame_time, 0.0, m.video.duration_s);
  m.decoder = m.video.backend;
  return true;
}

/** @brief True for kinds the built-in decoders may reject but ffmpeg can read. */
bool ffmpeg_can_try(FileKind kind)
{
  return kind == FileKind::AVIF || kind == FileKind::HEIF || kind == FileKind::TIFF || kind == FileKind::WebP
         || kind == FileKind::Other || kind == FileKind::Unknown;
}

/** @brief Decodes a still with the built-in decoders, then ffmpeg as the universal fallback. */
bool load_still(const LoadRequest& rq, const std::string& ext, const std::vector<uint8_t>& bytes, MediaSource& m, std::string& error)
{
  if(m.kind == FileKind::HTML)
  {
    error = "that link returns a web page, not the image itself. Right-click the picture on the site and use "
            "'Copy image address' / 'Open image in new tab', then paste that URL.";
    return false;
  }
  if(bytes.empty())
  {
    error = "the file is empty";
    return false;
  }
  if(m.kind == FileKind::JPEG)
  {
    m.jpeg = parse_jpeg_info(bytes.data(), bytes.size());
  }
  LoadResult r = decode_image(bytes.data(), bytes.size(), rq.limits.max_image_edge_px, m.image, ext);
  if(!r.ok && rq.ffmpeg.available() && ffmpeg_can_try(m.kind))
  {
    std::string p = m.path;
    std::string err;
    if(p.empty() || rq.from_url)
    {
      p = rq.temp_still_path;
      m.temp_file = write_file(p, bytes.data(), bytes.size(), err);
    }
    if(decode_still_with_ffmpeg(rq.ffmpeg, p, m.image, err))
    {
      r.ok = true;
      r.decoder = "ffmpeg";
    }
    else
    {
      r.error += " (ffmpeg fallback: " + err + ")";
    }
  }
  if(!r.ok)
  {
    error = r.error;
    return false;
  }
  m.decoder = r.decoder;
  if(m.kind == FileKind::Unknown || m.kind == FileKind::Other)
  {
    m.kind = r.kind;
  }
  m.native_w = m.image.w;
  m.native_h = m.image.h;
  return true;
}

/** @brief Loads a file or a downloaded payload into a MediaSource (runs on the worker). */
MediaSource load_media(const LoadRequest& rq, Progress& progress, std::string& error)
{
  MediaSource m;
  m.url = rq.url;
  m.display_name = rq.name;
  m.content_type = rq.content_type;
  std::vector<uint8_t> bytes = rq.bytes;
  const std::string ext = file_extension(rq.from_url ? rq.url : rq.path);
  const bool stored = rq.from_url ? store_download(rq, ext, m, bytes, error) : read_local(rq, ext, m, bytes, error);
  if(!stored)
  {
    return m;
  }
  progress.set(kProgressDecoding, "decoding");
  if(m.kind == FileKind::Video)
  {
    load_video(rq, m, error);
  }
  else
  {
    load_still(rq, ext, bytes, m, error);
  }
  if(error.empty())
  {
    m.thumb = make_thumbnail(m.image, rq.limits.thumbnail_max_px);
  }
  return m;
}

/** @brief Downloads `rq.url` (capped) and loads the payload (runs on the worker). */
MediaSource download_media(LoadRequest rq, const NetworkConfig& net, uint64_t cap, Progress& progress, std::string& error)
{
  std::vector<uint8_t> bytes;
  const FetchResult fr = fetch_url(rq.url, net, bytes, cap, [&](uint64_t got, uint64_t total) {
    const float fraction = total != 0 ? static_cast<float>(got) / static_cast<float>(total) * kProgressDownloadShare : kProgressDownloadStart;
    progress.set(fraction, "downloading " + format_bytes(got));
    return !progress.cancel.load();
  });
  if(!fr.ok)
  {
    error = fr.error;
    return MediaSource{};
  }
  rq.bytes = std::move(bytes);
  rq.content_type = fr.content_type;
  if(!fr.final_url.empty())
  {
    rq.url = fr.final_url;
  }
  MediaSource m = load_media(rq, progress, error);
  m.url = rq.url;
  return m;
}

} // namespace

void App::load_slot_path(Slot& slot, const std::string& path)
{
  const int slot_id = slot.id;
  slot.loading = true;
  slot.error = false;
  slot.status = "loading " + file_name(path) + "...";
  LoadRequest rq;
  rq.path = path;
  rq.name = file_name(path);
  rq.frame_time = video_time_;
  rq.settings = settings_;
  rq.limits = config_.limits;
  rq.ffmpeg = ffmpeg_;
  rq.temp_still_path = temp_path("bin");
  touch();
  worker_.submit("loading " + rq.name, [this, rq, slot_id](Progress& progress) -> std::function<void()> {
    std::string error;
    MediaSource m = load_media(rq, progress, error);
    return [this, slot_id, m = std::move(m), error]() mutable {
      Slot* s = slot_by_id(slot_id);
      if(s != nullptr)
      {
        apply_media(*s, std::move(m), error);
      }
    };
  });
}

void App::detect_site(Slot& slot, const std::string& url)
{
  /* auto-detect the site from the host straight away */
  if(slot.is_original || slot.site_index >= 0)
  {
    return;
  }
  const int detected = detect_site_from_url(config_.sites, url);
  if(detected >= 0)
  {
    slot.site_index = detected;
    slot.site_query = config_.sites[static_cast<std::size_t>(detected)].name;
  }
}

void App::load_slot_url(Slot& slot, const std::string& url_in)
{
  const std::string url = trim(url_in);
  if(!looks_like_url(url) || static_cast<int>(url.size()) > config_.limits.max_url_chars)
  {
    slot.status = "paste a full http(s) link";
    slot.error = true;
    return;
  }
  if(!net_available())
  {
    slot.status = "URL fetching is not available in this build";
    slot.error = true;
    return;
  }
  detect_site(slot, url);
  const int slot_id = slot.id;
  slot.loading = true;
  slot.error = false;
  slot.status = "downloading...";
  LoadRequest rq;
  rq.from_url = true;
  rq.url = url;
  rq.name = file_name(url.substr(0, url.find_first_of("?#")));
  if(rq.name.empty())
  {
    rq.name = url_host(url);
  }
  rq.frame_time = video_time_;
  rq.settings = settings_;
  rq.limits = config_.limits;
  rq.ffmpeg = ffmpeg_;
  rq.temp_video_path = temp_path(file_extension(url).empty() ? "mp4" : file_extension(url));
  rq.temp_still_path = temp_path("bin");
  const uint64_t cap = std::max(settings_.max_download_bytes, settings_.video.max_bytes);
  const NetworkConfig net = config_.network;
  touch();
  worker_.submit("downloading " + rq.name, [this, rq, slot_id, cap, net](Progress& progress) -> std::function<void()> {
    std::string error;
    MediaSource m = download_media(rq, net, cap, progress, error);
    return [this, slot_id, m = std::move(m), error]() mutable {
      Slot* s = slot_by_id(slot_id);
      if(s != nullptr)
      {
        apply_media(*s, std::move(m), error);
      }
    };
  });
}

void App::load_from_clipboard(Slot* preferred)
{
  const char* clip = glfwGetClipboardString(window_);
  if(clip == nullptr || *clip == '\0')
  {
    return;
  }
  const std::string v = trim(clip);
  Slot* dest = preferred != nullptr ? preferred : next_empty_slot(nullptr);
  if(dest == nullptr)
  {
    toast("no free slot (limits.max_site_slots)", config_.app.toast.short_s);
    return;
  }
  if(looks_like_url(v))
  {
    dest->url_input = v;
    load_slot_url(*dest, v);
  }
  else if(file_exists(v))
  {
    dest->url_input = v;
    load_slot_path(*dest, v);
  }
  else
  {
    toast("clipboard does not contain a URL or a file path", config_.app.toast.short_s);
  }
}

void App::apply_media(Slot& slot, MediaSource media, const std::string& error)
{
  slot.loading = false;
  if(!error.empty())
  {
    slot.error = true;
    slot.status = error;
    return;
  }
  renderer_.release(slot.thumb);
  slot.media = std::move(media);
  slot.error = false;
  slot.status = slot.media.format_line(config_.metrics.custom_table_fit_error_pct);
  /* a thumbnail-sized texture for the slot card (keeps VRAM low on the handheld); built off-thread */
  if(!slot.media.thumb.valid())
  {
    slot.media.thumb = make_thumbnail(slot.media.image, config_.limits.thumbnail_max_px);
  }
  renderer_.upload_rgba(slot.thumb, slot.media.thumb);
  slot.media.thumb = Image{};
  if(slot.is_original)
  {
    invalidate_comparisons();
  }
  else
  {
    Comparison* c = comparison_for(slot.id);
    if(c != nullptr)
    {
      c->ok = false;
      c->heat = HeatMap{};
      release_comparison(*c);
    }
  }
  if(settings_.auto_visualize)
  {
    pending_visualize_ = true;
  }
}

void App::on_drop(int count, const char** paths)
{
  CC_REQUIRE(paths != nullptr || count <= 0, return);
  if(count <= 0)
  {
    return;
  }
  touch();
  const int accepted = std::min(count, config_.limits.max_dropped_files);
  Slot* target = hovered_drop_slot_ >= 0 ? slot_by_id(hovered_drop_slot_) : nullptr;
  int next = 0;
  if(target != nullptr)
  {
    load_slot_path(*target, paths[next++]);
  }
  /* remaining files go to empty slots: original first, then sites (creating new ones as needed) */
  for(; next < accepted; ++next)
  {
    Slot* dest = next_empty_slot(target);
    if(dest == nullptr)
    {
      toast("no free slot for the remaining files (limits.max_site_slots)", config_.app.toast.normal_s);
      break;
    }
    load_slot_path(*dest, paths[next]);
  }
  toast(std::to_string(accepted) + (accepted == 1 ? " file dropped" : " files dropped"), config_.app.toast.short_s);
}

/* ---- video seeking ------------------------------------------------------- */

void App::seek_slot(Slot& slot, double t, bool then_visualize)
{
  if(!slot.media.is_video() || !slot.media.video.ok)
  {
    return;
  }
  const int slot_id = slot.id;
  const VideoInfo info = slot.media.video;
  const FfmpegTools tools = ffmpeg_;
  const int thumb_px = config_.limits.thumbnail_max_px;
  slot.loading = true;
  worker_.submit("seeking", [this, slot_id, info, tools, t, then_visualize, thumb_px](Progress&) -> std::function<void()> {
    Image frame;
    Image thumb;
    std::string err;
    const bool ok = extract_frame(tools, info, t, frame, err);
    if(ok)
    {
      thumb = make_thumbnail(frame, thumb_px);
    }
    return [this, slot_id, frame = std::move(frame), thumb = std::move(thumb), err, ok, t, then_visualize]() mutable {
      Slot* s = slot_by_id(slot_id);
      if(s == nullptr)
      {
        return;
      }
      s->loading = false;
      if(!ok)
      {
        s->error = true;
        s->status = err;
        return;
      }
      s->media.image = std::move(frame);
      s->media.frame_time = t;
      s->status = s->media.format_line(config_.metrics.custom_table_fit_error_pct) + "  @ "
                  + std::to_string(t).substr(0, style::kTimeDigits) + " s";
      renderer_.upload_rgba(s->thumb, thumb);
      if(then_visualize)
      {
        pending_visualize_ = true;
      }
    };
  });
}

void App::seek_all(double t)
{
  video_time_ = t;
  seek_slot(original_, t, true);
  for(Slot& s : sites_)
  {
    seek_slot(s, t, true);
  }
}

/* ---- comparisons ---------------------------------------------------------- */

void App::invalidate_comparisons()
{
  for(std::unique_ptr<Comparison>& c : comps_)
  {
    c->ok = false;
    c->heat = HeatMap{};
    release_comparison(*c);
  }
}

void App::visualize_all()
{
  pending_visualize_ = false;
  if(!original_.media.loaded())
  {
    return;
  }
  for(int i = 0; i < static_cast<int>(sites_.size()); ++i)
  {
    if(sites_[static_cast<std::size_t>(i)].media.loaded())
    {
      visualize(i);
    }
  }
}

namespace {

/** @brief Inputs of a comparison job, copied so the job never touches App state. */
struct CompareRequest
{
  std::shared_ptr<const Image> original;
  std::shared_ptr<const Image> site;
  bool at_site_res = true;
  Metric metric = Metric::AbsDiff;
  bool block = false;
  int slot_id = 0;
  int dx = 0;
  int dy = 0;
  double t_before = 0;
  double t_after = 0;
  int max_tex = 0;
  MetricsConfig metrics;
  ThreadingConfig threading;
};

/** @brief Picks the comparison size: the site's or the original's, shrunk to fit the GPU. */
void choose_size(const CompareRequest& rq, int& w, int& h)
{
  w = rq.at_site_res ? rq.site->w : rq.original->w;
  h = rq.at_site_res ? rq.site->h : rq.original->h;
  const int longest = std::max(w, h);
  if(longest > rq.max_tex)
  {
    const double s = static_cast<double>(rq.max_tex) / longest;
    w = std::max(1, static_cast<int>(std::lround(w * s)));
    h = std::max(1, static_cast<int>(std::lround(h * s)));
  }
}

/** @brief Brings both images to the common size; a cropped side gets a matching centre crop. */
void resample_pair(const CompareRequest& rq, int w, int h, Comparison& result)
{
  const Image& original = *rq.original;
  const Image& site = *rq.site;
  const bool same_o = (w == original.w && h == original.h);
  const bool same_s = (w == site.w && h == site.h);
  if(rq.at_site_res)
  {
    /* the original is brought to the site's size */
    result.before = result.aspect_mismatch ? fit_cover(original, w, h, rq.dx, rq.dy) : (same_o ? original : resample(original, w, h));
    result.after = same_s ? site : resample(site, w, h);
  }
  else
  {
    /* the site version is brought back up to the original's size */
    result.before = same_o ? original : resample(original, w, h);
    result.after = result.aspect_mismatch ? fit_cover(site, w, h, rq.dx, rq.dy) : (same_s ? site : resample(site, w, h));
  }
}

/** @brief The comparison job body (runs on the worker). */
std::shared_ptr<Comparison> run_comparison(const CompareRequest& rq, Progress& progress)
{
  std::shared_ptr<Comparison> result = std::make_shared<Comparison>();
  result->slot_id = rq.slot_id;
  result->frame_time_before = rq.t_before;
  result->frame_time_after = rq.t_after;
  int w = 0;
  int h = 0;
  choose_size(rq, w, h);
  const double ar_o = static_cast<double>(rq.original->w) / rq.original->h;
  const double ar_s = static_cast<double>(rq.site->w) / rq.site->h;
  result->aspect_mismatch = std::fabs(ar_o - ar_s) / ar_o > static_cast<double>(rq.metrics.aspect_mismatch_tolerance);
  result->resample_scale = static_cast<float>(w) / static_cast<float>(rq.original->w);
  result->align_dx = rq.dx;
  result->align_dy = rq.dy;
  progress.set(kProgressResampling, "resampling");
  resample_pair(rq, w, h, *result);
  result->w = w;
  result->h = h;
  progress.set(kProgressStatistics, "statistics");
  result->stats = compute_stats(result->before, result->after, rq.metrics, rq.threading, &progress);
  progress.set(kProgressHeatMap, "heat map");
  result->heat = compute_heatmap(result->before, result->after, rq.metric, rq.metrics, rq.threading, &progress);
  if(rq.block)
  {
    block_average(result->heat, rq.metrics.block_size_px, rq.metrics);
  }
  result->heat_metric = rq.metric;
  result->heat_blocked = rq.block;
  result->ok = result->stats.ok && result->heat.valid();
  if(!result->ok)
  {
    result->error = "comparison failed (empty image?)";
  }
  return result;
}

} // namespace

void App::visualize(int site_index)
{
  if(site_index < 0 || site_index >= static_cast<int>(sites_.size()))
  {
    return;
  }
  Slot& site = sites_[static_cast<std::size_t>(site_index)];
  if(!original_.media.loaded() || !site.media.loaded())
  {
    return;
  }
  Comparison& comp = comparison_for_slot(site.id);
  comp.stats_pending = true;
  comp.heat_pending = true;
  comp.error.clear();
  CompareRequest rq;
  /* one shared copy of each input for the worker thread (never touched by the UI afterwards) */
  rq.original = std::make_shared<const Image>(original_.media.image);
  rq.site = std::make_shared<const Image>(site.media.image);
  rq.at_site_res = settings_.compare_at_site_resolution;
  rq.metric = heat_.metric;
  rq.block = heat_.block_view;
  rq.slot_id = site.id;
  rq.dx = comp.align_dx;
  rq.dy = comp.align_dy;
  rq.t_before = original_.media.frame_time;
  rq.t_after = site.media.frame_time;
  rq.max_tex = renderer_.max_texture_size();
  rq.metrics = config_.metrics;
  rq.threading = config_.threading;
  touch();
  worker_.submit("comparing " + site.site_label(config_.sites), [this, rq](Progress& progress) -> std::function<void()> {
    std::shared_ptr<Comparison> result = run_comparison(rq, progress);
    return [this, result]() {
      Comparison* c = comparison_for(result->slot_id);
      if(c == nullptr)
      {
        return;
      }
      release_comparison(*c);
      *c = std::move(*result);
      c->stats_pending = false;
      c->heat_pending = false;
      renderer_.upload_rgba(c->tex_before, c->before);
      renderer_.upload_rgba(c->tex_after, c->after);
      renderer_.upload_heat(c->tex_heat, c->heat);
    };
  });
}

void App::request_heat(Comparison& comp)
{
  if(!comp.ok || comp.heat_pending)
  {
    return;
  }
  if(comp.heat.valid() && comp.heat_metric == heat_.metric && comp.heat_blocked == heat_.block_view)
  {
    return;
  }
  comp.heat_pending = true;
  const std::shared_ptr<const Image> before = std::make_shared<const Image>(comp.before);
  const std::shared_ptr<const Image> after = std::make_shared<const Image>(comp.after);
  const Metric metric = heat_.metric;
  const bool block = heat_.block_view;
  const int slot_id = comp.slot_id;
  const MetricsConfig metrics = config_.metrics;
  const ThreadingConfig threading = config_.threading;
  touch();
  worker_.submit(std::string("computing ") + metric_name(metric),
                 [this, before, after, metric, block, slot_id, metrics, threading](Progress& progress) -> std::function<void()> {
                   std::shared_ptr<HeatMap> hm = std::make_shared<HeatMap>();
                   *hm = compute_heatmap(*before, *after, metric, metrics, threading, &progress);
                   if(block)
                   {
                     block_average(*hm, metrics.block_size_px, metrics);
                   }
                   return [this, hm, slot_id, metric, block]() {
                     Comparison* c = comparison_for(slot_id);
                     if(c == nullptr)
                     {
                       return;
                     }
                     c->heat_pending = false;
                     if(!c->ok || c->before.w != hm->w || c->before.h != hm->h)
                     {
                       return; /* stale */
                     }
                     c->heat = *hm;
                     c->heat_metric = metric;
                     c->heat_blocked = block;
                     renderer_.upload_heat(c->tex_heat, c->heat);
                   };
                 });
}

float App::heat_scale_max(const Comparison* comp) const
{
  if(comp != nullptr && comp->heat.valid() && heat_.auto_scale)
  {
    return std::max(comp->heat.p_auto, kMinHeatScale);
  }
  return heat_.manual_max;
}

float App::clamped_gamma() const { return std::max(config_.heatmap.gamma_floor, heat_.gamma); }

void App::export_heat_png()
{
  Comparison* c = active_comparison();
  if(c == nullptr || !c->heat.valid())
  {
    return;
  }
  const Slot* s = active_site();
  std::string base = s != nullptr ? to_lower(s->site_label(config_.sites)) : std::string("site");
  for(char& ch : base)
  {
    if(std::isalnum(static_cast<unsigned char>(ch)) == 0)
    {
      ch = '_';
    }
  }
  const std::string def = "heatmap_" + base + ".png";
  std::string path = def;
  if(dialogs_.available && !save_file_dialog(dialogs_, path, "Save heat map as PNG", def))
  {
    return;
  }
  /* render the colour-mapped heat map on the CPU */
  const float vmax = heat_scale_max(c);
  const float gamma = clamped_gamma();
  Image out = Image::blank(c->heat.w, c->heat.h, kOpaqueAlpha);
  for(int y = 0; y < c->heat.h; ++y)
  {
    for(int x = 0; x < c->heat.w; ++x)
    {
      const float t = std::pow(std::clamp(c->heat.at(x, y) / vmax, 0.0f, 1.0f), gamma);
      float rgb[kColorChannels] = {0.0f, 0.0f, 0.0f};
      colormap_eval(heat_.cmap, t, rgb);
      uint8_t* p = out.px(x, y);
      for(int k = 0; k < kColorChannels; ++k)
      {
        p[k] = static_cast<uint8_t>(style::level(rgb[k]));
      }
    }
  }
  std::string err;
  toast(save_png(out, path, err) ? "saved " + path : err, config_.app.toast.normal_s);
}

/* ---- view helpers --------------------------------------------------------- */

void App::set_layout(Layout l)
{
  layout_ = l;
  touch();
}

void App::cycle_layout(int dir)
{
  const int n = kLayoutCount;
  int l = ((static_cast<int>(layout_) + dir) % n + n) % n;
  if(static_cast<Layout>(l) == Layout::Survey && sites_.size() < 2)
  {
    l = ((l + dir) % n + n) % n;
  }
  set_layout(static_cast<Layout>(l));
}

void App::zoom_step(int dir, float anchor_x, float anchor_y)
{
  /* Zoom steps follow darktable's develop.c: a small step below 200 %, a big one above, and
   * snapping to "fit", 1:1 and 2:1 when a step crosses them.  `anchor` is the image-normalised
   * point under the cursor, which stays put while zooming. */
  const ZoomConfig& zc = config_.view.zoom;
  const float fit = std::max(fit_scale_, 1e-4f);
  const float z = view_.zoom > 0.0f ? view_.zoom : fit;
  const float step = (dir > 0 ? (z >= 2.0f ? zc.step_above_2x : zc.step_below_2x) : (z > 2.0f ? zc.step_above_2x : zc.step_below_2x));
  float nz = dir > 0 ? z * step : z / step;
  if((nz - fit) * (z - fit) < 0)
  {
    nz = fit;
  }
  else if((nz - 1.0f) * (z - 1.0f) < 0)
  {
    nz = 1.0f;
  }
  else if((nz - 2.0f) * (z - 2.0f) < 0)
  {
    nz = 2.0f;
  }
  nz = std::clamp(nz, std::min(fit * zc.min_fit_fraction, 1.0f), zc.max);
  if(std::isfinite(anchor_x) && std::isfinite(anchor_y))
  {
    /* keep the anchor fixed: centre' = anchor + (centre - anchor) * z/nz */
    view_.center_x = anchor_x + (view_.center_x - anchor_x) * (z / nz);
    view_.center_y = anchor_y + (view_.center_y - anchor_y) * (z / nz);
  }
  view_.zoom = (std::fabs(nz - fit) < zc.snap_epsilon) ? 0.0f : nz;
  if(view_.zoom == 0.0f)
  {
    view_.reset_zoom();
  }
  touch();
}

void App::toggle_zoom_1_1()
{
  /* Lightroom's Z / darktable's middle click: fit <-> 1:1 (<-> 2:1 on the third click). */
  if(view_.zoom == 0.0f)
  {
    view_.zoom = 1.0f;
  }
  else if(std::fabs(view_.zoom - 1.0f) < config_.view.zoom.snap_epsilon)
  {
    view_.zoom = 2.0f;
  }
  else
  {
    view_.reset_zoom();
  }
  touch();
}

/* ---- per-frame ------------------------------------------------------------ */

void App::frame()
{
  worker_.poll();
  if(pending_visualize_ && !worker_.busy())
  {
    visualize_all();
  }
  /* the heat map follows the metric selection lazily */
  for(std::unique_ptr<Comparison>& c : comps_)
  {
    if(c->ok)
    {
      request_heat(*c);
    }
  }
  const ImGuiIO& io = ImGui::GetIO();
  if(io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || io.MouseWheel != 0 || ImGui::IsAnyMouseDown() || io.InputQueueCharacters.Size > 0
     || ImGui::IsKeyPressed(ImGuiKey_Tab))
  {
    touch();
  }
  draws_.items.clear();
  hovered_drop_slot_ = -1;
  ui_menubar();
  handle_shortcuts();
  layout_main_window();
  ui_status_bar();
  if(show_settings_)
  {
    ui_settings_window();
  }
  if(show_about_)
  {
    ui_about_window();
  }
}

void App::layout_main_window()
{
  const PanelConfig& pc = config_.app.panels;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float menu_h = ImGui::GetFrameHeight();
  const float status_h = ImGui::GetFrameHeight() + style::kStatusExtraHeightPx;
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + menu_h));
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - menu_h - status_h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style::kPanelPaddingPx, style::kPanelPaddingPx));
  ImGui::Begin("##main", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
                   | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
                   | ImGuiWindowFlags_NoScrollWithMouse);
  const float avail_x = ImGui::GetContentRegionAvail().x;
  const float left_w = show_left_panel_ ? std::min(pc.left_width_px * ui_scale(), avail_x * pc.left_max_fraction) : 0.0f;
  const float right_w = show_right_panel_ ? std::min(pc.right_width_px * ui_scale(), avail_x * pc.right_max_fraction) : 0.0f;
  if(show_left_panel_)
  {
    ImGui::BeginChild("left", ImVec2(left_w, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ui_slots_panel();
    ImGui::EndChild();
    ImGui::SameLine();
  }
  const float center_w = ImGui::GetContentRegionAvail().x - (show_right_panel_ ? right_w + ImGui::GetStyle().ItemSpacing.x : 0.0f);
  ImGui::BeginChild("center", ImVec2(center_w, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ui_viewer();
  ImGui::EndChild();
  if(show_right_panel_)
  {
    ImGui::SameLine();
    ImGui::BeginChild("right", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ui_right_panel();
    ImGui::EndChild();
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

} // namespace cc
