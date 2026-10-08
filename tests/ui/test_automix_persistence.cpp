// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>

#include "UI/Automix/AutomixPersistence.hpp"
#include "UI/Automix/AutomixTaste.hpp"
#include "UI/Deck/KeyMath.hpp"
#include "UI/Fx/FxModel.hpp"
#include "UI/Settings/Preferences.hpp"

using namespace zyron;
using ui::LayoutSizes;

TEST_CASE("MixProfile survives a save/load round trip", "[ui][automix_profile]") {
  for (const auto mode : {core::MixMode::Smooth, core::MixMode::Club, core::MixMode::Battle}) {
    const auto original = core::MixProfile::preset(mode);
    const auto restored = ui::parseMixProfile(ui::formatMixProfile(original));
    REQUIRE(restored.mode == original.mode);
    REQUIRE(restored.blend == original.blend);
    REQUIRE(restored.filter == original.filter);
    REQUIRE(restored.loopRoll == original.loopRoll);
    REQUIRE(restored.brake == original.brake);
    REQUIRE(restored.scratch == original.scratch);
    REQUIRE(restored.cut == original.cut);
    REQUIRE(restored.transitionBeats == original.transitionBeats);
  }
}

TEST_CASE("A custom profile keeps its hand-ticked techniques", "[ui][automix_profile]") {
  core::MixProfile custom = core::MixProfile::preset(core::MixMode::Custom);
  custom.scratch = true;
  custom.cut = true;
  custom.blend = false;
  custom.transitionBeats = 64.0;
  const auto restored = ui::parseMixProfile(ui::formatMixProfile(custom));
  REQUIRE(restored.mode == core::MixMode::Custom);
  REQUIRE(restored.scratch);
  REQUIRE(restored.cut);
  REQUIRE_FALSE(restored.blend);
  REQUIRE(restored.transitionBeats == 64.0);
}

TEST_CASE("Profile parsing is defensive", "[ui][automix_profile]") {
  SECTION("empty or garbage text gives the Club default") {
    for (const char* text : {"", "\n\n", "not a key value line", "=novalue", "mode=nonsense\nblend=maybe"}) {
      const auto p = ui::parseMixProfile(text);
      REQUIRE(p.mode == core::MixMode::Club);
      REQUIRE(p.transitionBeats == 32.0);
    }
  }
  SECTION("unknown keys, comments, CRLF and spaces are tolerated") {
    const auto p = ui::parseMixProfile("# note\r\nfuture=1\r\n mode = battle \r\nscratch=0\r\n");
    REQUIRE(p.mode == core::MixMode::Battle);
    REQUIRE_FALSE(p.scratch);  // overridden from the Battle preset
    REQUIRE(p.cut);            // missing keys keep the preset's value
  }
  SECTION("the transition length snaps to 16 / 32 / 64") {
    REQUIRE(ui::parseMixProfile("transitionBeats=20").transitionBeats == 16.0);
    REQUIRE(ui::parseMixProfile("transitionBeats=50").transitionBeats == 64.0);
    REQUIRE(ui::parseMixProfile("transitionBeats=-4").transitionBeats == 32.0);
    REQUIRE(ui::parseMixProfile("transitionBeats=abc").transitionBeats == 32.0);
  }
}

TEST_CASE("Layout sizes round trip and reject junk", "[ui][layout]") {
  const LayoutSizes saved{312, 288};
  const auto restored = ui::parseLayoutSizes(ui::formatLayoutSizes(saved));
  REQUIRE(restored.bottomHeight == 312);
  REQUIRE(restored.automixSettingsWidth == 288);

  const auto junk = ui::parseLayoutSizes("bottomHeight=-50\nautomixSettingsWidth=wide\n");
  REQUIRE(junk.bottomHeight == 0);  // 0 = use the default
  REQUIRE(junk.automixSettingsWidth == 0);
}

