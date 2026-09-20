#include "ImmersiveOptionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "fontIds.h"

Rect ImmersiveOptionsActivity::modalRect() const {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const bool landscape = sw > sh;
  const int width = landscape ? std::min(590, sw - 50) : sw - 28;
  const int height = std::min(350, sh - 40);
  return Rect{(sw - width) / 2, (sh - height) / 2, width, height};
}

const char* ImmersiveOptionsActivity::rowLabel(const int row) const {
  switch (row) {
    case 0:
      return tr(STR_IMMERSION_MODE);
    case 1:
      return tr(STR_PAGES);
    case 2:
      return tr(STR_PROGRESS_BAR);
    case 3:
      return tr(STR_PERCENTAGE);
    case 4:
      return tr(STR_BATTERY);
    default:
      return "";
  }
}

bool ImmersiveOptionsActivity::rowValue(const int row) const {
  switch (row) {
    case 0:
      return SETTINGS.immersiveMode != 0;
    case 1:
      return SETTINGS.immersiveShowPages != 0;
    case 2:
      return SETTINGS.immersiveShowProgress != 0;
    case 3:
      return SETTINGS.immersiveShowPercentage != 0;
    case 4:
      return SETTINGS.immersiveShowBattery != 0;
    default:
      return false;
  }
}

void ImmersiveOptionsActivity::toggleRow(const int row) {
  switch (row) {
    case 0:
      SETTINGS.immersiveMode ^= 1;
      break;
    case 1:
      SETTINGS.immersiveShowPages ^= 1;
      break;
    case 2:
      SETTINGS.immersiveShowProgress ^= 1;
      break;
    case 3:
      SETTINGS.immersiveShowPercentage ^= 1;
      break;
    case 4:
      SETTINGS.immersiveShowBattery ^= 1;
      break;
    default:
      return;
  }
  SETTINGS.saveToFile();
  requestUpdate(true);
}

void ImmersiveOptionsActivity::drawSwitch(const Rect& rect, const bool on) const {
  constexpr int inset = 3;
  const int radius = rect.height / 2;
  if (on) {
    renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, radius, Color::Black);
  } else {
    renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 2, radius, true);
  }

  const int knobSize = rect.height - inset * 2;
  const int knobX = on ? rect.x + rect.width - inset - knobSize : rect.x + inset;
  const int knobY = rect.y + inset;
  renderer.fillRoundedRect(knobX, knobY, knobSize, knobSize, knobSize / 2, on ? Color::White : Color::Black);
}

void ImmersiveOptionsActivity::close() {
  ActivityResult result;
  result.isCancelled = false;
  setResult(std::move(result));
  finish();
}

bool ImmersiveOptionsActivity::handleHomeGesture() {
  close();
  return true;
}

void ImmersiveOptionsActivity::loop() {
  const Rect modal = modalRect();
  constexpr int headerH = 54;
  const int rowsTop = modal.y + headerH + 8;
  const int rowH = (modal.height - headerH - 16) / ROW_COUNT;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    close();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    toggleRow(selectedRow);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    selectedRow = (selectedRow + ROW_COUNT - 1) % ROW_COUNT;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::NavNext)) {
    selectedRow = (selectedRow + 1) % ROW_COUNT;
    requestUpdate();
    return;
  }

  int row = -1;
  const auto touch = mappedInput.rowTouch(row, rowsTop, rowH, ROW_COUNT, modal.x + 10, modal.x + modal.width - 10, rowH);
  if (touch == MappedInputManager::RowTouch::Down) {
    if (row >= 0 && row != selectedRow) {
      selectedRow = row;
      requestUpdate();
    }
  } else if (touch == MappedInputManager::RowTouch::Tap && row >= 0) {
    selectedRow = row;
    toggleRow(row);
  }
}

void ImmersiveOptionsActivity::render(RenderLock&&) {
  const Rect modal = modalRect();
  constexpr int headerH = 54;
  const int rowsTop = modal.y + headerH + 8;
  const int rowH = (modal.height - headerH - 16) / ROW_COUNT;

  renderer.fillRect(modal.x, modal.y, modal.width, modal.height, false);
  renderer.drawRect(modal.x, modal.y, modal.width, modal.height, 2, true);

  const char* title = tr(STR_IMMERSIVE_OPTIONS);
  const int titleW = renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, modal.x + (modal.width - titleW) / 2,
                    modal.y + (headerH - renderer.getLineHeight(UI_12_FONT_ID)) / 2, title, true,
                    EpdFontFamily::BOLD);
  renderer.drawLine(modal.x + 12, modal.y + headerH - 1, modal.x + modal.width - 13, modal.y + headerH - 1);

  constexpr int switchW = 52;
  constexpr int switchH = 26;
  for (int row = 0; row < ROW_COUNT; ++row) {
    const int y = rowsTop + row * rowH;
    if (row == selectedRow) {
      renderer.drawRoundedRect(modal.x + 8, y + 2, modal.width - 16, rowH - 4, 2, 7, true);
    }
    const int labelY = y + (rowH - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, modal.x + 20, labelY, rowLabel(row), true, EpdFontFamily::BOLD);
    const Rect sw{modal.x + modal.width - 20 - switchW, y + (rowH - switchH) / 2, switchW, switchH};
    drawSwitch(sw, rowValue(row));
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
