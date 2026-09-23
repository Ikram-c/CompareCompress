/**
 * @file fuzzy_combo.cpp
 * @brief std::string text boxes and the ranked site dropdown.
 */
#include "app/fuzzy_combo.h"

#include "app/ui_style.h"
#include "core/fuzzy.h"
#include "core/sites.h"
#include "util/contract.h"
#include "util/strings.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace cc {

namespace {

/** @brief Minimum capacity reserved so ImGui can type into the string in place. */
constexpr std::size_t kMinCapacity = 64;

/** @brief User data handed to ImGui's input callback. */
struct InputCtx
{
  std::string* str = nullptr;
  FuzzySiteComboState* combo = nullptr;
  int rows = 0;
  int max_chars = 0;
};

/**
 * @brief ImGui input callback: grows the std::string, enforces the length
 * limit and moves the dropdown highlight.
 *
 * Power of 10 note: ImGui requires a plain function pointer here.
 */
int input_callback(ImGuiInputTextCallbackData* data)
{
  InputCtx* ctx = static_cast<InputCtx*>(data->UserData);
  CC_REQUIRE(ctx != nullptr && ctx->str != nullptr, return 0);
  if(data->EventFlag == ImGuiInputTextFlags_CallbackResize)
  {
    ctx->str->resize(static_cast<std::size_t>(data->BufTextLen));
    data->Buf = &(*ctx->str)[0];
  }
  else if(data->EventFlag == ImGuiInputTextFlags_CallbackEdit && ctx->max_chars > 0 && data->BufTextLen > ctx->max_chars)
  {
    data->DeleteChars(ctx->max_chars, data->BufTextLen - ctx->max_chars);
  }
  else if(data->EventFlag == ImGuiInputTextFlags_CallbackHistory && ctx->combo != nullptr)
  {
    if(data->EventKey == ImGuiKey_UpArrow)
    {
      ctx->combo->highlighted = std::max(0, ctx->combo->highlighted - 1);
    }
    if(data->EventKey == ImGuiKey_DownArrow)
    {
      ctx->combo->highlighted = std::min(std::max(0, ctx->rows - 1), ctx->combo->highlighted + 1);
    }
  }
  return 0;
}

/** @brief Makes sure the string has room for ImGui to write into. */
void reserve_input(std::string& s, int max_chars)
{
  const std::size_t want = std::max(kMinCapacity, static_cast<std::size_t>(std::max(1, max_chars)));
  if(s.capacity() < want)
  {
    s.reserve(want);
  }
}

/** @brief A ranked dropdown row. */
struct Row
{
  int index = 0;
  int score = 0;
  std::vector<int> positions;
};

/** @brief Ranks the database for a query; every site matches an empty query. */
std::vector<Row> rank_sites(const std::vector<SiteInfo>& sites, const FuzzyConfig& cfg, const std::string& q)
{
  std::vector<Row> rows;
  for(std::size_t i = 0; i < sites.size(); ++i)
  {
    const FuzzyMatch m = fuzzy_match_aliases(q, sites[i].name, sites[i].aliases, cfg);
    if(m.matched)
    {
      rows.push_back(Row{static_cast<int>(i), m.score, m.positions});
    }
  }
  if(!q.empty())
  {
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.score > b.score; });
  }
  return rows;
}

/** @brief Tooltip describing what a site does to uploads. */
void site_tooltip(const SiteInfo& s)
{
  ImGui::BeginTooltip();
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * style::kNotesWrapEm);
  ImGui::TextUnformatted(s.name.c_str());
  ImGui::Separator();
  ImGui::TextWrapped("%s", s.notes.c_str());
  if(!s.hosts.empty())
  {
    std::string hosts;
    for(const std::string& h : s.hosts)
    {
      hosts += (hosts.empty() ? "" : ", ") + h;
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Auto-detected from: %s", hosts.c_str());
  }
  ImGui::TextDisabled("(typical behaviour, approximate - measure it!)");
  ImGui::PopTextWrapPos();
  ImGui::EndTooltip();
}

