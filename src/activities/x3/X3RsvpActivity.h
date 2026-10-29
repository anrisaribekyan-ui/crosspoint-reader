#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "x3rsvp.h"

// One word at a time, centered. Word timing and splitting live in lib/X3Core/x3rsvp.h.
class X3RsvpActivity final : public Activity {
  enum class Mode : uint8_t { List, Play };
  Mode mode = Mode::List;
  std::vector<std::string> files;  // full paths
  int sel = 0;

  std::string path;
  uint32_t fileSize = 0;
  uint32_t fedPos = 0;          // bytes already given to the splitter
  uint32_t offset = 0;          // byte offset after the current word
  std::vector<uint32_t> marks;  // offsets of every 10th word, for rewind
  int wordCount = 0;
  x3r::WordSplitter splitter;
  std::string word;
  bool playing = false;
  bool atEnd = false;
  int wpm = 240;
  volatile int refreshMs = 450;  // measured by render()
  uint32_t nextAtMs = 0;
  int sinceFull = 0;

  void loadFiles();
  bool openBook(const std::string& file);
  bool readWord();  // advances to the next word
  void seekTo(uint32_t pos);
  void rewind();
  void savePos() const;
  void drawList();
  void drawPlay();

 public:
  X3RsvpActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("X3Rsvp", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return playing; }
};
