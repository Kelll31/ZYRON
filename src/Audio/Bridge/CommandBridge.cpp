// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Bridge/CommandBridge.hpp"

namespace zyron::audio {

RtMessage RtMessage::makePlay(core::DeckId deck) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckPlay;
  msg.deck = deck;
  return msg;
}

RtMessage RtMessage::makePause(core::DeckId deck) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckPause;
  msg.deck = deck;
  return msg;
}

RtMessage RtMessage::makeCue(core::DeckId deck) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckCue;
  msg.deck = deck;
  return msg;
}

RtMessage RtMessage::makeSeek(core::DeckId deck, std::int64_t sample) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckSeek;
  msg.deck = deck;
  msg.data.seekSample = sample;
  return msg;
}

RtMessage RtMessage::makeSeekSeconds(core::DeckId deck, double seconds) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckSeekSeconds;
  msg.deck = deck;
  msg.data.seekSeconds = seconds;
  return msg;
}

RtMessage RtMessage::makeLoop(core::DeckId deck, double startSeconds, double endSeconds, bool active) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckLoop;
  msg.deck = deck;
  msg.data.loop.startSeconds = startSeconds;
  msg.data.loop.endSeconds = endSeconds;
  msg.data.loop.active = active;
  return msg;
}

RtMessage RtMessage::makeGain(core::DeckId deck, float gainDb) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckGain;
  msg.deck = deck;
  msg.data.gainDb = gainDb;
  return msg;
}

RtMessage RtMessage::makeVolume(core::DeckId deck, float linear) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckVolume;
  msg.deck = deck;
  msg.data.volumeLinear = linear;
  return msg;
}

RtMessage RtMessage::makeEq(core::DeckId deck, core::EqBand band, float gainDb) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckEq;
  msg.deck = deck;
  msg.data.eq.band = band;
  msg.data.eq.gainDb = gainDb;
  return msg;
}

RtMessage RtMessage::makeScratch(core::DeckId deck, core::ScratchPattern pattern, double beats,
                                 double beatSeconds) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckScratch;
  msg.deck = deck;
  msg.data.scratch.pattern = static_cast<std::uint8_t>(pattern);
  msg.data.scratch.beats = static_cast<float>(beats);
  msg.data.scratch.beatSeconds = static_cast<float>(beatSeconds);
  return msg;
}

RtMessage RtMessage::makePhaseLock(core::DeckId deck, core::DeckId master, double targetBpm,
                                   std::int64_t targetFirstBeat, int targetRate, double masterBpm,
                                   std::int64_t masterFirstBeat, int masterRate) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckPhaseLock;
  msg.deck = deck;
  msg.data.phaseLock.master = static_cast<std::uint8_t>(core::index(master));
  msg.data.phaseLock.targetBpm = targetBpm;
  msg.data.phaseLock.targetFirstBeat = targetFirstBeat;
  msg.data.phaseLock.targetRate = targetRate;
  msg.data.phaseLock.masterBpm = masterBpm;
  msg.data.phaseLock.masterFirstBeat = masterFirstBeat;
  msg.data.phaseLock.masterRate = masterRate;
  return msg;
}

RtMessage RtMessage::makeTempoGlide(core::DeckId deck, double speed, double seconds) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckTempoGlide;
  msg.deck = deck;
  msg.data.glide.speed = speed;
  msg.data.glide.seconds = seconds;
  return msg;
}

RtMessage RtMessage::makeFilter(core::DeckId deck, float bipolar) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckFilter;
  msg.deck = deck;
  msg.data.filterBipolar = bipolar;
  return msg;
}

RtMessage RtMessage::makePlaybackSpeed(core::DeckId deck, double speedRatio) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckPlaybackSpeed;
  msg.deck = deck;
  msg.data.speedRatio = speedRatio;
  return msg;
}

RtMessage RtMessage::makeKeylock(core::DeckId deck, bool enabled) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckKeylock;
  msg.deck = deck;
  msg.data.keylockEnabled = enabled;
  return msg;
}

RtMessage RtMessage::makeKeyShift(core::DeckId deck, float semitones) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckKeyShift;
  msg.deck = deck;
  msg.data.keyShiftSemitones = semitones;
  return msg;
}

RtMessage RtMessage::makeFx(core::DeckId deck, int slot, core::FxType type, bool enabled, float wet, float param,
                            bool tailAfterFader) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckFx;
  msg.deck = deck;
  msg.data.fx.slot = static_cast<std::uint8_t>(slot);
  msg.data.fx.type = type;
  msg.data.fx.enabled = enabled;
  msg.data.fx.tailAfterFader = tailAfterFader;
  msg.data.fx.wet = wet;
  msg.data.fx.param = param;
  return msg;
}

RtMessage RtMessage::makeFxTempo(core::DeckId deck, double beatSeconds) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckFxTempo;
  msg.deck = deck;
  msg.data.fxBeatSeconds = static_cast<float>(beatSeconds);
  return msg;
}

