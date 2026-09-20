#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/themes/vesper/VesperHomeLayout.h"
#include "components/themes/vesper/VesperTheme.h"
#include "fontIds.h"

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // File Browser, Library, File transfer, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsServers) {
    count++;
  }
  return count;
}

bool HomeActivity::usesVesperLibraryPane() const {
  return SETTINGS.uiTheme == CrossPointSettings::UI_THEME::VESPERUI &&
         SETTINGS.interfaceOrientation != CrossPointSettings::UI_PORTRAIT;
}

bool HomeActivity::loadVesperLibraryWindow() {
  if (!usesVesperLibraryPane() || recentBooks.empty()) return false;

  RecentBook hero = recentBooks.front();
  library::LibraryIndexFile index;
  if (!index.open(library::libraryIndexPath())) {
    library::BuildStats stats;
    if (!library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0) ||
        !index.open(library::libraryIndexPath())) {
      vesperLibraryTotal = 0;
      return false;
    }
  }

  vesperLibraryTotal = static_cast<int>(index.bookCount());
  const int maxOffset = std::max(0, vesperLibraryTotal - VESPER_LIBRARY_VISIBLE_ROWS);
  vesperLibraryOffset = std::clamp(vesperLibraryOffset, 0, maxOffset);

  std::vector<RecentBook> window;
  window.reserve(1 + VESPER_LIBRARY_VISIBLE_ROWS);
  window.push_back(std::move(hero));

  for (int row = vesperLibraryOffset;
       row < vesperLibraryTotal && static_cast<int>(window.size()) <= VESPER_LIBRARY_VISIBLE_ROWS; ++row) {
    const uint16_t ordinal = index.ordinalForRow(library::SortOrder::TitleAsc, static_cast<uint16_t>(row));
    if (ordinal == 0xFFFF) continue;

    library::ClixRecord record{};
    std::string path;
    if (!index.readRecord(ordinal, record) || !index.readPath(record, path)) continue;
    if (path == window.front().path) continue;

    std::string title;
    if (!index.readTitle(record, title) || title.empty()) {
      if (!index.readName(record, title)) continue;
      const size_t dot = title.find_last_of('.');
      if (dot != std::string::npos && dot > 0) title.erase(dot);
    }

    std::string author;
    index.readAuthor(record, author);
    window.push_back(RecentBook{std::move(path), std::move(title), std::move(author), ""});
  }

  index.close();
  recentBooks = std::move(window);
  return recentBooks.size() > 1;
}

void HomeActivity::scrollVesperLibrary(const int delta) {
  if (!usesVesperLibraryPane() || vesperLibraryTotal <= VESPER_LIBRARY_VISIBLE_ROWS || delta == 0) return;
  const int maxOffset = std::max(0, vesperLibraryTotal - VESPER_LIBRARY_VISIBLE_ROWS);
  const int next = std::clamp(vesperLibraryOffset + delta, 0, maxOffset);
  if (next == vesperLibraryOffset) return;

  vesperLibraryOffset = next;
  if (!loadVesperLibraryWindow()) return;

  selectorIndex = recentBooks.size() > 1 ? 1 : 0;
  freeCoverBuffer();
  coverRendered = false;
  vesperGrayCoversOnPanel = false;
  recentsLoaded = true;
  recentsLoading = false;
  requestUpdate(true);
}

void HomeActivity::toggleVesperInterfaceOrientation() {
  SETTINGS.interfaceOrientation = SETTINGS.interfaceOrientation == CrossPointSettings::UI_PORTRAIT
                                      ? CrossPointSettings::UI_LANDSCAPE_CW
                                      : CrossPointSettings::UI_PORTRAIT;
  SETTINGS.saveToFile();
  applyDisplayOrientation();

  vesperLibraryOffset = 0;
  vesperLibraryTotal = 0;
  freeCoverBuffer();
  coverRendered = false;
  vesperGrayCoversOnPanel = false;
  recentsLoaded = false;
  recentsLoading = false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);
  selectorIndex = 0;
  requestUpdate(true);
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }

  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::VESPERUI && !recentBooks.empty()) {
    loadVesperProgress(recentBooks.front());
    if (usesVesperLibraryPane()) loadVesperLibraryWindow();
  }
}