TEST_CASE("Upper-area layout sizes round trip, old files still load", "[ui][layout]") {
  LayoutSizes saved;
  saved.waveformHeight = 90;
  saved.mixerWidth2 = 240;
  saved.mixerWidth4 = 300;
  saved.deckSplitPercent = 45;
  const auto restored = ui::parseLayoutSizes(ui::formatLayoutSizes(saved));
  REQUIRE(restored.waveformHeight == 90);
  REQUIRE(restored.mixerWidth2 == 240);
  REQUIRE(restored.mixerWidth4 == 300);
  REQUIRE(restored.deckSplitPercent == 45);

  const auto old = ui::parseLayoutSizes("bottomHeight=200\nautomixSettingsWidth=300\n");  // a file from before
  REQUIRE(old.bottomHeight == 200);
  REQUIRE(old.waveformHeight == 0);
  REQUIRE(old.deckSplitPercent == 0);

  REQUIRE(ui::parseLayoutSizes("deckSplitPercent=500\nmixerWidth2=-4\n").deckSplitPercent == 0);
  REQUIRE(ui::parseLayoutSizes("mixerWidth2=-4\n").mixerWidth2 == 0);
}

TEST_CASE("The DJ-sound toggles of the mix profile round trip", "[ui][automix_profile]") {
  core::MixProfile custom = core::MixProfile::preset(core::MixMode::Custom);
  custom.doubleDrop = true;
  custom.stems = true;
  custom.fxOut = false;
  custom.fxHits = true;
  const auto restored = ui::parseMixProfile(ui::formatMixProfile(custom));
  REQUIRE(restored.doubleDrop);
  REQUIRE(restored.stems);
  REQUIRE_FALSE(restored.fxOut);
  REQUIRE(restored.fxHits);
}

TEST_CASE("Old automix files without the new keys keep the preset values", "[ui][automix_profile]") {
  const auto battle = ui::parseMixProfile("mode=battle\nblend=0\n");
  const auto preset = core::MixProfile::preset(core::MixMode::Battle);
  REQUIRE(battle.doubleDrop == preset.doubleDrop);
  REQUIRE(battle.stems == preset.stems);
  REQUIRE(battle.fxOut == preset.fxOut);
  REQUIRE(battle.fxHits == preset.fxHits);
  REQUIRE(ui::parseMixProfile("mode=club\nfxHits=maybe\n").fxHits == core::MixProfile::preset(core::MixMode::Club).fxHits);
}

TEST_CASE("Taste counting turns the third pick into a favourite", "[ui][taste]") {
  ui::TasteCounts counts;
  ui::recordChoice(counts, core::TransitionStyle::EchoOut);
  ui::recordChoice(counts, core::TransitionStyle::EchoOut);
  REQUIRE(ui::favouritesOf(counts).empty());
  ui::recordChoice(counts, core::TransitionStyle::EchoOut);
  ui::recordChoice(counts, core::TransitionStyle::Brake);
  for (int i = 0; i < 5; ++i) {
    ui::recordChoice(counts, core::TransitionStyle::DoubleDrop);
  }
  const auto favourites = ui::favouritesOf(counts);
  REQUIRE(favourites == std::vector<core::TransitionStyle>{core::TransitionStyle::DoubleDrop,
                                                           core::TransitionStyle::EchoOut});  // enum order, once each
}

TEST_CASE("Taste survives a save/load round trip and rejects junk", "[ui][taste]") {
  ui::TasteCounts counts;
  counts[core::TransitionStyle::StemBlend] = 4;
  counts[core::TransitionStyle::ReverbOut] = 1;
  REQUIRE(ui::parseTaste(ui::formatTaste(counts)) == counts);
  const auto junk = ui::parseTaste("Nonsense=3\nBrake=abc\nBrake=-2\n=5\nScratch=2\n# c\n");
  REQUIRE(junk.size() == 1);
  REQUIRE(junk.at(core::TransitionStyle::Scratch) == 2);
}

TEST_CASE("Favourites make the rotation use a style once more", "[ui][taste]") {
  auto profile = core::MixProfile::preset(core::MixMode::Club);
  const auto before = profile.rotation().size();
  profile.favourites = ui::favouritesOf({{core::TransitionStyle::Brake, 3}});
  REQUIRE(profile.rotation().size() == before + 1);
}

TEST_CASE("Effective key: one semitone is +7 on the Camelot wheel", "[ui][key]") {
  REQUIRE(ui::shiftCamelot("8A", 0) == "8A");
  REQUIRE(ui::shiftCamelot("8A", 1) == "3A");
  REQUIRE(ui::shiftCamelot("8B", -1) == "1B");
  REQUIRE(ui::shiftCamelot("12A", 2) == "2A");
  REQUIRE(ui::shiftCamelot("1A", -6) == "7A");
  REQUIRE(ui::shiftCamelot("5a", 12) == "5A");
  REQUIRE(ui::shiftCamelot("8A", 0.4) == "8A");  // rounds to whole semitones
  for (const char* odd : {"", "Am", "13A", "0B", "8C", "x"}) {
    REQUIRE(ui::shiftCamelot(odd, 3) == odd);
  }
}

