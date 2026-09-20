#include "XtcReaderActivity.h"

#include "ImmersiveOptionsActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

namespace {
constexpr int PAGE_TURN_RATES[] = {0, 1, 3, 6, 12};
constexpr size_t PAGE_TURN_RATE_COUNT = sizeof(PAGE_TURN_RATES) / sizeof(PAGE_TURN_RATES[0]);
}  // namespace

#include "CrossPointSettings.h"
#include "ProgressFile.h"
#include "ReaderActivity.h"
#include "ReaderUtils.h"
#include "XtcReaderChapterSelectionActivity.h"
#include "XtcReaderMenuActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/VesperReaderFooter.h"
#include "fontIds.h"

bool XtcReaderActivity::loadBook() {
  auto loadedXtc = makeUniqueNoThrow<Xtc>(bookPath, "/.crosspoint");
  if (!loadedXtc) {
    LOG_ERR("XTR", "Failed to allocate XTC object");
    return false;
  }
  if (!loadedXtc->load()) {
    LOG_ERR("XTR", "Failed to load XTC");
    return false;
  }
  xtc = std::move(loadedXtc);
  xtc->setupCacheDir();
  loadProgress();
  return true;
}

void XtcReaderActivity::openReaderMenu() {
  if (!xtc) return;

  const bool hasChapters = xtc->hasChapters() && !xtc->getChapters().empty();
  auto menu = makeUniqueNoThrow<XtcReaderMenuActivity>(renderer, mappedInput, xtc->getTitle(), currentPage,
                                                       xtc->getPageCount(), hasChapters, selectedPageTurnOption);
  if (!menu) {
    LOG_ERR("XTR", "OOM: XTC reader menu");
    return;
  }

  startActivityForResult(std::move(menu), [this](const ActivityResult& result) {
    const auto* menuResult = std::get_if<MenuResult>(&result.data);
    if (!menuResult) return;

    toggleAutoPageTurn(menuResult->pageTurnOption);
    if (!result.isCancelled && menuResult->action >= 0) {
      onReaderMenuConfirm(static_cast<XtcReaderMenuActivity::MenuAction>(menuResult->action));
    }
  });
}

void XtcReaderActivity::openChapterSelection() {
  if (xtc && xtc->hasChapters() && !xtc->getChapters().empty()) {
    startActivityForResult(std::make_unique<XtcReaderChapterSelectionActivity>(renderer, mappedInput, xtc, currentPage),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               currentPage = std::get<PageResult>(result.data).page;
                               requestUpdate();
                             }
                           });
  }
}

bool XtcReaderActivity::handleFormatInput() {
  if (!xtc) return false;

  if (automaticPageTurnActive && pageTurnDuration > 0 && millis() - lastPageTurnTime >= pageTurnDuration) {
    if (pageTurn(true)) {
      lastPageTurnTime = millis();
      requestUpdate();
    } else {
      automaticPageTurnActive = false;
      selectedPageTurnOption = 0;
    }
    return true;
  }

  // VesperUI/full-screen reader menu rather than jumping directly to chapters.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      ReaderUtils::isTouchMenuGesture(renderer, mappedInput, tapInputOrientation())) {
    openReaderMenu();
    return true;
  }
  return false;
}

void XtcReaderActivity::onReaderMenuConfirm(const XtcReaderMenuActivity::MenuAction action) {
  if (!xtc) return;

  switch (action) {
    case XtcReaderMenuActivity::MenuAction::GO_TO_PAGE: {
      requestUpdateAndWait();
      const uint32_t pageCount = xtc->getPageCount();
      if (pageCount == 0) return;
      const size_t maxLength = std::to_string(static_cast<unsigned long>(pageCount)).length();
      const uint32_t displayPage = std::min(currentPage + 1, pageCount);
      auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_GO_TO_PAGE),
                                                               std::to_string(static_cast<unsigned long>(displayPage)),
                                                               maxLength, InputType::Number);
      if (!keyboard) {
        LOG_ERR("XTR", "OOM: Go to Page keyboard");
        return;
      }

      startActivityForResult(std::move(keyboard), [this, pageCount](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }
        const auto* keyboardResult = std::get_if<KeyboardResult>(&result.data);
        if (!keyboardResult || keyboardResult->text.empty()) {
          requestUpdate();
          return;
        }

        uint32_t page = 0;
        for (const char ch : keyboardResult->text) {
          if (ch < '0' || ch > '9') return;
          page = page * 10 + static_cast<uint32_t>(ch - '0');
        }
        page = std::clamp(page, static_cast<uint32_t>(1), pageCount);
        currentPage = page - 1;
        requestUpdate();
      });
      break;
    }
    case XtcReaderMenuActivity::MenuAction::SELECT_CHAPTER:
      requestUpdateAndWait();
      openChapterSelection();
      break;
    case XtcReaderMenuActivity::MenuAction::AUTO_PAGE_TURN:
      // The menu popup already updated selectedPageTurnOption; the result
      // handler applies it before dispatching this action.
      break;
    case XtcReaderMenuActivity::MenuAction::IMMERSIVE_OPTIONS:
      startActivityForResult(std::make_unique<ImmersiveOptionsActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { openReaderMenu(); });
      break;
    case XtcReaderMenuActivity::MenuAction::GO_HOME:
      onGoHome();
      break;
    case XtcReaderMenuActivity::MenuAction::DELETE_CACHE: {
      const uint32_t page = currentPage;
      if (xtc->clearCache()) {
        xtc->setupCacheDir();
        currentPage = page;
        saveProgress();
      }
      requestUpdate();
      break;
    }
  }
}