/** @brief Draws a site name with the fuzzy-matched characters highlighted, plus its first alias. */
void draw_row_text(const SiteInfo& s, const Row& row, ImVec2 pos)
{
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 normal = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 highlight = ImGui::GetColorU32(style::kColHighlight);
  std::size_t pi = 0;
  float x = pos.x;
  for(std::size_t c = 0; c < s.name.size(); ++c)
  {
    const bool hit = pi < row.positions.size() && row.positions[pi] == static_cast<int>(c);
    if(hit)
    {
      ++pi;
    }
    const char ch[2] = {s.name[c], 0};
    dl->AddText(ImVec2(x, pos.y), hit ? highlight : normal, ch);
    x += ImGui::CalcTextSize(ch).x;
  }
  const std::string alias = s.aliases.empty() ? std::string() : s.aliases.front();
  if(!alias.empty() && to_lower(alias) != to_lower(s.name))
  {
    const ImVec2 sz = ImGui::CalcTextSize(alias.c_str());
    dl->AddText(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - sz.x - style::kDropdownAliasMarginPx, pos.y),
                ImGui::GetColorU32(ImGuiCol_TextDisabled), alias.c_str());
  }
}

/**
 * @brief Draws the dropdown window.
 * @param id ImGui id of the owning combo (the window id is derived from it).
 * @param args Site list and layout parameters.
 * @param rows Ranked rows to show.
 * @param st Combo state (keyboard selection, scroll request).
 * @param box_min Top-left screen corner of the text box the dropdown hangs from.
 * @param box_max Bottom-right screen corner of that text box.
 * @param picked Receives the row index the user clicked, or -1.
 * @return true when the pointer is over the dropdown.
 */
bool draw_dropdown(const char* id, const FuzzySiteComboArgs& args, const std::vector<Row>& rows, FuzzySiteComboState& st,
                   ImVec2 box_min, ImVec2 box_max, int& picked)
{
  picked = -1;
  const float row_h = ImGui::GetTextLineHeightWithSpacing();
  const int shown = std::min(static_cast<int>(rows.size()), args.fuzzy->dropdown_rows);
  ImGui::SetNextWindowPos(ImVec2(box_min.x, box_max.y + style::kDropdownOffsetPx));
  ImGui::SetNextWindowSize(ImVec2(std::max(args.width, args.fuzzy->dropdown_min_width_px),
                                  row_h * static_cast<float>(shown) + ImGui::GetStyle().WindowPadding.y * 2.0f));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                                 | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_Tooltip
                                 | ImGuiWindowFlags_NoNav;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style::kDropdownPaddingXPx, style::kDropdownPaddingYPx));
  char popup_name[64];
  std::snprintf(popup_name, sizeof popup_name, "##sitepopup_%08X", ImGui::GetID(id));
  bool hovered = false;
  if(ImGui::Begin(popup_name, nullptr, flags))
  {
    hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    for(int r = 0; r < static_cast<int>(rows.size()); ++r)
    {
      const Row& row = rows[static_cast<std::size_t>(r)];
      const SiteInfo& s = (*args.sites)[static_cast<std::size_t>(row.index)];
      ImGui::PushID(r);
      const bool is_hl = (r == st.highlighted);
      const ImVec2 row_pos = ImGui::GetCursorScreenPos();
      if(ImGui::Selectable("##row", is_hl, 0, ImVec2(0, row_h)))
      {
        picked = r;
      }
      if(is_hl && !ImGui::IsItemVisible())
      {
        ImGui::SetScrollHereY(0.5f); /* keyboard navigation past the visible rows */
      }
      if(ImGui::IsItemHovered())
      {
        st.highlighted = r;
        site_tooltip(s);
      }
      draw_row_text(s, row, ImVec2(row_pos.x + style::kLabelPadXPx, row_pos.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f));
      ImGui::PopID();
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();
  return hovered;
}

} // namespace

bool InputTextStd(const char* label, std::string& s, int flags, const char* hint, int max_chars)
{
  CC_REQUIRE(label != nullptr, return false);
  InputCtx ctx;
  ctx.str = &s;
  ctx.max_chars = max_chars;
  reserve_input(s, max_chars);
  /* ImGui writes into the buffer in place; std::string's buffer is contiguous and the
   * resize callback grows it, while the edit callback trims it to max_chars. */
  const int all_flags = flags | ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackEdit;
  if(hint != nullptr)
  {
    return ImGui::InputTextWithHint(label, hint, &s[0], s.capacity() + 1, all_flags, input_callback, &ctx);
  }
  return ImGui::InputText(label, &s[0], s.capacity() + 1, all_flags, input_callback, &ctx);
}

