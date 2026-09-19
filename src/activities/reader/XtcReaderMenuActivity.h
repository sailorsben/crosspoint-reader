#pragma once

#include <I18n.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class XtcReaderMenuActivity final : public UiListActivity {
 public:
  enum class MenuAction { GO_TO_PAGE, SELECT_CHAPTER, AUTO_PAGE_TURN, GO_HOME, DELETE_CACHE };

  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  explicit XtcReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                 uint32_t currentPage, uint32_t totalPages, bool hasChapters,
                                 uint8_t selectedPageTurnOption);

  void render(RenderLock&&) override;
  bool handleHomeGesture() override;

 private:
  static constexpr size_t MAX_MENU_ITEMS = 5;

  void buildMenuItems(bool hasChapters);
  void buildMenuRows();
  void closeCancelled();
  Rect menuRect() const;

  int listCount() const override { return static_cast<int>(menuItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void drawChrome() override;

  std::vector<MenuItem> menuItems;
  std::array<freeink::ui::ListItem, MAX_MENU_ITEMS> menuRows{};
  OptionPopup optionPopup;
  std::string title;
  uint32_t currentPage = 0;
  uint32_t totalPages = 0;
  uint8_t selectedPageTurnOption = 0;
  const std::array<const char*, 5> pageTurnLabels = {I18N.get(StrId::STR_STATE_OFF), "1", "3", "6", "12"};
};
