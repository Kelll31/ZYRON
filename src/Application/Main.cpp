// SPDX-License-Identifier: AGPL-3.0-only
//
// Composition root. It is the one place that knows every concrete module: it builds the Core services, picks the
// probes and the engine, wires them together and owns their lifetimes. (JUCE's application lifecycle lives here by
// necessity; all widgets live in UI/.)
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <iostream>
#include <memory>

#include "AI/Backends/Cuda/NvmlGpuProbe.hpp"
#include "Audio/Engine/AudioEngine.hpp"
#include "Audio/Routing/JuceAudioDeviceProbe.hpp"
#include "Core/Commands/CommandBus.hpp"
#include "Core/System/DynamicLibrary.hpp"
#include "Core/System/EngineStats.hpp"
#include "Core/System/HardwareInfo.hpp"
#include "Core/System/HardwareProbe.hpp"
#include "MIDI/JuceMidiProbe.hpp"
#include "Platform/Common/JuceSystemProbe.hpp"
#include "Platform/GpuLibraries.hpp"
#include "UI/MainWindow.hpp"

namespace {

using zyron::audio::JuceAudioDeviceProbe;
using zyron::core::HardwareDetector;
using zyron::core::HardwareReport;

constexpr int kSelfTestMilliseconds = 1500;
constexpr float kSelfTestToneDb =
    -70.0F;  // exercises the whole output path (and a real fade-out) while staying inaudible
const zyron::core::CommandOrigin kScriptOrigin{zyron::core::CommandOrigin::Kind::Script, "command-line"};

/// Probes that have no thread affinity: safe (and better) off the message thread.
HardwareDetector makeSystemAndGpuDetector() {
  return HardwareDetector{{
      std::make_shared<zyron::platform::JuceSystemProbe>(),
      std::make_shared<zyron::ai::NvmlGpuProbe>(zyron::core::openDynamicLibrary, zyron::platform::nvmlLibraryNames()),
      nullptr,
      nullptr,
  }};
}

/// Audio and MIDI enumeration: JUCE wants the message thread for these.
HardwareDetector makeDeviceDetector(JuceAudioDeviceProbe::Detail audioDetail) {
  return HardwareDetector{{
      nullptr,
      nullptr,
      std::make_shared<JuceAudioDeviceProbe>(audioDetail),
      std::make_shared<zyron::midi::JuceMidiProbe>(),
  }};
}

void appendWarnings(HardwareReport& into, const HardwareReport& from) {
  into.warnings.insert(into.warnings.end(), from.warnings.begin(), from.warnings.end());
}

class ZyronApplication final : public juce::JUCEApplication {
 public:
  const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
  const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
  bool moreThanOneInstanceAllowed() override { return false; }

  void initialise(const juce::String& commandLine) override {
    // `--print-hardware`: print the full hardware report (including audio device capabilities) and exit, no window.
    if (commandLine.contains("--print-hardware")) {
      HardwareReport report = makeSystemAndGpuDetector().detect();
      const HardwareReport devices = makeDeviceDetector(JuceAudioDeviceProbe::Detail::WithCapabilities).detect();
      report.audioDevices = devices.audioDevices;
      report.midiInputs = devices.midiInputs;
      report.midiOutputs = devices.midiOutputs;
      appendWarnings(report, devices);
      std::cout << zyron::core::formatReport(report) << std::flush;
      quit();
      return;
    }

    engine_ = std::make_shared<zyron::audio::AudioEngine>();
    bus_.addSink(engine_);
    engine_->start(store_.snapshot()->audioOutput);

    // `--audio-selftest`: run the output path for a moment with an inaudible tone and report whether callbacks ran.
    if (commandLine.contains("--audio-selftest")) {
      runAudioSelfTest();
      return;
    }

    window_ = std::make_unique<zyron::ui::MainWindow>(getApplicationName(), bus_, *engine_);
    startHardwareDetection();

    // `--smoke-test`: open the window, run the message loop once, exit 0. Used to prove that the binary starts and
    // shuts down cleanly.
    if (commandLine.contains("--smoke-test")) {
      juce::MessageManager::callAsync([this] { quit(); });
    }
  }

