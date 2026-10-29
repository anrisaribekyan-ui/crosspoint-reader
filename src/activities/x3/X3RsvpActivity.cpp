#include "X3RsvpActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* kPosFile = "/x3/rsvp.pos";
constexpr int kMaxFiles = 40;
constexpr size_t kChunk = 512;
constexpr int kFullEvery = 40;  // words between deep refreshes that clear ghosting
constexpr int kMarkEvery = 10;
constexpr size_t kMaxMarks = 16;
constexpr unsigned long kRewindPressMs = 600;

void addTxt(const char* dir, std::vector<std::string>& out) {
  for (const auto& f : Storage.listFiles(dir, kMaxFiles)) {
    std::string n = f.c_str();
    if (n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0 && out.size() < static_cast<size_t>(kMaxFiles))
      out.push_back(std::string(dir) + "/" + n);
  }
}

std::string baseName(const std::string& p) {
  const size_t s = p.rfind('/');
  return s == std::string::npos ? p : p.substr(s + 1);
}
}  // namespace

void X3RsvpActivity::loadFiles() {
  files.clear();
  addTxt("/rsvp", files);
  addTxt("/notes", files);
  std::sort(files.begin(), files.end());
}

void X3RsvpActivity::onEnter() {
  Activity::onEnter();
  loadFiles();
  requestUpdate();
}

void X3RsvpActivity::onExit() {
  if (mode == Mode::Play) savePos();
  Activity::onExit();
}

void X3RsvpActivity::savePos() const {
  if (path.empty()) return;
  Storage.ensureDirectoryExists("/x3");
  const String body = String(path.c_str()) + "\n" + String(offset) + "\n" + String(wpm) + "\n";
  Storage.writeFile(kPosFile, body);
}

void X3RsvpActivity::seekTo(uint32_t pos) {
  offset = pos;
  fedPos = pos;
  splitter.reset(pos);
  word.clear();
  atEnd = false;
}

bool X3RsvpActivity::openBook(const std::string& file) {
  HalFile f;
  if (!Storage.openFileForRead("X3", file, f)) return false;
  fileSize = static_cast<uint32_t>(f.size());
  f.close();
  path = file;
  marks.clear();
  wordCount = 0;
  uint32_t start = 0;
  std::string raw;
  if (Storage.readFileToString("X3", kPosFile, 256, raw)) {
    const size_t a = raw.find('\n');
    const size_t b = a == std::string::npos ? a : raw.find('\n', a + 1);
    if (b != std::string::npos && raw.compare(0, a, file) == 0) {
      start = static_cast<uint32_t>(std::strtoul(raw.c_str() + a + 1, nullptr, 10));
      wpm = x3r::clampWpm(std::atoi(raw.c_str() + b + 1));
      if (start >= fileSize) start = 0;
    }
  }
  seekTo(start);
  return true;
}

bool X3RsvpActivity::readWord() {
  std::string w;
  size_t end = 0;
  HalFile f;
  bool opened = false;
  uint32_t fed = fedPos;
  // Feed chunks until the splitter returns a word or the file ends.
  for (;;) {
    if (splitter.next(w, fed >= fileSize, &end)) break;
    if (fed >= fileSize) {
      if (opened) f.close();
      fedPos = fed;
      atEnd = true;
      word.clear();
      return false;
    }
    if (!opened) {
      if (!Storage.openFileForRead("X3", path, f)) return false;
      opened = true;
    }
    char buf[kChunk];
    // Position the file after everything already fed to the splitter.
    f.seek(fed);
    const int n = f.read(buf, std::min<size_t>(kChunk, fileSize - fed));
    if (n <= 0) {
      fed = fileSize;
      continue;
    }
    splitter.feed(buf, static_cast<size_t>(n));
    fed += static_cast<uint32_t>(n);
  }
  if (opened) f.close();
  fedPos = fed;
  word = w;
  offset = static_cast<uint32_t>(end);
  if (wordCount % kMarkEvery == 0) {
    marks.push_back(offset);
    if (marks.size() > kMaxMarks) marks.erase(marks.begin());
  }
  wordCount++;
  return true;
}

void X3RsvpActivity::rewind() {
  // Back about 3 marks (~30 words).
  for (int i = 0; i < 3 && marks.size() > 1; i++) marks.pop_back();
  const uint32_t pos = marks.empty() ? 0 : marks.back();
  if (!marks.empty()) marks.pop_back();
  seekTo(pos);
  readWord();
}