void XtcReaderActivity::toggleAutoPageTurn(const uint8_t option) {
  selectedPageTurnOption = option;
  if (option == 0 || option >= PAGE_TURN_RATE_COUNT || PAGE_TURN_RATES[option] <= 0) {
    automaticPageTurnActive = false;
    pageTurnDuration = 0;
    return;
  }
  pageTurnDuration = (60UL * 1000UL) / static_cast<unsigned long>(PAGE_TURN_RATES[option]);
  lastPageTurnTime = millis();
  automaticPageTurnActive = true;
}

void XtcReaderActivity::applyDisplayOrientation() { ReaderUtils::applyOrientation(renderer, SETTINGS.orientation); }

void XtcReaderActivity::applyInitialOrientation() { ReaderUtils::applyOrientation(renderer, SETTINGS.orientation); }

GfxRenderer::Orientation XtcReaderActivity::tapInputOrientation() const {
  // Visual orientation is authoritative. The XTC tap-profile setting survives
  // only as the user's normal/inverted semantic preference in
  // detectTouchPageTurn(); it no longer defines a second coordinate system.
  return renderer.getOrientation();
}

void XtcReaderActivity::drawVesperFooter() const {
  if (!immersiveFooterVisible() || !xtc) return;
  const int total = static_cast<int>(xtc->getPageCount());
  const int current = std::min(static_cast<int>(currentPage) + 1, std::max(1, total));
  const int percent = total > 0 ? std::clamp((current * 100 + total / 2) / total, 0, 100) : 0;
  VesperReaderFooter::draw(renderer, {current, total, percent}, true);
}

void XtcReaderActivity::renderBook() {
  if (!xtc) {
    return;
  }

  renderPage();
  saveProgress();
}

XtcReaderActivity::StatusBarInfo XtcReaderActivity::getStatusBarInfo() const {
  const auto sb = SETTINGS.statusBarSpec();
  const int bookPageCount = static_cast<int>(xtc->getPageCount());
  const int bookPage = static_cast<int>(currentPage) + 1;
  std::string title = sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE ? xtc->getTitle() : "";

  if (!xtc->hasChapters()) {
    return StatusBarInfo{bookPage, bookPageCount, std::move(title)};
  }

  const auto& chapters = xtc->getChapters();
  const auto chapterIt = std::find_if(chapters.begin(), chapters.end(), [this](const xtc::ChapterInfo& chapter) {
    return currentPage >= chapter.startPage && currentPage <= chapter.endPage;
  });

  if (chapterIt == chapters.end() || chapterIt->endPage < chapterIt->startPage) {
    return StatusBarInfo{bookPage, bookPageCount, std::move(title)};
  }

  if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = chapterIt->name.empty() ? tr(STR_UNNAMED) : chapterIt->name;
  }

  return StatusBarInfo{static_cast<int>(currentPage - chapterIt->startPage) + 1,
                       static_cast<int>(chapterIt->endPage - chapterIt->startPage) + 1, std::move(title)};
}

