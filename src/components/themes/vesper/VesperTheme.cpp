#include "VesperTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "VesperHomeLayout.h"
#include "components/UITheme.h"
#include "components/icons/blocks.h"
#include "components/icons/book.h"
#include "components/icons/bookmark.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/hotspot.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/vesperLogo.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

namespace {
constexpr int kLogoSourceSize = VesperAssets::LOGO_SIZE;
constexpr int kNavIconSize = 28;

const uint8_t* iconForName(const UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Wifi:
      return WifiIcon;
    case UIIcon::Hotspot:
      return HotspotIcon;
    case UIIcon::Bookmark:
      return BookmarkIcon;
    case UIIcon::Blocks:
      return BlocksIcon;
    default:
      return nullptr;
  }
}

void drawVesperLogo(const GfxRenderer& renderer, const int x, const int y, const int size) {
  constexpr int rowBytes = (kLogoSourceSize + 7) / 8;
  for (int row = 0; row < size; row++) {
    const int srcRow = row * kLogoSourceSize / size;
    for (int col = 0; col < size; col++) {
      const int srcCol = col * kLogoSourceSize / size;
      const uint8_t byte = VesperAssets::Logo56[srcRow * rowBytes + (srcCol >> 3)];
      if (((byte >> (7 - (srcCol & 7))) & 1) == 0) renderer.drawPixel(x + col, y + row, true);
    }
  }
}

void drawSectionLabel(const GfxRenderer& renderer, const Rect& rect, const char* label) {
  if (!label || rect.width <= 0 || rect.height <= 0) return;
  const std::string text = renderer.truncatedText(UI_10_FONT_ID, label, rect.width, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x, rect.y + 2, text.c_str(), true, EpdFontFamily::BOLD);
}

void drawTitleLines(const GfxRenderer& renderer, const RecentBook& book, const Rect& rect, const int fontId,
                    const int maxLines, const bool bold) {
  if (rect.width <= 0 || rect.height <= 0) return;
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const auto lines = renderer.wrappedText(fontId, book.title.c_str(), rect.width, maxLines, style);
  int y = rect.y;
  const int lineHeight = renderer.getLineHeight(fontId);
  for (const auto& line : lines) {
    if (y + lineHeight > rect.y + rect.height) break;
    renderer.drawText(fontId, rect.x, y, line.c_str(), true, style);
    y += lineHeight;
  }
}

void drawProgress(const GfxRenderer& renderer, const RecentBook& book, const Rect& text, const int top,
                  const int bottomLimit) {
  if (book.progressPercent < 0 || top + 34 >= bottomLimit || text.width < 80) return;

  const int percent = std::clamp(book.progressPercent, 0, 100);
  char percentText[8];
  snprintf(percentText, sizeof(percentText), "%d%%", percent);
  const int percentWidth = renderer.getTextWidth(SMALL_FONT_ID, percentText, EpdFontFamily::BOLD);
  const int barWidth = std::max(30, text.width - percentWidth - 8);
  constexpr int barHeight = 8;

  renderer.drawRect(text.x, top, barWidth, barHeight);
  const int fillWidth = (barWidth - 2) * percent / 100;
  if (fillWidth > 0) renderer.fillRect(text.x + 1, top + 1, fillWidth, barHeight - 2, true);
  renderer.drawText(SMALL_FONT_ID, text.x + barWidth + 8, top - 4, percentText, true, EpdFontFamily::BOLD);

  if (book.progressTotal > 0) {
    char pageText[40];
    snprintf(pageText, sizeof(pageText), "Page %lu / %lu", static_cast<unsigned long>(book.progressCurrent),
             static_cast<unsigned long>(book.progressTotal));
    renderer.drawText(SMALL_FONT_ID, text.x, top + barHeight + 5, pageText);
  }
}

