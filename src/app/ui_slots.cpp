/**
 * @file ui_slots.cpp
 * @brief Left panel: the "original" slot and the per-site slots (upload /
 * paste URL / specify site), plus the "Visualize compression" button.
 */
#include "app/app.h"

#include "app/ui_style.h"
#include "core/sites.h"
#include "platform/dialogs.h"
#include "util/strings.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace cc {

namespace {

/** @brief Draws an image fitted (letterboxed) into a rectangle. */
void draw_fitted(ImDrawList* dl, const GpuTexture& t, ImVec2 p0, ImVec2 p1)
{
  if(!t.valid())
  {
    return;
  }
  const float bw = p1.x - p0.x;
  const float bh = p1.y - p0.y;
  const float s = std::min(bw / static_cast<float>(t.w), bh / static_cast<float>(t.h));
  const float w = static_cast<float>(t.w) * s;
  const float h = static_cast<float>(t.h) * s;
  const ImVec2 a(p0.x + (bw - w) * 0.5f, p0.y + (bh - h) * 0.5f);
  dl->AddImage(style::texture_id(t.id), a, ImVec2(a.x + w, a.y + h), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
}

/** @brief Small "VIDEO" badge like darktable's thumbnail overlays. */
void draw_video_badge(ImDrawList* dl, ImVec2 p0)
{
  const char* badge = "VIDEO";
  const ImVec2 sz = ImGui::CalcTextSize(badge);
  const float pad = style::kBadgePadPx;
  const float tpad = style::kBadgeTextPadPx;
  dl->AddRectFilled(ImVec2(p0.x + pad, p0.y + pad), ImVec2(p0.x + tpad + pad + sz.x, p0.y + tpad + sz.y), style::kColBadgeBg,
                    style::kInnerRadiusPx);
  dl->AddText(ImVec2(p0.x + tpad, p0.y + tpad - style::kLabelPadYPx), style::kColLabelText, badge);
}

} // namespace

void App::ui_slot_header(Slot& slot, float avail)
{
  ImGui::TextUnformatted(slot.title.c_str());
  if(!slot.is_original && slot.has_site_name())
  {
    ImGui::SameLine();
    ImGui::TextDisabled("- %s", slot.site_label(config_.sites).c_str());
  }
  if(slot.media.loaded() || slot.error)
  {
    ImGui::SameLine(avail - ImGui::CalcTextSize("clear").x - ImGui::GetStyle().FramePadding.x * 2.0f);
    if(ImGui::SmallButton("clear"))
    {
      clear_slot(slot);
    }
    HelpTooltip("Empty this slot");
  }
}

void App::ui_slot_hover_details(const Slot& slot)
{
  ImGui::BeginTooltip();
  ImGui::TextUnformatted(slot.media.display_name.c_str());
  ImGui::Separator();
  ImGui::TextUnformatted(slot.media.format_line(config_.metrics.custom_table_fit_error_pct).c_str());
  if(slot.media.kind == FileKind::JPEG && slot.media.jpeg.ok)
  {
    const JpegInfo& j = slot.media.jpeg;
    ImGui::Text("quantiser: luma q~%d, chroma q~%d, table fit %.1f%%", j.quality_luma, j.quality_chroma,
                static_cast<double>(j.table_fit_error));
    ImGui::Text("markers: %s%s%s%s", j.has_jfif ? "JFIF " : "", j.has_exif ? "EXIF " : "", j.has_icc ? "ICC " : "",
                j.has_adobe ? "Adobe " : "");
    if(!j.has_exif && !j.has_icc)
    {
      ImGui::TextDisabled("(metadata / colour profile stripped)");
    }
  }
  if(slot.media.is_video())
  {
    ImGui::Text("frame shown: %.2f s of %.1f s", slot.media.frame_time, slot.media.video.duration_s);
  }
  ImGui::TextDisabled("decoded with %s", slot.media.decoder.c_str());
  ImGui::TextDisabled("%s", slot.media.url.empty() ? slot.media.path.c_str() : slot.media.url.c_str());
  ImGui::EndTooltip();
}

void App::ui_slot_context_menu(Slot& slot)
{
  if(!ImGui::BeginPopupContextItem("slotmenu"))
  {
    return;
  }
  if(ImGui::MenuItem("Open file...", nullptr, false, dialogs_.available))
  {
    std::string path;
    if(open_file_dialog(dialogs_, path, "Choose the " + slot.title + " file", true))
    {
      load_slot_path(slot, path);
    }
  }
  if(ImGui::MenuItem("Paste URL / path from clipboard"))
  {
    load_from_clipboard(&slot);
  }
  if(ImGui::MenuItem("Clear", nullptr, false, slot.media.loaded()))
  {
    clear_slot(slot);
  }
  ImGui::EndPopup();
}

void App::ui_slot_dropzone(Slot& slot, float avail, float thumb_h)
{
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 p1(p0.x + avail, p0.y + thumb_h);
  ImGui::InvisibleButton("dropzone", ImVec2(avail, thumb_h));
  const bool hovered = ImGui::IsItemHovered();
  const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 border = ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Border);
  dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), style::kCornerRadiusPx);
  if(slot.thumb.valid())
  {
    draw_fitted(dl, slot.thumb, p0, p1);
  }
  else
  {
    const char* txt = slot.loading ? "loading..." : "Upload\n(click, drop a file, or paste a URL)";
    const float wrap = avail - style::kTextInsetPx;
    const ImVec2 sz = ImGui::CalcTextSize(txt, nullptr, false, wrap);
    dl->AddText(nullptr, 0.0f, ImVec2(p0.x + (avail - sz.x) * 0.5f, p0.y + (thumb_h - sz.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled), txt, nullptr, wrap);
  }
  if(slot.loading)
  {
    const float f = std::clamp(worker_.progress().fraction.load(), style::kMinProgressFraction, 1.0f);
    dl->AddRectFilled(ImVec2(p0.x, p1.y - style::kProgressBarHeightPx), ImVec2(p0.x + avail * f, p1.y),
                      ImGui::GetColorU32(ImGuiCol_PlotHistogram), style::kBarRadiusPx);
  }
  dl->AddRect(p0, p1, border, style::kCornerRadiusPx);
  if(hovered)
  {
    dl->AddRect(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p1.x - 1, p1.y - 1), border, style::kInnerRadiusPx);
    hovered_drop_slot_ = slot.id;
  }
  if(slot.media.is_video() && slot.thumb.valid())
  {
    draw_video_badge(dl, p0);
  }
  if(clicked && !slot.loading)
  {
    std::string path;
    if(!dialogs_.available)
    {
      toast("no file dialog available: drag and drop a file or paste a path/URL below", config_.app.toast.normal_s);
    }
    else if(open_file_dialog(dialogs_, path, "Choose the " + slot.title + " file", true))
    {
      load_slot_path(slot, path);
    }
  }
  if(hovered && slot.media.loaded() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    ui_slot_hover_details(slot);
  }
  ui_slot_context_menu(slot);
}

