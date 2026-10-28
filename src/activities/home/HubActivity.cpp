#include "HubActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "x3_pack.h"

namespace {
constexpr size_t kPackMaxBytes = 16 * 1024;
constexpr int kCols = 2;
constexpr int kRows = 3;
constexpr int kGap = 8;
constexpr int kPad = 8;
}  // namespace

void HubActivity::onEnter() {
  Activity::onEnter();
  std::string json;
  if (Storage.readFileToString("HUB", "/cache/pack.json", kPackMaxBytes, json)) {
    x3::parsePack(json, pack);
  }
  requestUpdate();
}

void HubActivity::openTile(int index) {
  switch (index) {
    case 0:  // Reading
      activityManager.goToFileBrowser();
      break;
    case 4:  // Services: no screen yet, falls back to settings
    case 5:  // Apps
      activityManager.goToSettings();
      break;
    default:
      break;  // English, Today, Faith: screens come later
  }
}

void HubActivity::loop() {
  buttonNavigator.onNext([this] {
    selected = (selected + 1) % x3::kHubTiles;
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selected = (selected + x3::kHubTiles - 1) % x3::kHubTiles;
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openTile(selected);
  }
}

void HubActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();

  const std::string title = pack.valid ? pack.weekday + ", " + pack.dateText : std::string("X3");
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, w, metrics.headerHeight}, title.c_str(), nullptr, false);

  const int top = metrics.topPadding + metrics.headerHeight + kPad;
  const int bottom = h - metrics.buttonHintsHeight - kPad;
  const int tileW = (w - 2 * kPad - (kCols - 1) * kGap) / kCols;
  const int tileH = (bottom - top - (kRows - 1) * kGap) / kRows;

  const auto tiles = x3::hubTiles(pack);
  const int tagH = renderer.getLineHeight(SMALL_FONT_ID);
  const int bigH = renderer.getLineHeight(UI_12_FONT_ID);
  for (int i = 0; i < x3::kHubTiles; i++) {
    const int x = kPad + (i % kCols) * (tileW + kGap);
    const int y = top + (i / kCols) * (tileH + kGap);
    const bool sel = i == selected;
    if (sel) {
      renderer.fillRect(x, y, tileW, tileH, true);
    } else {
      renderer.drawRect(x, y, tileW, tileH, 2, true);
    }
    const bool ink = !sel;  // black text on white, white text on selected
    renderer.drawText(SMALL_FONT_ID, x + 10, y + 8, tiles[i].tag.c_str(), ink);
    renderer.drawText(UI_12_FONT_ID, x + 10, y + 14 + tagH, tiles[i].big.c_str(), ink);
    renderer.drawText(SMALL_FONT_ID, x + 10, y + 20 + tagH + bigH, tiles[i].sub.c_str(), ink);
  }

  const auto labels = mappedInput.mapLabels("", "OK", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
