// SPDX-License-Identifier: AGPL-3.0-only
#include "Application/AutomixController.hpp"

#include <algorithm>
#include <vector>

#include "Analysis/Structure/BarProfile.hpp"

namespace zyron::application {

namespace {

constexpr int kTickMilliseconds = 20;  // transition steps land within a few ms of their beat
constexpr double kMinTrackSeconds = 30.0;
constexpr double kMinSpeed = 0.5;
constexpr std::chrono::milliseconds kRestoreDelay{400};  // well after the pause has faded out
constexpr double kLiveMixBeats = 32.0;  // a live mix is shorter than a planned one: about 11 s at 174 BPM
constexpr std::chrono::seconds kLoadTimeout{20};
constexpr std::chrono::seconds kMessageLifetime{6};
const core::CommandOrigin kLiveOrigin{core::CommandOrigin::Kind::Ai, "live_mix"};

}  // namespace

AutomixController::AutomixController(core::CommandBus& bus, audio::AudioEngine& engine,
                                     library::LibraryService& library)
    : bus_(bus),
      engine_(engine),
      library_(library),
      scheduler_(std::make_shared<ai::CommandTimelineScheduler>(bus)),
      loop_(bus, setBuilder_, planner_, *scheduler_) {
  loop_.setDeckLoadSource(&engine_);
  loop_.setMixPointProvider(this);
  bus_.addSink(scheduler_);
}

AutomixController::~AutomixController() {
  stopTimer();
  bus_.removeSink(scheduler_);
}

bool AutomixController::start() {
  if (isTimerRunning()) {
    stop();
  }
  startFailure_.clear();

  // Only analysed tracks can be planned: the set builder needs tempo, key and energy, and the mix needs a beat grid.
  std::vector<core::TrackItem> catalog;
  for (const auto& track : library_.listAll()) {
    if (track.bpm > 0.0 && track.energy > 0.0 && track.durationSec >= kMinTrackSeconds) {
      catalog.push_back(track);
    }
  }
  if (catalog.size() < 2) {
    const auto pending = library_.scanStatus().analysisPending;
    startFailure_ = "Automix needs at least two analysed tracks (" + std::to_string(catalog.size()) + " ready" +
                    (pending > 0 ? ", analysis is still running: " + std::to_string(pending) + " tasks left" : "") +
                    ")";
    return false;
  }

  core::AutonomousDjConfig config;
  config.targetGenre.clear();  // tags are unreliable; the tempo window below keeps the set coherent
  config.minBpm = catalog.front().bpm;
  config.maxBpm = catalog.front().bpm;
  for (const auto& track : catalog) {
    config.minBpm = std::min(config.minBpm, track.bpm);
    config.maxBpm = std::max(config.maxBpm, track.bpm);
  }

  // The set holds every analysed track (a set length cut it to about an hour: 10 of 19 tracks).
  double totalSec = 0.0;
  for (const auto& track : catalog) {
    totalSec += track.durationSec;
  }
  config.targetDurationMinutes = totalSec / 60.0 + 1.0;
  config.styleRotation = profile_.rotation();
  config.separateStems = profile_.stems;
  config.fxHits = profile_.fxHits;
  config.transitionDurationBeats = profile_.transitionBeats;

  grids_ = {};
  startedAt_ = std::chrono::steady_clock::now();
  if (!loop_.start(config, catalog)) {
    startFailure_ = loop_.telemetry().statusMessage;
    return false;
  }
  // The set's tracks go first in the analysis queue: their drops and mix points decide how the mixes sound.
  for (const auto& entry : loop_.currentSetPlan().tracks) {
    library_.prioritizeAnalysis(entry.track.id);
  }
  startTimer(kTickMilliseconds);
  return true;
}

void AutomixController::stop() {
  stopTimer();
  loop_.stop();
  liveMix_ = LiveMix{};
  liveMessage_.clear();
}

core::TrackMixPoints AutomixController::mixPoints(std::int64_t trackId) {
  constexpr auto kCacheLifetime = std::chrono::seconds(2);
  const auto now = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(pointsMutex_);
    const auto found = pointsCache_.find(trackId);
    if (found != pointsCache_.end() && now - found->second.at < kCacheLifetime) {
      return found->second.points;
    }
  }

