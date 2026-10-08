// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Autonomous/AutonomousDjLoop.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <utility>

#include "AI/Transition/TransitionPlanner.hpp"

namespace zyron::ai {

namespace {

/// Camelot neighbours mix without a clash: the same key, +-1 on the wheel, or the relative major/minor.
bool keysCompatible(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) {
    return false;
  }
  const auto parse = [](const std::string& key, int& number, char& letter) {
    number = std::atoi(key.c_str());
    letter = key.empty() ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(key.back())));
    return number >= 1 && number <= 12 && (letter == 'A' || letter == 'B');
  };
  int na = 0;
  int nb = 0;
  char la = ' ';
  char lb = ' ';
  if (!parse(a, na, la) || !parse(b, nb, lb)) {
    return false;
  }
  const int step = std::abs(na - nb) % 12;
  const int wheel = std::min(step, 12 - step);
  return (la == lb && wheel <= 1) || (la != lb && wheel == 0);
}

/// Mean bar energy (0..1) of a track between two times; -1 without a profile.
double meanEnergy(const core::TrackMixPoints& points, double fromSec, double toSec) {
  if (points.barSec <= 0.0 || points.barEnergy.empty() || toSec <= fromSec) {
    return -1.0;
  }
  const auto first = static_cast<long>(std::floor((fromSec - points.barOriginSec) / points.barSec));
  const auto last = static_cast<long>(std::ceil((toSec - points.barOriginSec) / points.barSec));
  double sum = 0.0;
  int count = 0;
  for (long bar = std::max(0L, first); bar < last && bar < static_cast<long>(points.barEnergy.size()); ++bar) {
    sum += points.barEnergy[static_cast<std::size_t>(bar)];
    ++count;
  }
  return count > 0 ? sum / count : -1.0;
}

bool vocalsIn(const core::TrackMixPoints& points, double fromSec, double toSec) {
  for (const auto& [start, end] : points.vocals) {
    if (start < toSec && end > fromSec) {
      return true;
    }
  }
  return false;
}

/// The smallest key shift (semitones, within +-2) that makes `incoming` mix with `outgoing`; 0 when it already does or
/// nothing that small helps. One semitone up is seven steps round the Camelot wheel.
float harmonicShift(const std::string& outgoing, const std::string& incoming) {
  if (keysCompatible(outgoing, incoming) || incoming.size() < 2) {
    return 0.0F;
  }
  const int number = std::atoi(incoming.c_str());
  const char letter = incoming.back();
  for (const int semitones : {1, -1, 2, -2}) {
    const int shifted = ((number - 1 + 7 * semitones) % 12 + 12) % 12 + 1;
    if (keysCompatible(outgoing, std::to_string(shifted) + letter)) {
      return static_cast<float>(semitones);
    }
  }
  return 0.0F;
}

/// Gain trim that brings a track to the set's loudness (club masters sit around -9 LUFS). 0 when not measured.
float loudnessTrim(double lufs) {
  constexpr double kTargetLufs = -9.0;
  if (lufs >= 0.0) {
    return 0.0F;
  }
  return static_cast<float>(std::clamp(kTargetLufs - lufs, -12.0, 12.0));
}

constexpr double kStartLookaheadSec = 1.5;  // plan the transition this long before it starts, on the beat grid
}  // namespace

namespace {
}  // namespace

AutonomousDjLoop::AutonomousDjLoop(
    core::CommandBus& commandBus,
    core::ISetBuilder& setBuilder,
    core::ITransitionPlanner& transitionPlanner,
    core::ICommandTimelineScheduler& timelineScheduler)
    : commandBus_(commandBus),
      setBuilder_(setBuilder),
      transitionPlanner_(transitionPlanner),
      timelineScheduler_(timelineScheduler) {}

