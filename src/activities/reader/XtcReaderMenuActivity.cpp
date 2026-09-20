#include "XtcReaderMenuActivity.h"

#include <GfxRenderer.h>

#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

XtcReaderMenuActivity::XtcReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                             const uint32_t currentPage, const uint32_t totalPages,
                                             const bool hasChapters, const uint8_t selectedPageTurnOption)
    : UiListActivity("XtcReaderMenu", renderer, mappedInput),
      title(std::move(title)),
      currentPage(currentPage),
      totalPages(totalPages),
      selectedPageTurnOption(selectedPageTurnOption) {
  buildMenuItems(hasChapters);
  buildMenuRows();
}

void XtcReaderMenuActivity::buildMenuItems(const bool hasChapters) {
  menuItems.clear();
  menuItems.reserve(MAX_MENU_ITEMS);
  menuItems.push_back({MenuAction::GO_TO_PAGE, StrId::STR_GO_TO_PAGE});
  if (hasChapters) menuItems.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  menuItems.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN});
  menuItems.push_back({MenuAction::IMMERSIVE_OPTIONS, StrId::STR_IMMERSIVE_OPTIONS});
  menuItems.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  menuItems.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
}

void XtcReaderMenuActivity::buildMenuRows() {
  for (size_t i = 0; i < menuItems.size(); i++) {
    fui::ListItem item;
    item.label = I18N.get(menuItems[i].labelId);
    item.actionValue = static_cast<int16_t>(i);
    menuRows[i] = item;
  }
}

void XtcReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1, 0, selectedPageTurnOption};
  setResult(std::move(result));
  finish();
}

bool XtcReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void XtcReaderMenuActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(menuItems.size()) || optionPopup.isActive()) return;
  app.clearTapFlash();
  nav.selected = index;

  if (menuItems[static_cast<size_t>(index)].action == MenuAction::AUTO_PAGE_TURN) {
    optionPopup.show(I18N.get(StrId::STR_AUTO_TURN_PAGES_PER_MIN), pageTurnLabels.data(),
                     static_cast<int>(pageTurnLabels.size()), selectedPageTurnOption, [this](const int option) {
                       selectedPageTurnOption = static_cast<uint8_t>(option);
                       requestUpdate();
                     });
    requestUpdate();
    return;
  }

  setResult(MenuResult{static_cast<int>(menuItems[static_cast<size_t>(index)].action), 0, selectedPageTurnOption});
  finish();
}

bool XtcReaderMenuActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool XtcReaderMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    closeCancelled();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }
  return false;
}

Rect XtcReaderMenuActivity::menuRect() const {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const bool landscape = screenW > screenH;

  if (landscape) {
    constexpr int width = 640;
    constexpr int height = 366;
    return Rect{(screenW - width) / 2, (screenH - height) / 2, width, height};
  }

  constexpr int marginX = 20;
  constexpr int marginY = 60;
  return Rect{marginX, marginY, screenW - marginX * 2, screenH - marginY * 2};
}

void XtcReaderMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect modal = menuRect();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int inset = screenW > screenH ? 16 : 14;
  const int headerHeight = screenW > screenH ? 50 : 58;

  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(modal.y + headerHeight), static_cast<int16_t>(screenW - (modal.x + modal.width) + inset),
      static_cast<int16_t>(screenH - (modal.y + modal.height) + inset), static_cast<int16_t>(modal.x + inset)});

  char progress[32];
  snprintf(progress, sizeof(progress), "%lu / %lu", static_cast<unsigned long>(currentPage + 1),
           static_cast<unsigned long>(totalPages));
  fui::TextStyle summary = screen.theme().smallText;
  summary.align = fui::TextAlign::Center;
  screen.target().text(screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight), metrics.verticalSpacing), progress,
                       summary);

  for (size_t i = 0; i < menuItems.size(); i++) {
    if (menuItems[i].action == MenuAction::AUTO_PAGE_TURN) {
      menuRows[i].value = pageTurnLabels[selectedPageTurnOption];
    }
  }

  fui::ListProps props;
  props.items = menuRows.data();
  props.count = static_cast<uint16_t>(menuItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  syncListViewport(screen, props);
  screen.list(props);
}

void XtcReaderMenuActivity::drawChrome() {
  const Rect modal = menuRect();
  const bool landscape = renderer.getScreenWidth() > renderer.getScreenHeight();
  const int headerHeight = landscape ? 50 : 58;

  // True overlay: preserve the book page around the menu and only paint the
  // modal itself. The page framebuffer is still present when this activity is
  // pushed, even though the logical UI orientation may have changed.
  renderer.fillRect(modal.x, modal.y, modal.width, modal.height, false);
  renderer.drawRect(modal.x, modal.y, modal.width, modal.height, 2, true);

  const char* header = tr(STR_READER_MENU);
  const int headerWidth = renderer.getTextWidth(UI_12_FONT_ID, header, EpdFontFamily::BOLD);
  const int headerX = modal.x + (modal.width - headerWidth) / 2;
  const int headerY = modal.y + (headerHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2 - (landscape ? 1 : 0);
  renderer.drawText(UI_12_FONT_ID, headerX, headerY, header, true, EpdFontFamily::BOLD);
  renderer.drawLine(modal.x + 12, modal.y + headerHeight - 1, modal.x + modal.width - 13, modal.y + headerHeight - 1);
}

void XtcReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  drawChrome();
  renderUi();
  renderer.displayBuffer();
}