void App::ui_slot_url_line(Slot& slot, float avail)
{
  const ImGuiStyle& st = ImGui::GetStyle();
  ImGui::SetNextItemWidth(avail - ImGui::CalcTextSize("Fetch").x - st.FramePadding.x * 2.0f - st.ItemSpacing.x);
  const bool enter = InputTextStd("##url", slot.url_input, ImGuiInputTextFlags_EnterReturnsTrue, "paste image/video URL or a file path",
                                  config_.limits.max_url_chars);
  if(!ImGui::IsItemActive())
  {
    HelpTooltip("Direct links only (right-click the picture on the site > 'Copy image address').\n"
                "The site is detected from the link's host automatically. A local path works too.");
  }
  ImGui::SameLine();
  const bool fetch = ImGui::Button("Fetch");
  if((enter || fetch) && !slot.loading)
  {
    const std::string v = trim(slot.url_input);
    if(looks_like_url(v))
    {
      load_slot_url(slot, v);
    }
    else if(file_exists(v))
    {
      load_slot_path(slot, v);
    }
    else if(!v.empty())
    {
      slot.status = "not a URL and not an existing file";
      slot.error = true;
    }
  }
}

void App::ui_slot(Slot& slot)
{
  ImGui::PushID(slot.id);
  const PanelConfig& pc = config_.app.panels;
  const float avail = ImGui::GetContentRegionAvail().x;
  const float thumb_h = std::clamp(avail * pc.slot_thumb_height_fraction, pc.slot_thumb_min_px * ui_scale(), pc.slot_thumb_max_px * ui_scale());
  ui_slot_header(slot, avail);
  ui_slot_dropzone(slot, avail, thumb_h);
  ui_slot_url_line(slot, avail);
  if(!slot.is_original)
  {
    FuzzySiteComboArgs args;
    args.sites = &config_.sites;
    args.fuzzy = &config_.fuzzy;
    args.max_query_chars = config_.limits.max_query_chars;
    args.width = avail;
    FuzzySiteCombo("site", args, slot.site_query, slot.site_index, slot.combo); /* the label updates live */
  }
  if(!slot.status.empty())
  {
    if(slot.error)
    {
      ImGui::PushStyleColor(ImGuiCol_Text, style::kColError);
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(slot.status.c_str());
    ImGui::PopTextWrapPos();
    if(slot.error)
    {
      ImGui::PopStyleColor();
    }
  }
  ImGui::PopID();
}

void App::ui_visualize_button()
{
  const bool ready = original_.media.loaded()
                     && std::any_of(sites_.begin(), sites_.end(), [](const Slot& s) { return s.media.loaded(); });
  ImGui::BeginDisabled(!ready || worker_.busy());
  ImGui::PushStyleColor(ImGuiCol_Button, style::kColVisualize);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style::kColVisualizeHovered);
  if(ImGui::Button("Visualize compression", ImVec2(-1, ImGui::GetFrameHeight() * style::kVisualizeButtonHeightFrames)))
  {
    visualize_all();
  }
  ImGui::PopStyleColor(2);
  ImGui::EndDisabled();
  if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
  {
    ImGui::SetTooltip(ready ? "Resample the original to each site's output size, compute the statistics and the heat map.\n"
                              "(auto-runs when a slot changes unless disabled in Settings)"
                            : "Load an original and at least one site version first.");
  }
  ImGui::Checkbox("auto", &settings_.auto_visualize);
  HelpTooltip("Recompute automatically whenever a slot changes");
  ImGui::SameLine();
  if(ImGui::Checkbox("compare at site size", &settings_.compare_at_site_resolution))
  {
    pending_visualize_ = true;
  }
  HelpTooltip("On: the original is downscaled to the site's output size (what viewers actually get).\n"
              "Off: the site version is upscaled back to the original's size (shows resolution loss as blur).");
}