bool AutonomousDjLoop::start(
    const core::AutonomousDjConfig& config,
    const std::vector<core::TrackItem>& catalog) {
  std::lock_guard<std::mutex> lock(mutex_);

  core::SetBuilderRequest req;
  req.targetGenre = config.targetGenre;
  req.targetDurationMinutes = config.targetDurationMinutes;
  req.minBpm = config.minBpm;
  req.maxBpm = config.maxBpm;
  req.preset = config.energyPreset;

  setPlan_ = setBuilder_.buildSet(req, catalog);
  if (!setPlan_.success || setPlan_.tracks.empty()) {
    telemetry_.status = core::AutonomousDjStatus::Error;
    telemetry_.statusMessage = "Set planning failed: insufficient matching catalog tracks.";
    return false;
  }

  config_ = config;
  activeDeck_ = core::DeckId::A;
  nextDeck_ = core::DeckId::B;
  currentTrackIndex_ = 0;

  const auto origin = core::CommandOrigin{core::CommandOrigin::Kind::Ai, "autonomous_dj"};

  // Clear timeline and prepare Deck A
  timelineScheduler_.clear();
  timelineScheduler_.clearDeckOverride(core::DeckId::A);
  timelineScheduler_.clearDeckOverride(core::DeckId::B);

  const auto firstTrackId = setPlan_.tracks[0].track.id;
  (void)commandBus_.submit(core::LoadTrack{activeDeck_, core::TrackId{firstTrackId}}, origin);
  (void)commandBus_.submit(core::SetVolume{activeDeck_, 1.0f}, origin);
  (void)commandBus_.submit(core::SetEq{activeDeck_, core::EqBand::Low, 0.0f}, origin);
  (void)commandBus_.submit(core::SetEq{activeDeck_, core::EqBand::Mid, 0.0f}, origin);
  (void)commandBus_.submit(core::SetEq{activeDeck_, core::EqBand::High, 0.0f}, origin);
  (void)commandBus_.submit(core::SetCrossfader{0.0f}, origin);
  if (mixPoints_ != nullptr) {
    const core::TrackMixPoints points = mixPoints_->mixPoints(firstTrackId);
    firstTrackSeekSec_ = points.mixInSec > 0.0 ? points.mixInSec : -1.0;
  } else {
    firstTrackSeekSec_ = -1.0;
  }

  // A play command sent before the file is decoded would be lost: with a load source, wait for the deck first.
  const bool playNow = (loads_ == nullptr);
  if (playNow) {
    (void)commandBus_.submit(core::Play{activeDeck_}, origin);
  }

  telemetry_.status = core::AutonomousDjStatus::Running;
  telemetry_.phase = playNow ? core::AutonomousDjPhase::PlayingTrack : core::AutonomousDjPhase::LoadingFirstTrack;
  telemetry_.activeDeck = activeDeck_;
  telemetry_.nextDeck = nextDeck_;
  telemetry_.currentTrackId = firstTrackId;
  telemetry_.currentTrackIndex = 1;
  telemetry_.totalTracksInSet = setPlan_.tracks.size();
  telemetry_.elapsedMinutes = 0.0;
  telemetry_.statusMessage = "Playing track 1 of " + std::to_string(setPlan_.tracks.size()) +
                             ": " + setPlan_.tracks[0].track.title;

  return true;
}

void AutonomousDjLoop::stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto origin = core::CommandOrigin{core::CommandOrigin::Kind::Ai, "autonomous_dj"};
  timelineScheduler_.clear();
  for (const core::DeckId deck : {core::DeckId::A, core::DeckId::B}) {
    (void)commandBus_.submit(core::Pause{deck}, origin);
    // A transition may have been cut short: leave the decks neutral, not ducked or killed.
    (void)commandBus_.submit(core::SetVolume{deck, 1.0f}, origin);
    for (const core::EqBand band : {core::EqBand::Low, core::EqBand::Mid, core::EqBand::High}) {
      (void)commandBus_.submit(core::SetEq{deck, band, 0.0f}, origin);
    }
  }

  telemetry_.status = core::AutonomousDjStatus::Stopped;
  telemetry_.phase = core::AutonomousDjPhase::Idle;
  telemetry_.statusMessage = "Autonomous DJ stopped.";
}

void AutonomousDjLoop::pause() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (telemetry_.status == core::AutonomousDjStatus::Running) {
    telemetry_.status = core::AutonomousDjStatus::Paused;
    telemetry_.statusMessage = "Autonomous DJ paused.";
  }
}

void AutonomousDjLoop::resume() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (telemetry_.status == core::AutonomousDjStatus::Paused) {
    telemetry_.status = core::AutonomousDjStatus::Running;
    telemetry_.statusMessage = "Autonomous DJ resumed.";
  }
}