void X3RsvpActivity::loop() {
  if (mode == Mode::List) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    const int n = static_cast<int>(files.size());
    if (n > 0 && mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      sel = (sel + n - 1) % n;
      requestUpdate();
    }
    if (n > 0 && mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      sel = (sel + 1) % n;
      requestUpdate();
    }
    if (n > 0 && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (openBook(files[sel])) {
        mode = Mode::Play;
        playing = false;
        readWord();
      }
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    savePos();
    playing = false;
    mode = Mode::List;
    requestUpdate();
    return;
  }
  bool changed = false;
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    wpm = x3r::clampWpm(wpm - x3r::kWpmStep);
    changed = true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    wpm = std::min(x3r::clampWpm(wpm + x3r::kWpmStep), x3r::maxWpmFor(refreshMs));
    changed = true;
  }
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, kRewindPressMs)) {
    rewind();
    changed = true;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (atEnd) {
      seekTo(0);
      marks.clear();
      wordCount = 0;
      readWord();
    }
    playing = !playing;
    if (!playing) savePos();
    nextAtMs = millis();
    changed = true;
  }
  if (playing && !atEnd && static_cast<int32_t>(millis() - nextAtMs) >= 0) {
    if (readWord()) {
      nextAtMs = millis() + static_cast<uint32_t>(x3r::delayMs(word, wpm, refreshMs));
    } else {
      playing = false;
      savePos();
    }
    changed = true;
  }
  if (changed) requestUpdate();
}

void X3RsvpActivity::drawList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, w, metrics.headerHeight}, "Чтение по слову",
                 "файлы .txt из /rsvp и /notes", false);
  if (files.empty()) {
    renderer.drawText(UI_10_FONT_ID, 14, 200, "Положи .txt в папку /rsvp на SD", true);
  }
  const int top = metrics.topPadding + metrics.headerHeight + 10;
  const int rowH = 44;
  const int rows = std::max(1, (renderer.getScreenHeight() - top - 90) / rowH);
  const int first = std::max(0, std::min(sel - rows / 2, static_cast<int>(files.size()) - rows));
  for (int i = 0; i < rows && first + i < static_cast<int>(files.size()); i++) {
    const int y = top + i * rowH;
    const bool on = first + i == sel;
    if (on) renderer.fillRect(8, y, w - 16, rowH - 4, true);
    renderer.drawText(NOTOSANS_18_FONT_ID, 18, y + 6, baseName(files[first + i]).c_str(), !on);
  }
  const auto labels = mappedInput.mapLabels("Назад", "Читать", "Вверх", "Вниз");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void X3RsvpActivity::drawPlay() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  // Largest font that fits the width.
  const int fonts[] = {RSVP_56_FONT_ID, NOTOSANS_30_FONT_ID, NOTOSANS_22_FONT_ID, NOTOSANS_16_FONT_ID};
  int fontId = fonts[3];
  EpdFontFamily::Style style = EpdFontFamily::BOLD;
  for (int f : fonts) {
    if (renderer.getTextWidth(f, word.c_str(), style) <= w - 24) {
      fontId = f;
      break;
    }
  }
  const std::string shown = word.empty() ? std::string(atEnd ? "Конец" : "...") : word;
  const int tw = renderer.getTextWidth(fontId, shown.c_str(), style);
  renderer.drawText(fontId, (w - tw) / 2, h / 2 - renderer.getLineHeight(fontId) / 2, shown.c_str(), true, style);

  char info[96];
  std::snprintf(info, sizeof(info), "%d сл/мин · макс ~%d · %s", wpm, x3r::maxWpmFor(refreshMs),
                playing ? "идёт" : "пауза");
  renderer.drawText(UI_10_FONT_ID, 14, h - 120, info, true);
  const int pct = fileSize ? static_cast<int>(static_cast<uint64_t>(offset) * 100 / fileSize) : 0;
  renderer.drawRect(14, h - 96, w - 28, 10, 1, true);
  renderer.fillRect(14, h - 96, (w - 28) * pct / 100, 10, true);
  const auto labels = mappedInput.mapLabels("Назад", playing ? "Пауза" : "Дальше", "Медленнее", "Быстрее");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void X3RsvpActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (mode == Mode::List)
    drawList();
  else
    drawPlay();
  const bool full = mode == Mode::Play && playing && ++sinceFull >= kFullEvery;
  const uint32_t t0 = millis();
  if (full) {
    sinceFull = 0;
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer();
    if (mode == Mode::Play) {
      const int took = static_cast<int>(millis() - t0);
      refreshMs = (refreshMs * 3 + took) / 4;
    }
  }
}
