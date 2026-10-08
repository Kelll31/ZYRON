// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>
#include <memory>

#include "Library/Database/Database.hpp"
#include "Library/Database/LibraryRepository.hpp"
#include "Library/Database/Migrations.hpp"
#include "Library/Database/SqliteLibrarySource.hpp"

using Catch::Matchers::WithinAbs;
using namespace zyron::library;

TEST_CASE("SqliteLibrarySource queries and search via FTS5", "[library][source]") {
  auto db = std::make_shared<Database>(Database::openInMemory());
  Migrations::apply(*db);

  TrackRecord t1;
  t1.filepath = "/music/dnb/noisia_outer_edges.flac";
  t1.contentHash = "hash_noisia_1";
  t1.title = "Collider";
  t1.artist = "Noisia";
  t1.album = "Outer Edges";
  t1.genre = "Neurofunk";
  t1.bpm = 174.0;
  t1.key = "8A";
  t1.energy = 8.8;
  t1.durationSec = 286.0;
  LibraryRepository::insertTrack(*db, t1);

  TrackRecord t2;
  t2.filepath = "/music/house/daft_punk_one_more_time.mp3";
  t2.contentHash = "hash_daft_2";
  t2.title = "One More Time";
  t2.artist = "Daft Punk";
  t2.album = "Discovery";
  t2.genre = "French House";
  t2.bpm = 123.0;
  t2.key = "11B";
  t2.energy = 6.5;
  t2.durationSec = 320.0;
  LibraryRepository::insertTrack(*db, t2);

  SqliteLibrarySource source(db, nullptr);

  SECTION("listAll returns all tracks with correct fields") {
    const auto all = source.listAll();
    REQUIRE(all.size() == 2);

    const auto& track = (all[0].artist == "Noisia") ? all[0] : all[1];
    CHECK(track.title == "Collider");
    CHECK(track.artist == "Noisia");
    CHECK(track.genre == "Neurofunk");
    CHECK_THAT(track.bpm, WithinAbs(174.0, 1e-4));
    CHECK(track.key == "8A");
    CHECK_THAT(track.energy, WithinAbs(8.8, 1e-4));
    CHECK(track.formatDuration() == "4:46");
  }

  SECTION("search by artist or genre returns matching tracks") {
    const auto neuro = source.search("Neurofunk");
    REQUIRE(neuro.size() == 1);
    CHECK(neuro[0].title == "Collider");

    const auto daft = source.search("Daft");
    REQUIRE(daft.size() == 1);
    CHECK(daft[0].title == "One More Time");
  }

  SECTION("empty search returns all tracks") {
    const auto res = source.search("");
    CHECK(res.size() == 2);
  }
}
