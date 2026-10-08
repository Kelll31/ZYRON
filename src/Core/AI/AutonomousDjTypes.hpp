// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <optional>
#include <utility>
#include <vector>

#include "Core/AI/SetBuilderTypes.hpp"
#include "Core/AI/TransitionTypes.hpp"
#include "Core/Library/LibraryTypes.hpp"
#include "Core/State/Ids.hpp"

namespace zyron::core {

/// Lifecycle states of the autonomous AI DJ engine (SPEC section 58, ROADMAP P8-03).
enum class AutonomousDjStatus : std::uint8_t {
  Stopped = 0,
  Running,
  Paused,
  Finished,
  Error
};

/// Operational phase within a track or mix transition.
enum class AutonomousDjPhase : std::uint8_t {
  Idle = 0,
  LoadingFirstTrack,
  PlayingTrack,
  PreparingIncomingTrack,
  ExecutingTransition,
  Finished
};

/// Configuration parameters for autonomous DJ sets.
struct AutonomousDjConfig {
  std::string targetGenre{"Drum & Bass"};
  double targetDurationMinutes{60.0};
  double minBpm{160.0};
  double maxBpm{180.0};
  EnergyCurvePreset energyPreset{EnergyCurvePreset::PeakHour};
  TransitionStyle transitionStyle{TransitionStyle::BassSwap};
  double transitionDurationBeats{64.0};
  double leadTimeSeconds{30.0};          // Lead time before track end to initiate incoming mix
  std::vector<TransitionStyle> styleRotation;  // used in turn when not empty (see MixProfile); else transitionStyle
  bool fxHits{false};  // the mix profile's FX hits on the drops
  bool separateStems{false};  // ask for the stems of each track while it is preloaded (stem transitions need them)
};

/// Telemetry status snapshot of the autonomous DJ loop.
struct AutonomousDjTelemetry {
  AutonomousDjStatus status{AutonomousDjStatus::Stopped};
  AutonomousDjPhase phase{AutonomousDjPhase::Idle};
  DeckId activeDeck{DeckId::A};
  DeckId nextDeck{DeckId::B};
  std::int64_t currentTrackId{0};
  std::int64_t nextTrackId{0};
  std::size_t currentTrackIndex{0};
  std::size_t totalTracksInSet{0};
  double elapsedMinutes{0.0};
  std::string statusMessage{"Autonomous DJ is idle"};
};

/// The points of a track the mix is built around (seconds from the track start, negative = not set). They come from
/// the track markers: the user's own, or else the AI's proposals.
struct TrackMixPoints {
  double mixInSec{-1.0};   // where the track comes in
  double mixOutSec{-1.0};  // where the mix out of the track starts
  double dropSec{-1.0};    // the first drop: the transition into this track ends on it (the outgoing is gone)
  std::vector<double> drops;  // every drop, in time order (a mix out must not run into one)

