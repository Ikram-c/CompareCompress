/**
 * @file fuzzy_combo.h
 * @brief ImGui widgets: a std::string InputText and the fuzzy "specify site" combo.
 */
#pragma once

#include "core/config.h"

#include <string>
#include <vector>

namespace cc {

/**
 * @brief InputText bound to a std::string (resizes as you type).
 * @param label ImGui label / id.
 * @param s The bound string.
 * @param flags ImGuiInputTextFlags.
 * @param hint Placeholder text, or null.
 * @param max_chars Upper bound on the text length (`limits.max_url_chars` and friends).
 * @return true when the text changed (or Enter was pressed with EnterReturnsTrue).
 */
bool InputTextStd(const char* label, std::string& s, int flags, const char* hint, int max_chars);

/** @brief Per-widget state of FuzzySiteCombo(). */
struct FuzzySiteComboState
{
  int highlighted = 0; /**< row highlighted in the dropdown */
  bool open = false;
};

/** @brief Everything FuzzySiteCombo() needs besides its state. */
struct FuzzySiteComboArgs
{
  const std::vector<SiteInfo>* sites = nullptr; /**< the site database */
  const FuzzyConfig* fuzzy = nullptr;           /**< scoring weights and dropdown geometry */
  int max_query_chars = 0;                      /**< `limits.max_query_chars` */
  float width = 0;                              /**< width of the text box */
};

/**
 * @brief Text box with a ranked dropdown of known sites.
 * @param id ImGui id.
 * @param args Database, weights, limits and width.
 * @param query The text the user typed (or the chosen site's name).
 * @param selected Index into the database, or -1 for a custom name.
 * @param st Widget state.
 * @return true when `selected` changed.
 */
bool FuzzySiteCombo(const char* id, const FuzzySiteComboArgs& args, std::string& query, int& selected, FuzzySiteComboState& st);

/** @brief Tooltip on the last item with the standard hover delay. */
void HelpTooltip(const char* text);

} // namespace cc
