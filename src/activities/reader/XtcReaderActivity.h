#pragma once

#include <Xtc.h>

#include <memory>
#include <string>

#include "ReaderActivity.h"
#include "XtcReaderMenuActivity.h"

class XtcReaderActivity final : public ReaderActivity {
  std::shared_ptr<Xtc> xtc;
  uint32_t currentPage = 0;
  bool automaticPageTurnActive = false;
  uint8_t selectedPageTurnOption = 0;
  unsigned long lastPageTurnTime = 0;
  unsigned long pageTurnDuration = 0;

  enum class StatusBarOverlayPosition { Bottom, Top };
  struct StatusBarInfo {
    int currentPage;
    int pageCount;
    std::string title;
  };

  void renderPage();
  void openReaderMenu();
  void openChapterSelection();
  void onReaderMenuConfirm(XtcReaderMenuActivity::MenuAction action);
  void toggleAutoPageTurn(uint8_t option);
  void renderStatusBarOverlay(GfxRenderer& renderer, StatusBarOverlayPosition position) const;
  StatusBarInfo getStatusBarInfo() const;
  void saveProgress() const;
  void loadProgress();

  bool loadBook() override;
  std::string getBookTitle() const override { return xtc ? xtc->getTitle() : ""; }
  std::string getBookAuthor() const override { return xtc ? xtc->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return xtc ? xtc->getThumbBmpPath() : ""; }
  bool handleFormatInput() override;
  void renderBook() override;
  void applyInitialOrientation() override;
  GfxRenderer::Orientation tapInputOrientation() const override;

 public:
  explicit XtcReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("XtcReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~XtcReaderActivity() override = default;

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
