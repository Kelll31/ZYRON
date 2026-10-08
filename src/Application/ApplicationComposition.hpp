// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "Audio/Engine/AudioEngine.hpp"
#include "Application/AutomixController.hpp"
#include "Application/NeuralModels.hpp"
#include "Application/StemService.hpp"
#include "Core/Commands/CommandBus.hpp"
#include "Core/Events/EventBus.hpp"
#include "Core/State/AppState.hpp"
#include "Library/LibraryService.hpp"
#include "Recording/MasterRecorder.hpp"

namespace zyron::application {

/// The wiring of the real application, in one place: state, command bus, library, audio engine. Main.cpp and the
/// app-level tests build the same object, so what the tests prove is what the user runs.
///
/// It does not open an audio device and creates no window: the caller decides (Main starts the device, tests drive
/// the engine callback directly).
class ApplicationComposition {
 public:
  /// `recordingsDir` receives the master recordings (default: dataDir/recordings). `dataDir` holds library.db and the
  /// remembered scan folders. Creating it is part of construction. If the library
  /// cannot be opened the application still comes up, without a library; libraryError() says why.
  explicit ApplicationComposition(std::filesystem::path dataDir, std::filesystem::path recordingsDir = {});
  ~ApplicationComposition();

  ApplicationComposition(const ApplicationComposition&) = delete;
  ApplicationComposition& operator=(const ApplicationComposition&) = delete;

  [[nodiscard]] core::StateStore& store() noexcept { return store_; }
  [[nodiscard]] core::CommandBus& bus() noexcept { return bus_; }
  [[nodiscard]] const std::shared_ptr<audio::AudioEngine>& engine() const noexcept { return engine_; }
  [[nodiscard]] const std::shared_ptr<library::LibraryService>& library() const noexcept { return library_; }
  [[nodiscard]] const std::filesystem::path& dataDir() const noexcept { return dataDir_; }
  [[nodiscard]] const std::string& libraryError() const noexcept { return libraryError_; }
  /// Null when there is no library (nothing to mix).
  [[nodiscard]] AutomixController* automix() const noexcept { return automix_.get(); }
  [[nodiscard]] NeuralModels& models() noexcept { return models_; }

 private:
  [[nodiscard]] std::string setRecording(bool enabled);

  std::filesystem::path dataDir_;
  std::filesystem::path recordingsDir_;
  std::string libraryError_;
  core::StateStore store_;
  core::EventBus events_;
  core::CommandBus bus_{store_, events_};
  std::shared_ptr<library::LibraryService> library_;  // before the engine: the engine resolves paths through it
  NeuralModels models_;
  recording::MasterRecorder recorder_;  // before the engine: the engine's tap points at it
  std::shared_ptr<audio::AudioEngine> engine_;
  std::unique_ptr<StemService> stems_;           // after the engine: its worker hands stems to the engine
  std::unique_ptr<AutomixController> automix_;  // last: it drives the bus, the engine and the library
};

}  // namespace zyron::application
