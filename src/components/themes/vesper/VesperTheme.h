#pragma once

#include "components/themes/lyra/LyraTheme.h"

namespace VesperMetrics {

constexpr ThemeMetrics baseValues = {
    .batteryWidth = 16,
    .batteryHeight = 12,
    .topPadding = 4,
    .batteryBarHeight = 38,
    .headerHeight = 62,
    .verticalSpacing = 10,
    .previewPadding = 10,
    .previewHeightPercent = 30,
    .contentSidePadding = 14,
    .listRowHeight = 44,
    .listWithSubtitleRowHeight = 60,
    .listRowGap = 2,
    .listRowRadius = 3,
    .listInset = 10,
    .listSidePadding = 12,
    .listSelectionStyle = 0,
    .listScrollWidth = 3,
    .listScrollSide = 0,
    .listTitleBold = false,
    .headerSidePadding = 14,
    .headerUnderlineSize = 1,
    .headerTitleAlign = 1,
    .headerBatterySide = 0,
    .headerBatteryDetached = true,
    .menuRowHeight = 50,
    .menuSpacing = 4,
    .tabSpacing = 6,
    .tabBarHeight = 42,
    .scrollBarWidth = 3,
    .scrollBarRightOffset = 4,
    .homeTopPadding = 64,
    .homeCoverHeight = 270,
    .homeCoverTileHeight = 650,
    .homeRecentBooksCount = 4,
    .homeContinueReadingInMenu = false,
    .homeMenuTopOffset = 4,
    .buttonHintsHeight = 40,
    .sideButtonHintsWidth = 30,
    .progressBarHeight = 10,
    .progressBarMarginTop = 1,
    .statusBarHorizontalMargin = 6,
    .statusBarVerticalMargin = 15,
    .keyboardKeyHeight = 48,
    .keyboardKeySpacing = 2,
    .keyboardCenteredText = true,
    .keyboardVerticalOffset = -6,
    .keyboardTextFieldWidthPercent = 88,
    .keyboardWidthPercent = 94,
    .popupTopOffsetRatio = 0.14f,
    .popupMarginX = 14,
    .popupMarginY = 12,
    .popupFrameThickness = 1,
    .popupCornerRadius = 4,
    .popupTextBold = false,
    .popupTextInverted = false,
    .popupTextBaselineOffsetY = -2,
    .popupProgressBarHeight = 4,
    .popupProgressDrawOutline = true,
    .popupProgressClampPercent = true,
    .popupProgressFillInverted = false,
    .popupProgressOutlineInverted = false,
    .optionPopupItemSpacing = 6,
    .optionPopupInnerPadding = 16,
    .optionPopupSelectionVPadding = 10,
    .optionPopupDialogSideMargin = 16,
    .textFieldHorizontalPadding = 8,
    .textFieldNormalThickness = 1,
    .textFieldCursorThickness = 2,
    .textFieldLineEndOffset = 0,
    .controlRadius = 4,
    .sheetRadius = 4,
    .capsuleRadius = 4,
};

constexpr ThemeMetrics portraitValues = baseValues;

constexpr ThemeMetrics landscapeValues = [] {
  ThemeMetrics v = baseValues;
  v.topPadding = 2;
  v.batteryBarHeight = 34;
  v.headerHeight = 60;
  v.verticalSpacing = 7;
  v.contentSidePadding = 12;
  v.listRowHeight = 38;
  v.listWithSubtitleRowHeight = 50;
  v.listInset = 8;
  v.headerSidePadding = 12;
  v.menuRowHeight = 42;
  v.tabBarHeight = 38;
  v.homeTopPadding = 62;
  v.homeCoverHeight = 250;
  v.homeCoverTileHeight = 346;
  v.homeMenuTopOffset = 4;
  v.popupTopOffsetRatio = 0.08f;
  v.keyboardKeyHeight = 38;
  v.keyboardVerticalOffset = -4;
  return v;
}();

}  // namespace VesperMetrics

class VesperTheme final : public LyraTheme {
 public:
  // Draws only the cover pixels, respecting the current renderer mode. Home
  // reuses this for the LSB/MSB grayscale passes.
  static bool drawBookCover(const GfxRenderer& renderer, const RecentBook& book, Rect target);

  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
};
