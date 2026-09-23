/**
 * @file main.cpp
 * @brief Entry point: configuration, GLFW window, OpenGL 3.3 core context,
 * Dear ImGui and the main loop.
 *
 * The main loop sleeps when idle (glfwWaitEventsTimeout) so the app costs
 * almost nothing on a handheld until you touch it.
 *
 * Command line:
 * @code
 *   compresscompare [--config FILE] [--exit-after SECONDS] [file ...]
 *   compresscompare --help | --version | --check-config | --system-info [--config FILE]
 * @endcode
 */
#include "app/app.h"
#include "app/ui_style.h"
#include "core/accel.h"
#include "core/config.h"
#include "core/process.h"
#include "gpu/gl.h"
#include "util/contract.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

/** @brief Version shown by --version and in the window title. */
constexpr const char* kVersion = "0.3.0";
/** @brief OpenGL context version the renderer needs. */
constexpr int kGlMajor = 3;
constexpr int kGlMinor = 3;
/** @brief Command-line arguments accepted (formal bound on the argument loop). */
constexpr int kMaxArgs = 1024;

cc::App* g_app = nullptr;

void glfw_error(int code, const char* desc) { std::fprintf(stderr, "GLFW error %d: %s\n", code, desc != nullptr ? desc : "?"); }

void drop_callback(GLFWwindow* /*window*/, int count, const char** paths)
{
  if(g_app != nullptr)
  {
    g_app->on_drop(count, paths);
  }
}

/** @brief Reports a start-up failure on stderr and, on Windows, in a message box. */
void fatal(const std::string& msg)
{
  std::fprintf(stderr, "%s\n", msg.c_str());
#ifdef _WIN32
  MessageBoxA(nullptr, msg.c_str(), "CompressCompare", MB_OK | MB_ICONERROR);
#endif
}

/** @brief UTF-8 command-line arguments (Windows hands main() the ANSI code page). */
std::vector<std::string> utf8_args(int argc, char** argv)
{
  std::vector<std::string> out;
#ifdef _WIN32
  int n = 0;
  LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &n);
  if(wargv != nullptr)
  {
    for(int i = 1; i < n && i < kMaxArgs; ++i)
    {
      const int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
      std::string s(static_cast<std::size_t>(len > 0 ? len - 1 : 0), '\0');
      if(len > 1)
      {
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &s[0], len, nullptr, nullptr);
      }
      out.push_back(s);
    }
    LocalFree(wargv);
    return out;
  }
#endif
  for(int i = 1; i < argc && i < kMaxArgs; ++i)
  {
    out.emplace_back(argv[i]);
  }
  return out;
}

/** @brief What the command line asked for. */
struct CommandLine
{
  std::string config_path;      /**< `--config FILE` */
  std::vector<std::string> files; /**< files to load: original first, then sites */
  double exit_after_s = 0; /**< `--exit-after SECONDS`: close the window by itself (smoke tests) */
  bool help = false;
  bool version = false;
  bool check_config = false;
  bool system_info = false; /**< `--system-info`: OpenCV / OpenCL / video back-end report */
};

/** @brief Parses the arguments; returns false with a message for malformed ones. */
bool parse_command_line(const std::vector<std::string>& args, CommandLine& cl, std::string& err)
{
  for(std::size_t i = 0; i < args.size() && i < kMaxArgs; ++i)
  {
    const std::string& a = args[i];
    if(a == "--help" || a == "-h")
    {
      cl.help = true;
    }
    else if(a == "--version")
    {
      cl.version = true;
    }
    else if(a == "--check-config")
    {
      cl.check_config = true;
    }
    else if(a == "--system-info")
    {
      cl.system_info = true;
    }
    else if(a.rfind("-psn_", 0) == 0)
    {
      /* process serial number some macOS versions pass to apps started from Finder */
    }
    else if(a == "--config")
    {
      if(i + 1 >= args.size())
      {
        err = "--config needs a file path";
        return false;
      }
      cl.config_path = args[++i];
    }
    else if(a == "--exit-after")
    {
      if(i + 1 >= args.size())
      {
        err = "--exit-after needs a number of seconds";
        return false;
      }
      cl.exit_after_s = std::strtod(args[++i].c_str(), nullptr);
      if(cl.exit_after_s <= 0)
      {
        err = "--exit-after needs a positive number of seconds";
        return false;
      }
    }
    else if(!a.empty() && a[0] == '-' && a.size() > 1)
    {
      err = "unknown option " + a;
      return false;
    }
    else
    {
      cl.files.push_back(a);
    }
  }
  return true;
}

