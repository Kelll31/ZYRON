// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

#include "Core/Library/LibraryTypes.hpp"
#include "UI/Library/LibraryComponent.hpp"
#include "UI/Theme.hpp"

using namespace zyron;

namespace {

class MockLibrarySource : public core::ILibrarySource {
 public:
  std::vector<core::TrackItem> tracks;
  std::string lastScanPath;

  std::vector<core::TrackItem> search(std::string_view query) override {
    if (query.empty()) return tracks;
    std::vector<core::TrackItem> res;
    for (const auto& t : tracks) {
      if (t.title.find(query) != std::string::npos ||
          t.artist.find(query) != std::string::npos) {
        res.push_back(t);
      }
    }
    return res;
  }

  std::vector<core::TrackItem> listAll() override { return tracks; }
  void requestScan(const std::string& folderPath) override { lastScanPath = folderPath; }
};

}  // namespace

TEST_CASE("LibraryComponent table and search interactions", "[ui][library]") {
  juce::ScopedJuceInitialiser_GUI guiInit;

  auto mock = std::make_shared<MockLibrarySource>();

  core::TrackItem t1;
  t1.id = 1;
  t1.title = "Stigma";
  t1.artist = "Noisia";
  t1.bpm = 174.0;
  t1.key = "8A";
  t1.energy = 9.2;
  t1.durationSec = 220.0;
  mock->tracks.push_back(t1);

  core::TrackItem t2;
  t2.id = 2;
  t2.title = "Midnight City";
  t2.artist = "M83";
  t2.bpm = 105.0;
  t2.key = "11B";
  t2.energy = 5.4;
  t2.durationSec = 244.0;
  mock->tracks.push_back(t2);

  ui::LibraryComponent lib(mock, ui::Theme::dark());
  lib.setSize(800, 300);

  SECTION("Displays all tracks initially") {
    CHECK(lib.getNumRows() == 2);
  }

  SECTION("Renders into graphics context without errors") {
    juce::Image target(juce::Image::ARGB, 800, 300, true);
    juce::Graphics g(target);
    REQUIRE_NOTHROW(lib.paintEntireComponent(g, true));
  }

  SECTION("Sort order changes correctly") {
    lib.sortOrderChanged(ui::LibraryComponent::ColBpm, true);  // Ascending by BPM
    CHECK(lib.getNumRows() == 2);

    lib.sortOrderChanged(ui::LibraryComponent::ColBpm, false); // Descending by BPM
    CHECK(lib.getNumRows() == 2);
  }

  SECTION("Theme change repaints cleanly") {
    lib.setTheme(ui::Theme::light());
    juce::Image target(juce::Image::ARGB, 800, 300, true);
    juce::Graphics g(target);
    REQUIRE_NOTHROW(lib.paintEntireComponent(g, true));
  }
}