  // Energy and voice, to match two tracks at a transition (all empty / 0 when the track is not analysed yet).
  std::vector<float> barEnergy;      // per bar, 0..1 relative to the track's loud level (30 dB range), full band
  std::vector<float> barBassEnergy;  // the same below 150 Hz: tells a drop from a breakdown
  double barOriginSec{0.0};          // bar i covers [barOriginSec + i * barSec, barOriginSec + (i + 1) * barSec)
  double barSec{0.0};                // 0: no profile
  std::vector<std::pair<double, double>> vocals;  // [start, end) seconds of every vocal section, in time order
  double loudnessLufs{0.0};          // integrated loudness (BS.1770 / R128); 0.0 = not measured, real values are < 0
};

class IMixPointProvider {
 public:
  virtual ~IMixPointProvider() = default;
  [[nodiscard]] virtual TrackMixPoints mixPoints(std::int64_t trackId) = 0;
};

/// One row of the Automix queue: what plays when, and why the AI chose it.
struct AutomixQueueEntry {
  TrackItem track;
  float fit{1.0F};          // 0..1: how well the track follows the previous one (the first track: 1)
  std::string reason;       // the AI's explanation (tempo, key, energy of the transition)
  TrackMixPoints points;    // markers the mix will use for this track
  TransitionStyle transitionIn{TransitionStyle::BassSwap};  // how the mix INTO this track will be done
  bool transitionChosen{false};  // picked by the DJ (otherwise it comes from the mix profile's rotation)
};

struct AutomixQueue {
  std::vector<AutomixQueueEntry> entries;
  std::size_t current{0};   // index of the track that plays now
  bool running{false};
  std::string summary;      // one line about the whole plan
};

/// How the Automix mixes, chosen in Settings: from a smooth radio-style blend to a DJ battle.
enum class MixMode : std::uint8_t {
  Smooth,  // long, clean blends and filter fades: easy listening, like a streaming app's crossfade but on the beat
  Club,    // blends, filter sweeps, loop rolls and brakes
  Battle,  // short and hard: scratches, loop rolls, cuts on the drop, brakes
  Custom   // the techniques ticked by hand
};

struct MixProfile {
  MixMode mode{MixMode::Club};
  bool blend{true};
  bool filter{true};
  bool loopRoll{true};
  bool brake{true};
  bool scratch{false};
  bool cut{false};
  bool beatLoop{true};
  bool doubleDrop{false};  // both drops together when the keys fit
  bool stems{false};       // separate the tracks into stems ahead of time and mix with them (acapella over the beat)
  bool fxOut{true};        // the outgoing track leaves in an echo (or a reverb wash in the smooth mode)
  bool fxHits{false};      // impacts, air horns and risers on the drops (synthesized)
  double transitionBeats{32.0};
  std::vector<TransitionStyle> favourites;  // what the DJ keeps choosing by hand: used more often

  /// The preset of a mode (Custom keeps the given toggles).
  [[nodiscard]] static MixProfile preset(MixMode mode) {
    MixProfile p;
    p.mode = mode;
    switch (mode) {
      case MixMode::Smooth:
        p = {mode, true, true, false, false, false, false, false, false, false, true, false, 32.0, {}};
        break;
      case MixMode::Club:
        p = {mode, true, true, true, true, false, false, true, false, true, true, false, 32.0, {}};
        break;
      case MixMode::Battle:
        p = {mode, false, true, true, true, true, true, true, true, false, true, true, 16.0, {}};
        break;
      case MixMode::Custom:
        break;
    }
    return p;
  }

  /// The transitions to use in turn (never empty: a plain blend when nothing is ticked).
  [[nodiscard]] std::vector<TransitionStyle> rotation() const {
    std::vector<TransitionStyle> styles;
    if (mode == MixMode::Battle) {  // the signature moves first, interleaved
      if (scratch) styles.push_back(TransitionStyle::Scratch);
      if (doubleDrop) styles.push_back(TransitionStyle::DoubleDrop);
      if (fxOut) styles.push_back(TransitionStyle::EchoOut);
      if (loopRoll) styles.push_back(TransitionStyle::LoopRoll);
      if (beatLoop) styles.push_back(TransitionStyle::BeatLoopIn);
      if (cut) styles.push_back(TransitionStyle::QuickCut);
      if (scratch) styles.push_back(TransitionStyle::Scratch);
      if (brake) styles.push_back(TransitionStyle::Brake);
      if (filter) styles.push_back(TransitionStyle::FilterFade);
    } else {
      if (blend) styles.push_back(TransitionStyle::BassSwap);
      if (filter) styles.push_back(TransitionStyle::FilterFade);
      if (beatLoop) styles.push_back(TransitionStyle::BeatLoopIn);
      if (loopRoll) styles.push_back(TransitionStyle::LoopRoll);
      if (blend) styles.push_back(TransitionStyle::BassSwap);
      if (brake) styles.push_back(TransitionStyle::Brake);
      if (scratch) styles.push_back(TransitionStyle::Scratch);
      if (cut) styles.push_back(TransitionStyle::QuickCut);
      if (stems) styles.push_back(TransitionStyle::StemBlend);
      if (doubleDrop) styles.push_back(TransitionStyle::DoubleDrop);
      if (fxOut) styles.push_back(mode == MixMode::Smooth ? TransitionStyle::ReverbOut : TransitionStyle::EchoOut);
    }
    for (const TransitionStyle favourite : favourites) {
      styles.push_back(favourite);  // once more each: the DJ's taste weighs in
    }
    if (styles.empty()) styles.push_back(TransitionStyle::BassSwap);
    return styles;
  }
};

/// A stretch of a track where the next (or running) transition does something, for drawing on the waveforms.
struct TransitionRegion {
  enum class Kind : std::uint8_t { Mix, Loop, Effect, Drop };
  DeckId deck{DeckId::A};
  double startSec{0.0};  // track time on that deck
  double endSec{0.0};
  Kind kind{Kind::Mix};
  std::string label;     // "BLEND", "LOOP ROLL", "SCRATCH", "DROP", ...
};

struct TransitionPreview {
  std::vector<TransitionRegion> regions;
};

/// What the UI needs from Automix: switch it on and off and show how it is doing. The application owns the loop that
/// drives the decks (SPEC section 58).
class IAutomixControl {
 public:
  virtual ~IAutomixControl() = default;

