#pragma once

#include <GfxRenderer.h>

#include "components/themes/BaseTheme.h"

class VesperReaderFooter {
 public:
  struct Data {
    int currentPage = 0;
    int totalPages = 0;
    int percent = 0;
  };

  static bool hasContent();
  static bool persistentVisible();
  static int height();
  static void draw(GfxRenderer& renderer, const Data& data, bool clearBackground = true);

 private:
  static void drawBattery(GfxRenderer& renderer, const Rect& rect);
};