void AutonomousDjLoop::update(
    double elapsedSeconds,
    double activeDeckRemainingSec,
    double activeDeckBeat) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (telemetry_.status != core::AutonomousDjStatus::Running) {
    return;
  }

  telemetry_.elapsedMinutes = elapsedSeconds / 60.0;
  lastElapsedSec_ = elapsedSeconds;

  // During a transition the outgoing deck may loop or brake, so its position stops being a clock: the transition's
  // beats then run on time from where it was planned, at the tempo it had.
  // The incoming deck is that clock once it plays: it never loops and runs beat for beat with the outgoing one.
  if (telemetry_.phase == core::AutonomousDjPhase::ExecutingTransition && clockFromIncoming_ && incomingPlaying_ &&
      clockBeatsPerSec_ > 0.0 &&
      incomingPositionSec_ >= clockAnchorSec_) {
    activeDeckBeat = std::max(clockAnchorBeat_,
                              clockAnchorBeat_ + (incomingPositionSec_ - clockAnchorSec_) * clockBeatsPerSec_);
  }

  // Advance scheduled timeline events
  timelineScheduler_.advanceToBeats(activeDeck_, activeDeckBeat);

  // Check user override
  if (timelineScheduler_.isDeckOverridden(activeDeck_)) {
    telemetry_.statusMessage = "User manual control active on Deck " +
                               std::to_string(static_cast<int>(core::index(activeDeck_)) + 1);
    return;
  }

  const auto origin = core::CommandOrigin{core::CommandOrigin::Kind::Ai, "autonomous_dj"};

  if (telemetry_.phase == core::AutonomousDjPhase::LoadingFirstTrack) {
    std::string failure;
    if (isLoaded(activeDeck_, telemetry_.currentTrackId, &failure)) {
      if (firstTrackSeekSec_ > 0.0) {
        (void)commandBus_.submit(core::Seek{activeDeck_, firstTrackSeekSec_}, origin);
      }
      if (mixPoints_ != nullptr) {
        (void)commandBus_.submit(
            core::SetTrackGainTrim{activeDeck_, loudnessTrim(mixPoints_->mixPoints(telemetry_.currentTrackId).loudnessLufs)},
            origin);
      }
      (void)commandBus_.submit(core::Play{activeDeck_}, origin);
      telemetry_.phase = core::AutonomousDjPhase::PlayingTrack;
    } else if (!failure.empty()) {
      telemetry_.status = core::AutonomousDjStatus::Error;
      telemetry_.statusMessage = "Could not load the first track: " + failure;
    }
    return;
  }

  if (telemetry_.phase == core::AutonomousDjPhase::PlayingTrack) {
    const bool hasNext = (currentTrackIndex_ + 1) < setPlan_.tracks.size();
    // The next track is loaded as soon as the current one plays, so its deck shows its waveform and markers in advance
    // and everything is ready (decoded, grid known) when the transition begins.
    if (hasNext) {
      const auto& nextTrack = setPlan_.tracks[currentTrackIndex_ + 1].track;

      // Bring the incoming deck to a neutral state (the previous track left it ducked), then load it.
      (void)commandBus_.submit(core::SetEq{nextDeck_, core::EqBand::Low, 0.0f}, origin);
      (void)commandBus_.submit(core::SetEq{nextDeck_, core::EqBand::Mid, 0.0f}, origin);
      (void)commandBus_.submit(core::SetEq{nextDeck_, core::EqBand::High, 0.0f}, origin);
      (void)commandBus_.submit(core::SetVolume{nextDeck_, 1.0f}, origin);
      (void)commandBus_.submit(core::SetKeyShift{nextDeck_, 0.0F}, origin);
      (void)commandBus_.submit(core::SetFx{nextDeck_, 0, core::FxType::None, false, 0.0f, 0.5f, true}, origin);
      if (mixPoints_ != nullptr) {
        (void)commandBus_.submit(
            core::SetTrackGainTrim{nextDeck_, loudnessTrim(mixPoints_->mixPoints(nextTrack.id).loudnessLufs)}, origin);
      }
      (void)commandBus_.submit(core::LoadTrack{nextDeck_, core::TrackId{nextTrack.id}}, origin);

      telemetry_.phase = core::AutonomousDjPhase::PreparingIncomingTrack;
      telemetry_.nextTrackId = nextTrack.id;
      telemetry_.statusMessage = "Loading next track: " + nextTrack.title;
    }
  }

  if (telemetry_.phase == core::AutonomousDjPhase::PreparingIncomingTrack) {
    std::string failure;
    // Stem transitions need stems: ask for them as soon as the next track is on its deck (minutes ahead of the mix).
    if (config_.separateStems && stemsRequestedFor_ != telemetry_.nextTrackId &&
        isLoaded(nextDeck_, telemetry_.nextTrackId, nullptr)) {
      stemsRequestedFor_ = telemetry_.nextTrackId;
      (void)commandBus_.submit(core::SeparateStems{nextDeck_}, origin);
    }
    if (!isLoaded(nextDeck_, telemetry_.nextTrackId, &failure)) {
      if (!failure.empty()) {
        telemetry_.status = core::AutonomousDjStatus::Error;
        telemetry_.statusMessage = "Could not load the next track: " + failure;
      }
    } else if (forceTransition_ || activeDeckRemainingSec <= leadSeconds() + kStartLookaheadSec) {
      // The transition starts exactly on the beat of the mix-out point (a bar of the outgoing track), not on whatever
      // update noticed it: the steps are scheduled ahead on the beat grid.
      double startBeat = std::ceil(activeDeckBeat);
      const double bpm = setPlan_.tracks[currentTrackIndex_].track.bpm;
      if (!forceTransition_ && bpm > 0.0) {
        startBeat = std::max(startBeat, std::round(activeDeckBeat + (activeDeckRemainingSec - leadSeconds()) * bpm / 60.0));
      }
      forceTransition_ = false;
      startTransition(activeDeckBeat, startBeat);
    }
  } else if (telemetry_.phase == core::AutonomousDjPhase::ExecutingTransition) {
    const double transitionEndBeat = transitionStartBeat_ + transitionBeats_;
    if (activeDeckBeat >= transitionEndBeat || activeDeckRemainingSec <= 1.0) {
      // Transition completed: swap decks
      runningPreview_ = {};
      // The decks take turns: A <-> B with two decks, A -> B -> C -> D -> A with four.
      activeDeck_ = nextDeck_;
      nextDeck_ = static_cast<core::DeckId>((core::index(activeDeck_) + 1) % deckCount_);
      ++currentTrackIndex_;

      telemetry_.activeDeck = activeDeck_;
      telemetry_.nextDeck = nextDeck_;
      telemetry_.currentTrackIndex = currentTrackIndex_ + 1;
      telemetry_.currentTrackId = setPlan_.tracks[currentTrackIndex_].track.id;
      telemetry_.nextTrackId = 0;

      if (currentTrackIndex_ + 1 >= setPlan_.tracks.size()) {
        telemetry_.phase = core::AutonomousDjPhase::Finished;
        telemetry_.status = core::AutonomousDjStatus::Finished;
        telemetry_.statusMessage = "Autonomous DJ set completed successfully.";
      } else {
        telemetry_.phase = core::AutonomousDjPhase::PlayingTrack;
        telemetry_.statusMessage = "Playing track " + std::to_string(currentTrackIndex_ + 1) +
                                   " of " + std::to_string(setPlan_.tracks.size()) + ": " +
                                   setPlan_.tracks[currentTrackIndex_].track.title;
      }
    }
  }
}

