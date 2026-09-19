#include "Activity.h"

#include "ActivityManager.h"
#include "CrossPointSettings.h"

void Activity::onEnter() { LOG_DBG("ACT", "Entering activity: %s", name.c_str()); }

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

void Activity::applyDisplayOrientation() {
  GfxRenderer::Orientation orientation = GfxRenderer::Orientation::Portrait;
  switch (SETTINGS.interfaceOrientation) {
    case CrossPointSettings::UI_LANDSCAPE_CW:
      orientation = GfxRenderer::Orientation::LandscapeClockwise;
      break;
    case CrossPointSettings::UI_LANDSCAPE_CCW:
      orientation = GfxRenderer::Orientation::LandscapeCounterClockwise;
      break;
    case CrossPointSettings::UI_PORTRAIT:
    default:
      break;
  }
  if (renderer.getOrientation() != orientation) {
    renderer.setOrientation(orientation);
  }
}

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

void Activity::onGoHome(HomeMenuItem item) { activityManager.goHome(item); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }
