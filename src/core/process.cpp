/**
 * @file process.cpp
 * @brief Windows (CreateProcess) and POSIX (fork/exec) back ends of run_process().
 */
#include "core/process.h"

#include "util/contract.h"
#include "util/strings.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace cc {

namespace {

/** @brief Milliseconds per second, for the time-out message. */
constexpr int kMillisPerSecond = 1000;
/** @brief Exit code execvp() leaves behind when the program cannot be started. */
constexpr int kExecFailedExitCode = 127;

/** @brief Converts a UTF-8 path to a std::filesystem::path. */
std::filesystem::path to_path(const std::string& utf8)
{
#ifdef _WIN32
  return std::filesystem::u8path(utf8);
#else
  return std::filesystem::path(utf8);
#endif
}

/** @brief Converts a std::filesystem::path to UTF-8. */
std::string from_path(const std::filesystem::path& p)
{
#ifdef _WIN32
  return p.u8string();
#else
  return p.string();
#endif
}

} // namespace

#ifdef _WIN32

namespace {

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

/** @brief Quotes one argument following the MSVC CRT rules. */
std::wstring quote_arg(const std::wstring& a)
{
  if(!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos)
  {
    return a;
  }
  std::wstring out = L"\"";
  std::size_t backslashes = 0;
  for(const wchar_t c : a)
  {
    if(c == L'\\')
    {
      ++backslashes;
      continue;
    }
    if(c == L'"')
    {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
      continue;
    }
    out.append(backslashes, L'\\');
    backslashes = 0;
    out.push_back(c);
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

/** @brief Builds the command line CreateProcess expects. */
std::wstring build_command_line(const std::vector<std::string>& argv)
{
  std::wstring cmdline;
  for(std::size_t i = 0; i < argv.size(); ++i)
  {
    if(i != 0)
    {
      cmdline.push_back(L' ');
    }
    cmdline += quote_arg(widen(argv[i]));
  }
  return cmdline;
}

/**
 * @brief Reads a pipe until it closes.
 * @param cap Stop storing beyond this many bytes (0 = no cap) but keep draining so the child never blocks.
 * @param truncated Set when the cap was hit; may be null.
 */
void drain_pipe(HANDLE h, const ProcessConfig& cfg, std::vector<uint8_t>& out, uint64_t cap, bool* truncated)
{
  std::vector<uint8_t> buf(cfg.pipe_chunk_bytes);
  for(long reads = 0; reads < cfg.max_read_iterations; ++reads)
  {
    DWORD got = 0;
    if(!ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) || got == 0)
    {
      return;
    }
    if(cap != 0 && out.size() + got > cap)
    {
      if(truncated != nullptr)
      {
        *truncated = true;
      }
      continue;
    }
    out.insert(out.end(), buf.begin(), buf.begin() + got);
  }
}

/** @brief Both ends of an inheritable pipe, closed on destruction. */
struct Pipe
{
  HANDLE read = nullptr;
  HANDLE write = nullptr;
  ~Pipe()
  {
    if(read != nullptr)
    {
      CloseHandle(read);
    }
    if(write != nullptr)
    {
      CloseHandle(write);
    }
  }
  bool create()
  {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    if(!CreatePipe(&read, &write, &sa, 0))
    {
      return false;
    }
    return SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0) != FALSE;
  }
  void close_write()
  {
    if(write != nullptr)
    {
      CloseHandle(write);
      write = nullptr;
    }
  }
};

/** @brief Waits for the child while draining both pipes; kills it on time-out. */
void wait_for_child(const PROCESS_INFORMATION& pi, Pipe& out_pipe, Pipe& err_pipe, const ProcessConfig& cfg, uint64_t max_stdout_bytes,
                    int timeout_ms, ProcessResult& r)
{
  std::vector<uint8_t> errbytes;
  std::thread err_thread([&]() { drain_pipe(err_pipe.read, cfg, errbytes, cfg.stderr_cap_bytes, nullptr); });
  bool killed = false;
  std::thread watchdog;
  if(timeout_ms > 0)
  {
    watchdog = std::thread([&]() {
      if(WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeout_ms)) == WAIT_TIMEOUT)
      {
        killed = true;
        TerminateProcess(pi.hProcess, 1);
      }
    });
  }
  bool truncated = false;
  drain_pipe(out_pipe.read, cfg, r.out, max_stdout_bytes, &truncated);
  WaitForSingleObject(pi.hProcess, INFINITE);
  if(watchdog.joinable())
  {
    watchdog.join();
  }
  err_thread.join();
  DWORD code = 0;
  const BOOL have_code = GetExitCodeProcess(pi.hProcess, &code);
  r.exit_code = have_code != FALSE ? static_cast<int>(code) : -1;
  r.err.assign(errbytes.begin(), errbytes.end());
  if(killed)
  {
    r.failure = "timed out after " + std::to_string(timeout_ms / kMillisPerSecond) + " s";
  }
  else if(truncated)
  {
    r.failure = "output exceeded the size limit";
  }
}

} // namespace