  void shutdown() override {
    alive_->store(false);  // queued callbacks become no-ops
    window_.reset();
    if (engine_ != nullptr) {
      bus_.removeSink(engine_);
      engine_->stop();
      engine_.reset();
    }
  }

  void systemRequestedQuit() override { quit(); }

 private:
  void runAudioSelfTest() {
    const zyron::core::AudioEngineStats before = engine_->stats();
    (void)bus_.submit(zyron::core::SetTestTone{true, 440.0F, kSelfTestToneDb}, kScriptOrigin);

    juce::Timer::callAfterDelay(kSelfTestMilliseconds, [this, before, alive = alive_] {
      if (!alive->load()) {
        return;
      }
      const zyron::core::AudioEngineStats after = engine_->stats();

      // Stopping fades the output out first (the tone is still playing, at -70 dB) and waits for the audio thread to
      // report silence; it must finish well inside the timeout, otherwise the handshake did not work.
      const juce::uint32 stopStarted = juce::Time::getMillisecondCounter();
      engine_->stop();
      const juce::uint32 stopMilliseconds = juce::Time::getMillisecondCounter() - stopStarted;
      const bool stopOk = stopMilliseconds < zyron::audio::AudioEngine::kFadeOutTimeoutMs;

      const bool ok =
          after.deviceOpen && after.callbackCount > before.callbackCount + 10 && after.lastError.empty() && stopOk;
      std::cout << zyron::core::formatEngineStats(after)
                << "\nCallbacks during test: " << (after.callbackCount - before.callbackCount)
                << "\nFade-out + stop: " << stopMilliseconds << " ms (limit "
                << zyron::audio::AudioEngine::kFadeOutTimeoutMs << ")"
                << "\nResult: " << (ok ? "OK" : "FAILED") << std::endl;
      setApplicationReturnValue(ok ? 0 : 1);
      quit();
    });
  }

  void startHardwareDetection() {
    // System + GPU on a worker thread (the NVML probe can take a while); the result is merged on the message thread.
    juce::Thread::launch([alive = alive_, this, detector = makeSystemAndGpuDetector()] {
      HardwareReport partial = detector.detect();
      juce::MessageManager::callAsync([alive, this, partial = std::move(partial)] {
        if (!alive->load()) {
          return;
        }
        report_.system = partial.system;
        report_.gpu = partial.gpu;
        appendWarnings(report_, partial);
        publishReport();
      });
    });

    // Audio + MIDI on the message thread, after the window is up. Only the quick scan (names, not capabilities).
    juce::MessageManager::callAsync([alive = alive_, this] {
      if (!alive->load()) {
        return;
      }
      const HardwareReport partial = makeDeviceDetector(JuceAudioDeviceProbe::Detail::NamesOnly).detect();
      report_.audioDevices = partial.audioDevices;
      report_.midiInputs = partial.midiInputs;
      report_.midiOutputs = partial.midiOutputs;
      appendWarnings(report_, partial);
      publishReport();
    });
  }

  void publishReport() {
    if (window_ != nullptr) {
      window_->showHardwareReport(report_);
    }
  }

  zyron::core::StateStore store_;
  zyron::core::EventBus events_;
  zyron::core::CommandBus bus_{store_, events_};
  std::shared_ptr<zyron::audio::AudioEngine> engine_;
  std::unique_ptr<zyron::ui::MainWindow> window_;
  HardwareReport report_;
  std::shared_ptr<std::atomic<bool>> alive_{std::make_shared<std::atomic<bool>>(true)};
};

}  // namespace

START_JUCE_APPLICATION(ZyronApplication)
