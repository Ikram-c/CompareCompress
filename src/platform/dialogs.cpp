/**
 * @file dialogs.cpp
 * @brief Windows, macOS and Linux back ends of the file dialogs.
 */
#include "platform/dialogs.h"

#include "core/image.h"
#include "core/process.h"
#include "util/contract.h"
#include "util/strings.h"

#include <cstring>
#include <cwchar>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace cc {

#ifdef _WIN32

namespace {

/** @brief Characters of the path buffer handed to comdlg32. */
constexpr std::size_t kPathChars = 4096;

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

const wchar_t* const kFilterImages = L"Images\0*.jpg;*.jpeg;*.png;*.webp;*.gif;*.bmp;*.avif;*.heic;*.heif;*.tif;*.tiff\0All files\0*.*\0";
const wchar_t* const kFilterAll = L"Images and videos\0*.jpg;*.jpeg;*.png;*.webp;*.gif;*.bmp;*.avif;*.heic;*.heif;*.tif;*.tiff;"
                                  L"*.mp4;*.mov;*.m4v;*.webm;*.mkv;*.avi;*.ts;*.flv;*.wmv;*.3gp;*.mpg;*.mpeg\0"
                                  L"Images\0*.jpg;*.jpeg;*.png;*.webp;*.gif;*.bmp;*.avif;*.heic;*.heif;*.tif;*.tiff\0"
                                  L"Videos\0*.mp4;*.mov;*.m4v;*.webm;*.mkv;*.avi;*.ts;*.flv;*.wmv;*.3gp;*.mpg;*.mpeg\0"
                                  L"All files\0*.*\0";

} // namespace

DialogTools locate_dialogs(const FfmpegConfig& /*search*/, const ProcessConfig& process)
{
  DialogTools t;
  t.available = true;
  t.process = process;
  return t;
}

bool open_file_dialog(const DialogTools& tools, std::string& path, const std::string& title, bool video_too)
{
  CC_REQUIRE(tools.available, return false);
  wchar_t buf[kPathChars] = {0};
  const std::wstring wtitle = widen(title);
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof ofn;
  ofn.hwndOwner = GetActiveWindow();
  ofn.lpstrFilter = video_too ? kFilterAll : kFilterImages;
  ofn.lpstrFile = buf;
  ofn.nMaxFile = static_cast<DWORD>(kPathChars);
  ofn.lpstrTitle = wtitle.c_str();
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  if(!GetOpenFileNameW(&ofn))
  {
    return false;
  }
  path = narrow(buf);
  return !path.empty();
}

bool save_file_dialog(const DialogTools& tools, std::string& path, const std::string& title, const std::string& default_name)
{
  CC_REQUIRE(tools.available, return false);
  wchar_t buf[kPathChars] = {0};
  const std::wstring wdef = widen(default_name);
  std::wcsncpy(buf, wdef.c_str(), kPathChars - 1);
  const std::wstring wtitle = widen(title);
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof ofn;
  ofn.hwndOwner = GetActiveWindow();
  ofn.lpstrFilter = L"PNG image\0*.png\0All files\0*.*\0";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = static_cast<DWORD>(kPathChars);
  ofn.lpstrTitle = wtitle.c_str();
  ofn.lpstrDefExt = L"png";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_EXPLORER;
  if(!GetSaveFileNameW(&ofn))
  {
    return false;
  }
  path = narrow(buf);
  return !path.empty();
}

#else

namespace {

/** @brief Runs a dialog helper and takes its stdout as the chosen path. */
bool run_dialog(const DialogTools& tools, const std::vector<std::string>& argv, std::string& path)
{
  const ProcessResult r = run_process(argv, tools.process, tools.process.dialog_stdout_cap_bytes, tools.process.dialog_timeout_ms);
  if(!r.succeeded())
  {
    return false;
  }
  path = trim(std::string(r.out.begin(), r.out.end()));
  return !path.empty();
}

#ifdef __APPLE__
/** @brief Text safe inside an AppleScript string literal (quotes and backslashes dropped). */
std::string applescript_text(const std::string& s)
{
  std::string out;
  for(const char c : s)
  {
    if(c != '"' && c != '\\')
    {
      out += c;
    }
  }
  return out;
}

/** @brief Absolute path, so the dialog works with the minimal PATH of an app started from Finder. */
constexpr const char* kOsascript = "/usr/bin/osascript";
#endif

} // namespace

DialogTools locate_dialogs(const FfmpegConfig& search, const ProcessConfig& process)
{
  DialogTools t;
  t.process = process;
#ifdef __APPLE__
  (void)search; /* osascript ships with every macOS */
  t.available = true;
#else
  t.zenity = find_program("zenity", search);
  t.kdialog = find_program("kdialog", search);
  t.available = !t.zenity.empty() || !t.kdialog.empty();
#endif
  return t;
}

bool open_file_dialog(const DialogTools& tools, std::string& path, const std::string& title, bool /*video_too*/)
{
  CC_REQUIRE(tools.available, return false);
#ifdef __APPLE__
  return run_dialog(tools, {kOsascript, "-e", "POSIX path of (choose file with prompt \"" + applescript_text(title) + "\")"}, path);
#else
  if(!tools.zenity.empty())
  {
    return run_dialog(tools, {tools.zenity, "--file-selection", "--title=" + title}, path);
  }
  if(!tools.kdialog.empty())
  {
    return run_dialog(tools, {tools.kdialog, "--getopenfilename", ".", "--title", title}, path);
  }
  return false;
#endif
}

bool save_file_dialog(const DialogTools& tools, std::string& path, const std::string& title, const std::string& default_name)
{
  CC_REQUIRE(tools.available, return false);
#ifdef __APPLE__
  return run_dialog(tools,
                    {kOsascript, "-e",
                     "POSIX path of (choose file name with prompt \"" + applescript_text(title) + "\" default name \""
                         + applescript_text(default_name) + "\")"},
                    path);
#else
  if(!tools.zenity.empty())
  {
    return run_dialog(tools, {tools.zenity, "--file-selection", "--save", "--confirm-overwrite", "--title=" + title, "--filename=" + default_name},
                      path);
  }
  if(!tools.kdialog.empty())
  {
    return run_dialog(tools, {tools.kdialog, "--getsavefilename", default_name, "--title", title}, path);
  }
  return false;
#endif
}

#endif

bool open_user_config(const ProcessConfig& process, std::string& path, std::string& err)
{
  path = user_config_path();
  if(path.empty())
  {
    err = "no per-user settings folder on this platform: put config.yaml next to the executable";
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  if(!file_exists(path))
  {
    const std::string text = std::string("# CompressCompare settings. Every value below is a built-in default: change what you need,\n"
                                         "# save, and restart the app. Delete this file to return to the defaults.\n\n")
                             + builtin_config_yaml();
    if(!write_file(path, reinterpret_cast<const uint8_t*>(text.data()), text.size(), err))
    {
      return false;
    }
  }
#ifdef _WIN32
  (void)process;
  return true;
#else
#ifdef __APPLE__
  const std::vector<std::string> argv = {"/usr/bin/open", "-t", path};
#else
  const std::vector<std::string> argv = {"xdg-open", path};
#endif
  const ProcessResult r = run_process(argv, process, process.dialog_stdout_cap_bytes, process.dialog_timeout_ms);
  if(!r.succeeded())
  {
    err = "could not start a text editor; the file is " + path;
    return false;
  }
  return true;
#endif
}

} // namespace cc
