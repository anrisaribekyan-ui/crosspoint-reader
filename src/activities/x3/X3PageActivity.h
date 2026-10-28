#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Launcher pages fed by the gateway pack. One class, one page per instance.
class X3PageActivity final : public Activity {
 public:
  enum class Page : uint8_t {
    English,
    Today,
    Prayer,
    Services,
    Apps,
    Notes,
    TextView,
    Calendar,
    Qr,
    Focus,
    Update,
    Games
  };

 private:
  Page page;
  std::string arg;  // TextView: file path
  ButtonNavigator buttonNavigator;
  int sel = 0;

  // English
  bool revealed = false;
  bool answered = false;
  // Today
  std::vector<bool> doneLocal;
  // Notes / TextView
  std::vector<std::string> names;
  std::vector<std::string> lines;
  int textPage = 0;
  int linesPerPage = 1;
  // Focus
  int focusMin = 25;
  bool running = false;
  bool finished = false;
  uint32_t endMs = 0;
  int shownMin = -1;

  void open(Page next, const std::string& path = {});
  void loadNotes();
  void loadText();
  int itemCount() const;
  void activate();
  int remainingMin() const;

  int drawWrapped(const std::string& text, int fontId, int x, int y, int width, int maxLines, bool black = true);
  void drawList(const std::vector<std::string>& rows, int top, int bottom);

 public:
  X3PageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Page page, std::string arg = {})
      : Activity("X3Page", renderer, mappedInput), page(page), arg(std::move(arg)) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return running; }
};
