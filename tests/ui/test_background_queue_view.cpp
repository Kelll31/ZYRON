// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "Core/AI/BackgroundQueueTypes.hpp"
#include "UI/Settings/BackgroundQueueComponent.hpp"
#include "UI/Theme.hpp"

using namespace zyron;

namespace {

class MockQueueManager : public core::IBackgroundQueueManager {
 public:
  std::vector<core::QueueJobSnapshot> jobs;
  std::uint64_t cancelledId{0};
  bool cleared{false};

  [[nodiscard]] std::vector<core::QueueJobSnapshot> allJobs() const override { return jobs; }

  [[nodiscard]] std::size_t activeJobCount() const override {
    std::size_t c = 0;
    for (const auto& j : jobs) {
      if (j.status == core::QueueItemStatus::Running || j.status == core::QueueItemStatus::Queued) ++c;
    }
    return c;
  }

  bool cancelJob(std::uint64_t id) override {
    cancelledId = id;
    for (auto& j : jobs) {
      if (j.id == id) {
        j.status = core::QueueItemStatus::Cancelled;
        return true;
      }
    }
    return false;
  }

  void clearFinishedJobs() override {
    cleared = true;
    jobs.clear();
  }
};

}  // namespace

TEST_CASE("BackgroundQueueComponent: headless rendering and interactions", "[ui][queue]") {
  juce::ScopedJuceInitialiser_GUI guiInit;

  auto mock = std::make_shared<MockQueueManager>();

  core::QueueJobSnapshot j1;
  j1.id = 101;
  j1.name = "Stem Separation: Track A";
  j1.priority = core::QueuePriority::Interactive;
  j1.status = core::QueueItemStatus::Running;
  j1.progress = 0.45F;
  j1.deviceIndex = 0;
  j1.deviceName = "GPU 0";
  mock->jobs.push_back(j1);

  core::QueueJobSnapshot j2;
  j2.id = 102;
  j2.name = "Batch Caching: Library Folder";
  j2.priority = core::QueuePriority::Background;
  j2.status = core::QueueItemStatus::Queued;
  j2.progress = 0.0F;
  j2.deviceIndex = 1;
  j2.deviceName = "GPU 1";
  mock->jobs.push_back(j2);

  ui::BackgroundQueueComponent comp(mock);
  comp.setSize(800, 300);

  SECTION("Reports correct row and job count") {
    CHECK(comp.getNumRows() == 2);
    CHECK(comp.jobs().size() == 2);
  }

  SECTION("Paints cells and progress bar cleanly without exceptions") {
    juce::Image img(juce::Image::ARGB, 800, 300, true);
    juce::Graphics g(img);

    for (int col = 1; col <= 6; ++col) {
      comp.paintCell(g, 0, col, 120, 24, false);
      comp.paintCell(g, 1, col, 120, 24, true);
    }
    comp.paintRowBackground(g, 0, 800, 24, false);
    comp.paintRowBackground(g, 1, 800, 24, true);
  }

  SECTION("Theme update and resizing succeed") {
    comp.setTheme(ui::Theme::light());
    comp.setSize(1000, 500);
    CHECK(comp.getWidth() == 1000);
  }
}