  // For each kind the user's marker wins over the AI's; among equals the earliest (the first drop) is used.
  core::TrackMixPoints points;
  const auto pick = [&](const char* type) {
    const core::TrackMarker* best = nullptr;
    const auto all = library_.markers(trackId);
    for (const auto& marker : all) {
      if (marker.type != type) {
        continue;
      }
      if (best == nullptr || (marker.source == "user" && best->source != "user")) {
        best = &marker;
      }
    }
    return best != nullptr ? best->timeSec : -1.0;
  };
  points.mixInSec = pick(core::TrackMarker::kMixIn);
  points.mixOutSec = pick(core::TrackMarker::kMixOut);
  points.dropSec = pick(core::TrackMarker::kDrop);
  for (const auto& marker : library_.markers(trackId)) {
    if (marker.type == core::TrackMarker::kDrop) {
      points.drops.push_back(marker.timeSec);
    }
  }
  std::sort(points.drops.begin(), points.drops.end());

  // Energy and voice: the bar profile and loudness of the analysis, the vocal sections from the paired markers
  // (kVocal opens a section, the next kVocalEnd closes it; one left open runs to the end of the track).
  if (const auto item = library_.findTrack(trackId); item.has_value()) {
    points.loudnessLufs = item->loudnessLufs;
  }
  analysis::BarProfile profile;
  if (analysis::BarProfile::decode(library_.barProfile(trackId), profile)) {
    points.barEnergy = profile.energy;
    points.barBassEnergy = profile.bass;
    points.barOriginSec = profile.originSec;
    points.barSec = profile.barSec;
  }
  double vocalStart = -1.0;
  for (const auto& marker : library_.markers(trackId)) {  // ordered by time
    if (marker.type == core::TrackMarker::kVocal && vocalStart < 0.0) {
      vocalStart = marker.timeSec;
    } else if (marker.type == core::TrackMarker::kVocalEnd && vocalStart >= 0.0) {
      points.vocals.emplace_back(vocalStart, marker.timeSec);
      vocalStart = -1.0;
    }
  }
  if (vocalStart >= 0.0) {
    const auto item = library_.findTrack(trackId);
    points.vocals.emplace_back(vocalStart, item.has_value() && item->durationSec > vocalStart ? item->durationSec : vocalStart);
  }

  std::lock_guard<std::mutex> lock(pointsMutex_);
  pointsCache_[trackId] = CachedPoints{now, points};
  return points;
}

core::AutomixQueue AutomixController::queue() const {
  core::AutomixQueue result = loop_.queue();
  // Show the points each mix will use (a snapshot of the markers; the cache keeps this cheap).
  for (auto& entry : result.entries) {
    entry.points = const_cast<AutomixController*>(this)->mixPoints(entry.track.id);
  }
  return result;
}

core::AutonomousDjTelemetry AutomixController::telemetry() const {
  core::AutonomousDjTelemetry telemetry = loop_.telemetry();
  if (!startFailure_.empty() && telemetry.status != core::AutonomousDjStatus::Running) {
    telemetry.status = core::AutonomousDjStatus::Error;
    telemetry.statusMessage = startFailure_;
  }
  return telemetry;
}

const AutomixController::DeckGrid& AutomixController::gridOf(core::DeckId deck, std::int64_t trackId) {
  DeckGrid& cached = grids_[core::index(deck)];
  if (cached.trackId != trackId || cached.bpm <= 0.0) {
    cached = DeckGrid{trackId, 0.0, 0.0};
    if (const auto track = library_.findTrack(trackId); track.has_value() && track->bpm > 0.0) {
      cached.bpm = track->bpm;
      cached.firstBeatSec = track->firstBeatSec;
    }
  }
  return cached;
}

void AutomixController::timerCallback() {
  tick();
}

double AutomixController::beatOf(core::DeckId deck, const core::LiveEngineState& live) {
  const core::LiveDeckState& state = live.decks[core::index(deck)];
  const DeckGrid& grid = gridOf(deck, engine_.loadStatus(deck).track.value);
  return grid.bpm > 0.0 ? (state.positionSec - grid.firstBeatSec) * grid.bpm / 60.0 : 0.0;
}

bool AutomixController::jumpToTransition() {
  constexpr double kLeadInSeconds = 12.0;  // far enough ahead to hear the end of the track before the mix starts
  const core::AutonomousDjTelemetry state = loop_.telemetry();
  if (state.status != core::AutonomousDjStatus::Running) {
    return false;
  }
  const core::LiveDeckState deck = engine_.liveState().decks[core::index(state.activeDeck)];
  if (!deck.hasTrack || deck.durationSec <= 0.0) {
    return false;
  }
  const core::TrackMixPoints points = mixPoints(state.currentTrackId);
  const double mixOut = points.mixOutSec > 0.0 ? points.mixOutSec : deck.durationSec - 30.0;
  const double target = std::clamp(mixOut - kLeadInSeconds, 0.0, deck.durationSec);
  // An AI-origin command: moving the playhead is not the DJ taking over the mix.
  return !bus_.submit(core::Seek{state.activeDeck, target}, kLiveOrigin).has_value();
}

