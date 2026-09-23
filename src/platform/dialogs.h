/**
 * @file dialogs.h
 * @brief Native "open file" / "save file" dialogs.
 *
 * Windows: comdlg32 (no extra dependency).  Linux: zenity or kdialog when
 * present.  macOS: osascript.  When no dialog is available the UI falls back
 * to a path text box and drag-and-drop.
 */
#pragma once

#include "core/config.h"

#include <string>

namespace cc {

/** @brief Which dialog helper exists on this machine, plus the process settings used to run it. */
struct DialogTools
{
  bool available = false;
  std::string zenity;  /**< full path, "" if missing (Linux) */
  std::string kdialog; /**< full path, "" if missing (Linux) */
  ProcessConfig process;
};

/**
 * @brief Looks for the dialog helpers.
 * @param search `ffmpeg` section: the same search directories are used for zenity / kdialog.
 * @param process `process` section (dialog time-out and output cap).
 */
[[nodiscard]] DialogTools locate_dialogs(const FfmpegConfig& search, const ProcessConfig& process);

/**
 * @brief Shows an open-file dialog.
 * @param tools From locate_dialogs().
 * @param path Receives the chosen file.
 * @param title Dialog title.
 * @param video_too Offer video formats as well as images.
 * @return true when the user picked a file.
 */
[[nodiscard]] bool open_file_dialog(const DialogTools& tools, std::string& path, const std::string& title, bool video_too);

/**
 * @brief Shows a save-file dialog.
 * @param tools From locate_dialogs().
 * @param path Receives the chosen file.
 * @param title Dialog title.
 * @param default_name Suggested file name.
 * @return true when the user confirmed a name.
 */
[[nodiscard]] bool save_file_dialog(const DialogTools& tools, std::string& path, const std::string& title,
                                    const std::string& default_name);

/**
 * @brief Opens the per-user config.yaml (user_config_path()) in the system text
 * editor, creating it from the built-in defaults first when it does not exist.
 * Changes take effect at the next start.
 * @param process `process` section (time-out of the `open` / `xdg-open` call).
 * @param path Receives the file's path.
 * @param err Receives a message on failure.
 */
[[nodiscard]] bool open_user_config(const ProcessConfig& process, std::string& path, std::string& err);

} // namespace cc