core::TransitionStyle AutonomousDjLoop::styleFor(std::size_t index) const {
  if (index < setPlan_.tracks.size()) {
    if (const auto chosen = chosenStyles_.find(setPlan_.tracks[index].track.id); chosen != chosenStyles_.end()) {
      return chosen->second;
    }
  }
  if (config_.styleRotation.empty()) {
    return config_.transitionStyle;
  }
  const std::size_t ahead = index > currentTrackIndex_ + 1 ? index - currentTrackIndex_ - 1 : 0;
  return config_.styleRotation[(transitionCount_ + ahead) % config_.styleRotation.size()];
}

AutonomousDjLoop::Decision AutonomousDjLoop::decide(double outStartSec) const {
  Decision d;
  const auto& outTrack = setPlan_.tracks[currentTrackIndex_].track;
  const auto& nextTrack = setPlan_.tracks[currentTrackIndex_ + 1].track;
  const core::TrackMixPoints outPoints = mixPoints_ != nullptr ? mixPoints_->mixPoints(outTrack.id) : core::TrackMixPoints{};
  const core::TrackMixPoints inPoints = mixPoints_ != nullptr ? mixPoints_->mixPoints(nextTrack.id) : core::TrackMixPoints{};
  d.outStartSec = outStartSec;
  d.outBeatSec = outTrack.bpm > 0.0 ? 60.0 / outTrack.bpm : 0.0;
  d.inBeatSec = nextTrack.bpm > 0.0 ? 60.0 / nextTrack.bpm : 0.0;
  d.style = styleFor(currentTrackIndex_ + 1);

  // Where the incoming track starts: so that its drop lands on the last beat of the transition (the outgoing track is
  // gone by then), or at its mix-in point. Beat-aligned: the markers sit on the beat grid.
  d.cue = TransitionPlanner::cueIncoming(inPoints, nextTrack.bpm, config_.transitionDurationBeats);

  // The check: will the mix be clean? The outgoing track must not start its next drop while the incoming one builds
  // up. If a drop of it falls inside the window, the transition ends before it, or, when that leaves too little
  // room, the outgoing track loops the bars before its drop so the drop never comes.
  if (outStartSec >= 0.0 && d.outBeatSec > 0.0) {
    const double windowEnd = outStartSec + d.cue.durationBeats * d.outBeatSec;
    for (const double drop : outPoints.drops) {
      if (drop <= outStartSec + 0.5 * d.outBeatSec || drop >= windowEnd - d.outBeatSec) {
        continue;
      }
      const double beatsToDrop = std::floor((drop - outStartSec) / d.outBeatSec / 4.0) * 4.0;
      if (beatsToDrop >= TransitionPlanner::kMinDropMixBeats) {
        d.cue = TransitionPlanner::cueIncoming(inPoints, nextTrack.bpm, beatsToDrop);
        d.check = "shortened to " + std::to_string(static_cast<int>(d.cue.durationBeats)) + " beats (outgoing drop ahead)";
      } else {
        d.loopBeats = beatsToDrop >= 8.0 ? 8.0 : 4.0;
        d.loopStartSec = drop - d.loopBeats * d.outBeatSec;
        d.check = "outgoing looped before its drop";
      }
      break;
    }
  }
  const bool chosenByDj = chosenStyles_.count(nextTrack.id) > 0;
  const double outWindowEnd = outStartSec + d.cue.durationBeats * d.outBeatSec;
  const double inWindowEnd = d.cue.startSec + d.cue.durationBeats * d.inBeatSec;

  // Energy: a jump up wants a punchier entry, a step down a smoother one (unless the DJ picked the style).
  const double outEnergy = meanEnergy(outPoints, outStartSec, outWindowEnd);
  const double inEnergy = meanEnergy(inPoints, inWindowEnd, inWindowEnd + 8.0 * 4.0 * d.inBeatSec);
  if (outEnergy >= 0.0 && inEnergy >= 0.0) {
    d.energyDelta = inEnergy - outEnergy;
    if (!chosenByDj && d.energyDelta > 0.25 && d.cue.dropAtEnd &&
        (d.style == core::TransitionStyle::BassSwap || d.style == core::TransitionStyle::FilterFade)) {
      d.style = core::TransitionStyle::BeatLoopIn;
      d.check += ", energy up: punchier entry";
    } else if (!chosenByDj && d.energyDelta < -0.25 &&
               (d.style == core::TransitionStyle::QuickCut || d.style == core::TransitionStyle::Scratch ||
                d.style == core::TransitionStyle::Brake || d.style == core::TransitionStyle::LoopRoll)) {
      d.style = core::TransitionStyle::FilterFade;
      d.check += ", energy down: smoother exit";
    }
  }

  // Voices: two vocals on top of each other are the most obvious clash. The incoming mids (where the voice sits) then
  // wait for the swap.
  if (outStartSec >= 0.0 && vocalsIn(outPoints, outStartSec, outWindowEnd) &&
      vocalsIn(inPoints, d.cue.startSec, inWindowEnd)) {
    d.vocalClash = true;
    d.check += ", vocals kept apart";
  }

  // A double drop needs keys that fit and the transition ending exactly on one of the outgoing drops.
  if (d.style == core::TransitionStyle::DoubleDrop) {
    bool fits = d.cue.dropAtEnd && keysCompatible(outTrack.key, nextTrack.key) && outStartSec >= 0.0;
    if (fits) {
      fits = false;
      for (const double drop : outPoints.drops) {
        fits = fits || std::abs(outWindowEnd - drop) < d.outBeatSec;
      }
    }
    if (!fits) {
      d.style = core::TransitionStyle::BassSwap;
    }
  }

  // Stem transitions need the stems of both decks.
  if (d.style == core::TransitionStyle::StemBlend) {
    const bool outStems = loads_ != nullptr && loads_->loadStatus(activeDeck_).stemPhase == core::StemPhase::Ready;
    const bool inStems = loads_ != nullptr && loads_->loadStatus(nextDeck_).stemPhase == core::StemPhase::Ready;
    if (!outStems || !inStems) {
      d.style = core::TransitionStyle::BassSwap;
      d.check += ", stems not ready";
    }
  }

  // A beat loop-in starts the incoming deck on its drop (that bar is looped), not on the build-up before it.
  d.inDropSec = inPoints.dropSec;
  if (d.style == core::TransitionStyle::BeatLoopIn) {
    if (d.cue.dropAtEnd && d.loopBeats <= 0.0 && inPoints.dropSec > 0.0 && d.inBeatSec > 0.0) {
      d.cue.startSec = inPoints.dropSec;
    } else {
      d.style = core::TransitionStyle::BassSwap;  // no known drop, or the outgoing already loops: a plain blend
    }
  }
  return d;
}