RtMessage RtMessage::makeTrackTrim(core::DeckId deck, float db) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckTrackTrim;
  msg.deck = deck;
  msg.data.trackTrimDb = db;
  return msg;
}

RtMessage RtMessage::makeMasterProcessing(bool glue, bool limiter) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::MixerProcessing;
  msg.data.processing.glue = glue;
  msg.data.processing.limiter = limiter;
  return msg;
}

RtMessage RtMessage::makeFxHit(core::FxHitType type, float level, double beatSeconds) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::MixerFxHit;
  msg.data.fxHit.type = type;
  msg.data.fxHit.level = level;
  msg.data.fxHit.beatSeconds = beatSeconds;
  return msg;
}

RtMessage RtMessage::makeCrossfader(float position) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::MixerCrossfader;
  msg.data.crossfaderPosition = position;
  return msg;
}

RtMessage RtMessage::makeCrossfaderCurve(CrossfaderCurve curve) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::MixerCrossfaderCurve;
  msg.data.crossfaderCurve = curve;
  return msg;
}

RtMessage RtMessage::makeChannelAssign(int channel, CrossfaderAssign assign) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::MixerChannelAssign;
  msg.data.channelAssign.channel = channel;
  msg.data.channelAssign.assign = assign;
  return msg;
}

RtMessage RtMessage::makeMasterGain(float gainDb) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::MixerMasterGain;
  msg.data.masterGainDb = gainDb;
  return msg;
}

RtMessage RtMessage::makeMasterLimiter(bool enabled, float ceilingDb) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::MixerLimiter;
  msg.data.limiter.enabled = enabled;
  msg.data.limiter.ceilingDb = ceilingDb;
  return msg;
}

RtMessage RtMessage::makeMixerCue(int channel, bool enabled) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::MixerCue;
  msg.data.cue.channel = channel;
  msg.data.cue.enabled = enabled;
  return msg;
}

RtMessage RtMessage::makeTestTone(bool enabled, float freqHz, float levelDb) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::TestTone;
  msg.data.testTone.enabled = enabled;
  msg.data.testTone.frequencyHz = freqHz;
  msg.data.testTone.levelDb = levelDb;
  return msg;
}

RtMessage RtMessage::makeStemVolume(core::DeckId deck, core::StemKind stem, float volumeLinear) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckStemVolume;
  msg.deck = deck;
  msg.data.stemControl.stem = stem;
  msg.data.stemControl.volumeLinear = volumeLinear;
  return msg;
}

RtMessage RtMessage::makeStemMute(core::DeckId deck, core::StemKind stem, bool muted) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckStemMute;
  msg.deck = deck;
  msg.data.stemControl.stem = stem;
  msg.data.stemControl.active = muted;
  return msg;
}

RtMessage RtMessage::makeStemSolo(core::DeckId deck, core::StemKind stem, bool solo) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckStemSolo;
  msg.deck = deck;
  msg.data.stemControl.stem = stem;
  msg.data.stemControl.active = solo;
  return msg;
}

RtMessage RtMessage::makeStemCue(core::DeckId deck, core::StemKind stem, bool cue) noexcept {
  RtMessage msg;
  msg.type = RtMessageType::DeckStemCue;
  msg.deck = deck;
  msg.data.stemControl.stem = stem;
  msg.data.stemControl.active = cue;
  return msg;
}

