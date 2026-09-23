/**
 * @file process.h
 * @brief Runs an external program (ffmpeg / ffprobe) without a shell and
 * captures its binary stdout and text stderr.  No console window on Windows.
 */
#pragma once

#include "core/config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cc {

/** @brief Outcome of run_process(). */
struct ProcessResult
{
  bool started = false;      /**< the program was launched */
  int exit_code = -1;        /**< -1 when it did not exit normally */
  std::vector<uint8_t> out;  /**< captured stdout (binary) */
  std::string err;           /**< captured stderr (text), capped at `process.stderr_cap_bytes` */
  std::string failure;       /**< why it could not be started, timed out or was truncated */

  /** @brief True when the program ran to completion with exit code 0 and nothing was truncated. */
  [[nodiscard]] bool succeeded() const { return started && exit_code == 0 && failure.empty(); }
};

/**
 * @brief Runs a program and waits for it.
 * @param argv argv[0] is the program (absolute path or name on PATH); no shell is involved.
 * @param cfg `process` section (pipe sizes, stderr cap, read-iteration bound).
 * @param max_stdout_bytes Stop storing stdout beyond this (0 = no cap); the child is still drained.
 * @param timeout_ms Kill the child after this long (0 = wait for ever).
 */
[[nodiscard]] ProcessResult run_process(const std::vector<std::string>& argv, const ProcessConfig& cfg, uint64_t max_stdout_bytes,
                                        int timeout_ms);

/** @brief Directory of the running executable, or "" when unknown. */
[[nodiscard]] std::string executable_dir();

/**
 * @brief Finds a program next to the executable, in the configured sub-folders, or on PATH.
 * @param name Program name without `.exe`.
 * @param cfg `ffmpeg` section (search_subdirs, extra_search_dirs).
 * @return The full path, or "".
 */
[[nodiscard]] std::string find_program(const std::string& name, const FfmpegConfig& cfg);

} // namespace cc
