#include "X3PageActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/x3/X3GameActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/QrUtils.h"
#include "x3/X3Service.h"

namespace {
constexpr size_t kTextMax = 12 * 1024;
constexpr int kMaxNotes = 40;
constexpr int kPad = 14;

const char* kAppNames[] = {"Заметки", "Календарь",         "Игры",      "QR сервера", "Фокус 25",
                           "Сервисы", "Настройки / Wi-Fi", "Обновление"};
constexpr int kAppCount = 8;

std::string num(int v) { return std::to_string(v); }

std::string jsonText(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    if (static_cast<unsigned char>(c) < 0x20) continue;
    o += c;
  }
  return o;
}
}  // namespace

void X3PageActivity::open(Page next, const std::string& path) {
  startActivityForResult(std::make_unique<X3PageActivity>(renderer, mappedInput, next, path), nullptr);
}

void X3PageActivity::loadNotes() {
  names.clear();
  for (const auto& f : Storage.listFiles("/notes", kMaxNotes)) {
    std::string n = f.c_str();
    if (n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0) names.push_back(n);
  }
  std::sort(names.begin(), names.end());
}

void X3PageActivity::loadText() {
  lines.clear();
  std::string raw;
  if (!Storage.readFileToString("X3", arg, kTextMax, raw)) {
    lines.push_back("Файл не найден:");
    lines.push_back(arg);
    return;
  }
  const int width = renderer.getScreenWidth() - 2 * kPad;
  size_t start = 0;
  while (start <= raw.size()) {
    size_t nl = raw.find('\n', start);
    if (nl == std::string::npos) nl = raw.size();
    const std::string para = raw.substr(start, nl - start);
    if (para.empty()) {
      lines.emplace_back();
    } else {
      for (auto& l : renderer.wrappedText(UI_10_FONT_ID, para.c_str(), width, 200)) lines.push_back(std::move(l));
    }
    start = nl + 1;
  }
}

