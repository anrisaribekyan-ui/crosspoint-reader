#pragma once
#include <cstdint>
#include <memory>

#include "activities/Activity.h"
#include "x3games.h"

// One screen for all launcher games. Logic lives in lib/X3Core/x3games.h.
class X3GameActivity final : public Activity {
 public:
  enum class Game : uint8_t { Tetris, G2048, Mines, Sudoku, Snake, Checkers, Strike };
  static constexpr int kGameCount = 7;
  static const char* name(Game g);

 private:
  Game game;
  std::unique_ptr<x3g::Tetris> tetris;
  std::unique_ptr<x3g::G2048> g2048;
  std::unique_ptr<x3g::Mines> mines;
  std::unique_ptr<x3g::Sudoku> sudoku;
  std::unique_ptr<x3g::Snake> snake;
  std::unique_ptr<x3g::Checkers> checkers;
  std::unique_ptr<x3g::Strike> strike;
  uint32_t lastTickMs = 0;

  void start();
  uint32_t tickPeriodMs() const;
  bool isOver() const;
  void send(x3g::Key key);
  void drawBoard(int left, int top, int width, int height);

 public:
  X3GameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Game game)
      : Activity("X3Game", renderer, mappedInput), game(game) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return tickPeriodMs() != 0 && !isOver(); }
};
