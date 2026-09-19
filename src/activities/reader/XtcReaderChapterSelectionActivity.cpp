#include "XtcReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <vector>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

XtcReaderChapterSelectionActivity::XtcReaderChapterSelectionActivity(GfxRenderer& renderer,
                                                                     MappedInputManager& mappedInput,
                                                                     const std::shared_ptr<Xtc>& xtc,
                                                                     const uint32_t currentPage)
    : UiListActivity("XtcReaderChapterSelection", renderer, mappedInput), xtc(xtc), currentPage(currentPage) {}

int XtcReaderChapterSelectionActivity::findChapterIndexForPage(const uint32_t page) const {
  if (!xtc) {
    return 0;
  }

  const auto& chapters = xtc->getChapters();
  const auto chapter = std::find_if(chapters.begin(), chapters.end(), [page](const auto& candidate) {
    return page >= candidate.startPage && page <= candidate.endPage;
  });
  return chapter != chapters.end() ? static_cast<int>(chapter - chapters.begin()) : 0;
}

void XtcReaderChapterSelectionActivity::onEnter() {
  UiListActivity::onEnter();

  if (!xtc) {
    return;
  }

  buildRowItems();

  // Open on the current chapter, which may sit past the first page; the first
  // screen build pulls the viewport to it (ListNav follow-on-build).
  nav.selected = findChapterIndexForPage(currentPage);
}

// Derives rowItems from the xtc's chapters. Called once from onEnter() since
// chapters are static for this screen's lifetime.
void XtcReaderChapterSelectionActivity::buildRowItems() {
  const auto& chapters = xtc->getChapters();
  rowItems.clear();
  rowItems.reserve(chapters.size());
  for (const auto& chapter : chapters) {
    fui::ListItem item;
    item.label = chapter.name.empty() ? tr(STR_UNNAMED) : chapter.name.c_str();
    item.actionValue = static_cast<int16_t>(rowItems.size());
    rowItems.push_back(item);
  }
}

void XtcReaderChapterSelectionActivity::activateIndex(const int index) {
  // The activated row leaves this screen (finish); a lingering flash would
  // gray an unrelated element on the next render.
  app.clearTapFlash();
  const auto& chapters = xtc->getChapters();
  if (!chapters.empty() && index >= 0 && index < static_cast<int>(chapters.size())) {
    nav.selected = index;
    setResult(PageResult{chapters[index].startPage});
    finish();
  }
}

bool XtcReaderChapterSelectionActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }

  if (!xtc) {
    return true;  // no book: nothing else to route this pass
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

Rect XtcReaderChapterSelectionActivity::overlayRect() const {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  if (screenW > screenH) {
    const int width = std::min(720, screenW - 40);
    const int height = std::min(400, screenH - 48);
    return Rect{(screenW - width) / 2, (screenH - height) / 2, width, height};
  }

  const int width = screenW - 28;
  const int height = screenH - 90;
  return Rect{14, 45, width, height};
}

void XtcReaderChapterSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect modal = overlayRect();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int headerHeight = screenW > screenH ? 50 : 58;
  const int inset = 14;

  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(modal.y + headerHeight), static_cast<int16_t>(screenW - (modal.x + modal.width) + inset),
      static_cast<int16_t>(screenH - (modal.y + modal.height) + inset), static_cast<int16_t>(modal.x + inset)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (!xtc) {
    return;
  }
  if (rowItems.empty()) {
    screen.centeredText(tr(STR_NO_CHAPTERS), screen.theme().bodyText);
    return;
  }

  // rowItems is built once in onEnter() (see buildRowItems()) and reused
  // here on every repaint.
  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void XtcReaderChapterSelectionActivity::drawChrome() {
  const Rect modal = overlayRect();
  const bool landscape = renderer.getScreenWidth() > renderer.getScreenHeight();
  const int headerHeight = landscape ? 50 : 58;

  renderer.fillRect(modal.x, modal.y, modal.width, modal.height, false);
  renderer.drawRect(modal.x, modal.y, modal.width, modal.height, 2, true);

  const char* header = tr(STR_SELECT_CHAPTER);
  const int headerWidth = renderer.getTextWidth(UI_12_FONT_ID, header, EpdFontFamily::BOLD);
  const int headerX = modal.x + (modal.width - headerWidth) / 2;
  const int headerY = modal.y + (headerHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, headerX, headerY, header, true, EpdFontFamily::BOLD);
  renderer.drawLine(modal.x + 12, modal.y + headerHeight - 1, modal.x + modal.width - 13, modal.y + headerHeight - 1);
}

void XtcReaderChapterSelectionActivity::render(RenderLock&&) {
  drawChrome();
  renderUi();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
