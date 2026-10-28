#pragma once
#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Home screen with six tiles fed by the gateway pack. Long-press OK syncs.
class HubActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selected = 0;

  void openTile(int index);
  void sync();

 public:
  explicit HubActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Hub", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
