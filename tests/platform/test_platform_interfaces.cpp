// SPDX-License-Identifier: AGPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "Core/System/FileSystem.hpp"
#include "Core/System/PlatformWindow.hpp"

using zyron::core::createPlatformFileSystem;
using zyron::core::createPlatformWindow;

TEST_CASE("Platform FileSystem interface and factory") {
  auto fs = createPlatformFileSystem();
  REQUIRE(fs != nullptr);

  SECTION("standard directories are non-empty and well-formed") {
    const auto appData = fs->appDataDir();
    const auto models = fs->modelsDir();
    const auto cache = fs->cacheDir();
    const auto recordings = fs->recordingsDir();
    const auto music = fs->defaultMusicDir();

    CHECK_FALSE(appData.empty());
    CHECK_FALSE(models.empty());
    CHECK_FALSE(cache.empty());
    CHECK_FALSE(recordings.empty());
    CHECK_FALSE(music.empty());

    // models is a subdirectory of appData
    CHECK(models.parent_path() == appData);
  }

  SECTION("isAudioFile recognizes supported audio extensions case-insensitively") {
    CHECK(fs->isAudioFile("track.mp3"));
    CHECK(fs->isAudioFile("TRACK.MP3"));
    CHECK(fs->isAudioFile("song.wav"));
    CHECK(fs->isAudioFile("AUDIO.WAVE"));
    CHECK(fs->isAudioFile("music.flac"));
    CHECK(fs->isAudioFile("sample.aif"));
    CHECK(fs->isAudioFile("beat.AIFF"));
    CHECK(fs->isAudioFile("stem.m4a"));
    CHECK(fs->isAudioFile("stream.aac"));
    CHECK(fs->isAudioFile("loop.ogg"));

    CHECK_FALSE(fs->isAudioFile("document.pdf"));
    CHECK_FALSE(fs->isAudioFile("executable.exe"));
    CHECK_FALSE(fs->isAudioFile("source.cpp"));
    CHECK_FALSE(fs->isAudioFile("data.json"));
    CHECK_FALSE(fs->isAudioFile("no_extension"));
    CHECK_FALSE(fs->isAudioFile(""));
  }

  SECTION("isPathSafe guards against path traversal and control characters") {
    CHECK(fs->isPathSafe("music/track.mp3"));
    CHECK(fs->isPathSafe("C:/Music/dnb.wav"));
    CHECK(fs->isPathSafe("/home/user/music/tune.flac"));
    CHECK(fs->isPathSafe("song.mp3"));

    // Path traversal above root/base
    CHECK_FALSE(fs->isPathSafe("../secret.txt"));
    CHECK_FALSE(fs->isPathSafe("../../etc/passwd"));
    CHECK_FALSE(fs->isPathSafe("dir/../../escape.txt"));

    // Empty and null characters
    CHECK_FALSE(fs->isPathSafe(""));
    const std::string nullInPath("music\0/track.mp3", 16);
    CHECK_FALSE(fs->isPathSafe(nullInPath));
  }

  SECTION("directory creation and file queries function correctly") {
    const auto tempDir = std::filesystem::temp_directory_path() / "zyron_fs_test_dir";
    const auto testFile = tempDir / "test.txt";

    // Clean up any stale test dir
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);

    REQUIRE(fs->createDirectories(tempDir));
    REQUIRE(fs->exists(tempDir));

    {
      std::ofstream out(testFile);
      out << "zyron-test-content";
    }

    REQUIRE(fs->exists(testFile));
    CHECK(fs->fileSize(testFile) == 18);

    std::filesystem::remove_all(tempDir, ec);
    CHECK_FALSE(fs->exists(testFile));
    CHECK(fs->fileSize(testFile) == 0);
  }
}

TEST_CASE("PlatformWindow interface and factory") {
  auto window = createPlatformWindow();
  REQUIRE(window != nullptr);

  SECTION("display scale factor is in a valid range") {
    const double scale = window->displayScaleFactor();
    CHECK(scale >= 0.5);
    CHECK(scale <= 5.0);
  }

  SECTION("isSystemDarkMode returns a boolean without throwing") {
    // Should execute safely without error
    const bool isDark = window->isSystemDarkMode();
    (void)isDark;
    SUCCEED("isSystemDarkMode queried successfully");
  }
}