void print_usage()
{
  std::printf("CompressCompare %s\n"
              "usage: compresscompare [--config FILE] [--exit-after SECONDS] [file ...]\n"
              "       compresscompare --check-config | --system-info [--config FILE]\n"
              "       compresscompare --help | --version\n\n"
              "Files are loaded into the slots in order: the original first, then one per site.\n"
              "config.yaml next to the executable (or in the working directory) overrides the built-in\n"
              "settings; --config names another file. --check-config validates and exits.\n"
              "--system-info prints the OpenCV build, the OpenCL device and the video back-ends.\n"
              "--exit-after closes the window after that many seconds (for smoke tests).\n",
              kVersion);
}

/** @brief Per-user settings file for ImGui's window layout. */
std::string ini_path()
{
  std::error_code ec;
  std::filesystem::path dir;
#ifdef _WIN32
  const wchar_t* appdata = _wgetenv(L"APPDATA");
  if(appdata != nullptr)
  {
    dir = std::filesystem::path(appdata) / L"CompressCompare";
  }
#elif defined(__APPLE__)
  const char* home = std::getenv("HOME");
  if(home != nullptr)
  {
    dir = std::filesystem::path(home) / "Library" / "Application Support" / "CompressCompare";
  }
#else
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  const char* home = std::getenv("HOME");
  if(xdg != nullptr)
  {
    dir = std::filesystem::path(xdg) / "compresscompare";
  }
  else if(home != nullptr)
  {
    dir = std::filesystem::path(home) / ".config" / "compresscompare";
  }
#endif
  if(dir.empty())
  {
    return "compresscompare_ui.ini";
  }
  std::filesystem::create_directories(dir, ec);
#ifdef _WIN32
  return (dir / L"ui.ini").u8string();
#else
  return (dir / "ui.ini").string();
#endif
}

/**
 * @brief Content scale of the primary monitor (1.0 when unknown).
 * On macOS window sizes and ImGui coordinates are in points and GLFW hands
 * Retina pixels over through the framebuffer scale, so the scale is 1 there.
 */
float primary_monitor_scale()
{
#ifdef __APPLE__
  return 1.0f;
#else
  GLFWmonitor* mon = glfwGetPrimaryMonitor();
  if(mon == nullptr)
  {
    return 1.0f;
  }
  float sx = 1.0f;
  float sy = 1.0f;
  glfwGetMonitorContentScale(mon, &sx, &sy);
  return sx > 0.0f ? sx : 1.0f;
#endif
}

/** @brief Creates the window and context; null on failure (message already shown). */
GLFWwindow* create_window(const cc::Config& cfg, float scale)
{
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kGlMajor);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, kGlMinor);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  const int w = static_cast<int>(static_cast<float>(cfg.app.window.width_px) * scale);
  const int h = static_cast<int>(static_cast<float>(cfg.app.window.height_px) * scale);
  GLFWwindow* window = glfwCreateWindow(w, h, cfg.app.name.c_str(), nullptr, nullptr);
  if(window == nullptr)
  {
    fatal("Could not create an OpenGL 3.3 window. Update your GPU driver.");
    return nullptr;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  const char* missing = nullptr;
  if(!gl::load(reinterpret_cast<gl::GetProcFn>(glfwGetProcAddress), &missing))
  {
    fatal(std::string("OpenGL function missing: ") + (missing != nullptr ? missing : "?"));
    glfwDestroyWindow(window);
    return nullptr;
  }
  return window;
}

/** @brief Sets up Dear ImGui with the configured tooltip delays and DPI scale. */
void init_imgui(GLFWwindow* window, const cc::Config& cfg, float scale, const std::string& ini)
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad; /* gamepad: handy on a handheld */
  io.IniFilename = ini.c_str();
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.ScaleAllSizes(scale);
#if IMGUI_VERSION_NUM >= 19200
  style.FontScaleDpi = scale * cfg.app.ui_scale;
#else
  io.FontGlobalScale = scale * cfg.app.ui_scale;
#endif
  style.HoverDelayShort = cfg.app.tooltip.hover_delay_short_s;
  style.HoverDelayNormal = cfg.app.tooltip.hover_delay_normal_s;
  style.HoverStationaryDelay = cfg.app.tooltip.hover_stationary_delay_s;
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330 core");
}