void XtcReaderActivity::renderStatusBarOverlay(GfxRenderer& renderer, const StatusBarOverlayPosition position) const {
  const auto sb = SETTINGS.statusBarSpec();
  const bool drawBottom = sb.xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_BOTTOM &&
                          position == StatusBarOverlayPosition::Bottom;
  const bool drawTop = sb.xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_TOP &&
                       position == StatusBarOverlayPosition::Top;
  if (!drawBottom && !drawTop) {
    return;
  }

  const GfxRenderer::Orientation pageOrientation = renderer.getOrientation();
  const GfxRenderer::Orientation overlayOrientation = pageOrientation;

  const int statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (statusBarHeight <= 0) {
    return;
  }

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);

  int clearY;
  int paddingBottom = 0;
  if (position == StatusBarOverlayPosition::Bottom) {
    clearY = renderer.getScreenHeight() - orientedMarginBottom - statusBarHeight - 4;
    if (clearY < 0) {
      clearY = 0;
    }
  } else {
    clearY = orientedMarginTop;
    paddingBottom = renderer.getScreenHeight() - statusBarHeight - orientedMarginBottom - orientedMarginTop - 4;
  }
  const int clearHeight = position == StatusBarOverlayPosition::Bottom
                              ? renderer.getScreenHeight() - orientedMarginBottom - clearY
                              : statusBarHeight + 4;
  if (clearHeight > 0) {
    renderer.fillRect(0, clearY, renderer.getScreenWidth(), clearHeight, false);
  }

  const int pageCount = static_cast<int>(xtc->getPageCount());
  const int displayPage = static_cast<int>(currentPage) + 1;
  const float progress = pageCount > 0 ? (static_cast<float>(displayPage) * 100.0f) / pageCount : 0.0f;
  const auto pageInfo = getStatusBarInfo();
  GUI.drawStatusBar(renderer, progress, pageInfo.currentPage, pageInfo.pageCount, pageInfo.title, paddingBottom);

}

void XtcReaderActivity::renderPage() {
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  const uint16_t pageWidth = xtc->getPageWidth();
  const uint16_t pageHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();

  size_t pageBufferSize;
  if (bitDepth == 2) {
    pageBufferSize = static_cast<size_t>(pageWidth) * ((static_cast<size_t>(pageHeight) + 7) / 8) * 2;
  } else {
    pageBufferSize = ((pageWidth + 7) / 8) * pageHeight;
  }

  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(pageBufferSize));
  if (!pageBuffer) {
    LOG_ERR("XTR", "Failed to allocate page buffer (%lu bytes)", pageBufferSize);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_MEMORY_ERROR), true,
                              EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  const size_t bytesRead = xtc->loadPage(currentPage, pageBuffer, pageBufferSize);
  if (bytesRead == 0) {
    LOG_ERR("XTR", "Failed to load page %lu: bufferSize=%lu bitDepth=%u error=%s", currentPage, pageBufferSize,
            bitDepth, xtc::errorToString(xtc->getLastError()));
    free(pageBuffer);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_PAGE_LOAD_ERROR), true,
                              EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const float scale = std::min(static_cast<float>(screenW) / std::max<uint16_t>(1, pageWidth),
                               static_cast<float>(screenH) / std::max<uint16_t>(1, pageHeight));
  const int fitW = std::max(1, std::min(screenW, static_cast<int>(pageWidth * scale + 0.5f)));
  const int fitH = std::max(1, std::min(screenH, static_cast<int>(pageHeight * scale + 0.5f)));
  const int offsetX = (screenW - fitW) / 2;
  const int offsetY = (screenH - fitH) / 2;

  const size_t monoRowBytes = (pageWidth + 7) / 8;
  const size_t grayColBytes = (pageHeight + 7) / 8;
  const size_t grayPlaneSize = static_cast<size_t>(pageWidth) * grayColBytes;
  const uint8_t* plane1 = pageBuffer;
  const uint8_t* plane2 = bitDepth == 2 ? pageBuffer + grayPlaneSize : nullptr;

  const auto sourceValue = [&](const uint16_t x, const uint16_t y) -> uint8_t {
    if (bitDepth == 2) {
      const size_t colIndex = pageWidth - 1 - x;
      const size_t byteOffset = colIndex * grayColBytes + y / 8;
      const int bit = 7 - (y % 8);
      return static_cast<uint8_t>((((plane1[byteOffset] >> bit) & 1) << 1) | ((plane2[byteOffset] >> bit) & 1));
    }
    const size_t byteOffset = static_cast<size_t>(y) * monoRowBytes + x / 8;
    return ((pageBuffer[byteOffset] >> (7 - (x % 8))) & 1) ? 0 : 3;
  };

  const auto renderLevel = [&](const int pass) {
    for (int dy = 0; dy < fitH; ++dy) {
      const uint16_t sy = static_cast<uint16_t>(std::min<int>(pageHeight - 1, dy * pageHeight / fitH));
      for (int dx = 0; dx < fitW; ++dx) {
        const uint16_t sx = static_cast<uint16_t>(std::min<int>(pageWidth - 1, dx * pageWidth / fitW));
        const uint8_t value = sourceValue(sx, sy);
        bool pixel = false;
        if (pass == 0)
          pixel = value >= 1;
        else if (pass == 1)
          pixel = value == 1;
        else
          pixel = value == 1 || value == 2;
        if (pixel) renderer.drawPixel(offsetX + dx, offsetY + dy, pass == 0);
      }
    }
  };

  renderer.clearScreen();
  renderLevel(0);

  const bool vesper = SETTINGS.uiTheme == CrossPointSettings::UI_THEME::VESPERUI;
  if (vesper) {
    drawVesperFooter();
  } else if (SETTINGS.statusBarSpec().xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_TOP) {
    renderStatusBarOverlay(renderer, StatusBarOverlayPosition::Top);
  } else {
    renderStatusBarOverlay(renderer, StatusBarOverlayPosition::Bottom);
  }

  if (bitDepth == 2) {
    if (pagesUntilFullRefresh <= 1) {
      if (renderer.grayscaleCapabilities().base == HalDisplay::GrayscaleBase::Combined) {
        renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
      } else {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        renderer.preconditionGrayscale();
      }
      pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    } else {
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
      pagesUntilFullRefresh--;
    }

    renderer.clearScreen(0x00);
    renderLevel(1);
    if (vesper) drawVesperFooter();
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    renderLevel(2);
    if (vesper) drawVesperFooter();
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();

    renderer.clearScreen();
    renderLevel(0);
    if (vesper) drawVesperFooter();
    renderer.cleanupGrayscaleWithFrameBuffer();
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }

  free(pageBuffer);
  LOG_DBG("XTR", "Rendered page %lu/%lu (%u-bit, %dx%d fit)", currentPage + 1, xtc->getPageCount(), bitDepth, fitW,
          fitH);
}

