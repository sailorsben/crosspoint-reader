#pragma once

#include "activities/Activity.h"

class ImmersiveOptionsActivity final : public Activity {
 public:
  ImmersiveOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ImmersiveOptions", renderer, mappedInput) {}

  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;

 private:
  static constexpr int ROW_COUNT = 5;
  int selectedRow = 0;

  Rect modalRect() const;
  void toggleRow(int row);
  bool rowValue(int row) const;
  const char* rowLabel(int row) const;
  void drawSwitch(const Rect& rect, bool on) const;
  void close();
};