void HomeActivity::loadVesperProgress(RecentBook& book) {
  book.progressPercent = -1;
  book.progressCurrent = 0;
  book.progressTotal = 0;
  if (!FsHelpers::hasXtcExtension(book.path)) return;

  Xtc xtc(book.path, "/.crosspoint");
  if (!xtc.load()) return;

  const uint32_t total = xtc.getPageCount();
  if (total == 0) return;

  uint32_t current = 0;
  HalFile progress;
  const std::string progressPath = xtc.getCachePath() + "/progress.bin";
  if (Storage.openFileForRead("HOME", progressPath, progress)) {
    uint8_t bytes[4] = {};
    if (progress.read(bytes, sizeof(bytes)) == static_cast<int>(sizeof(bytes))) {
      current = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
                (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
    }
    progress.close();
  }
  current = std::min(current, total - 1);

  book.progressCurrent = current + 1;
  book.progressTotal = total;
  book.progressPercent = static_cast<int>(xtc.calculateProgress(current));
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;

  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::VESPERUI) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect band{0, metrics.homeTopPadding, renderer.getScreenWidth(), metrics.homeCoverTileHeight};
    const auto layout = VesperHome::bookLayout(band, static_cast<int>(recentBooks.size()));

    for (size_t i = 0; i < recentBooks.size(); i++) {
      RecentBook& book = recentBooks[i];
      const Rect target = VesperHome::coverRect(layout, static_cast<int>(i));
      if (target.width <= 0 || target.height <= 0) continue;

      const std::string fallbackPath = book.coverBmpPath;
      std::string grayPath;
      bool ready = false;

      if (FsHelpers::hasEpubExtension(book.path)) {
        Epub epub(book.path, "/.crosspoint");
        if (epub.load(false, true)) {
          grayPath = epub.getVesperThumbBmpPath(target.width, target.height);
          ready = Storage.exists(grayPath.c_str());
          if (!ready) {
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / std::max<size_t>(1, recentBooks.size())));
            ready = epub.generateVesperThumbBmp(target.width, target.height);
          }
        }
      } else if (FsHelpers::hasXtcExtension(book.path)) {
        Xtc xtc(book.path, "/.crosspoint");
        if (xtc.load()) {
          grayPath = xtc.getVesperThumbBmpPath(target.width, target.height);
          ready = Storage.exists(grayPath.c_str());
          if (!ready) {
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / std::max<size_t>(1, recentBooks.size())));
            ready = xtc.generateVesperThumbBmp(target.width, target.height);
          }
        }
      }

      book.coverBmpPath = ready ? grayPath : fallbackPath;
      progress++;
    }

    // Any old snapshot contains the 1-bit placeholders. Force a fresh static
    // Vesper frame before the grayscale plane pass.
    freeCoverBuffer();
    coverRendered = false;
    vesperGrayCoversOnPanel = false;
    recentsLoaded = true;
    recentsLoading = false;
    requestUpdate();
    return;
  }

  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::promoteRecentToHero(const int index) {
  if (index <= 0 || index >= static_cast<int>(recentBooks.size())) return;

  if (usesVesperLibraryPane()) {
    const RecentBook selected = recentBooks[static_cast<size_t>(index)];
    RecentBook hero = RECENT_BOOKS.getDataFromBook(selected.path);
    if (hero.title.empty()) hero.title = selected.title;
    if (hero.author.empty()) hero.author = selected.author;
    if (hero.path.empty()) hero.path = selected.path;
    recentBooks[0] = std::move(hero);
    loadVesperProgress(recentBooks[0]);
    loadVesperLibraryWindow();
  } else {
    std::swap(recentBooks[0], recentBooks[static_cast<size_t>(index)]);
    loadVesperProgress(recentBooks[0]);
  }
  selectorIndex = 0;

  // The promoted hero needs the large Vesper grayscale thumbnail; side-list
  // rows deliberately have no covers.
  freeCoverBuffer();
  coverRendered = false;
  vesperGrayCoversOnPanel = false;
  recentsLoaded = false;
  recentsLoading = false;
  requestUpdate();
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  vesperGrayCoversOnPanel = false;
  vesperLibraryOffset = 0;
  vesperLibraryTotal = 0;
  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, hasOpdsServers);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  vesperGrayCoversOnPanel = false;
  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

bool HomeActivity::drawVesperCoverArt() {
  if (recentBooks.empty()) return false;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect band{0, metrics.homeTopPadding, renderer.getScreenWidth(), metrics.homeCoverTileHeight};
  const auto layout = VesperHome::bookLayout(band, static_cast<int>(recentBooks.size()));

  bool any = false;
  for (size_t i = 0; i < recentBooks.size(); i++) {
    const Rect target = VesperHome::coverRect(layout, static_cast<int>(i));
    any |= VesperTheme::drawBookCover(renderer, recentBooks[i], target);
  }
  return any;
}

