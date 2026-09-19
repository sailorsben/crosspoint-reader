#include "VesperTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
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
constexpr int kLogoSize = VesperAssets::LOGO_SIZE;
constexpr int kNavIconSize = 24;

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

void drawVesperLogo(const GfxRenderer& renderer, const int x, const int y) {
  constexpr int rowBytes = (kLogoSize + 7) / 8;
  for (int row = 0; row < kLogoSize; row++) {
    for (int col = 0; col < kLogoSize; col++) {
      const uint8_t byte = VesperAssets::Logo36[row * rowBytes + (col >> 3)];
      if (((byte >> (7 - (col & 7))) & 1) == 0) renderer.drawPixel(x + col, y + row, true);
    }
  }
}

void drawSectionLabel(const GfxRenderer& renderer, const Rect& rect, const char* label) {
  if (!label || rect.width <= 0 || rect.height <= 0) return;
  const std::string text = renderer.truncatedText(UI_10_FONT_ID, label, rect.width, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x, rect.y + 2, text.c_str(), true, EpdFontFamily::BOLD);
}

bool drawCover(const GfxRenderer& renderer, const RecentBook& book, const Rect& target, const int thumbHeight) {
  if (target.width <= 0 || target.height <= 0) return false;
  bool rendered = false;
  if (!book.coverBmpPath.empty()) {
    const std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, thumbHeight);
    HalFile file;
    if (Storage.openFileForRead("VUI", coverPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        int drawWidth = target.width;
        int drawHeight = static_cast<int>((static_cast<int64_t>(drawWidth) * bitmap.getHeight()) / bitmap.getWidth());
        if (drawHeight > target.height) {
          drawHeight = target.height;
          drawWidth = static_cast<int>((static_cast<int64_t>(drawHeight) * bitmap.getWidth()) / bitmap.getHeight());
        }
        drawWidth = std::max(1, drawWidth);
        drawHeight = std::max(1, drawHeight);
        const int drawX = target.x + (target.width - drawWidth) / 2;
        const int drawY = target.y + (target.height - drawHeight) / 2;
        rendered = renderer.drawBitmap(bitmap, drawX, drawY, drawWidth, drawHeight);
        renderer.drawRect(drawX, drawY, drawWidth, drawHeight);
      }
      file.close();
    }
  }
  if (!rendered) {
    renderer.drawRect(target.x, target.y, target.width, target.height);
    renderer.drawIcon(CoverIcon, target.x + (target.width - 32) / 2, target.y + std::max(4, (target.height - 32) / 2),
                      32);
  }
  return rendered;
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

void drawPortraitRecents(GfxRenderer& renderer, const VesperHome::Layout& layout,
                         const std::vector<RecentBook>& recentBooks, const ThemeMetrics& metrics) {
  for (int i = 0; i < layout.recentCount; i++) {
    const Rect card = layout.recent[i];
    constexpr int titleReserve = 42;
    const Rect cover{card.x + 4, card.y + 2, card.width - 8, std::max(1, card.height - titleReserve - 4)};
    drawCover(renderer, recentBooks[static_cast<size_t>(i + 1)], cover, metrics.homeCoverHeight);
    const Rect title{card.x + 3, card.y + card.height - titleReserve + 4, card.width - 6, titleReserve - 4};
    drawTitleLines(renderer, recentBooks[static_cast<size_t>(i + 1)], title, SMALL_FONT_ID, 2, false);
  }
}

void drawLandscapeRecents(GfxRenderer& renderer, const VesperHome::Layout& layout,
                          const std::vector<RecentBook>& recentBooks, const ThemeMetrics& metrics) {
  for (int i = 0; i < layout.recentCount; i++) {
    const Rect card = layout.recent[i];
    const int coverWidth = std::min(76, card.width * 34 / 100);
    const Rect cover{card.x + 3, card.y + 3, coverWidth, card.height - 6};
    drawCover(renderer, recentBooks[static_cast<size_t>(i + 1)], cover, metrics.homeCoverHeight);
    const int textX = cover.x + cover.width + 10;
    const Rect title{textX, card.y + 10, card.x + card.width - textX - 4, card.height - 16};
    drawTitleLines(renderer, recentBooks[static_cast<size_t>(i + 1)], title, UI_10_FONT_ID, 2, false);
  }
}
}  // namespace

void VesperTheme::drawHeader(const GfxRenderer& renderer, const Rect rect, const char* title,
                             const char* subtitle) const {
  BaseTheme::drawHeader(renderer, rect, title, subtitle);
  drawVesperLogo(renderer, rect.x + (rect.width - kLogoSize) / 2, rect.y + 1);
}

void VesperTheme::drawRecentBookCover(GfxRenderer& renderer, const Rect rect,
                                      const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                      bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                      std::function<bool()> storeCoverBuffer) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const VesperHome::Layout layout = VesperHome::bookLayout(rect, static_cast<int>(recentBooks.size()));

  if (!bufferRestored) {
    drawSectionLabel(renderer, layout.currentHeading, tr(STR_CONTINUE_READING));
    renderer.drawRect(layout.hero.x, layout.hero.y, layout.hero.width, layout.hero.height);
    drawCover(renderer, recentBooks[0], layout.heroCover, metrics.homeCoverHeight);

    const RecentBook& current = recentBooks[0];
    const Rect text = layout.heroText;
    const int titleHeight = layout.landscape ? 98 : 112;
    drawTitleLines(renderer, current, Rect{text.x, text.y, text.width, titleHeight}, UI_12_FONT_ID, 3, true);

    const int authorY = text.y + titleHeight + 6;
    if (!current.author.empty() && authorY + renderer.getLineHeight(UI_10_FONT_ID) < text.y + text.height) {
      const std::string author =
          renderer.truncatedText(UI_10_FONT_ID, current.author.c_str(), text.width, EpdFontFamily::REGULAR);
      renderer.drawText(UI_10_FONT_ID, text.x, authorY, author.c_str());
    }

    const int buttonHeight = layout.landscape ? 44 : 48;
    const int buttonY = text.y + text.height - buttonHeight;
    if (buttonY > authorY + renderer.getLineHeight(UI_10_FONT_ID)) {
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
        drawLandscapeRecents(renderer, layout, recentBooks, metrics);
      else
        drawPortraitRecents(renderer, layout, recentBooks, metrics);
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

    const uint8_t* bitmap = rowIcon ? iconForName(rowIcon(i)) : nullptr;
    if (bitmap) renderer.drawIcon(bitmap, item.x + (item.width - kNavIconSize) / 2, item.y + 7, kNavIconSize);

    const std::string raw = buttonLabel(i);
    const std::string label =
        renderer.truncatedText(SMALL_FONT_ID, raw.c_str(), std::max(1, item.width - 8), EpdFontFamily::REGULAR);
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label.c_str());
    const int textY = item.y + item.height - renderer.getLineHeight(SMALL_FONT_ID) - 5;
    renderer.drawText(SMALL_FONT_ID, item.x + (item.width - textWidth) / 2, textY, label.c_str());
  }
}