ProcessResult run_process(const std::vector<std::string>& argv, const ProcessConfig& cfg, uint64_t max_stdout_bytes, int timeout_ms)
{
  ProcessResult r;
  CC_REQUIRE(!argv.empty(), r.failure = "empty command"; return r);
  CC_REQUIRE(cfg.pipe_chunk_bytes > 0 && cfg.max_read_iterations > 0, r.failure = "internal: bad process config"; return r);
  /* CreateProcess with bInheritHandles=TRUE would leak another concurrent
   * child's pipe ends into this one (and keep its stdout open); running one
   * child at a time is simpler than PROC_THREAD_ATTRIBUTE_HANDLE_LIST. */
  static std::mutex s_run_mutex;
  const std::lock_guard<std::mutex> run_lock(s_run_mutex);
  std::wstring cmdline = build_command_line(argv);
  Pipe out_pipe;
  Pipe err_pipe;
  if(!out_pipe.create() || !err_pipe.create())
  {
    r.failure = "CreatePipe failed";
    return r;
  }
  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = out_pipe.write;
  si.hStdError = err_pipe.write;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi{};
  const BOOL ok = CreateProcessW(nullptr, &cmdline[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  out_pipe.close_write();
  err_pipe.close_write();
  if(ok == FALSE)
  {
    r.failure = "cannot start '" + argv[0] + "' (error " + std::to_string(GetLastError()) + ")";
    return r;
  }
  r.started = true;
  wait_for_child(pi, out_pipe, err_pipe, cfg, max_stdout_bytes, timeout_ms, r);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return r;
}

std::string executable_dir()
{
  wchar_t buf[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
  if(n == 0)
  {
    return "";
  }
  return from_path(std::filesystem::path(std::wstring(buf, n)).parent_path());
}

#else /* POSIX ----------------------------------------------------------- */

namespace {

/** @brief Reads stderr on its own thread so a chatty child never blocks. */
void drain_stderr(int fd, const ProcessConfig& cfg, std::vector<uint8_t>& errbytes)
{
  std::vector<uint8_t> buf(cfg.pipe_chunk_bytes);
  for(long reads = 0; reads < cfg.max_read_iterations; ++reads)
  {
    const ssize_t n = read(fd, buf.data(), buf.size());
    if(n <= 0)
    {
      return;
    }
    if(errbytes.size() + static_cast<std::size_t>(n) <= cfg.stderr_cap_bytes)
    {
      errbytes.insert(errbytes.end(), buf.begin(), buf.begin() + n);
    }
  }
}

/**
 * @brief Reads stdout with a deadline.
 * @param fd Read end of the child's stdout pipe.
 * @param pid The child, killed when the deadline passes.
 * @param cfg Chunk size and iteration bound.
 * @param max_stdout_bytes Bytes kept; the rest is discarded.
 * @param timeout_ms Deadline measured from now.
 * @param out Receives the bytes read.
 * @param killed Set when the deadline passed and the child was killed.
 * @param truncated Set when max_stdout_bytes was exceeded.
 */
void drain_stdout(int fd, pid_t pid, const ProcessConfig& cfg, uint64_t max_stdout_bytes, int timeout_ms, std::vector<uint8_t>& out,
                  bool& killed, bool& truncated)
{
  std::vector<uint8_t> buf(cfg.pipe_chunk_bytes);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for(long reads = 0; reads < cfg.max_read_iterations; ++reads)
  {
    pollfd pfd{fd, POLLIN, 0};
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    const int wait_ms = static_cast<int>(std::max<long long>(0, remaining));
    const int pr = poll(&pfd, 1, timeout_ms > 0 ? wait_ms : -1);
    if(pr == 0)
    {
      killed = true;
      kill(pid, SIGKILL);
      return;
    }
    const ssize_t n = read(fd, buf.data(), buf.size());
    if(n <= 0)
    {
      return;
    }
    if(max_stdout_bytes != 0 && out.size() + static_cast<std::size_t>(n) > max_stdout_bytes)
    {
      truncated = true;
      continue;
    }
    out.insert(out.end(), buf.begin(), buf.begin() + n);
  }
  truncated = true;
}

/** @brief Child side of fork(): wires the pipes and execs.  Never returns. */
[[noreturn]] void exec_child(char* const* args, const int out_pipe[2], const int err_pipe[2])
{
  dup2(out_pipe[1], STDOUT_FILENO);
  dup2(err_pipe[1], STDERR_FILENO);
  close(out_pipe[0]);
  close(out_pipe[1]);
  close(err_pipe[0]);
  close(err_pipe[1]);
  execvp(args[0], args);
  _exit(kExecFailedExitCode);
}

} // namespace

ProcessResult run_process(const std::vector<std::string>& argv, const ProcessConfig& cfg, uint64_t max_stdout_bytes, int timeout_ms)
{
  ProcessResult r;
  CC_REQUIRE(!argv.empty(), r.failure = "empty command"; return r);
  CC_REQUIRE(cfg.pipe_chunk_bytes > 0 && cfg.max_read_iterations > 0, r.failure = "internal: bad process config"; return r);
  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};
  if(pipe(out_pipe) != 0 || pipe(err_pipe) != 0)
  {
    r.failure = "pipe() failed";
    return r;
  }
  std::vector<char*> args;
  args.reserve(argv.size() + 1);
  for(const std::string& a : argv)
  {
    args.push_back(const_cast<char*>(a.c_str()));
  }
  args.push_back(nullptr);
  const pid_t pid = fork();
  if(pid < 0)
  {
    r.failure = "fork() failed";
    return r;
  }
  if(pid == 0)
  {
    exec_child(args.data(), out_pipe, err_pipe);
  }
  close(out_pipe[1]);
  close(err_pipe[1]);
  r.started = true;
  std::vector<uint8_t> errbytes;
  std::thread err_thread([&]() { drain_stderr(err_pipe[0], cfg, errbytes); });
  bool truncated = false;
  bool killed = false;
  drain_stdout(out_pipe[0], pid, cfg, max_stdout_bytes, timeout_ms, r.out, killed, truncated);
  int status = 0;
  const pid_t waited = waitpid(pid, &status, 0);
  CC_ENSURE(waited == pid);
  err_thread.join();
  close(out_pipe[0]);
  close(err_pipe[0]);
  r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  r.err.assign(errbytes.begin(), errbytes.end());
  if(r.exit_code == kExecFailedExitCode && r.out.empty())
  {
    r.failure = "cannot start '" + argv[0] + "'";
  }
  if(killed)
  {
    r.failure = "timed out after " + std::to_string(timeout_ms / kMillisPerSecond) + " s";
  }
  else if(truncated)
  {
    r.failure = "output exceeded the size limit";
  }
  return r;
}

std::string executable_dir()
{
#ifdef __APPLE__
  char buf[4096];
  uint32_t n = sizeof buf;
  if(_NSGetExecutablePath(buf, &n) != 0)
  {
    return "";
  }
  return std::filesystem::path(buf).parent_path().string();
#else
  std::error_code ec;
  const std::filesystem::path p = std::filesystem::read_symlink("/proc/self/exe", ec);
  if(ec)
  {
    return "";
  }
  return p.parent_path().string();
#endif
}

#endif

namespace {

/** @brief Appends the directories of the PATH environment variable. */
void append_path_dirs(std::vector<std::filesystem::path>& dirs)
{
#ifdef _WIN32
  const wchar_t* path = _wgetenv(L"PATH");
  if(path == nullptr)
  {
    return;
  }
  std::wstring cur;
  for(const wchar_t* p = path;; ++p)
  {
    if(*p == L';' || *p == 0)
    {
      if(!cur.empty())
      {
        dirs.emplace_back(cur);
      }
      cur.clear();
      if(*p == 0)
      {
        break;
      }
    }
    else
    {
      cur.push_back(*p);
    }
  }
#else
  const char* path = std::getenv("PATH");
  if(path == nullptr)
  {
    return;
  }
  for(const std::string& d : split(path, ':'))
  {
    if(!d.empty())
    {
      dirs.emplace_back(d);
    }
  }
#endif
}

} // namespace

std::string find_program(const std::string& name, const FfmpegConfig& cfg)
{
  CC_REQUIRE(!name.empty(), return "");
  /* Everything here is std::filesystem::path based so non-ASCII directories
   * never go through the ANSI code page. */
  std::vector<std::filesystem::path> dirs;
  const std::string here = executable_dir();
  if(!here.empty())
  {
    const std::filesystem::path h = to_path(here);
    dirs.push_back(h);
    for(const std::string& sub : cfg.search_subdirs)
    {
      dirs.push_back((h / to_path(sub)).lexically_normal());
    }
  }
  append_path_dirs(dirs);
#ifndef _WIN32
  for(const std::string& extra : cfg.extra_search_dirs)
  {
    dirs.push_back(to_path(extra));
  }
#endif
#ifdef _WIN32
  const std::filesystem::path exe = to_path(name + ".exe");
#else
  const std::filesystem::path exe = to_path(name);
#endif
  std::error_code ec;
  for(const std::filesystem::path& d : dirs)
  {
    const std::filesystem::path p = d / exe;
    if(std::filesystem::is_regular_file(p, ec))
    {
      return from_path(p);
    }
  }
  return "";
}

} // namespace cc
