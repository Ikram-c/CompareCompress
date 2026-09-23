/**
 * @file ui_style.h
 * @brief Cosmetic constants shared by the ui_*.cpp files: paddings, radii,
 * colours and text-wrap widths.
 *
 * These describe the look of the widgets, not the behaviour of the
 * application, so they are named constants here rather than keys in
 * config.yaml (which holds every number a user might want to tune).
 */
#pragma once

#include <imgui.h>

#include <cstdint>
#include <type_traits>

namespace cc {
namespace style {

/* ---- spacing (logical pixels at 100 % scale) ------------------------------ */
constexpr float kPanelPaddingPx = 4.0f;
constexpr float kStatusPaddingXPx = 8.0f;
constexpr float kStatusPaddingYPx = 2.0f;
constexpr float kStatusExtraHeightPx = 4.0f;
constexpr float kInsetPx = 8.0f;        /**< labels inside panes */
constexpr float kInsetSmallPx = 6.0f;   /**< labels inside survey tiles */
constexpr float kInsetLargePx = 12.0f;  /**< hint text on the empty canvas */
constexpr float kLabelPadXPx = 4.0f;
constexpr float kLabelPadYPx = 2.0f;
constexpr float kBadgePadPx = 4.0f;
constexpr float kBadgeTextPadPx = 8.0f;
constexpr float kCornerRadiusPx = 4.0f;
constexpr float kInnerRadiusPx = 3.0f;
constexpr float kBarRadiusPx = 2.0f;
constexpr float kProgressBarHeightPx = 4.0f;
constexpr float kProgressBarHeightLargePx = 6.0f;
constexpr float kProgressBarTopPx = 36.0f; /**< below the hint text on the empty canvas */
constexpr float kMenuRightMarginPx = 16.0f;
constexpr float kDropdownOffsetPx = 2.0f;
constexpr float kDropdownPaddingXPx = 6.0f;
constexpr float kDropdownPaddingYPx = 4.0f;
constexpr float kDropdownAliasMarginPx = 10.0f;
constexpr float kSwatchInsetPx = 1.0f;
constexpr float kSwatchHeightReducePx = 2.0f;
constexpr float kActiveMarkerWidthPx = 3.0f;
constexpr float kActiveMarkerHeightPx = 40.0f;
constexpr float kActiveMarkerOffsetPx = 2.0f;
constexpr float kLoupeGapPx = 4.0f;
constexpr float kLoupeZoomLabelOffsetXPx = 30.0f;
constexpr float kLoupeZoomLabelOffsetYPx = 20.0f;
constexpr float kColorSwatchPx = 18.0f;
constexpr float kToolbarComboWidthPx = 230.0f;
constexpr float kSiteComboWidthPx = 160.0f;
constexpr float kSamplePointsWidthPx = 90.0f;
constexpr float kToolbarComboMaxFraction = 0.6f;
constexpr float kSiteComboMaxFraction = 0.4f;
constexpr float kPreviewShrink = 0.9f;        /**< site preview on the empty canvas */
constexpr float kVisualizeButtonHeightFrames = 1.6f;
constexpr float kSlotsFooterFrames = 2.4f;    /**< frame heights reserved below the slot list */
constexpr float kHalfGammaWidth = 0.48f;      /**< gamma slider share of the panel width */
constexpr float kHandleFillFactor = 0.6f;     /**< split handle radius scaling */
constexpr float kHandleExtraPx = 6.0f;
constexpr float kHandleArcFraction = 0.55f;
constexpr float kDividerWidthNearPx = 2.0f;
constexpr float kDividerWidthPx = 1.0f;
constexpr float kHandleOutlinePx = 1.5f;
constexpr int kHandleArcSegments = 12;
constexpr float kArcAStart = 0.3f, kArcAEnd = 2.6f;   /**< radians: the two arcs of the rotate glyph */
constexpr float kArcBStart = 3.44f, kArcBEnd = 5.74f;

/* ---- text ----------------------------------------------------------------- */
constexpr float kTooltipWrapEm = 30.0f; /**< tooltip wrap width in font sizes */
constexpr float kNotesWrapEm = 28.0f;
constexpr float kMinProgressFraction = 0.02f; /**< so an empty bar is still visible */
constexpr float kTextInsetPx = 16.0f;         /**< wrap margin of the drop-zone hint */
constexpr int kTimeDigits = 5;                /**< "12.34" seconds in slot status lines */

/* ---- colours -------------------------------------------------------------- */
constexpr ImU32 kColLabelBg = IM_COL32(0, 0, 0, 150);
constexpr ImU32 kColLabelBgLight = IM_COL32(0, 0, 0, 110);
constexpr ImU32 kColLabelText = IM_COL32(255, 255, 255, 230);
constexpr ImU32 kColCanvasBg = IM_COL32(18, 18, 20, 255);
constexpr ImU32 kColPaneBorder = IM_COL32(60, 60, 65, 255);
constexpr ImU32 kColTileBorder = IM_COL32(70, 70, 75, 255);
constexpr ImU32 kColTileActive = IM_COL32(255, 200, 80, 255);
constexpr ImU32 kColProgress = IM_COL32(80, 160, 255, 255);
constexpr ImU32 kColLoupeBorder = IM_COL32(255, 255, 255, 160);
constexpr ImU32 kColCrosshair = IM_COL32(255, 60, 60, 220);
constexpr ImU32 kColBadgeBg = IM_COL32(0, 0, 0, 160);
constexpr ImU32 kColDividerNear = IM_COL32(255, 255, 255, 230);
constexpr ImU32 kColDivider = IM_COL32(255, 255, 255, 180);
constexpr ImU32 kColHandleFillNear = IM_COL32(0, 0, 0, 200);
constexpr ImU32 kColHandleFill = IM_COL32(0, 0, 0, 110);
constexpr ImU32 kColHandleLineNear = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kColHandleLine = IM_COL32(255, 255, 255, 120);
constexpr ImU32 kColHandleGlyph = IM_COL32(255, 255, 255, 140);
constexpr ImU32 kColThreshold = IM_COL32(255, 255, 255, 200);
constexpr ImU32 kColWhite = IM_COL32(255, 255, 255, 255);
inline const ImVec4 kColError(1.0f, 0.45f, 0.4f, 1.0f);
inline const ImVec4 kColToast(1.0f, 0.85f, 0.4f, 1.0f);
inline const ImVec4 kColVisualize(0.20f, 0.45f, 0.75f, 1.0f);
inline const ImVec4 kColVisualizeHovered(0.26f, 0.55f, 0.88f, 1.0f);
inline const ImVec4 kColHighlight(1.0f, 0.75f, 0.25f, 1.0f);
inline const ImVec4 kColClearColor(0.10f, 0.10f, 0.11f, 1.0f);

/** @brief Value of the brightest 8-bit level. */
constexpr float kMaxLevel = 255.0f;

/** @brief Converts a 0..1 float channel to an 8-bit level. */
inline int level(float c) { return static_cast<int>(c * kMaxLevel); }

/** @brief Character counts of the fixed text buffers used by the panels. */
constexpr std::size_t kNumberBufChars = 48;
constexpr std::size_t kLineBufChars = 96;
constexpr std::size_t kLabelBufChars = 160;

/**
 * @brief Converts a GL texture name to ImGui's texture id.
 *
 * A template so that only the branch matching this ImGui version's ImTextureID
 * (an integer since 1.91, a pointer before) is instantiated.
 */
template <typename T = ImTextureID>
T texture_id(unsigned int gl_name)
{
  if constexpr(std::is_pointer_v<T>)
  {
    return reinterpret_cast<T>(static_cast<std::intptr_t>(gl_name));
  }
  else
  {
    return static_cast<T>(gl_name);
  }
}

} // namespace style
} // namespace cc