std::optional<RtMessage> CommandBridge::translateCommand(const core::Command& command) noexcept {
  return std::visit(
      [](const auto& cmd) -> std::optional<RtMessage> {
        using T = std::decay_t<decltype(cmd)>;
        if constexpr (std::is_same_v<T, core::Play>) {
          return RtMessage::makePlay(cmd.deck);
        } else if constexpr (std::is_same_v<T, core::Pause>) {
          return RtMessage::makePause(cmd.deck);
        } else if constexpr (std::is_same_v<T, core::Cue>) {
          return RtMessage::makeCue(cmd.deck);
        } else if constexpr (std::is_same_v<T, core::SetGain>) {
          return RtMessage::makeGain(cmd.deck, cmd.db);
        } else if constexpr (std::is_same_v<T, core::SetVolume>) {
          return RtMessage::makeVolume(cmd.deck, cmd.linear);
        } else if constexpr (std::is_same_v<T, core::SetEq>) {
          return RtMessage::makeEq(cmd.deck, cmd.band, cmd.db);
        } else if constexpr (std::is_same_v<T, core::SetFilter>) {
          return RtMessage::makeFilter(cmd.deck, cmd.position);
        } else if constexpr (std::is_same_v<T, core::GlideTempo>) {
          return RtMessage::makeTempoGlide(cmd.deck, cmd.speed, cmd.seconds);
        } else if constexpr (std::is_same_v<T, core::Scratch>) {
          return RtMessage::makeScratch(cmd.deck, cmd.pattern, cmd.beats, cmd.beatSeconds);
        } else if constexpr (std::is_same_v<T, core::SetStemVolume>) {
          return RtMessage::makeStemVolume(cmd.deck, cmd.stem, cmd.linear);
        } else if constexpr (std::is_same_v<T, core::SetStemMute>) {
          return RtMessage::makeStemMute(cmd.deck, cmd.stem, cmd.muted);
        } else if constexpr (std::is_same_v<T, core::SetStemSolo>) {
          return RtMessage::makeStemSolo(cmd.deck, cmd.stem, cmd.solo);
        } else if constexpr (std::is_same_v<T, core::SetStemCue>) {
          return RtMessage::makeStemCue(cmd.deck, cmd.stem, cmd.cue);
        } else if constexpr (std::is_same_v<T, core::Seek>) {
          return RtMessage::makeSeekSeconds(cmd.deck, cmd.seconds);
        } else if constexpr (std::is_same_v<T, core::SetPlaybackSpeed>) {
          return RtMessage::makePlaybackSpeed(cmd.deck, cmd.speed);
        } else if constexpr (std::is_same_v<T, core::SetKeylock>) {
          return RtMessage::makeKeylock(cmd.deck, cmd.enabled);
        } else if constexpr (std::is_same_v<T, core::SetKeyShift>) {
          return RtMessage::makeKeyShift(cmd.deck, cmd.semitones);
        } else if constexpr (std::is_same_v<T, core::SetFx>) {
          return RtMessage::makeFx(cmd.deck, cmd.slot, cmd.type, cmd.enabled, cmd.wet, cmd.param, cmd.tailAfterFader);
        } else if constexpr (std::is_same_v<T, core::SetFxTempo>) {
          return RtMessage::makeFxTempo(cmd.deck, cmd.beatSeconds);
        } else if constexpr (std::is_same_v<T, core::SetTrackGainTrim>) {
          return RtMessage::makeTrackTrim(cmd.deck, cmd.db);
        } else if constexpr (std::is_same_v<T, core::SetMasterProcessing>) {
          return RtMessage::makeMasterProcessing(cmd.glue, cmd.limiter);
        } else if constexpr (std::is_same_v<T, core::TriggerFxHit>) {
          return RtMessage::makeFxHit(cmd.type, cmd.level, cmd.beatSeconds);
        } else if constexpr (std::is_same_v<T, core::SetLoop>) {
          return RtMessage::makeLoop(cmd.deck, cmd.startSeconds, cmd.endSeconds, cmd.active);
        } else if constexpr (std::is_same_v<T, core::SetTestTone>) {
          return RtMessage::makeTestTone(cmd.enabled, cmd.frequencyHz, cmd.levelDb);
        } else if constexpr (std::is_same_v<T, core::UnloadTrack>) {
          return RtMessage::makePause(cmd.deck);
        } else if constexpr (std::is_same_v<T, core::SetCrossfader>) {
          return RtMessage::makeCrossfader(cmd.position);
        } else if constexpr (std::is_same_v<T, core::SetCrossfaderCurve>) {
          return RtMessage::makeCrossfaderCurve(cmd.curve);
        } else if constexpr (std::is_same_v<T, core::SetCrossfaderAssign>) {
          return RtMessage::makeChannelAssign(static_cast<int>(core::index(cmd.deck)), cmd.assign);
        } else if constexpr (std::is_same_v<T, core::SetMasterGain>) {
          return RtMessage::makeMasterGain(cmd.db);
        } else if constexpr (std::is_same_v<T, core::SetDeckCue>) {
          return RtMessage::makeMixerCue(static_cast<int>(core::index(cmd.deck)), cmd.enabled);
        } else {
          return std::nullopt;
        }
      },
      command);
}

bool CommandBridge::pushCommand(const core::Command& command) noexcept {
  const auto msg = translateCommand(command);
  if (msg.has_value()) {
    return queue_.push(*msg);
  }
  return true;  // Commands not requiring RT action (e.g. SetAudioOutput) succeed trivially
}

bool CommandBridge::pushMessage(const RtMessage& message) noexcept {
  return queue_.push(message);
}

bool CommandBridge::popMessage(RtMessage& message) noexcept {
  return queue_.pop(message);
}

std::uint64_t CommandBridge::droppedMessagesCount() const noexcept {
  return queue_.droppedCount();
}

std::size_t CommandBridge::queueSize() const noexcept {
  return queue_.size();
}

void CommandBridge::publishTelemetry(const AudioTelemetry& telemetry) noexcept {
  telemetryBuffer_.write(telemetry);
}

bool CommandBridge::readTelemetry(AudioTelemetry& telemetry) noexcept {
  return telemetryBuffer_.read(telemetry);
}

void CommandBridge::reset() noexcept {
  queue_.reset();
}

}  // namespace zyron::audio