core::TransitionPreview AutonomousDjLoop::previewOf(const Decision& d) const {
  core::TransitionPreview preview;
  using Kind = core::TransitionRegion::Kind;
  const double dur = d.cue.durationBeats;
  if (d.outStartSec >= 0.0 && d.outBeatSec > 0.0) {
    const double at = d.outStartSec;
    const auto out = [&](double fromBeat, double toBeat, Kind kind, const char* label) {
      preview.regions.push_back({activeDeck_, at + fromBeat * d.outBeatSec, at + toBeat * d.outBeatSec, kind, label});
    };
    std::string mixLabel(core::transitionStyleName(d.style));
    for (auto& c : mixLabel) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    preview.regions.push_back({activeDeck_, at, at + dur * d.outBeatSec, Kind::Mix, mixLabel});
    switch (d.style) {
      case core::TransitionStyle::LoopRoll: out(dur - 8.0, dur, Kind::Loop, "LOOP ROLL"); break;
      case core::TransitionStyle::Scratch: out(dur - 4.0, dur, Kind::Effect, "SCRATCH"); break;
      case core::TransitionStyle::Brake: out(dur - 1.0, dur, Kind::Effect, "BRAKE"); break;
      case core::TransitionStyle::FilterFade: out(dur * 0.5, dur, Kind::Effect, "FILTER"); break;
      case core::TransitionStyle::QuickCut: out(dur - 0.25, dur, Kind::Effect, "CUT"); break;
      case core::TransitionStyle::DoubleDrop: out(dur, dur + 16.0, Kind::Drop, "DOUBLE DROP"); break;
      case core::TransitionStyle::EchoOut: out(dur - 4.0, dur + 8.0, Kind::Effect, "ECHO OUT"); break;
      case core::TransitionStyle::ReverbOut: out(dur - 4.0, dur + 8.0, Kind::Effect, "REVERB OUT"); break;
      default: break;
    }
    if (d.loopBeats > 0.0) {
      preview.regions.push_back(
          {activeDeck_, d.loopStartSec, d.loopStartSec + d.loopBeats * d.outBeatSec, Kind::Loop, "LOOP"});
    }
  }
  if (d.style == core::TransitionStyle::BeatLoopIn) {
    preview.regions.push_back({nextDeck_, d.inDropSec, d.inDropSec + 4.0 * d.inBeatSec, Kind::Loop, "BEAT LOOP"});
    preview.regions.push_back({nextDeck_, d.inDropSec, d.inDropSec + d.inBeatSec, Kind::Drop, "DROP"});
  } else if (d.inBeatSec > 0.0) {
    const double inEnd = d.cue.startSec + dur * d.inBeatSec;
    preview.regions.push_back({nextDeck_, d.cue.startSec, inEnd, Kind::Mix, "MIX IN"});
    if (d.cue.dropAtEnd) {
      preview.regions.push_back({nextDeck_, inEnd, inEnd + d.inBeatSec, Kind::Drop, "DROP"});
    }
  }
  return preview;
}