void HelpTooltip(const char* text)
{
  if(text != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * style::kTooltipWrapEm);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

namespace {

/** @brief What the text box reported this frame. */
struct TextBox
{
  bool entered = false;   /**< Enter was pressed */
  bool active = false;    /**< has keyboard focus */
  bool activated = false; /**< gained focus this frame */
  bool edited = false;    /**< text changed this frame */
  ImVec2 box_min;
  ImVec2 box_max;
};

/** @brief Draws the search box and reports its state. */
TextBox draw_text_box(const FuzzySiteComboArgs& args, std::string& query, FuzzySiteComboState& st, int rows)
{
  TextBox box;
  ImGui::SetNextItemWidth(args.width);
  InputCtx ctx;
  ctx.str = &query;
  ctx.combo = &st;
  ctx.rows = rows;
  ctx.max_chars = args.max_query_chars;
  reserve_input(query, args.max_query_chars);
  box.entered = ImGui::InputTextWithHint("##site", "Specify site (type to search)", &query[0], query.capacity() + 1,
                                         ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackEdit
                                             | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_EnterReturnsTrue,
                                         input_callback, &ctx);
  box.active = ImGui::IsItemActive();
  box.activated = ImGui::IsItemActivated();
  box.edited = ImGui::IsItemEdited();
  box.box_min = ImGui::GetItemRectMin();
  box.box_max = ImGui::GetItemRectMax();
  if(!box.active)
  {
    HelpTooltip("Type a few letters: \"ig\", \"twtr\", \"fb\" all work. Up/Down to move, Enter to pick.\n"
                "Pasting a URL in the slot fills this in automatically.");
  }
  return box;
}

/** @brief Selection logic shared by Enter, clicks and the auto-pick. */
struct Picker
{
  const std::vector<SiteInfo>& sites;
  std::string& query;
  int& selected;
  FuzzySiteComboState& st;
  bool changed = false;

  void pick(int index)
  {
    if(index >= 0 && index < static_cast<int>(sites.size()))
    {
      changed = changed || (selected != index);
      selected = index;
      query = sites[static_cast<std::size_t>(index)].name;
    }
    st.open = false;
  }

  /** @brief Enter: the highlighted row, an exact name, or keep the custom text. */
  void on_enter(const std::vector<Row>& rows)
  {
    if(!rows.empty())
    {
      pick(rows[static_cast<std::size_t>(st.highlighted)].index);
      return;
    }
    const int exact = find_site_by_name(sites, query);
    if(exact >= 0)
    {
      pick(exact);
    }
    else
    {
      st.open = false; /* keep as a custom site name */
    }
  }

  /** @brief The user typed an exact name / alias but did not press Enter. */
  void auto_pick()
  {
    const int exact = find_site_by_name(sites, query);
    if(exact >= 0)
    {
      selected = exact;
      query = sites[static_cast<std::size_t>(exact)].name;
      changed = true;
    }
  }
};

} // namespace

bool FuzzySiteCombo(const char* id, const FuzzySiteComboArgs& args, std::string& query, int& selected, FuzzySiteComboState& st)
{
  CC_REQUIRE(id != nullptr && args.sites != nullptr && args.fuzzy != nullptr, return false);
  const std::vector<SiteInfo>& sites = *args.sites;
  const std::vector<Row> rows = rank_sites(sites, *args.fuzzy, trim(query));
  st.highlighted = std::clamp(st.highlighted, 0, std::max(0, static_cast<int>(rows.size()) - 1));
  Picker picker{sites, query, selected, st};
  ImGui::PushID(id);
  const TextBox box = draw_text_box(args, query, st, static_cast<int>(rows.size()));
  if(box.activated)
  {
    st.open = true;
    st.highlighted = 0;
  }
  if(box.edited)
  {
    /* typing invalidates the previous choice until something is picked again */
    if(selected >= 0 && to_lower(trim(query)) != to_lower(sites[static_cast<std::size_t>(selected)].name))
    {
      selected = kNoSite;
      picker.changed = true;
    }
    st.highlighted = 0;
    st.open = true;
  }
  if(box.entered)
  {
    picker.on_enter(rows);
  }
  if(ImGui::IsKeyPressed(ImGuiKey_Escape) && box.active)
  {
    st.open = false;
  }
  bool popup_hovered = false;
  if(st.open && !rows.empty())
  {
    int picked = -1;
    popup_hovered = draw_dropdown(id, args, rows, st, box.box_min, box.box_max, picked);
    if(picked >= 0)
    {
      picker.pick(rows[static_cast<std::size_t>(picked)].index);
    }
  }
  /* close when focus leaves both the box and the dropdown */
  if(st.open && !box.active && !popup_hovered && !box.activated && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
  {
    st.open = false;
  }
  if(!st.open && !box.active && selected < 0 && !trim(query).empty())
  {
    picker.auto_pick();
  }
  ImGui::PopID();
  return picker.changed;
}

} // namespace cc
