#include "HubActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>

#include <cmath>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/x3/X3PageActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "x3/X3Service.h"
#include "x3core.h"

namespace {
constexpr uint32_t kEnterSyncSec = 600;
constexpr uint32_t kHourlySyncSec = 3600;
constexpr unsigned long kLongPressMs = 700;
constexpr int kCols = 2;
constexpr int kRows = 3;
constexpr int kGap = 8;
constexpr int kPad = 8;
}  // namespace

void HubActivity::onEnter() {
  Activity::onEnter();
  x3svc::begin();
  if (x3svc::syncDue(kEnterSyncSec)) {
    sync();
  }
  requestUpdate();
}

void HubActivity::sync() {
  syncing = true;
  requestUpdateAndWait();
  x3svc::syncNow();
  syncing = false;
  requestUpdate();
}

void HubActivity::openTile(int index) {
  using Page = X3PageActivity::Page;
  Page page = Page::Apps;
  switch (index) {
    case 0:  // Reading
      activityManager.goToFileBrowser();
      return;
    case 1:
      page = Page::English;
      break;
    case 2:
      page = Page::Today;
      break;
    case 3:
      page = Page::Prayer;
      break;
    case 4:  // Bible
      activityManager.goToFileBrowser("/Bible");
      return;
    default:
      page = Page::Apps;
      break;
  }
  startActivityForResult(std::make_unique<X3PageActivity>(renderer, mappedInput, page), nullptr);
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
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, kLongPressMs)) {
    sync();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openTile(selected);
    return;
  }
  if (x3svc::syncDue(kHourlySyncSec)) sync();
}

namespace {
// Wi-Fi symbol: a dot and three arcs, drawn with small squares.
void drawWifiIcon(const GfxRenderer& r, int cx, int baseY) {
  r.fillRect(cx - 3, baseY - 3, 6, 6, true);
  for (int radius = 12; radius <= 36; radius += 12) {
    for (int deg = -45; deg <= 45; deg += 5) {
      const float a = static_cast<float>(deg) * 3.14159265f / 180.0f;
      const int x = cx + static_cast<int>(std::sin(a) * radius);
      const int y = baseY - static_cast<int>(std::cos(a) * radius);
      r.fillRect(x - 1, y - 1, 3, 3, true);
    }
  }
}
}  // namespace

void HubActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();

  const auto& pack = x3svc::pack();
  const std::string title = pack.valid ? pack.weekday + ", " + pack.dateText : std::string("X3");
  std::string status = x3svc::freshnessLabel();
  if (x3svc::hasConfig() && x3svc::lastAttemptSec() != 0 && x3svc::lastResult() != x3svc::Result::Ok) {
    status += " · офлайн";
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, w, metrics.headerHeight}, title.c_str(), status.c_str(), false);

  if (syncing) drawWifiIcon(renderer, w - 40, metrics.topPadding + 44);

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

  const auto labels = mappedInput.mapLabels("", "Открыть", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