TEST_CASE("Without keylock the tempo also moves the heard key", "[ui][key]") {
  REQUIRE(ui::effectiveSemitones(2.0F, true, 1.08) == 2.0);
  REQUIRE(ui::effectiveSemitones(0.0F, false, 1.0) == 0.0);
  REQUIRE(ui::effectiveSemitones(0.0F, false, 2.0) == Catch::Approx(12.0));
  REQUIRE(ui::effectiveSemitones(1.0F, false, 0.0) == 1.0);  // no valid speed: ignored
  REQUIRE(ui::formatSemitones(0.0) == "0.0 st");
  REQUIRE(ui::formatSemitones(2.0) == "+2.0 st");
  REQUIRE(ui::formatSemitones(-1.5) == "-1.5 st");
}

TEST_CASE("The loudness trim brings a track to -9 LUFS within the engine range", "[ui][loudness]") {
  REQUIRE(ui::loudnessTrimDb(-14.0).value() == Catch::Approx(5.0F));
  REQUIRE(ui::loudnessTrimDb(-6.0).value() == Catch::Approx(-3.0F));
  REQUIRE(ui::loudnessTrimDb(-40.0).value() == 12.0F);
  REQUIRE(ui::loudnessTrimDb(-1.0).value() == Catch::Approx(-8.0F));
  REQUIRE_FALSE(ui::loudnessTrimDb(0.0).has_value());  // not measured
  REQUIRE_FALSE(ui::loudnessTrimDb(3.0).has_value());
}

TEST_CASE("Preferences round trip; old files keep the defaults", "[ui][preferences]") {
  ui::Preferences custom;
  custom.autoLoudness = false;
  custom.limiter = false;
  REQUIRE(ui::parsePreferences(ui::formatPreferences(custom)) == custom);
  const auto defaults = ui::parsePreferences("");
  REQUIRE(defaults.autoLoudness);
  REQUIRE(defaults.glue);
  REQUIRE(defaults.limiter);
  const auto partial = ui::parsePreferences("# c\nglue=0\njunk\nlimiter=perhaps\n");
  REQUIRE_FALSE(partial.glue);
  REQUIRE(partial.limiter);
}

TEST_CASE("FX tempo is re-sent only for a real change, a few times a second", "[ui][fx]") {
  ui::FxTempoTracker tracker;
  REQUIRE_FALSE(tracker.update(0.0, 1.0, 0).has_value());  // no tempo yet
  const auto first = tracker.update(120.0, 1.0, 1);
  REQUIRE(first.has_value());
  REQUIRE(*first == Catch::Approx(0.5));
  REQUIRE_FALSE(tracker.update(120.0, 1.0, 50).has_value());          // unchanged
  REQUIRE_FALSE(tracker.update(120.0, 1.003, 60).has_value());        // 0.3 %: below the threshold
  REQUIRE_FALSE(tracker.update(120.0, 1.05, 3).has_value());          // a real change, but too soon after the last send
  const auto later = tracker.update(120.0, 1.05, 100);
  REQUIRE(later.has_value());
  REQUIRE(*later == Catch::Approx(60.0 / (120.0 * 1.05)));
  const auto clamped = tracker.update(5.0, 1.0, 200);  // 5 BPM would be a 12 s beat: the engine accepts 3 s at most
  REQUIRE(clamped.has_value());
  REQUIRE(*clamped == ui::kFxBeatSecondsMax);
}

TEST_CASE("An FX hit follows the tempo of the first playing deck", "[ui][fx]") {
  std::array<ui::DeckBeat, 3> decks{{{false, 128.0, 1.0}, {true, 0.0, 1.0}, {true, 120.0, 1.0}}};
  REQUIRE(ui::fxHitBeatSeconds(decks) == Catch::Approx(0.5));
  decks[2].playbackSpeed = 1.1;
  REQUIRE(ui::fxHitBeatSeconds(decks) == Catch::Approx(60.0 / 132.0));
  decks[2].playing = false;
  REQUIRE(ui::fxHitBeatSeconds(decks) == 0.0);  // nothing to follow: free
}
