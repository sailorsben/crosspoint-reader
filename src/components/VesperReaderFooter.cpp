#include "VesperReaderFooter.h"

#include <HalGPIO.h>
#include <HalPowerManager.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace {
constexpr int kFooterHeight = 30;
constexpr int kOuterPad = 7;
constexpr int kGap = 7;
constexpr int kBatteryWidth = 48;
constexpr int kBatteryHeight = 22;
constexpr int kBarHeight = 24;
constexpr int kBarTextPad = 7;
}  // namespace

bool VesperReaderFooter::hasContent() {
  return SETTINGS.uiTheme == CrossPointSettings::UI_THEME::VESPERUI &&
         (SETTINGS.immersiveShowPages || SETTINGS.immersiveShowProgress || SETTINGS.immersiveShowPercentage ||
          SETTINGS.immersiveShowBattery);
}

bool VesperReaderFooter::persistentVisible() { return hasContent() && SETTINGS.immersiveMode == 0; }

int VesperReaderFooter::height() { return hasContent() ? kFooterHeight : 0; }

void VesperReaderFooter::drawBattery(const GfxRenderer& renderer, const Rect& rect) {
  const int bodyW = rect.width - 3;
  renderer.drawRoundedRect(rect.x, rect.y, bodyW, rect.height, 1, 3, true);
  const int nubH = std::max(6, rect.height / 3);
  renderer.fillRect(rect.x + bodyW, rect.y + (rect.height - nubH) / 2, 3, nubH, true);

  const bool charging = gpio.isUsbConnected();
  char pct[5];
  snprintf(pct, sizeof(pct), "%u", static_cast<unsigned>(powerManager.getBatteryPercentage()));
  const int textW = renderer.getTextWidth(SMALL_FONT_ID, pct, EpdFontFamily::BOLD);
  const int textY = rect.y + (rect.height - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
  const int textAreaX = rect.x + (charging ? 12 : 2);
  const int textAreaW = bodyW - (charging ? 13 : 4);
  renderer.drawText(SMALL_FONT_ID, textAreaX + (textAreaW - textW) / 2, textY, pct, true, EpdFontFamily::BOLD);

  if (charging) {
    const int bx = rect.x + 4;
    const int by = rect.y + (rect.height - 12) / 2;
    renderer.drawLine(bx + 5, by, bx + 2, by + 5, 2, true);
    renderer.drawLine(bx + 2, by + 5, bx + 6, by + 5, 2, true);
    renderer.drawLine(bx + 6, by + 5, bx + 2, by + 12, 2, true);
  }
}

void VesperReaderFooter::draw(const GfxRenderer& renderer, const Data& data, const bool clearBackground) {
  if (!hasContent()) return;

  int mt, mr, mb, ml;
  renderer.getOrientedViewableTRBL(&mt, &mr, &mb, &ml);
  const int footerY = renderer.getScreenHeight() - mb - kFooterHeight;
  const int left = ml + kOuterPad;
  const int right = renderer.getScreenWidth() - mr - kOuterPad;
  if (right <= left || footerY < 0) return;

  if (clearBackground) renderer.fillRect(0, footerY, renderer.getScreenWidth(), kFooterHeight + mb, false);

  const bool showBattery = SETTINGS.immersiveShowBattery != 0;
  const bool showBar = SETTINGS.immersiveShowProgress != 0;
  const bool showPages = SETTINGS.immersiveShowPages != 0;
  const bool showPercent = SETTINGS.immersiveShowPercentage != 0;
  const int batterySpace = showBattery ? kBatteryWidth + kGap : 0;
  const int contentRight = right - batterySpace;
  const int centerY = footerY + (kFooterHeight - kBarHeight) / 2;

  char pages[32] = {};
  if (showPages && data.totalPages > 0) {
    snprintf(pages, sizeof(pages), "%d / %d", std::max(1, data.currentPage), data.totalPages);
  }
  char percent[8] = {};
  if (showPercent) snprintf(percent, sizeof(percent), "%d%%", std::clamp(data.percent, 0, 100));

  if (showBar) {
    const int barW = std::max(1, contentRight - left);
    renderer.drawRoundedRect(left, centerY, barW, kBarHeight, 1, 4, true);

    // Progress is a solid baseline inside the tall status bar. This keeps both
    // text labels legible at every progress value while making the entire
    // outlined pill the progress control the user asked for.
    const int innerW = std::max(0, barW - 4);
    const int fillW = innerW * std::clamp(data.percent, 0, 100) / 100;
    if (fillW > 0) renderer.fillRect(left + 2, centerY + kBarHeight - 6, fillW, 4, true);

    const int textY = centerY + (kBarHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2 - 1;
    if (showPages && pages[0]) renderer.drawText(SMALL_FONT_ID, left + kBarTextPad, textY, pages, true);
    if (showPercent) {
      const int w = renderer.getTextWidth(SMALL_FONT_ID, percent);
      renderer.drawText(SMALL_FONT_ID, left + barW - kBarTextPad - w, textY, percent, true);
    }
  } else {
    int x = left;
    const int textY = centerY + (kBarHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2 - 1;
    if (showPages && pages[0]) {
      renderer.drawText(SMALL_FONT_ID, x, textY, pages, true);
      x += renderer.getTextWidth(SMALL_FONT_ID, pages) + 16;
    }
    if (showPercent) renderer.drawText(SMALL_FONT_ID, x, textY, percent, true);
  }

  if (showBattery) {
    drawBattery(renderer, Rect{right - kBatteryWidth, footerY + (kFooterHeight - kBatteryHeight) / 2, kBatteryWidth,
                               kBatteryHeight});
  }
}