void App::ui_slots_panel()
{
  ImGui::BeginChild("slots_scroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * style::kSlotsFooterFrames), ImGuiChildFlags_None);
  ui_slot(original_);
  ImGui::Separator();
  ImGui::Spacing();
  int remove_index = -1;
  for(int i = 0; i < static_cast<int>(sites_.size()); ++i)
  {
    Slot& s = sites_[static_cast<std::size_t>(i)];
    if(i == active_site_)
    {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float off = style::kActiveMarkerOffsetPx;
      ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x - off, p.y - off), ImVec2(p.x - off + style::kActiveMarkerWidthPx, p.y + style::kActiveMarkerHeightPx),
                                                ImGui::GetColorU32(ImGuiCol_PlotHistogram), style::kBarRadiusPx);
    }
    ui_slot(s);
    ImGui::PushID(s.id);
    if(ImGui::SmallButton("show in viewer"))
    {
      active_site_ = i;
      if(layout_ == Layout::Survey)
      {
        set_layout(Layout::Loupe);
      }
    }
    HelpTooltip("Make this the site shown in the compare view (Left/Right arrows cycle)");
    if(sites_.size() > 1)
    {
      ImGui::SameLine();
      if(ImGui::SmallButton("remove"))
      {
        remove_index = i;
      }
    }
    ImGui::PopID();
    ImGui::Separator();
    ImGui::Spacing();
  }
  if(remove_index >= 0)
  {
    remove_site_slot(remove_index);
  }
  ImGui::BeginDisabled(static_cast<int>(sites_.size()) >= config_.limits.max_site_slots);
  if(ImGui::Button("+ Add another site", ImVec2(-1, 0)))
  {
    add_site_slot();
  }
  ImGui::EndDisabled();
  HelpTooltip("Compare the same original against as many sites (or the same site at different settings) as you like.");
  ImGui::EndChild();
  ui_visualize_button();
}

} // namespace cc
