/**
 * @file test_config.cpp
 * @brief The built-in config.yaml parses, override files merge, and every
 * kind of mistake is reported with the key path.
 */
#include "core/config.h"
#include "test_util.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace cc;

namespace {

/** @brief Writes an override file into the temp directory and returns its path. */
std::string write_override(const char* name, const std::string& text)
{
  const std::string path = (std::filesystem::temp_directory_path() / name).string();
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
  return path;
}

/** @brief Loads an override and returns whether it succeeded, printing the message on failure. */
bool load_override(const std::string& text, Config& cfg, std::string& err)
{
  const std::string path = write_override("cc_test_override.yaml", text);
  const bool ok = load_config(path, cfg, err);
  std::remove(path.c_str());
  return ok;
}

/** @brief The compiled-in defaults. */
void check_builtin()
{
  Config cfg;
  std::string err;
  CHECK(load_config("", cfg, err));
  CHECK_EQ_STR(err, "");
  CHECK(cfg.schema_version == kConfigSchemaVersion);
  CHECK_EQ_STR(cfg.app.name, "CompressCompare");
  CHECK(cfg.app.window.width_px > 0 && cfg.app.window.height_px > 0);
  CHECK(cfg.metrics.ssim.window % 2 == 1);
  CHECK(cfg.heatmap.metric == Metric::DeltaE2000);
  CHECK(cfg.heatmap.colormap == ColorMap::Inferno);
  CHECK(cfg.heatmap.after_mode == ViewMode::Overlay);
  CHECK(cfg.heatmap.overlay_base == 1);
  CHECK(cfg.sites.size() > 10);
  CHECK_EQ_STR(cfg.sites.front().name, "Instagram");
  CHECK(!cfg.sites.front().aliases.empty());
  CHECK(!cfg.sites.front().hosts.empty());
  CHECK(cfg.ffmpeg.search_subdirs.size() >= 1);
  CHECK(cfg.metrics.default_scale[static_cast<int>(Metric::SSIM)] > 0.0f);
  CHECK(default_config().schema_version == kConfigSchemaVersion);
  CHECK(builtin_config_yaml() != nullptr && builtin_config_yaml()[0] != '\0');
  CHECK(parse_config(builtin_config_yaml(), "test", cfg, err));
}

/** @brief Partial override files merge into the defaults. */
void check_merge()
{
  Config cfg;
  std::string err;
  const bool ok = load_override("app:\n  window:\n    width_px: 1000\nheatmap:\n  metric: ssim\n"
                                "sites:\n  - name: Instagram\n    notes: changed\n  - name: MySite\n    hosts: [mysite.example]\n",
                                cfg, err);
  CHECK(ok);
  CHECK_EQ_STR(err, "");
  CHECK(cfg.app.window.width_px == 1000);
  CHECK(cfg.app.window.height_px == default_config().app.window.height_px); /* untouched sibling keeps its default */
  CHECK(cfg.heatmap.metric == Metric::SSIM);
  CHECK(cfg.sites.size() == default_config().sites.size() + 1);
  CHECK_EQ_STR(cfg.sites.front().name, "Instagram");
  CHECK_EQ_STR(cfg.sites.front().notes, "changed");
  CHECK(cfg.sites.front().aliases == default_config().sites.front().aliases); /* fields not listed are kept */
  CHECK_EQ_STR(cfg.sites.back().name, "MySite");
  CHECK(cfg.sites.back().hosts.size() == 1);
  CHECK(cfg.sites.back().aliases.empty());
  CHECK_EQ_STR(cfg.sites.back().notes, "");

  /* an empty override changes nothing */
  CHECK(load_override("{}\n", cfg, err));
  CHECK(cfg.app.window.width_px == default_config().app.window.width_px);

  /* a missing override file is an error, not a silent default */
  CHECK(!load_config("/no/such/dir/config.yaml", cfg, err));
  CHECK(!err.empty());
}

/** @brief Every mistake names the file and the key. */
void check_errors()
{
  Config cfg;
  std::string err;
  CHECK(!load_override("app:\n  window:\n    width_px: wide\n", cfg, err));
  CHECK(err.find("app.window.width_px") != std::string::npos);

  CHECK(!load_override("app:\n  window:\n    width_px: 10\n", cfg, err)); /* below the minimum */
  CHECK(err.find("app.window.width_px") != std::string::npos);

  CHECK(!load_override("app:\n  window:\n    widht_px: 100\n", cfg, err)); /* typo */
  CHECK(err.find("app.window.widht_px") != std::string::npos);
  CHECK(err.find("unknown key") != std::string::npos);

  CHECK(!load_override("heatmap:\n  metric: nope\n", cfg, err));
  CHECK(err.find("heatmap.metric") != std::string::npos);

  CHECK(!load_override("metrics:\n  ssim:\n    window: 10\n", cfg, err)); /* must be odd */
  CHECK(err.find("metrics.ssim.window") != std::string::npos);

  CHECK(!load_override("schema_version: 99\n", cfg, err));
  CHECK(err.find("schema_version") != std::string::npos);

  CHECK(!load_override("sites:\n  - aliases: [x]\n", cfg, err)); /* a site needs a name */
  CHECK(err.find("sites[0].name") != std::string::npos);

  CHECK(!load_override("sites:\n  - name: Instagram\n    colour: red\n", cfg, err));
  CHECK(err.find("sites[0].colour") != std::string::npos);

  CHECK(!load_override("app: [1, 2]\n", cfg, err)); /* wrong shape */
  CHECK(!err.empty());

  CHECK(!load_override("app:\n  window: {width_px: 1000\n", cfg, err)); /* syntax error */
  CHECK(!err.empty());

  /* parse_config on a document that lacks a section */
  CHECK(!parse_config("schema_version: 1\n", "inline", cfg, err));
  CHECK(err.find("inline") != std::string::npos);
  CHECK(err.find("app") != std::string::npos);
}

/** @brief Whether the working directory happens to contain a config.yaml. */
bool file_exists_in_cwd()
{
  std::error_code ec;
  return std::filesystem::is_regular_file("config.yaml", ec);
}

/** @brief find_config_override() prefers the explicit path, then the exe dir, then the cwd. */
void check_lookup()
{
  CHECK_EQ_STR(find_config_override("/explicit/config.yaml", "/somewhere"), "/explicit/config.yaml");
  const std::string dir = std::filesystem::temp_directory_path().string();
  const std::string beside = write_override("config.yaml", "{}\n");
  const std::string found = find_config_override("", dir);
  CHECK(!found.empty());
  std::error_code ec;
  CHECK(std::filesystem::equivalent(found, beside, ec));
  std::remove(beside.c_str());
  CHECK_EQ_STR(find_config_override("", "/no/such/dir"), file_exists_in_cwd() ? "config.yaml" : "");
}

} // namespace

int main()
{
  check_builtin();
  check_merge();
  check_errors();
  check_lookup();
  return test_summary("test_config");
}