core::TransitionPreview AutonomousDjLoop::transitionPreview() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (telemetry_.status != core::AutonomousDjStatus::Running && telemetry_.status != core::AutonomousDjStatus::Paused) {
    return {};
  }
  if (telemetry_.phase == core::AutonomousDjPhase::ExecutingTransition) {
    return runningPreview_;
  }
  if (currentTrackIndex_ + 1 >= setPlan_.tracks.size() ||
      (telemetry_.phase != core::AutonomousDjPhase::PlayingTrack &&
       telemetry_.phase != core::AutonomousDjPhase::PreparingIncomingTrack)) {
    return {};
  }
  // Not started yet: it will start at the mix-out point (or the lead time before the end).
  const auto& outTrack = setPlan_.tracks[currentTrackIndex_].track;
  const core::TrackMixPoints points = mixPoints_ != nullptr ? mixPoints_->mixPoints(outTrack.id) : core::TrackMixPoints{};
  const double planned = plannedStartSec();
  const double start = planned > 0.0 ? planned
                       : (points.mixOutSec > 0.0 ? points.mixOutSec : outTrack.durationSec - config_.leadTimeSeconds);
  return start > 0.0 ? previewOf(decide(start)) : core::TransitionPreview{};
}

bool AutonomousDjLoop::setTransitionStyle(std::size_t index, std::optional<core::TransitionStyle> style) {
  std::lock_guard<std::mutex> lock(mutex_);
  const bool nextIsRunning = telemetry_.phase == core::AutonomousDjPhase::ExecutingTransition;
  if (index <= currentTrackIndex_ || index >= setPlan_.tracks.size() ||
      (index == currentTrackIndex_ + 1 && nextIsRunning)) {
    return false;
  }
  const std::int64_t id = setPlan_.tracks[index].track.id;
  if (style.has_value()) {
    chosenStyles_[id] = *style;
  } else {
    chosenStyles_.erase(id);
  }
  return true;
}