/** @brief The frame loop; returns when the window is closed. */
void run_loop(GLFWwindow* window, cc::App& app, const cc::FrameTimingConfig& timing, double exit_after_s)
{
  /* Intentionally unbounded: the event loop runs until the window closes. */
  while(!glfwWindowShouldClose(window))
  {
    if(exit_after_s > 0 && glfwGetTime() >= exit_after_s)
    {
      glfwSetWindowShouldClose(window, 1);
    }
    /* Power saving: wait for events when idle, poll quickly while busy/animating. */
    glfwWaitEventsTimeout(app.wants_fast_frames() ? timing.fast_interval_s : timing.idle_interval_s);
    if(glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
    {
      ImGui_ImplGlfw_Sleep(timing.iconified_sleep_ms);
      continue;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    app.frame();
    ImGui::Render();
    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    gl::Viewport(0, 0, w, h);
    const ImVec4& clear = cc::style::kColClearColor;
    gl::ClearColor(clear.x, clear.y, clear.z, clear.w);
    gl::Clear(gl::COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }
}

/** @brief Handles --help, --version and --check-config; returns true when the process should exit with `code`. */
bool handle_info_options(const CommandLine& cl, const cc::Config& config, const std::string& override_path, int& code)
{
  code = EXIT_SUCCESS;
  if(cl.help)
  {
    print_usage();
    return true;
  }
  if(cl.version)
  {
    std::printf("CompressCompare %s\n", kVersion);
    return true;
  }
  if(cl.system_info)
  {
    const cc::AccelStatus& st = cc::init_acceleration(config.acceleration, config.threading.max_workers);
    std::printf("CompressCompare %s\n", kVersion);
    std::printf("OpenCV:         %s (%d threads, %s)\n", st.opencv_version.c_str(), st.cpu_threads, st.cpu_features.c_str());
    std::printf("OpenCL device:  %s\n", st.device.empty() ? "none" : st.device.c_str());
    std::printf("OpenCL in use:  %s%s%s\n", st.opencl_ok ? "yes" : "no", st.opencl_ok ? "" : " - ", st.opencl_ok ? "" : st.reason.c_str());
    std::printf("Video back-end: %s (acceleration.video_backend: %s)\n", st.video_io.c_str(),
                cc::video_backend_key(config.acceleration.video_backend));
    std::printf("Still images:   %s\n", st.still_decoders.c_str());
    return true;
  }
  if(cl.check_config)
  {
    std::printf("%s: OK (%zu sites)\n", override_path.empty() ? "built-in config.yaml" : override_path.c_str(), config.sites.size());
    return true;
  }
  return false;
}

/** @brief Creates the App, feeds it the command-line files and runs it until the window closes. */
void run_app(GLFWwindow* window, const cc::Config& config, float scale, const CommandLine& cl, const std::string& override_path)
{
  cc::App app(window, config, scale);
  g_app = &app;
  glfwSetDropCallback(window, drop_callback);
  std::string err;
  if(!app.init(err))
  {
    std::fprintf(stderr, "renderer: %s\n", err.c_str());
  }
  if(!override_path.empty())
  {
    std::printf("configuration: %s\n", override_path.c_str());
  }
  if(!cl.files.empty())
  {
    std::vector<const char*> ptrs;
    ptrs.reserve(cl.files.size());
    for(const std::string& f : cl.files)
    {
      ptrs.push_back(f.c_str());
    }
    app.on_drop(static_cast<int>(ptrs.size()), ptrs.data());
  }
  run_loop(window, app, config.app.frame_timing, cl.exit_after_s);
  g_app = nullptr;
  glfwSetDropCallback(window, nullptr);
}

} // namespace

int main(int argc, char** argv)
{
  CommandLine cl;
  std::string err;
  if(!parse_command_line(utf8_args(argc, argv), cl, err))
  {
    fatal(err + " (try --help)");
    return EXIT_FAILURE;
  }
  cc::Config config;
  const std::string override_path = cl.help || cl.version ? std::string() : cc::find_config_override(cl.config_path, cc::executable_dir());
  if(!cl.help && !cl.version && !cc::load_config(override_path, config, err))
  {
    fatal("configuration error: " + err);
    return EXIT_FAILURE;
  }
  int code = EXIT_SUCCESS;
  if(handle_info_options(cl, config, override_path, code))
  {
    return code;
  }
  const cc::AccelStatus& accel = cc::init_acceleration(config.acceleration, config.threading.max_workers);
  std::printf("OpenCV %s, OpenCL: %s\n", accel.opencv_version.c_str(),
              accel.opencl_ok ? accel.device.c_str() : ("off (" + accel.reason + ")").c_str());
  glfwSetErrorCallback(glfw_error);
  if(glfwInit() == 0)
  {
    fatal("glfwInit failed");
    return EXIT_FAILURE;
  }
  const float main_scale = primary_monitor_scale();
  GLFWwindow* window = create_window(config, main_scale);
  if(window == nullptr)
  {
    glfwTerminate();
    return EXIT_FAILURE;
  }
  const std::string ini = ini_path();
  init_imgui(window, config, main_scale, ini);
  run_app(window, config, main_scale, cl, override_path);
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  const long violations = cc::contract_violation_count();
  if(violations != 0)
  {
    std::fprintf(stderr, "%ld contract violation(s) were recorded during this session\n", violations);
  }
  return EXIT_SUCCESS;
}
