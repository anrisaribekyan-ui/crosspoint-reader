#include "X3GameActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long kAltPressMs = 600;
constexpr int kPad = 14;

void centerText(const GfxRenderer& r, int fontId, int cx, int cy, const char* text, bool black = true,
                EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const int w = r.getTextWidth(fontId, text, style);
  r.drawText(fontId, cx - w / 2, cy - r.getLineHeight(fontId) / 2, text, black, style);
}
}  // namespace

const char* X3GameActivity::name(Game g) {
  switch (g) {
    case Game::Tetris:
      return "Тетрис";
    case Game::G2048:
      return "2048";
    case Game::Mines:
      return "Сапёр";
    case Game::Sudoku:
      return "Судоку";
    case Game::Snake:
      return "Змейка";
    case Game::Checkers:
      return "Шашки";
    case Game::Strike:
      return "КС на минималках";
  }
  return "";
}

void X3GameActivity::start() {
  const uint32_t seed = micros() ^ millis();
  tetris.reset();
  g2048.reset();
  mines.reset();
  sudoku.reset();
  snake.reset();
  checkers.reset();
  strike.reset();
  switch (game) {
    case Game::Tetris:
      tetris = makeUniqueNoThrow<x3g::Tetris>(seed);
      break;
    case Game::G2048:
      g2048 = makeUniqueNoThrow<x3g::G2048>(seed);
      break;
    case Game::Mines:
      mines = makeUniqueNoThrow<x3g::Mines>(seed);
      break;
    case Game::Sudoku:
      sudoku = makeUniqueNoThrow<x3g::Sudoku>(seed);
      break;
    case Game::Snake:
      snake = makeUniqueNoThrow<x3g::Snake>(seed);
      break;
    case Game::Checkers:
      checkers = makeUniqueNoThrow<x3g::Checkers>(seed);
      break;
    case Game::Strike:
      strike = makeUniqueNoThrow<x3g::Strike>(seed);
      break;
  }
  lastTickMs = millis();
}

uint32_t X3GameActivity::tickPeriodMs() const {
  switch (game) {
    case Game::Tetris:
      return 1200;
    case Game::Snake:
      return 800;
    case Game::Strike:
      return 1000;
    default:
      return 0;
  }
}

bool X3GameActivity::isOver() const {
  switch (game) {
    case Game::Tetris:
      return tetris && tetris->over;
    case Game::G2048:
      return g2048 && g2048->over();
    case Game::Mines:
      return mines && mines->over;
    case Game::Sudoku:
      return sudoku && sudoku->solved();
    case Game::Snake:
      return snake && snake->over;
    case Game::Checkers:
      return checkers && checkers->over;
    case Game::Strike:
      return strike && strike->over;
  }
  return false;
}

void X3GameActivity::onEnter() {
  Activity::onEnter();
  start();
  requestUpdate();
}

void X3GameActivity::send(x3g::Key key) {
  if (tetris) tetris->key(key);
  if (g2048) g2048->key(key);
  if (mines) mines->key(key);
  if (sudoku) sudoku->key(key);
  if (snake) snake->key(key);
  if (checkers) checkers->key(key);
  if (strike) strike->key(key);
}

void X3GameActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  bool changed = false;
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    send(x3g::Key::Left);
    changed = true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    send(x3g::Key::Right);
    changed = true;
  }
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, kAltPressMs)) {
    send(x3g::Key::Alt);
    changed = true;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (isOver())
      start();
    else
      send(x3g::Key::Ok);
    changed = true;
  }
  const uint32_t period = tickPeriodMs();
  if (period != 0 && !isOver() && !(snake && snake->paused) && millis() - lastTickMs >= period) {
    lastTickMs = millis();
    if (tetris) tetris->tick();
    if (snake) snake->tick();
    if (strike) strike->tick();
    changed = true;
  }
  if (changed) requestUpdate();
}