void AutonomousDjLoop::startTransition(double activeDeckBeat, double startBeat) {
  const auto origin = core::CommandOrigin{core::CommandOrigin::Kind::Ai, "autonomous_dj"};
  const auto& outTrack = setPlan_.tracks[currentTrackIndex_].track;
  const auto& nextTrack = setPlan_.tracks[currentTrackIndex_ + 1].track;

  const double outBeatSec = outTrack.bpm > 0.0 ? 60.0 / outTrack.bpm : 0.0;
  const double outStartSec =
      activePositionSec_ >= 0.0 && outBeatSec > 0.0 ? activePositionSec_ + (startBeat - activeDeckBeat) * outBeatSec : -1.0;
  const Decision d = decide(outStartSec);
  const TransitionPlanner::IncomingCue& cue = d.cue;

  if (cue.startSec > 0.0) {
    (void)commandBus_.submit(core::Seek{nextDeck_, cue.startSec}, origin);
  }
  transitionBeats_ = cue.durationBeats;

  // Keys that clash: the incoming track is shifted a semitone or two into a key that fits (keylock keeps its tempo).
  const float keyShift = harmonicShift(outTrack.key, nextTrack.key);
  if (keyShift != 0.0F) {
    (void)commandBus_.submit(core::SetKeyShift{nextDeck_, keyShift}, origin);
  }

  // Match tempo now; the beats are lined up on the audio thread in the very block the incoming deck starts.
  (void)commandBus_.submit(core::Sync{nextDeck_}, origin);
  ++transitionCount_;

  core::TransitionRequest req;
  req.outgoingDeck = activeDeck_;
  req.incomingDeck = nextDeck_;
  req.style = d.style;
  req.startBeat = startBeat;
  req.durationBeats = cue.durationBeats;
  req.dropAtEnd = cue.dropAtEnd;
  req.outgoingBpm = outTrack.bpm;
  req.incomingBpm = nextTrack.bpm;
  req.outgoingStartSec = outStartSec;
  req.outgoingSpeed = activeSpeed_;
  req.outgoingLoopStartSec = d.loopStartSec;
  req.outgoingLoopBeats = d.loopBeats;
  req.incomingDropSec = d.inDropSec;
  req.holdIncomingMids = d.vocalClash;
  req.fxHits = config_.fxHits;
  req.variation = static_cast<int>(transitionCount_ / std::max<std::size_t>(1, config_.styleRotation.size()));

  const auto plan = transitionPlanner_.planTransition(req);
  transitionPlanner_.scheduleTransition(plan, timelineScheduler_);
  transitionBeats_ = plan.durationBeats;
  timelineScheduler_.advanceToBeats(activeDeck_, activeDeckBeat);  // steps already due (a forced start) run now

  runningPreview_ = previewOf(d);
  transitionStartBeat_ = startBeat;
  clockAnchorBeat_ = startBeat;                                       // the incoming deck starts on this beat
  clockAnchorSec_ = cue.startSec > 0.0 ? cue.startSec : 0.0;          // ... at this position of its track
  clockBeatsPerSec_ = nextTrack.bpm > 0.0 ? nextTrack.bpm / 60.0 : 0.0;  // track seconds -> beats
  clockFromIncoming_ = d.style != core::TransitionStyle::BeatLoopIn;
  telemetry_.phase = core::AutonomousDjPhase::ExecutingTransition;
  telemetry_.statusMessage = "Mixing to next track (" + nextTrack.title + ") via " +
                             std::string(core::transitionStyleName(d.style)) + ", " + d.check +
                             (keyShift != 0.0F ? ", key shifted to fit" : "");
}

bool AutonomousDjLoop::isLoaded(core::DeckId deck, std::int64_t trackId, std::string* failure) const {
  if (loads_ == nullptr) {
    return true;  // no load source: loading is assumed to be instant
  }
  const auto status = loads_->loadStatus(deck);
  if (status.track.value != trackId) {
    return false;  // the request has not been picked up yet
  }
  if (status.phase == core::DeckLoadPhase::Failed && failure != nullptr) {
    *failure = status.message;
  }
  return status.phase == core::DeckLoadPhase::Ready;
}

double AutonomousDjLoop::doubleDropTarget(double fromSec) const {
  if (mixPoints_ == nullptr || currentTrackIndex_ + 1 >= setPlan_.tracks.size()) {
    return -1.0;
  }
  const auto& outTrack = setPlan_.tracks[currentTrackIndex_].track;
  const auto& nextTrack = setPlan_.tracks[currentTrackIndex_ + 1].track;
  if (!keysCompatible(outTrack.key, nextTrack.key) || outTrack.bpm <= 0.0) {
    return -1.0;
  }
  const core::TrackMixPoints points = mixPoints_->mixPoints(outTrack.id);
  const double beats = std::min(config_.transitionDurationBeats, TransitionPlanner::kMaxMixBeats);
  const double lead = beats * 60.0 / outTrack.bpm;
  // The last drop of the track that the build-up can still reach: the incoming track then takes over at its peak.
  double target = -1.0;
  for (const double drop : points.drops) {
    if (drop - lead > fromSec + 2.0 && drop > points.dropSec + 1.0) {
      target = drop;
    }
  }
  return target;
}

double AutonomousDjLoop::plannedStartSec() const {
  if (currentTrackIndex_ >= setPlan_.tracks.size()) {
    return -1.0;
  }
  const auto& track = setPlan_.tracks[currentTrackIndex_].track;
  if (currentTrackIndex_ + 1 < setPlan_.tracks.size() &&
      styleFor(currentTrackIndex_ + 1) == core::TransitionStyle::DoubleDrop && track.bpm > 0.0) {
    const double target = doubleDropTarget(std::max(0.0, activePositionSec_));
    if (target > 0.0) {
      const double beats = std::min(config_.transitionDurationBeats, TransitionPlanner::kMaxMixBeats);
      return target - beats * 60.0 / track.bpm;
    }
  }
  if (mixPoints_ != nullptr) {
    const core::TrackMixPoints points = mixPoints_->mixPoints(track.id);
    if (points.mixOutSec > 0.0) {
      return points.mixOutSec;
    }
  }
  return -1.0;
}

double AutonomousDjLoop::leadSeconds() const {
  // Called with mutex_ held (from update).
  constexpr double kMinLeadSeconds = 8.0;
  if (currentTrackIndex_ < setPlan_.tracks.size()) {
    const auto& track = setPlan_.tracks[currentTrackIndex_].track;
    const double start = plannedStartSec();
    if (start > 0.0 && track.durationSec > start) {
      return std::max(kMinLeadSeconds, track.durationSec - start);
    }
  }
  return config_.leadTimeSeconds;
}

