/**
 * @file view_mode.h
 * @brief What one pane of the compare view shows.
 */
#pragma once

#include <string>

namespace cc {

/** @brief Content of a compare-view pane; the values are the shader's `u_mode`. */
enum class ViewMode : int
{
  Before = 0,  /**< the original */
  After = 1,   /**< the site's copy */
  Heat = 2,    /**< the heat map alone */
  Overlay = 3, /**< the heat map blended over a base image */
};

/** @brief Configuration key of the mode (`after`, `heat`, `overlay`). */
const char* view_mode_key(ViewMode m);

/**
 * @brief Parses a configuration key into a view mode.
 * @param key Key as written in config.yaml.
 * @param out Receives the mode on success.
 * @return true when the key names a mode.
 */
bool view_mode_from_key(const std::string& key, ViewMode& out);

} // namespace cc