  /// Plans a set from the analysed tracks of the library and starts mixing. Returns false (and the telemetry says why)
  /// when there is nothing to play yet. Call from the UI thread.
  virtual bool start() = 0;
  virtual void stop() = 0;
  [[nodiscard]] virtual AutonomousDjTelemetry telemetry() const = 0;

  /// The planned set: what plays now, what comes next and why. Empty when nothing is planned.
  [[nodiscard]] virtual AutomixQueue queue() const { return {}; }
  /// "Play this next": with Automix running the track becomes the next one and the mix starts as soon as it is loaded;
  /// without Automix the playing deck is mixed into it right away. Returns an empty string on success, otherwise the
  /// reason (the track is not analysed, no free deck, ...). Call from the UI thread.
  virtual std::string mixNext(std::int64_t trackId) {
    (void)trackId;
    return "Mixing is not available";
  }
  /// Moves the playing track to a little before the transition the AI planned for it, so the mix can be heard at once.
  /// Returns false when nothing is planned or playing.
  virtual bool jumpToTransition() { return false; }
  /// What a live mix (without Automix) is doing, empty when none is running.
  [[nodiscard]] virtual std::string liveMixStatus() const { return {}; }
  /// How to mix from now on (the next transition already uses it). Call from the UI thread.
  virtual void setMixProfile(const MixProfile& profile) { (void)profile; }
  /// How many decks the DJ sees (2 or 4): a live mix only ever uses those. Call from the UI thread.
  virtual void setVisibleDeckCount(std::size_t count) { (void)count; }
  /// Chooses the transition into the queue entry `index` (nullopt: back to the profile's choice). False when that
  /// transition has already started or the index is not an upcoming track.
  virtual bool setTransitionStyle(std::size_t index, std::optional<TransitionStyle> style) {
    (void)index;
    (void)style;
    return false;
  }
  /// Where the next (or running) transition will loop, scratch, filter and drop, per deck, in track time.
  [[nodiscard]] virtual TransitionPreview transitionPreview() const { return {}; }

  /// Reorders or removes a track that has not started yet (the one being prepared is locked). False when refused.
  virtual bool moveUpcoming(std::size_t from, std::size_t to) {
    (void)from;
    (void)to;
    return false;
  }
  virtual bool removeUpcoming(std::size_t index) {
    (void)index;
    return false;
  }
};

/// Abstract interface for autonomous set sequencing and mixing (SPEC section 58, ROADMAP P8-03).
class IAutonomousDj {
 public:
  virtual ~IAutonomousDj() = default;

  /// Initializes and begins autonomous set playback using music library catalog.
  virtual bool start(
      const AutonomousDjConfig& config,
      const std::vector<TrackItem>& catalog) = 0;

  /// Halts autonomous playback and cancels pending scheduled transitions.
  virtual void stop() = 0;

  /// Temporarily pauses the autonomous sequencing loop.
  virtual void pause() = 0;

  /// Resumes autonomous sequencing loop.
  virtual void resume() = 0;

  /// Advances the autonomous loop state based on active deck playback progress.
  virtual void update(
      double elapsedSeconds,
      double activeDeckRemainingSec,
      double activeDeckBeat) = 0;

  /// Returns current operational telemetry.
  [[nodiscard]] virtual AutonomousDjTelemetry telemetry() const noexcept = 0;
};

}  // namespace zyron::core