std::size_t AutonomousDjLoop::firstEditableIndex() const {
  // The playing track and, once it is being loaded or mixed in, the next one are fixed.
  const bool nextLocked = telemetry_.phase == core::AutonomousDjPhase::PreparingIncomingTrack ||
                          telemetry_.phase == core::AutonomousDjPhase::ExecutingTransition;
  return currentTrackIndex_ + 1 + (nextLocked ? 1 : 0);
}

bool AutonomousDjLoop::moveUpcoming(std::size_t from, std::size_t to) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::size_t first = firstEditableIndex();
  if (from < first || to < first || from >= setPlan_.tracks.size() || to >= setPlan_.tracks.size() || from == to) {
    return false;
  }
  auto entry = std::move(setPlan_.tracks[from]);
  setPlan_.tracks.erase(setPlan_.tracks.begin() + static_cast<std::ptrdiff_t>(from));
  setPlan_.tracks.insert(setPlan_.tracks.begin() + static_cast<std::ptrdiff_t>(to), std::move(entry));
  for (std::size_t i = 0; i < setPlan_.tracks.size(); ++i) {
    setPlan_.tracks[i].trackIndex = static_cast<int>(i + 1);
  }
  telemetry_.totalTracksInSet = setPlan_.tracks.size();
  return true;
}

bool AutonomousDjLoop::insertNext(const core::TrackItem& track, bool mixNow) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (telemetry_.status != core::AutonomousDjStatus::Running || setPlan_.tracks.empty()) {
    return false;
  }
  core::SetTrackEntry entry;
  entry.track = track;
  entry.transitionScore.overallScore = 1.0f;
  entry.transitionScore.explanation = "Chosen by the DJ (live)";

  const std::size_t next = currentTrackIndex_ + 1;
  const bool mixing = telemetry_.phase == core::AutonomousDjPhase::ExecutingTransition;
  if (!mixing && next < setPlan_.tracks.size() &&
      telemetry_.phase == core::AutonomousDjPhase::PreparingIncomingTrack) {
    setPlan_.tracks[next] = std::move(entry);  // the next track was loading: replace it, the preload runs again
    telemetry_.phase = core::AutonomousDjPhase::PlayingTrack;
  } else {
    const std::size_t at = std::min(setPlan_.tracks.size(), next + (mixing ? 1 : 0));
    setPlan_.tracks.insert(setPlan_.tracks.begin() + static_cast<std::ptrdiff_t>(at), std::move(entry));
  }
  // The track may also be planned later on: it now plays here, so it is taken out there.
  bool seen = false;
  for (std::size_t i = currentTrackIndex_ + 1; i < setPlan_.tracks.size();) {
    if (setPlan_.tracks[i].track.id == track.id) {
      if (seen) {
        setPlan_.tracks.erase(setPlan_.tracks.begin() + static_cast<std::ptrdiff_t>(i));
        continue;
      }
      seen = true;
    }
    ++i;
  }
  for (std::size_t i = 0; i < setPlan_.tracks.size(); ++i) {
    setPlan_.tracks[i].trackIndex = static_cast<int>(i + 1);
  }
  telemetry_.totalTracksInSet = setPlan_.tracks.size();
  if (telemetry_.phase == core::AutonomousDjPhase::Finished) {
    return false;
  }
  forceTransition_ = mixNow && !mixing;
  return true;
}

bool AutonomousDjLoop::removeUpcoming(std::size_t index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < firstEditableIndex() || index >= setPlan_.tracks.size()) {
    return false;
  }
  setPlan_.tracks.erase(setPlan_.tracks.begin() + static_cast<std::ptrdiff_t>(index));
  for (std::size_t i = 0; i < setPlan_.tracks.size(); ++i) {
    setPlan_.tracks[i].trackIndex = static_cast<int>(i + 1);
  }
  telemetry_.totalTracksInSet = setPlan_.tracks.size();
  return true;
}

core::AutomixQueue AutonomousDjLoop::queue() const {
  std::lock_guard<std::mutex> lock(mutex_);
  core::AutomixQueue result;
  result.running = telemetry_.status == core::AutonomousDjStatus::Running ||
                   telemetry_.status == core::AutonomousDjStatus::Paused;
  result.current = currentTrackIndex_;
  result.summary = setPlan_.summary;
  for (const auto& planned : setPlan_.tracks) {
    core::AutomixQueueEntry entry;
    entry.track = planned.track;
    entry.fit = planned.transitionScore.overallScore;
    entry.reason = planned.transitionScore.explanation;
    const std::size_t index = result.entries.size();
    if (index > 0) {
      entry.transitionIn = styleFor(index);
      entry.transitionChosen = chosenStyles_.count(planned.track.id) > 0;
    }
    result.entries.push_back(std::move(entry));
  }
  return result;
}

core::AutonomousDjTelemetry AutonomousDjLoop::telemetry() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return telemetry_;
}

}  // namespace zyron::ai