bool HomeActivity::renderVesperGrayCovers(const HalDisplay::RefreshMode baseRefresh) {
  if (vesperGrayCoversOnPanel || recentBooks.empty()) return false;

  // Prefer a combined absolute pass: base + both gray planes are staged first
  // and the panel activates only once. X4 Pro supports Direct on compatible
  // panel revisions; SSD1677-style panels may expose Absolute as Combined.
  HalDisplay::GrayscaleMode mode = HalDisplay::GrayscaleMode::Direct;
  auto caps = renderer.grayscaleCapabilities(mode);
  if (!caps.supported() || caps.base != HalDisplay::GrayscaleBase::Combined) {
    mode = HalDisplay::GrayscaleMode::Absolute;
    caps = renderer.grayscaleCapabilities(mode);
  }

  const bool combined = caps.supported() && caps.base == HalDisplay::GrayscaleBase::Combined;
  if (combined && coverBufferStored && coverBuffer) {
    if (!renderer.displayGrayscaleBase(mode, baseRefresh)) return false;

    // Absolute planes must include the B/W UI outside the covers. Start each
    // plane from the already-rendered B/W framebuffer and only replace the
    // cover pixels with their 2-bit levels.
    renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    if (!drawVesperCoverArt()) {
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
      return false;
    }
    renderer.copyGrayscaleLsbBuffers();

    renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    drawVesperCoverArt();
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
    renderer.cleanupGrayscaleWithFrameBuffer();
    vesperGrayCoversOnPanel = true;
    return true;
  }

  // Compatibility fallback for panels without a combined absolute/direct
  // waveform. This is still one fewer visible Home rewrite than before:
  // displayGrayscaleBase() is the only B/W base activation.
  const auto overlayCaps = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Overlay);
  if (!overlayCaps.supported()) return false;

  if (!renderer.storeBwBuffer()) {
    LOG_ERR("HOME", "OOM: VesperUI grayscale cover framebuffer store");
    return false;
  }

  renderer.displayGrayscaleBase(baseRefresh);

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!drawVesperCoverArt()) {
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.restoreBwBuffer();
    renderer.cleanupGrayscaleWithFrameBuffer();
    return true;
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  drawVesperCoverArt();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.restoreBwBuffer();
  renderer.cleanupGrayscaleWithFrameBuffer();
  vesperGrayCoversOnPanel = true;
  return true;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();

  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::VESPERUI &&
      SETTINGS.longPressButtonBehavior == CrossPointSettings::ORIENTATION_CHANGE) {
    int longX = 0;
    int longY = 0;
    if (mappedInput.wasScreenLongPress(longX, longY)) {
      toggleVesperInterfaceOrientation();
      return;
    }
  }

  auto activateSelection = [this] {
    if (selectorIndex < static_cast<int>(recentBooks.size())) {
      if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::VESPERUI && selectorIndex > 0) {
        promoteRecentToHero(selectorIndex);
        return;
      }
      onSelectBook(recentBooks[static_cast<size_t>(selectorIndex)].path);
      return;
    }
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    switch (indexToMenuItem(menuIndex, hasOpdsServers)) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::LIBRARY:
        onLibraryOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:
        onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      default:
        break;
    }
  };

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (usesVesperLibraryPane() &&
      (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down)) {
    constexpr int scrollStep = VESPER_LIBRARY_VISIBLE_ROWS - 2;
    scrollVesperLibrary(swipe == MappedInputManager::SwipeDir::Up ? scrollStep : -scrollStep);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  // Back is otherwise unused on the home menu: open the most recently read
  // book directly (recentBooks is most-recent-first and already pruned of
  // files missing from the SD card).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && !recentBooks.empty()) {
    onSelectBook(recentBooks[0].path);
    return;
  }

  const int menuTop = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int renderedMenuCount =
      menuCount - (metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size()));

  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::VESPERUI) {
    const Rect bookBand{0, metrics.homeTopPadding, renderer.getScreenWidth(), metrics.homeCoverTileHeight};
    const auto layout = VesperHome::bookLayout(bookBand, static_cast<int>(recentBooks.size()));
    const Rect navAvailable{0, menuTop, renderer.getScreenWidth(), std::max(0, renderer.getScreenHeight() - menuTop)};

    int tx = 0;
    int ty = 0;
    if (mappedInput.isScreenTouchHeld(tx, ty)) {
      int touchedIndex = VesperHome::hitBook(layout, static_cast<int>(recentBooks.size()), tx, ty);
      if (touchedIndex < 0) {
        const int nav = VesperHome::hitNav(navAvailable, renderedMenuCount, tx, ty);
        if (nav >= 0) touchedIndex = static_cast<int>(recentBooks.size()) + nav;
      }
      if (touchedIndex >= 0 && selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
    }

    if (mappedInput.wasScreenTapped(tx, ty)) {
      int touchedIndex = VesperHome::hitBook(layout, static_cast<int>(recentBooks.size()), tx, ty);
      if (touchedIndex < 0) {
        const int nav = VesperHome::hitNav(navAvailable, renderedMenuCount, tx, ty);
        if (nav >= 0) touchedIndex = static_cast<int>(recentBooks.size()) + nav;
      }
      if (touchedIndex >= 0) {
        selectorIndex = touchedIndex;
        activateSelection();
        return;
      }
    }
  } else {
    const int coverColumnCount = std::max(1, metrics.homeRecentBooksCount);
    const int recentCount = std::min(static_cast<int>(recentBooks.size()), coverColumnCount);
    const int coverColumnWidth = (renderer.getScreenWidth() - 2 * metrics.contentSidePadding) / coverColumnCount;
    int touchedBook = -1;
    const auto coverTouch = mappedInput.colTouch(
        touchedBook, metrics.contentSidePadding, coverColumnWidth, recentCount, metrics.homeTopPadding,
        metrics.homeTopPadding + metrics.homeCoverTileHeight, coverColumnWidth);
    if (coverTouch != MappedInputManager::RowTouch::None) {
      if (coverTouch == MappedInputManager::RowTouch::Down) {
        if (selectorIndex != touchedBook) {
          selectorIndex = touchedBook;
          requestUpdate();
        }
      } else {
        selectorIndex = touchedBook;
        activateSelection();
      }
      return;
    }

    int menuRow = -1;
    // Row height from the theme, not the metrics table: RoundedRaff draws
    // font-derived rows and the touch grid must match the visuals exactly.
    const int menuRowHeight = GUI.getMenuRowHeight(renderer);
    const auto menuTouch = mappedInput.rowTouch(menuRow, menuTop, menuRowHeight + metrics.menuSpacing,
                                                renderedMenuCount, 0, INT32_MAX, menuRowHeight);
    if (menuTouch != MappedInputManager::RowTouch::None) {
      const int touchedIndex =
          metrics.homeContinueReadingInMenu ? menuRow : menuRow + static_cast<int>(recentBooks.size());
      if (menuTouch == MappedInputManager::RowTouch::Down) {
        if (selectorIndex != touchedIndex) {
          selectorIndex = touchedIndex;
          requestUpdate();
        }
      } else {
        selectorIndex = touchedIndex;
        activateSelection();
      }
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  // Band spans topPadding..homeTopPadding: the cover tile starts at the fixed
  // homeTopPadding, so the height must shrink by topPadding or the band (and a
  // centered title, e.g. RoundedRaff's book title) sinks into the tile.
  const char* homeTitle =
      SETTINGS.uiTheme == CrossPointSettings::UI_THEME::VESPERUI
          ? tr(STR_HOME_SHORTCUT)
          : (metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding - metrics.topPadding},
                 homeTitle);

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
  // instead of the 48 KB full framebuffer the previous bind captured.
  const bool coverGeometryChanged = coverRectX != 0 || coverRectY != metrics.homeTopPadding ||
                                    coverRectW != pageWidth || coverRectH != metrics.homeCoverTileHeight;
  coverRectX = 0;
  coverRectY = metrics.homeTopPadding;
  coverRectW = pageWidth;
  coverRectH = metrics.homeCoverTileHeight;
  if (coverGeometryChanged) vesperGrayCoversOnPanel = false;

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_LIBRARY), tr(STR_FILE_TRANSFER),
                                        tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Library, Transfer, Settings};

  if (hasOpdsServers) {
    menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 2, Blocks);
  }

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                         metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  const auto labels = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  const HalDisplay::RefreshMode homeRefresh =
      cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  bool displayed = false;
  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::VESPERUI && recentsLoaded && !recentsLoading) {
    displayed = renderVesperGrayCovers(homeRefresh);
  }
  if (!displayed) renderer.displayBuffer(homeRefresh);

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onLibraryOpen() { activityManager.goToLibrary(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