std::string AutomixController::mixNext(std::int64_t trackId) {
  const auto track = library_.findTrack(trackId);
  if (!track.has_value()) {
    return "The track is not in the library";
  }
  if (loop_.telemetry().status == core::AutonomousDjStatus::Running) {
    return loop_.insertNext(*track, true) ? std::string{} : "Automix cannot take another track right now";
  }
  return startLiveMix(*track);
}

std::string AutomixController::startLiveMix(const core::TrackItem& track) {
  if (liveMix_.phase != LiveMix::Phase::Idle) {
    return "A mix is already in progress";
  }
  const core::LiveEngineState live = engine_.liveState();

  // The deck that plays now is mixed out; the track goes on a free deck (one without a track is preferred).
  LiveMix mix;
  for (std::size_t i = 0; i < visibleDecks_; ++i) {
    if (live.decks[i].isPlaying && live.decks[i].hasTrack) {
      mix.outgoing = static_cast<core::DeckId>(i);
      mix.hasOutgoing = true;
      break;
    }
  }
  // The partner deck first (A <-> B, C <-> D): the mix alternates between the two decks the DJ sees, never into a
  // hidden deck of the four-deck layout. Any other deck that is not playing is the fallback.
  int best = -1;
  const std::size_t partner = mix.hasOutgoing ? (core::index(mix.outgoing) ^ 1U) : 0U;
  if (partner < visibleDecks_ && !live.decks[partner].isPlaying &&
      (!mix.hasOutgoing || partner != core::index(mix.outgoing))) {
    best = static_cast<int>(partner);
  }
  for (std::size_t i = 0; i < visibleDecks_ && best < 0; ++i) {
    const auto id = static_cast<core::DeckId>(i);
    if ((mix.hasOutgoing && id == mix.outgoing) || live.decks[i].isPlaying) {
      continue;
    }
    best = static_cast<int>(i);
  }
  if (best < 0) {
    return "No free deck: stop one of the other decks first";
  }
  mix.incoming = static_cast<core::DeckId>(best);

  if (mix.hasOutgoing) {
    if (track.bpm <= 0.0) {
      return "This track has no tempo yet: use Analyse now in its menu";
    }
    if (gridOf(mix.outgoing, engine_.loadStatus(mix.outgoing).track.value).bpm <= 0.0) {
      return "The playing track has no tempo grid yet, so it cannot be mixed";
    }
  }
  mix.track = track;
  mix.phase = LiveMix::Phase::Loading;
  mix.started = std::chrono::steady_clock::now();

  // A neutral deck, then the track.
  (void)bus_.submit(core::SetVolume{mix.incoming, 1.0F}, kLiveOrigin);
  for (const core::EqBand band : {core::EqBand::Low, core::EqBand::Mid, core::EqBand::High}) {
    (void)bus_.submit(core::SetEq{mix.incoming, band, 0.0F}, kLiveOrigin);
  }
  if (const auto error = bus_.submit(core::LoadTrack{mix.incoming, core::TrackId{track.id}}, kLiveOrigin)) {
    return error->message;
  }
  liveMix_ = mix;
  liveMessage_ = "Loading " + track.title + " on the free deck...";
  if (!isTimerRunning()) {
    startTimer(kTickMilliseconds);
  }
  return {};
}