bool XtcReaderActivity::pageTurn(bool isForward) {
  if (!xtc) return false;
  if (isForward) {
    if (currentPage < xtc->getPageCount()) {
      currentPage++;
      return true;
    }
  } else {
    if (currentPage > 0) {
      currentPage--;
      return true;
    }
  }
  return false;
}

bool XtcReaderActivity::skipPages(int amount) {
  if (!xtc) return false;
  int newPage = static_cast<int>(currentPage) + amount;
  if (newPage < 0) newPage = 0;
  if (newPage > static_cast<int>(xtc->getPageCount())) newPage = static_cast<int>(xtc->getPageCount());
  if (newPage != static_cast<int>(currentPage)) {
    currentPage = static_cast<uint32_t>(newPage);
    return true;
  }
  return false;
}

bool XtcReaderActivity::isAtEndOfBook() const { return xtc && currentPage >= xtc->getPageCount(); }

void XtcReaderActivity::onReturnFromEndOfBook() {
  if (xtc && xtc->getPageCount() > 0) {
    currentPage = xtc->getPageCount() - 1;
  } else {
    currentPage = 0;
  }
}

void XtcReaderActivity::saveProgress() const {
  if (!xtc) return;
  uint8_t data[4];
  data[0] = currentPage & 0xFF;
  data[1] = (currentPage >> 8) & 0xFF;
  data[2] = (currentPage >> 16) & 0xFF;
  data[3] = (currentPage >> 24) & 0xFF;
  if (!ProgressFile::writeAtomic(xtc->getCachePath(), data, sizeof(data))) {
    LOG_ERR("XTC", "Failed to save progress: page %lu", currentPage);
  }
}

void XtcReaderActivity::loadProgress() {
  if (!xtc) return;
  HalFile f;
  if (Storage.openFileForRead("XTC", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
      if (currentPage >= xtc->getPageCount() && xtc->getPageCount() > 0) {
        currentPage = xtc->getPageCount() - 1;
      }
      LOG_DBG("XTC", "Loaded progress: page %lu/%lu", currentPage + 1, xtc->getPageCount());
    }
  }
}

ScreenshotInfo XtcReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Xtc;
  if (xtc) {
    const std::string t = xtc->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
    const uint32_t pageCount = xtc->getPageCount();
    info.totalPages = pageCount;
    uint32_t clampedPage = (pageCount > 0 && currentPage >= pageCount) ? pageCount - 1 : currentPage;
    info.progressPercent = pageCount > 0 ? xtc->calculateProgress(clampedPage) : 0;
    info.currentPage = static_cast<int>(clampedPage) + 1;
  } else {
    info.currentPage = currentPage + 1;
  }
  return info;
}
