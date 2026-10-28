#pragma once
#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "x3core.h"

// Home screen with six tiles fed by the cached gateway pack (/cache/pack.json).
class HubActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  x3::Pack pack;
  int selected = 0;

  void openTile(int index);

 public:
  explicit HubActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Hub", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