void X3GameActivity::drawBoard(int left, int top, int width, int height) {
  const GfxRenderer& r = renderer;
  auto cellFor = [&](int cols, int rows) { return std::max(8, std::min(width / cols, height / rows)); };

  if (tetris) {
    const int c = cellFor(x3g::Tetris::W, x3g::Tetris::H);
    const int ox = left + (width - c * x3g::Tetris::W) / 2;
    r.drawRect(ox - 2, top - 2, c * x3g::Tetris::W + 4, c * x3g::Tetris::H + 4, 2, true);
    for (int y = 0; y < x3g::Tetris::H; y++)
      for (int x = 0; x < x3g::Tetris::W; x++)
        if (tetris->g[y][x]) r.fillRect(ox + x * c + 1, top + y * c + 1, c - 2, c - 2, true);
    int cells[4][2];
    tetris->cells(tetris->type, tetris->rot, tetris->px, tetris->py, cells);
    for (auto& p : cells)
      if (p[1] >= 0) {
        r.fillRect(ox + p[0] * c + 1, top + p[1] * c + 1, c - 2, c - 2, true);
        r.fillRect(ox + p[0] * c + 5, top + p[1] * c + 5, c - 10, c - 10, false);
      }
  } else if (g2048) {
    const int c = cellFor(4, 4);
    const int ox = left + (width - c * 4) / 2;
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++) {
        const int v = g2048->b[y][x];
        const int cx = ox + x * c, cy = top + y * c;
        if (v)
          r.fillRect(cx + 3, cy + 3, c - 6, c - 6, true);
        else
          r.drawRect(cx + 3, cy + 3, c - 6, c - 6, 2, true);
        if (v) {
          const std::string s = std::to_string(v);
          centerText(r, UI_12_FONT_ID, cx + c / 2, cy + c / 2, s.c_str(), false, EpdFontFamily::BOLD);
        }
      }
  } else if (mines) {
    const int c = cellFor(x3g::Mines::W, x3g::Mines::H);
    const int ox = left + (width - c * x3g::Mines::W) / 2;
    for (int y = 0; y < x3g::Mines::H; y++)
      for (int x = 0; x < x3g::Mines::W; x++) {
        const int i = y * x3g::Mines::W + x;
        const int cx = ox + x * c, cy = top + y * c;
        if (!mines->open[i]) {
          if (mines->over && mines->mine[i]) {
            r.drawRect(cx + 2, cy + 2, c - 4, c - 4, 2, true);
            centerText(r, UI_12_FONT_ID, cx + c / 2, cy + c / 2, "*");
          } else {
            r.fillRect(cx + 2, cy + 2, c - 4, c - 4, true);
          }
        } else {
          r.drawRect(cx + 2, cy + 2, c - 4, c - 4, 1, true);
          const int n = mines->count(x, y);
          if (mines->mine[i]) {
            centerText(r, UI_12_FONT_ID, cx + c / 2, cy + c / 2, "*");
          } else if (n > 0) {
            const std::string s = std::to_string(n);
            centerText(r, UI_12_FONT_ID, cx + c / 2, cy + c / 2, s.c_str());
          }
        }
      }
    if (!mines->over) r.drawRect(ox + mines->cx * c, top + mines->cy * c, c, c, 4, true);
  } else if (sudoku) {
    const int c = cellFor(9, 9);
    const int ox = left + (width - c * 9) / 2;
    for (int i = 0; i < 10; i++) {
      const int t = i % 3 == 0 ? 3 : 1;
      r.fillRect(ox + i * c - t / 2, top, t, c * 9 + 1, true);
      r.fillRect(ox, top + i * c - t / 2, c * 9 + 1, t, true);
    }
    for (int i = 0; i < 81; i++) {
      const int cx = ox + (i % 9) * c, cy = top + (i / 9) * c;
      if (sudoku->v[i]) {
        char s[2] = {static_cast<char>('0' + sudoku->v[i]), 0};
        centerText(r, UI_12_FONT_ID, cx + c / 2, cy + c / 2, s, true,
                   sudoku->given[i] ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
        if (!sudoku->given[i]) r.fillRect(cx + c / 2 - 6, cy + c - 8, 12, 2, true);
        if (sudoku->conflict(i)) r.fillRect(cx + 4, cy + 4, 8, 8, true);
      }
    }
    r.drawRect(ox + (sudoku->cur % 9) * c + 2, top + (sudoku->cur / 9) * c + 2, c - 3, c - 3, 3, true);
  } else if (snake) {
    const int c = cellFor(x3g::Snake::W, x3g::Snake::H);
    const int ox = left + (width - c * x3g::Snake::W) / 2;
    r.drawRect(ox - 2, top - 2, c * x3g::Snake::W + 4, c * x3g::Snake::H + 4, 2, true);
    for (size_t i = 0; i < snake->body.size(); i++) {
      const int pad = i == 0 ? 0 : 2;
      r.fillRect(ox + snake->body[i].first * c + pad, top + snake->body[i].second * c + pad, c - 2 * pad, c - 2 * pad,
                 true);
    }
    r.drawRect(ox + snake->fx * c + 3, top + snake->fy * c + 3, c - 6, c - 6, 3, true);
  } else if (checkers) {
    const int c = cellFor(8, 8);
    const int ox = left + (width - c * 8) / 2;
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        const int cx = ox + x * c, cy = top + y * c;
        if ((x + y) % 2)
          r.fillRectDither(cx, cy, c, c, DarkGray);
        else
          r.drawRect(cx, cy, c, c, 1, true);
        const int p = checkers->b[y * 8 + x];
        if (!p) continue;
        r.fillRoundedRect(cx + 5, cy + 5, c - 10, c - 10, (c - 10) / 2, Black);
        if (p > 0) r.fillRoundedRect(cx + 9, cy + 9, c - 18, c - 18, (c - 18) / 2, White);
        if (std::abs(p) == 2) centerText(r, UI_10_FONT_ID, cx + c / 2, cy + c / 2, "K", p > 0);
      }
    if (!checkers->over && checkers->playerTurn && !checkers->moves.empty()) {
      const auto& m = checkers->moves[checkers->pick];
      r.drawRect(ox + (m.from % 8) * c, top + (m.from / 8) * c, c, c, 4, true);
      r.drawRect(ox + (m.to % 8) * c + 4, top + (m.to / 8) * c + 4, c - 8, c - 8, 4, false);
      r.drawRect(ox + (m.to % 8) * c, top + (m.to / 8) * c, c, c, 4, true);
    }
  } else if (strike) {
    const int c = cellFor(x3g::Strike::N, x3g::Strike::N);
    const int ox = left + (width - c * x3g::Strike::N) / 2;
    for (int y = 0; y < x3g::Strike::N; y++)
      for (int x = 0; x < x3g::Strike::N; x++)
        if (strike->wall[y * x3g::Strike::N + x])
          r.fillRect(ox + x * c, top + y * c, c, c, true);
        else
          r.drawRect(ox + x * c, top + y * c, c, c, 1, true);
    for (const auto& b : strike->bots) {
      const int cx = ox + b.x * c, cy = top + b.y * c;
      if (b.alive) {
        r.fillRoundedRect(cx + 6, cy + 6, c - 12, c - 12, (c - 12) / 2, Black);
        centerText(r, UI_10_FONT_ID, cx + c / 2, cy + c / 2, "T", false);
      } else {
        centerText(r, UI_12_FONT_ID, cx + c / 2, cy + c / 2, "x");
      }
    }
    const int pcx = ox + strike->px * c + c / 2, pcy = top + strike->py * c + c / 2;
    r.drawRoundedRect(pcx - c / 2 + 6, pcy - c / 2 + 6, c - 12, c - 12, 3, (c - 12) / 2, true);
    int dx, dy;
    x3g::Strike::delta(strike->aim, dx, dy);
    r.drawLine(pcx, pcy, pcx + dx * c * 3 / 4, pcy + dy * c * 3 / 4, 3, true);
    centerText(r, UI_10_FONT_ID, pcx, pcy, "CT");
  }
}