void AutomixController::tickLiveMix() {
  using Phase = LiveMix::Phase;
  const auto now = std::chrono::steady_clock::now();
  if (liveMix_.phase == Phase::Idle) {
    if (!liveMessage_.empty() && now > liveMessageUntil_) {
      liveMessage_.clear();
    }
    return;
  }
  const auto finish = [&](const std::string& message) {
    liveMix_ = LiveMix{};
    liveMessage_ = message;
    liveMessageUntil_ = now + kMessageLifetime;
  };

  const core::LiveEngineState live = engine_.liveState();
  if (liveMix_.phase == Phase::Loading) {
    const core::DeckLoadStatus status = engine_.loadStatus(liveMix_.incoming);
    const bool mine = status.track.value == liveMix_.track.id;
    if (mine && status.phase == core::DeckLoadPhase::Failed) {
      finish("Live mix: could not load " + liveMix_.track.title + ": " + status.message);
      return;
    }
    if (!mine || status.phase != core::DeckLoadPhase::Ready) {
      if (now - liveMix_.started > kLoadTimeout) {
        finish("Live mix: loading " + liveMix_.track.title + " took too long");
      }
      return;
    }

    if (!liveMix_.hasOutgoing) {  // nothing was playing: just start the track
      (void)bus_.submit(core::Play{liveMix_.incoming}, kLiveOrigin);
      finish("Playing " + liveMix_.track.title);
      return;
    }

    // Start the incoming track at its mix-in point or so that its drop meets the bass swap, then sync and mix.
    const core::TrackMixPoints points = mixPoints(liveMix_.track.id);
    const auto cue = ai::TransitionPlanner::cueIncoming(points, liveMix_.track.bpm, kLiveMixBeats);
    if (cue.startSec > 0.0) {
      (void)bus_.submit(core::Seek{liveMix_.incoming, cue.startSec}, kLiveOrigin);
    }
    (void)bus_.submit(core::Sync{liveMix_.incoming}, kLiveOrigin);

    const double beat = beatOf(liveMix_.outgoing, live);
    core::TransitionRequest request;
    request.outgoingDeck = liveMix_.outgoing;
    request.incomingDeck = liveMix_.incoming;
    request.style = core::TransitionStyle::BassSwap;
    request.startBeat = beat;
    request.durationBeats = cue.durationBeats;
    request.dropAtEnd = cue.dropAtEnd;
    planner_.scheduleTransition(planner_.planTransition(request), *scheduler_);
    scheduler_->advanceToBeats(liveMix_.outgoing, beat);  // the first steps (the incoming deck starts) run now

    liveMix_.endBeat = beat + cue.durationBeats;
    liveMix_.phase = Phase::Mixing;
    liveMessage_ = "Mixing into " + liveMix_.track.title;
    return;
  }

  // Mixing: follow the outgoing deck's beats until the transition has run its course.
  const double beat = beatOf(liveMix_.outgoing, live);
  scheduler_->advanceToBeats(liveMix_.outgoing, beat);
  const bool outgoingStopped = !live.decks[core::index(liveMix_.outgoing)].isPlaying;
  if (beat >= liveMix_.endBeat || outgoingStopped) {
    // The transition leaves the outgoing fader down; open it again once the deck has stopped, or the next track
    // played there by hand would be silent.
    restoreDeck_ = liveMix_.outgoing;
    restoreAt_ = now + kRestoreDelay;
    finish("Now playing " + liveMix_.track.title);
  }
}

void AutomixController::setMixProfile(const core::MixProfile& profile) {
  profile_ = profile;
  loop_.setStyleRotation(profile_.rotation(), profile_.transitionBeats, profile_.stems, profile_.fxHits);
}

void AutomixController::tick() {
  if (restoreDeck_.has_value() && std::chrono::steady_clock::now() >= restoreAt_) {
    if (!engine_.liveState().decks[core::index(*restoreDeck_)].isPlaying) {
      (void)bus_.submit(core::SetVolume{*restoreDeck_, 1.0F}, kLiveOrigin);
    }
    restoreDeck_.reset();
  }
  tickLiveMix();
  const core::AutonomousDjTelemetry state = loop_.telemetry();
  if (state.status != core::AutonomousDjStatus::Running && state.status != core::AutonomousDjStatus::Paused) {
    if (liveMix_.phase == LiveMix::Phase::Idle && liveMessage_.empty() && !restoreDeck_.has_value()) {
      stopTimer();  // finished, failed or stopped from elsewhere, and no live mix needs the timer
    }
    return;
  }

  // The DJ loaded something else on the playing deck: the plan no longer describes what is playing.
  if ((state.phase == core::AutonomousDjPhase::PlayingTrack ||
       state.phase == core::AutonomousDjPhase::PreparingIncomingTrack) &&
      state.currentTrackId != 0) {
    const std::int64_t loaded = engine_.loadStatus(state.activeDeck).track.value;
    if (loaded != 0 && loaded != state.currentTrackId) {
      stop();
      startFailure_ = "Automix stopped: another track was loaded on the playing deck";
      return;
    }
  }

  const core::LiveEngineState live = engine_.liveState();
  const core::LiveDeckState& deck = live.decks[core::index(state.activeDeck)];
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();

  if (!deck.hasTrack) {
    loop_.update(elapsed, 1.0e9, 0.0);  // the first track is still loading: nothing to count down yet
    return;
  }

  // Beats are counted in track time from the grid anchor; the time left is wall-clock time, so it follows the pitch.
  const DeckGrid& grid = gridOf(state.activeDeck, state.currentTrackId);
  const double beat = grid.bpm > 0.0 ? (deck.positionSec - grid.firstBeatSec) * grid.bpm / 60.0 : 0.0;
  const double remaining = (deck.durationSec - deck.positionSec) / std::max(kMinSpeed, deck.playbackSpeed);
  const core::LiveDeckState& incoming = live.decks[core::index(state.nextDeck)];
  loop_.setActiveDeckState(deck.positionSec, deck.playbackSpeed, incoming.positionSec, incoming.isPlaying);
  loop_.update(elapsed, remaining, beat);
}

}  // namespace zyron::application