bool drawVesperNavIcon(const GfxRenderer& renderer, const UIIcon icon, const int x, const int y, const int size) {
  const int cx = x + size / 2;
  switch (icon) {
    case UIIcon::Folder: {
      const int top = y + 3;
      const int bottom = y + size - 2;
      renderer.drawLine(x + 2, top + 5, x + 9, top + 5, 2, true);
      renderer.drawLine(x + 9, top + 5, x + 13, top + 1, 2, true);
      renderer.drawLine(x + 13, top + 1, x + 20, top + 1, 2, true);
      renderer.drawLine(x + 20, top + 1, x + 23, top + 5, 2, true);
      renderer.drawLine(x + 23, top + 5, x + size - 2, top + 5, 2, true);
      renderer.drawLine(x + 2, top + 5, x + 2, bottom, 2, true);
      renderer.drawLine(x + size - 2, top + 5, x + size - 2, bottom, 2, true);
      renderer.drawLine(x + 2, bottom, x + size - 2, bottom, 2, true);
      return true;
    }
    case UIIcon::Library:
      renderer.drawRect(x + 3, y + 5, 6, size - 9, 2, true);
      renderer.drawRect(x + 11, y + 3, 6, size - 7, 2, true);
      renderer.drawRect(x + 19, y + 6, 6, size - 10, 2, true);
      renderer.drawLine(x + 2, y + size - 2, x + size - 2, y + size - 2, 2, true);
      return true;
    case UIIcon::Transfer:
      renderer.drawLine(cx - 5, y + size - 4, cx - 5, y + 4, 2, true);
      renderer.drawLine(cx - 5, y + 4, cx - 10, y + 9, 2, true);
      renderer.drawLine(cx - 5, y + 4, cx, y + 9, 2, true);
      renderer.drawLine(cx + 5, y + 4, cx + 5, y + size - 4, 2, true);
      renderer.drawLine(cx + 5, y + size - 4, cx, y + size - 9, 2, true);
      renderer.drawLine(cx + 5, y + size - 4, cx + 10, y + size - 9, 2, true);
      return true;
    case UIIcon::Settings:
      renderer.drawLine(x + 2, y + 6, x + size - 2, y + 6, 2, true);
      renderer.fillRect(x + 7, y + 3, 5, 7, true);
      renderer.drawLine(x + 2, y + 14, x + size - 2, y + 14, 2, true);
      renderer.fillRect(x + 17, y + 11, 5, 7, true);
      renderer.drawLine(x + 2, y + 22, x + size - 2, y + 22, 2, true);
      renderer.fillRect(x + 10, y + 19, 5, 7, true);
      return true;
    default:
      return false;
  }
}

void drawPortraitRecents(const GfxRenderer& renderer, const VesperHome::Layout& layout,
                         const std::vector<RecentBook>& recentBooks) {
  for (int i = 0; i < layout.recentCount; i++) {
    const Rect card = layout.recent[i];
    const Rect cover = VesperHome::coverRect(layout, i + 1);
    VesperTheme::drawBookCover(renderer, recentBooks[static_cast<size_t>(i + 1)], cover);
    renderer.drawRect(cover.x, cover.y, cover.width, cover.height);

    constexpr int titleReserve = 42;
    const Rect title{card.x + 3, card.y + card.height - titleReserve + 4, card.width - 6, titleReserve - 4};
    drawTitleLines(renderer, recentBooks[static_cast<size_t>(i + 1)], title, SMALL_FONT_ID, 2, false);
  }
}

void drawLandscapeRecents(const GfxRenderer& renderer, const VesperHome::Layout& layout,
                          const std::vector<RecentBook>& recentBooks) {
  for (int i = 0; i < layout.recentCount; i++) {
    const Rect card = layout.recent[i];
    const RecentBook& book = recentBooks[static_cast<size_t>(i + 1)];

    const Rect title{card.x + 8, card.y + 8, card.width - 16, std::max(1, card.height - 28)};
    drawTitleLines(renderer, book, title, UI_10_FONT_ID, 2, false);

    if (!book.author.empty()) {
      const std::string author =
          renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), card.width - 16, EpdFontFamily::REGULAR);
      renderer.drawText(SMALL_FONT_ID, card.x + 8,
                        card.y + card.height - renderer.getLineHeight(SMALL_FONT_ID) - 5, author.c_str());
    }
  }
}
}  // namespace

bool VesperTheme::drawBookCover(const GfxRenderer& renderer, const RecentBook& book, const Rect target) {
  if (target.width <= 0 || target.height <= 0 || book.coverBmpPath.empty()) return false;

  const std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, target.height);
  HalFile file;
  if (!Storage.openFileForRead("VUI", coverPath, file)) return false;

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    file.close();
    return false;
  }

  const float sourceRatio = static_cast<float>(bitmap.getWidth()) / bitmap.getHeight();
  const float targetRatio = static_cast<float>(target.width) / target.height;
  float cropX = 0.0f;
  float cropY = 0.0f;
  if (sourceRatio > targetRatio) {
    cropX = 1.0f - targetRatio / sourceRatio;
  } else if (sourceRatio < targetRatio) {
    cropY = 1.0f - sourceRatio / targetRatio;
  }

  const bool rendered = renderer.drawBitmap(bitmap, target.x, target.y, target.width, target.height, cropX, cropY);
  file.close();
  return rendered;
}

void VesperTheme::drawHeader(const GfxRenderer& renderer, const Rect rect, const char* title,
                             const char* subtitle) const {
  BaseTheme::drawHeader(renderer, rect, title, subtitle);

  const int logoSize = VesperAssets::LOGO_SIZE;
  const int logoX = rect.x + (rect.width - logoSize) / 2;
  const int logoY = rect.y + std::max(0, (rect.height - logoSize) / 2);
  drawVesperLogo(renderer, logoX, logoY, logoSize);
}

