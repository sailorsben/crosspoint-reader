#include "VesperTheme.h"

#include <GfxRenderer.h>

#include "components/icons/vesperLogo.h"

namespace {

void drawVesperLogo(const GfxRenderer& renderer, const int x, const int y) {
  constexpr int size = VesperAssets::LOGO_SIZE;
  constexpr int rowBytes = (size + 7) / 8;
  for (int row = 0; row < size; row++) {
    for (int col = 0; col < size; col++) {
      const uint8_t byte = VesperAssets::Logo36[row * rowBytes + (col >> 3)];
      if (((byte >> (7 - (col & 7))) & 1) == 0) {
        renderer.drawPixel(x + col, y + row, true);
      }
    }
  }
}

}  // namespace

void VesperTheme::drawHeader(const GfxRenderer& renderer, const Rect rect, const char* title,
                             const char* subtitle) const {
  BaseTheme::drawHeader(renderer, rect, title, subtitle);

  const int logoX = rect.x + (rect.width - VesperAssets::LOGO_SIZE) / 2;
  const int logoY = rect.y + 1;
  drawVesperLogo(renderer, logoX, logoY);
}