void X3PageActivity::onEnter() {
  Activity::onEnter();
  x3svc::begin();
  if (page == Page::Notes) loadNotes();
  if (page == Page::TextView) loadText();
  if (page == Page::Today) {
    for (const auto& it : x3svc::pack().todayItems) doneLocal.push_back(it.second);
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int body = renderer.getScreenHeight() - metrics.topPadding - metrics.headerHeight - metrics.buttonHintsHeight -
                   2 * metrics.verticalSpacing;
  linesPerPage = std::max(1, body / renderer.getLineHeight(UI_10_FONT_ID));
  requestUpdate();
}

int X3PageActivity::itemCount() const {
  switch (page) {
    case Page::Today:
      return static_cast<int>(x3svc::pack().todayItems.size());
    case Page::Apps:
      return kAppCount;
    case Page::Games:
      return X3GameActivity::kGameCount;
    case Page::Notes:
      return static_cast<int>(names.size());
    default:
      return 0;
  }
}

int X3PageActivity::remainingMin() const {
  const int32_t ms = static_cast<int32_t>(endMs - millis());
  return ms <= 0 ? 0 : static_cast<int>((ms + 59999) / 60000);
}

void X3PageActivity::activate() {
  switch (page) {
    case Page::English:
      if (!revealed) {
        revealed = true;
      }
      break;
    case Page::Today: {
      const auto& items = x3svc::pack().todayItems;
      if (sel < 0 || sel >= static_cast<int>(items.size()) || doneLocal[sel]) break;
      doneLocal[sel] = true;
      x3svc::pushEvent("task_done", "{\"t\":\"" + jsonText(items[sel].first) + "\"}");
      break;
    }
    case Page::Prayer:
      open(Page::TextView, "/faith/prayers.txt");
      return;
    case Page::Apps:
      if (sel == 0) open(Page::Notes);
      if (sel == 1) open(Page::Calendar);
      if (sel == 2) open(Page::Games);
      if (sel == 3) open(Page::Qr);
      if (sel == 4) open(Page::Focus);
      if (sel == 5) open(Page::Services);
      if (sel == 6) activityManager.goToSettings();
      if (sel == 7) open(Page::Update);
      return;
    case Page::Games:
      startActivityForResult(
          std::make_unique<X3GameActivity>(renderer, mappedInput, static_cast<X3GameActivity::Game>(sel)), nullptr);
      return;
    case Page::Notes:
      if (sel >= 0 && sel < static_cast<int>(names.size())) open(Page::TextView, "/notes/" + names[sel]);
      return;
    case Page::Focus:
      if (running) {
        running = false;
      } else {
        running = true;
        finished = false;
        endMs = millis() + static_cast<uint32_t>(focusMin) * 60000U;
      }
      break;
    case Page::Update: {
      GUI.drawPopup(renderer, "Синхронизация...");
      x3svc::syncNow();
      break;
    }
    default:
      break;
  }
  requestUpdate();
}

void X3PageActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  // Left / right: per-page meaning.
  buttonNavigator.onNext([this] {
    const int n = itemCount();
    switch (page) {
      case Page::English:
        if (revealed && !answered) {
          answered = true;
          x3svc::pushEvent("card", "{\"word\":\"" + jsonText(x3svc::pack().wordW) + "\",\"known\":true}");
        }
        break;
      case Page::Prayer:
        activityManager.goToFileBrowser("/Bible");
        return;
      case Page::TextView:
        if ((textPage + 1) * linesPerPage < static_cast<int>(lines.size())) textPage++;
        break;
      case Page::Focus:
        if (!running && focusMin < 90) focusMin += 5;
        break;
      default:
        if (n > 0) sel = (sel + 1) % n;
        break;
    }
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    const int n = itemCount();
    switch (page) {
      case Page::English:
        if (revealed && !answered) {
          answered = true;
          x3svc::pushEvent("card", "{\"word\":\"" + jsonText(x3svc::pack().wordW) + "\",\"known\":false}");
        }
        break;
      case Page::TextView:
        if (textPage > 0) textPage--;
        break;
      case Page::Focus:
        if (!running && focusMin > 5) focusMin -= 5;
        break;
      default:
        if (n > 0) sel = (sel + n - 1) % n;
        break;
    }
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) activate();

  if (page == Page::Focus && running) {
    const int left = remainingMin();
    if (left == 0) {
      running = false;
      finished = true;
      requestUpdate();
    } else if (left != shownMin) {
      requestUpdate();
    }
  }
}

int X3PageActivity::drawWrapped(const std::string& text, int fontId, int x, int y, int width, int maxLines,
                                bool black) {
  const int lh = renderer.getLineHeight(fontId);
  for (const auto& l : renderer.wrappedText(fontId, text.c_str(), width, maxLines)) {
    renderer.drawText(fontId, x, y, l.c_str(), black);
    y += lh;
  }
  return y;
}

void X3PageActivity::drawList(const std::vector<std::string>& rows, int top, int bottom) {
  const int w = renderer.getScreenWidth();
  const int rowH = renderer.getLineHeight(UI_12_FONT_ID) + 20;
  const int visible = std::max(1, (bottom - top) / rowH);
  const int first = sel >= visible ? sel - visible + 1 : 0;
  for (int i = first; i < static_cast<int>(rows.size()) && i < first + visible; i++) {
    const int y = top + (i - first) * rowH;
    const bool on = i == sel;
    if (on) renderer.fillRect(kPad / 2, y, w - kPad, rowH - 4, true);
    renderer.drawText(UI_12_FONT_ID, kPad, y + 8, rows[i].c_str(), !on);
  }
}

void X3PageActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& p = x3svc::pack();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = h - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int cw = w - 2 * kPad;
  const std::string fresh = x3svc::freshnessLabel();

  const char* title = "";
  switch (page) {
    case Page::English:
      title = "English";
      break;
    case Page::Today:
      title = "Сегодня";
      break;
    case Page::Prayer:
      title = "Вера";
      break;
    case Page::Services:
      title = "Сервисы";
      break;
    case Page::Apps:
      title = "Приложения";
      break;
    case Page::Notes:
      title = "Заметки";
      break;
    case Page::TextView:
      title = "Текст";
      break;
    case Page::Calendar:
      title = "Календарь";
      break;
    case Page::Qr:
      title = "QR сервера";
      break;
    case Page::Focus:
      title = "Фокус";
      break;
    case Page::Update:
      title = "Обновление";
      break;
    case Page::Games:
      title = "Игры";
      break;
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, w, metrics.headerHeight}, title, nullptr, false);

  const char* ok = "";
  const char* prev = "";
  const char* next = "";

  switch (page) {
    case Page::English: {
      if (!p.valid || p.wordW.empty()) {
        drawWrapped("Нет слова. Подключись к сети и нажми и удержи OK на главном экране.", UI_10_FONT_ID, kPad, top, cw,
                    6);
        break;
      }
      renderer.drawCenteredText(UI_12_FONT_ID, top + 40, p.wordW.c_str(), true, EpdFontFamily::BOLD);
      const std::string meta = p.wordIpa + (p.wordPos.empty() ? "" : "  " + p.wordPos);
      renderer.drawCenteredText(UI_10_FONT_ID, top + 40 + renderer.getLineHeight(UI_12_FONT_ID) + 10, meta.c_str());
      const int ty = top + 200;
      if (revealed) {
        renderer.drawCenteredText(UI_12_FONT_ID, ty, p.wordTr.c_str());
      }
      const std::string due = "К повтору: " + num(p.englishDue);
      renderer.drawCenteredText(SMALL_FONT_ID, bottom - 60, due.c_str());
      if (answered) renderer.drawCenteredText(UI_10_FONT_ID, bottom - 30, "Записано. Уйдёт при синхронизации.");
      ok = revealed ? "" : "Показать";
      prev = revealed && !answered ? "Не знаю" : "";
      next = revealed && !answered ? "Знаю" : "";
      break;
    }
    case Page::Today: {
      const std::string head = num(p.todayDone) + "/" + num(p.todayTotal) + " задач";
      renderer.drawText(UI_12_FONT_ID, kPad, top, head.c_str(), true, EpdFontFamily::BOLD);
      std::vector<std::string> rows;
      for (size_t i = 0; i < p.todayItems.size(); i++) {
        rows.push_back(std::string(doneLocal[i] ? "[x] " : "[ ] ") + p.todayItems[i].first);
      }
      if (rows.empty()) rows.push_back("Нет данных");
      drawList(rows, top + renderer.getLineHeight(UI_12_FONT_ID) + 16, bottom);
      ok = "Готово";
      prev = "Вверх";
      next = "Вниз";
      break;
    }
    case Page::Prayer: {
      int y = top;
      if (!p.verseRef.empty()) {
        renderer.drawText(UI_10_FONT_ID, kPad, y, ("Стих дня · " + p.verseRef).c_str(), true, EpdFontFamily::BOLD);
        y += renderer.getLineHeight(UI_10_FONT_ID) + 12;
        y = drawWrapped(p.verseText, UI_12_FONT_ID, kPad, y, cw, 12);
      } else {
        drawWrapped("Нет стиха дня. Нажми и удержи OK на главном экране для синхронизации.", UI_10_FONT_ID, kPad, y, cw,
                    5);
      }
      if (!p.fast.empty()) {
        renderer.drawText(SMALL_FONT_ID, kPad, bottom - renderer.getLineHeight(SMALL_FONT_ID), p.fast.c_str());
      }
      ok = "Молитвы";
      next = "Библия";
      break;
    }
    case Page::Services: {
      renderer.drawText(UI_12_FONT_ID, kPad, top, ("WB заказов: " + num(p.wbOrders)).c_str(), true,
                        EpdFontFamily::BOLD);
      const char* vpn = p.vpn == "online" ? "онлайн" : p.vpn == "offline" ? "офлайн" : "нет данных";
      renderer.drawText(UI_12_FONT_ID, kPad, top + renderer.getLineHeight(UI_12_FONT_ID) + 20,
                        (std::string("VPN: ") + vpn).c_str());
      renderer.drawText(SMALL_FONT_ID, kPad, bottom - renderer.getLineHeight(SMALL_FONT_ID), fresh.c_str());
      break;
    }
    case Page::Apps: {
      std::vector<std::string> rows(kAppNames, kAppNames + kAppCount);
      drawList(rows, top, bottom);
      ok = "Открыть";
      prev = "Вверх";
      next = "Вниз";
      break;
    }
    case Page::Games: {
      std::vector<std::string> rows;
      for (int i = 0; i < X3GameActivity::kGameCount; i++) {
        rows.push_back(X3GameActivity::name(static_cast<X3GameActivity::Game>(i)));
      }
      drawList(rows, top, bottom);
      ok = "Играть";
      prev = "Вверх";
      next = "Вниз";
      break;
    }
    case Page::Notes: {
      std::vector<std::string> rows = names;
      if (rows.empty()) rows.push_back("Положи .txt в /notes на карту");
      drawList(rows, top, bottom);
      ok = "Открыть";
      prev = "Вверх";
      next = "Вниз";
      break;
    }
    case Page::TextView: {
      const int lh = renderer.getLineHeight(UI_10_FONT_ID);
      const int from = textPage * linesPerPage;
      for (int i = 0; i < linesPerPage && from + i < static_cast<int>(lines.size()); i++) {
        renderer.drawText(UI_10_FONT_ID, kPad, top + i * lh, lines[from + i].c_str());
      }
      prev = "Назад";
      next = "Далее";
      break;
    }
    case Page::Calendar: {
      if (!p.valid) {
        drawWrapped("Нет данных. Нужна синхронизация.", UI_10_FONT_ID, kPad, top, cw, 3);
        break;
      }
      renderer.drawCenteredText(UI_12_FONT_ID, top + 30, p.weekday.c_str(), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_12_FONT_ID, top + 30 + renderer.getLineHeight(UI_12_FONT_ID) + 8,
                                p.dateText.c_str());
      int y = top + 160;
      if (!p.fast.empty()) y = drawWrapped("Пост: " + p.fast, UI_10_FONT_ID, kPad, y, cw, 3) + 12;
      const std::string t = "Задачи: " + num(p.todayDone) + "/" + num(p.todayTotal);
      renderer.drawText(UI_10_FONT_ID, kPad, y, t.c_str());
      break;
    }
    case Page::Qr: {
      if (!x3svc::hasConfig()) {
        drawWrapped("Нет config.json на карте.", UI_10_FONT_ID, kPad, top, cw, 3);
        break;
      }
      const int side = std::min(cw, bottom - top - 40);
      QrUtils::drawQrCode(renderer, Rect{(w - side) / 2, top, side, side}, x3svc::serverUrl());
      renderer.drawCenteredText(SMALL_FONT_ID, top + side + 10, x3svc::serverUrl().c_str());
      break;
    }
    case Page::Focus: {
      char buf[16];
      shownMin = running ? remainingMin() : focusMin;
      std::snprintf(buf, sizeof(buf), "%d мин", shownMin);
      renderer.drawCenteredText(UI_12_FONT_ID, top + 120, buf, true, EpdFontFamily::BOLD);
      if (finished) renderer.drawCenteredText(UI_12_FONT_ID, top + 220, "Время вышло");
      ok = running ? "Стоп" : "Старт";
      prev = running ? "" : "-5";
      next = running ? "" : "+5";
      break;
    }
    case Page::Update: {
      std::vector<std::string> rows;
      rows.push_back(std::string("Версия: ") + CROSSPOINT_VERSION);
      rows.push_back("Заряд: " + num(powerManager.getBatteryPercentage()) + "%");
      rows.push_back("Память: " + num(static_cast<int>(ESP.getFreeHeap() / 1024)) + " КБ свободно");
      rows.push_back("Данные: " + fresh);
      rows.push_back("Очередь: " + num(static_cast<int>(x3svc::queued())));
      rows.push_back(std::string("Итог: ") + x3svc::resultText(x3svc::lastResult()));
      const int lh = renderer.getLineHeight(UI_10_FONT_ID) + 14;
      for (size_t i = 0; i < rows.size(); i++) {
        renderer.drawText(UI_10_FONT_ID, kPad, top + static_cast<int>(i) * lh, rows[i].c_str());
      }
      ok = "Обновить";
      break;
    }
  }

  const auto labels = mappedInput.mapLabels("Назад", ok, prev, next);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