void X3GameActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();

  char sub[64] = "";
  if (tetris) std::snprintf(sub, sizeof(sub), "Счёт %d · линии %d", tetris->score, tetris->lines);
  if (g2048)
    std::snprintf(sub, sizeof(sub), "Счёт %d · %s", g2048->score, g2048->vertical ? "вверх/вниз" : "влево/вправо");
  if (mines)
    std::snprintf(sub, sizeof(sub), "Мин: %d · %s", x3g::Mines::N, mines->vertical ? "по вертикали" : "по горизонтали");
  if (sudoku) std::snprintf(sub, sizeof(sub), "%s", sudoku->vertical ? "курсор по вертикали" : "курсор по горизонтали");
  if (snake) std::snprintf(sub, sizeof(sub), "Счёт %d%s", snake->score, snake->paused ? " · пауза" : "");
  if (checkers) std::snprintf(sub, sizeof(sub), "%s", checkers->playerTurn ? "Твой ход" : "Думаю...");
  if (strike) std::snprintf(sub, sizeof(sub), "Осталось %d с", std::max(0, strike->seconds));
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, w, metrics.headerHeight}, name(game), sub, false);

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = h - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const char* altHelp = "";
  switch (game) {
    case Game::Tetris:
      altHelp = "удерж. OK: сбросить фигуру";
      break;
    case Game::G2048:
    case Game::Mines:
    case Game::Sudoku:
      altHelp = "удерж. OK: влево-вправо / вверх-вниз";
      break;
    case Game::Strike:
      altHelp = "удерж. OK: выстрел";
      break;
    default:
      break;
  }
  const int helpH = renderer.getLineHeight(SMALL_FONT_ID) + 6;
  drawBoard(kPad, top, w - 2 * kPad, bottom - top - helpH);
  renderer.drawCenteredText(SMALL_FONT_ID, bottom - helpH + 4, altHelp);

  if (isOver()) {
    const char* line1 = "Игра окончена";
    const char* line2 = "OK — заново";
    if (sudoku && sudoku->solved()) line1 = "Решено!";
    if (mines && mines->won) line1 = "Победа!";
    if (checkers && checkers->over) line1 = checkers->playerWon ? "Ты выиграл" : "Ты проиграл";
    if (strike) line1 = strike->ctWin ? "Counter-Terrorists win" : "Terrorists win";
    const int bw = w - 60;
    const int by = top + (bottom - top) / 2 - 50;
    renderer.fillRect(30, by, bw, 100, false);
    renderer.drawRect(30, by, bw, 100, 3, true);
    renderer.drawCenteredText(UI_12_FONT_ID, by + 18, line1, true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, by + 60, line2);
  }

  const char* ok = "OK";
  const char* left = "<";
  const char* right = ">";
  switch (game) {
    case Game::Tetris:
      ok = "Поворот";
      left = "Влево";
      right = "Вправо";
      break;
    case Game::G2048:
      ok = "";
      left = "<";
      right = ">";
      break;
    case Game::Mines:
      ok = "Открыть";
      left = "<";
      right = ">";
      break;
    case Game::Sudoku:
      ok = "Цифра";
      left = "<";
      right = ">";
      break;
    case Game::Snake:
      ok = "Пауза";
      left = "Налево";
      right = "Направо";
      break;
    case Game::Checkers:
      ok = "Ходить";
      left = "Ход -";
      right = "Ход +";
      break;
    case Game::Strike:
      ok = "Шаг";
      left = "Влево";
      right = "Вправо";
      break;
  }
  const auto labels = mappedInput.mapLabels("Назад", ok, left, right);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