void VesperTheme::drawRecentBookCover(GfxRenderer& renderer, const Rect rect,
                                      const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                      bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                      std::function<bool()> storeCoverBuffer) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const VesperHome::Layout layout = VesperHome::bookLayout(rect, static_cast<int>(recentBooks.size()));

  if (!bufferRestored) {
    renderer.drawRect(layout.hero.x, layout.hero.y, layout.hero.width, layout.hero.height);

    const Rect heroCover = VesperHome::coverRect(layout, 0);
    if (!drawBookCover(renderer, recentBooks[0], heroCover)) {
      renderer.drawIcon(CoverIcon, heroCover.x + (heroCover.width - 32) / 2,
                        heroCover.y + std::max(4, (heroCover.height - 32) / 2), 32);
    }
    renderer.drawRect(heroCover.x, heroCover.y, heroCover.width, heroCover.height);

    const RecentBook& current = recentBooks[0];
    const Rect text = layout.heroText;
    const int titleHeight = layout.landscape ? 98 : 112;
    drawTitleLines(renderer, current, Rect{text.x, text.y, text.width, titleHeight}, UI_12_FONT_ID, 3, true);

    const int authorY = text.y + titleHeight + 6;
    const int authorLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    if (!current.author.empty() && authorY + authorLineHeight < text.y + text.height) {
      const std::string author =
          renderer.truncatedText(UI_10_FONT_ID, current.author.c_str(), text.width, EpdFontFamily::REGULAR);
      renderer.drawText(UI_10_FONT_ID, text.x, authorY, author.c_str());
    }

    const int buttonHeight = layout.landscape ? 44 : 48;
    const int buttonY = text.y + text.height - buttonHeight;
    drawProgress(renderer, current, text, authorY + authorLineHeight + 18, buttonY - 10);

    if (buttonY > authorY + authorLineHeight) {
      renderer.fillRoundedRect(text.x, buttonY, text.width, buttonHeight, 3, Color::Black);
      const char* label = tr(STR_CONTINUE_READING);
      const int labelWidth = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
      const int labelY = buttonY + (buttonHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
      renderer.drawText(UI_10_FONT_ID, text.x + std::max(8, (text.width - labelWidth) / 2), labelY, label, false,
                        EpdFontFamily::BOLD);
    }

    if (layout.recentCount > 0) {
      drawSectionLabel(renderer, layout.recentHeading, tr(STR_MENU_RECENT_BOOKS));
      if (layout.dividerX >= 0) renderer.drawLine(layout.dividerX, rect.y, layout.dividerX, rect.y + rect.height - 1);
      if (layout.landscape)
        drawLandscapeRecents(renderer, layout, recentBooks);
      else
        drawPortraitRecents(renderer, layout, recentBooks);
    }

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
    bufferRestored = coverBufferStored;
  }

  if (selectorIndex == 0) {
    renderer.drawRect(layout.hero.x, layout.hero.y, layout.hero.width, layout.hero.height, 2, true);
  } else {
    const int recentIndex = selectorIndex - 1;
    if (recentIndex >= 0 && recentIndex < layout.recentCount) {
      const Rect card = layout.recent[recentIndex];
      renderer.drawRect(card.x, card.y, card.width, card.height, 2, true);
    }
  }
}

void VesperTheme::drawButtonMenu(GfxRenderer& renderer, const Rect rect, const int buttonCount, const int selectedIndex,
                                 const std::function<std::string(int index)>& buttonLabel,
                                 const std::function<UIIcon(int index)>& rowIcon) const {
  if (buttonCount <= 0) return;
  const Rect band = VesperHome::navBand(rect);
  renderer.drawLine(band.x, band.y, band.x + band.width - 1, band.y);

  for (int i = 0; i < buttonCount; i++) {
    const Rect item = VesperHome::navItem(rect, i, buttonCount);
    if (selectedIndex == i) {
      renderer.fillRectDither(item.x + 2, item.y + 2, std::max(1, item.width - 4), std::max(1, item.height - 4),
                              Color::LightGray);
    }
    if (i > 0) renderer.drawLine(item.x, item.y + 8, item.x, item.y + item.height - 8);

    const UIIcon icon = rowIcon ? rowIcon(i) : UIIcon::None;
    const int iconX = item.x + (item.width - kNavIconSize) / 2;
    const int iconY = item.y + 6;
    if (!drawVesperNavIcon(renderer, icon, iconX, iconY, kNavIconSize)) {
      const uint8_t* bitmap = iconForName(icon);
      if (bitmap) renderer.drawIcon(bitmap, iconX, iconY, kNavIconSize);
    }

    const std::string raw = buttonLabel(i);
    const std::string label =
        renderer.truncatedText(SMALL_FONT_ID, raw.c_str(), std::max(1, item.width - 8), EpdFontFamily::REGULAR);
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label.c_str());
    const int textY = item.y + item.height - renderer.getLineHeight(SMALL_FONT_ID) - 5;
    renderer.drawText(SMALL_FONT_ID, item.x + (item.width - textWidth) / 2, textY, label.c_str());
  }
}
