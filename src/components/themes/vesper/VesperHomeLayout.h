#pragma once

#include <algorithm>

#include "components/themes/BaseTheme.h"

namespace VesperHome {

struct Layout {
  bool landscape = false;
  Rect currentHeading;
  Rect hero;
  Rect heroCover;
  Rect heroText;
  Rect recentHeading;
  Rect recent[3];
  int recentCount = 0;
  int dividerX = -1;
};

inline bool contains(const Rect& r, const int x, const int y) {
  return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

inline Layout bookLayout(const Rect band, const int bookCount) {
  Layout l;
  l.landscape = band.width > band.height;
  l.recentCount = std::clamp(bookCount - 1, 0, 3);

  if (!l.landscape) {
    constexpr int pad = 14;
    constexpr int gap = 8;
    l.currentHeading = Rect{band.x + pad, band.y, band.width - 2 * pad, 26};
    l.hero = Rect{band.x + pad, band.y + 30, band.width - 2 * pad, 330};
    const int coverWidth = std::min(180, l.hero.width * 42 / 100);
    l.heroCover = Rect{l.hero.x + 7, l.hero.y + 7, coverWidth, l.hero.height - 14};
    const int textX = l.heroCover.x + l.heroCover.width + 14;
    l.heroText = Rect{textX, l.hero.y + 12, l.hero.x + l.hero.width - textX - 10, l.hero.height - 24};

    l.recentHeading = Rect{band.x + pad, l.hero.y + l.hero.height + 12, band.width - 2 * pad, 24};
    const int cardsY = l.recentHeading.y + l.recentHeading.height + 4;
    const int usable = band.width - 2 * pad - 2 * gap;
    const int cardWidth = usable / 3;
    const int cardsHeight = std::max(0, band.y + band.height - cardsY);
    for (int i = 0; i < 3; i++) {
      l.recent[i] = Rect{band.x + pad + i * (cardWidth + gap), cardsY, cardWidth, cardsHeight};
    }
    return l;
  }

  constexpr int pad = 12;
  constexpr int gap = 12;
  const int leftWidth = band.width * 64 / 100;
  l.dividerX = band.x + leftWidth;
  l.currentHeading = Rect{band.x + pad, band.y, leftWidth - 2 * pad, 24};
  l.hero = Rect{band.x + pad, band.y + 28, leftWidth - 2 * pad, band.height - 32};
  const int coverWidth = std::min(190, l.hero.width * 39 / 100);
  l.heroCover = Rect{l.hero.x + 7, l.hero.y + 7, coverWidth, l.hero.height - 14};
  const int textX = l.heroCover.x + l.heroCover.width + 14;
  l.heroText = Rect{textX, l.hero.y + 12, l.hero.x + l.hero.width - textX - 10, l.hero.height - 24};

  const int rightX = l.dividerX + gap;
  const int rightWidth = band.x + band.width - rightX - pad;
  l.recentHeading = Rect{rightX, band.y, rightWidth, 24};
  const int cardsY = l.recentHeading.y + l.recentHeading.height + 4;
  const int usableHeight = std::max(0, band.y + band.height - cardsY);
  const int rowGap = 5;
  const int rowHeight = std::max(1, (usableHeight - rowGap * 2) / 3);
  for (int i = 0; i < 3; i++) {
    l.recent[i] = Rect{rightX, cardsY + i * (rowHeight + rowGap), rightWidth, rowHeight};
  }
  return l;
}

inline Rect coverRect(const Layout& l, const int bookIndex) {
  if (bookIndex == 0) return l.heroCover;
  const int recentIndex = bookIndex - 1;
  if (recentIndex < 0 || recentIndex >= l.recentCount) return {};

  const Rect card = l.recent[recentIndex];
  if (l.landscape) {
    const int coverWidth = std::min(76, card.width * 34 / 100);
    return Rect{card.x + 3, card.y + 3, coverWidth, std::max(1, card.height - 6)};
  }

  constexpr int titleReserve = 42;
  return Rect{card.x + 4, card.y + 2, std::max(1, card.width - 8),
              std::max(1, card.height - titleReserve - 4)};
}

inline int hitBook(const Layout& l, const int bookCount, const int x, const int y) {
  if (bookCount > 0 && contains(l.hero, x, y)) return 0;
  for (int i = 0; i < l.recentCount; i++) {
    if (contains(l.recent[i], x, y)) return i + 1;
  }
  return -1;
}

inline Rect navBand(const Rect available) {
  const int height = std::min(available.height, available.width > available.height ? 58 : 70);
  return Rect{available.x, available.y, available.width, std::max(0, height)};
}

inline Rect navItem(const Rect available, const int index, const int count) {
  if (count <= 0) return {};
  const Rect band = navBand(available);
  const int x0 = band.x + (band.width * index) / count;
  const int x1 = band.x + (band.width * (index + 1)) / count;
  return Rect{x0, band.y, x1 - x0, band.height};
}

inline int hitNav(const Rect available, const int count, const int x, const int y) {
  const Rect band = navBand(available);
  if (!contains(band, x, y) || count <= 0) return -1;
  const int col = (x - band.x) * count / std::max(1, band.width);
  return std::clamp(col, 0, count - 1);
}

}  // namespace VesperHome
