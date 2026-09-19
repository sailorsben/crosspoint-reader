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

void XtcReaderMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

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
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight},
                 title.empty() ? tr(STR_READER_MENU) : title.c_str());
}

void XtcReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  renderer.clearScreen();
  drawChrome();
  renderUi();
  drawFooter();
  renderer.displayBuffer();
}
