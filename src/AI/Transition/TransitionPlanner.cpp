// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Transition/TransitionPlanner.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace zyron::ai {

namespace {

constexpr double kTempoSettleSeconds = 60.0;
constexpr int kFaderSteps = 32;  // per fade: ~0.1 s apart over 8 beats, each smoothed by the deck (no zipper noise)
constexpr int kEqSteps = 12;

/// Equal-power fader move from fully open to closed (or back) between two beats.
void addVolumeRamp(core::TransitionPlan& plan, core::DeckId deck, double fromBeat, double toBeat, bool up,
                   const char* what) {
  constexpr double kHalfPi = 1.57079632679489661923;
  for (int i = 1; i <= kFaderSteps; ++i) {
    const double t = static_cast<double>(i) / kFaderSteps;
    const double gain = i == kFaderSteps ? (up ? 1.0 : 0.0)  // land exactly open / closed
                                         : (up ? std::sin(t * kHalfPi) : std::cos(t * kHalfPi));
    plan.steps.push_back({fromBeat + (toBeat - fromBeat) * t, core::SetVolume{deck, static_cast<float>(gain)}, what});
  }
}

/// EQ band move in small dB steps between two beats.
void addEqRamp(core::TransitionPlan& plan, core::DeckId deck, core::EqBand band, double fromBeat, double toBeat,
               float fromDb, float toDb, const char* what) {
  for (int i = 1; i <= kEqSteps; ++i) {
    const double t = static_cast<double>(i) / kEqSteps;
    plan.steps.push_back({fromBeat + (toBeat - fromBeat) * t,
                          core::SetEq{deck, band, fromDb + (toDb - fromDb) * static_cast<float>(t)}, what});
  }
}

/// Linear fader move between two levels.
void addVolumeLine(core::TransitionPlan& plan, core::DeckId deck, double fromBeat, double toBeat, float from, float to,
                   const char* what) {
  for (int i = 1; i <= kEqSteps; ++i) {
    const double t = static_cast<double>(i) / kEqSteps;
    plan.steps.push_back({fromBeat + (toBeat - fromBeat) * t,
                          core::SetVolume{deck, from + (to - from) * static_cast<float>(t)}, what});
  }
}

/// DJ filter sweep between two positions (-1 low-pass .. +1 high-pass).
void addFilterRamp(core::TransitionPlan& plan, core::DeckId deck, double fromBeat, double toBeat, float from, float to,
                   const char* what) {
  constexpr int kSteps = 24;
  for (int i = 1; i <= kSteps; ++i) {
    const double t = static_cast<double>(i) / kSteps;
    plan.steps.push_back({fromBeat + (toBeat - fromBeat) * t,
                          core::SetFilter{deck, from + (to - from) * static_cast<float>(t)}, what});
  }
}

/// The blend family: the incoming track fades in from silence with its bass cut, the basses swap in one cut on a bar
/// (on the drop when it is known), and the outgoing track leaves in the requested style. Faders, EQ and filters move in
/// small steps; only the bass swap is a cut, on the beat.
void buildBlend(core::TransitionPlan& plan, const core::TransitionRequest& request) {
  const auto out = request.outgoingDeck;
  const auto in = request.incomingDeck;
  const double dur = plan.durationBeats;
  const double swap = request.dropAtEnd ? dur : dur * 0.5;
  const double outBeatSec = request.outgoingBpm > 0.0 ? 60.0 / request.outgoingBpm : 0.0;
  const bool loopingOut = request.outgoingLoopBeats > 0.0 && request.outgoingLoopStartSec >= 0.0 && outBeatSec > 0.0;
  core::TransitionStyle style = request.style;
  if (style == core::TransitionStyle::Scratch && (outBeatSec <= 0.0 || dur < 8.0)) {
    style = core::TransitionStyle::Brake;  // a scratch is timed in beats
  }
  if (style == core::TransitionStyle::LoopRoll && (loopingOut || request.outgoingStartSec < 0.0 || outBeatSec <= 0.0 ||
                                                   dur < 16.0)) {
    style = core::TransitionStyle::FilterFade;  // a roll needs the outgoing position, and a loop already holds it
  }

  // Incoming: closed fader, no bass (and a low-pass for the filter style), then in phase on the first beat.
  plan.steps.push_back({0.0, core::SetVolume{in, 0.0f}, "Incoming fader closed"});
  plan.steps.push_back({0.0, core::SetEq{in, core::EqBand::Low, -60.0f}, "Kill incoming bass"});
  if (style == core::TransitionStyle::FilterFade) {
    plan.steps.push_back({0.0, core::SetFilter{in, -0.6f}, "Incoming low-pass"});
    addFilterRamp(plan, in, 0.0, dur * 0.5, -0.6f, 0.0f, "Open incoming filter");
  }
  plan.steps.push_back({0.0, core::Play{in}, "Start incoming deck in phase"});
  if (style == core::TransitionStyle::QuickCut) {
    // The incoming build-up rises under a high-pass for the last two bars; the drop then slams in alone.
    plan.steps.push_back({0.0, core::SetFilter{in, 0.5f}, "Incoming high-pass"});
    addVolumeLine(plan, in, dur - 8.0, dur - 0.25, 0.0f, 0.6f, "Incoming build-up rises");
    plan.steps.push_back({dur, core::SetFilter{in, 0.0f}, "Incoming filter off on the drop"});
    plan.steps.push_back({dur, core::SetVolume{in, 1.0f}, "Incoming full on the drop"});
  } else if (style == core::TransitionStyle::BeatLoopIn && request.incomingDropSec >= 0.0 && request.incomingBpm > 0.0) {
    // The incoming drop's first bar, looped under the outgoing track, louder and rolled shorter (2, 1, 1/2, 1/4 beat);
    // on the last beat the loop lets go and the drop plays from its start.
    const double inBeat = 60.0 / request.incomingBpm;
    const double drop = request.incomingDropSec;
    plan.steps.push_back({0.0, core::SetLoop{in, drop, drop + 4.0 * inBeat, true}, "Loop the incoming beat"});
    plan.steps.push_back({0.0, core::SetEq{in, core::EqBand::Mid, -6.0f}, "Incoming beat: mids down"});
    addVolumeLine(plan, in, 0.0, dur * 0.5, 0.0f, 0.85f, "Bring the beat in");
    addEqRamp(plan, in, core::EqBand::Mid, dur * 0.5, dur - 4.0, -6.0f, 0.0f, "Incoming beat: mids in");
    const double rolls[][2] = {{dur - 8.0, 2.0}, {dur - 4.0, 1.0}, {dur - 2.0, 0.5}, {dur - 1.0, 0.25}};
    for (const auto& roll : rolls) {
      plan.steps.push_back({roll[0], core::SetLoop{in, drop, drop + roll[1] * inBeat, true}, "Roll the incoming beat"});
    }
    plan.steps.push_back({dur, core::SetLoop{in, drop, drop + 0.25 * inBeat, false}, "Let the beat go"});
    plan.steps.push_back({dur, core::Seek{in, drop}, "DROP from its start"});
    plan.steps.push_back({dur, core::SetVolume{in, 1.0f}, "Incoming full on the drop"});
  } else if (style == core::TransitionStyle::BassSwap) {
    // The way top DJs blend with a three-band EQ: the fader comes up early with the incoming track's EQ pulled down,
    // its highs come in first, then the mids are traded between the tracks, and the bass last, in one cut.
    plan.steps.push_back({0.0, core::SetEq{in, core::EqBand::Mid, -12.0f}, "Incoming mids down"});
    plan.steps.push_back({0.0, core::SetEq{in, core::EqBand::High, -8.0f}, "Incoming highs down"});
    addVolumeRamp(plan, in, 0.0, dur * 0.25, true, "Incoming fader up");
    addEqRamp(plan, in, core::EqBand::High, dur * 0.25, dur * 0.5, -8.0f, 0.0f, "Incoming highs in");
    // With vocals in both tracks the incoming mids (the voice) wait until just before the swap.
    const double midFrom = request.holdIncomingMids ? swap - 4.0 : (request.dropAtEnd ? dur * 0.5 : dur * 0.25);
    const double midTo = request.holdIncomingMids ? swap : (request.dropAtEnd ? dur * 0.75 : dur * 0.5);
    addEqRamp(plan, in, core::EqBand::Mid, midFrom, midTo, -12.0f, 0.0f, "Mid swap: incoming mids in");
    addEqRamp(plan, out, core::EqBand::Mid, midFrom, midTo, 0.0f, -10.0f, "Mid swap: outgoing mids out");
  } else {
    addVolumeRamp(plan, in, 0.0, dur * 0.5, true, "Fade incoming in");
  }

  if (loopingOut) {
    const double end = request.outgoingLoopStartSec + request.outgoingLoopBeats * outBeatSec;
    plan.steps.push_back({0.0, core::SetLoop{out, request.outgoingLoopStartSec, end, true},
                          "Loop outgoing: its next drop must not come in the middle of the mix"});
  }

  switch (style) {
    case core::TransitionStyle::FilterFade:
      addFilterRamp(plan, out, dur * 0.5, dur - 0.5, 0.0f, 0.85f, "Sweep outgoing high-pass");
      addVolumeRamp(plan, out, dur - 4.0, dur - 0.5, false, "Fade outgoing out");
      break;
    case core::TransitionStyle::LoopRoll: {
      // Loop the outgoing bar at dur-8 and halve it every time it comes round: 4, 2, 1, 1/2, 1/4 beats.
      const double rollStart = request.outgoingStartSec + (dur - 8.0) * outBeatSec;
      const double rolls[][2] = {{dur - 8.0, 4.0}, {dur - 4.0, 2.0}, {dur - 2.0, 1.0}, {dur - 1.0, 0.5}, {dur - 0.5, 0.25}};
      for (const auto& roll : rolls) {
        plan.steps.push_back({roll[0], core::SetLoop{out, rollStart, rollStart + roll[1] * outBeatSec, true},
                              "Loop roll"});
      }
      addEqRamp(plan, out, core::EqBand::High, dur - 8.0, dur - 0.5, 0.0f, -9.0f, "Thin the roll");
      addVolumeRamp(plan, out, dur - 4.0, dur - 0.25, false, "Fade the roll out");
      break;
    }
    case core::TransitionStyle::Brake: {
      addEqRamp(plan, out, core::EqBand::High, dur * 0.5, dur - 1.0, 0.0f, -6.0f, "Soften outgoing highs");
      addVolumeLine(plan, out, dur * 0.5, dur - 1.0, 1.0f, 0.75f, "Outgoing fader down a little");
      // The deck brakes by itself, sample by sample (a speed command cannot go below half speed).
      const double wallBeatSec = outBeatSec / (request.outgoingSpeed > 0.0 ? request.outgoingSpeed : 1.0);
      plan.steps.push_back({dur - 1.0, core::Scratch{out, core::ScratchPattern::Brake, 1.0, wallBeatSec}, "Brake"});
      plan.steps.push_back({dur - 0.05, core::SetVolume{out, 0.0f}, "Brake: silent"});
      break;
    }
    case core::TransitionStyle::Scratch: {
      // Scratched out: baby scratches, a transformer, then a backspin that stops the record right before the drop.
      const double wallBeatSec = outBeatSec / (request.outgoingSpeed > 0.0 ? request.outgoingSpeed : 1.0);
      addEqRamp(plan, out, core::EqBand::High, dur * 0.5, dur - 4.0, 0.0f, -4.0f, "Soften outgoing highs");
      // A different routine each time: three beats of scratch moves, then the backspin on the last beat.
      using P = core::ScratchPattern;
      struct Move {
        P pattern;
        double beats;
        const char* name;
      };
      static constexpr Move kRoutines[][3] = {
          {{P::Baby, 2.0, "Baby scratch"}, {P::Transformer, 1.0, "Transformer"}, {P::Baby, 0.0, ""}},
          {{P::Chirp, 1.0, "Chirp"}, {P::Flare, 1.0, "Flare"}, {P::Scribble, 1.0, "Scribble"}},
          {{P::Tear, 2.0, "Tear"}, {P::Crab, 1.0, "Crab"}, {P::Baby, 0.0, ""}},
          {{P::Stab, 1.0, "Stab"}, {P::Stab, 1.0, "Stab"}, {P::Flare, 1.0, "Flare"}},
          {{P::Drag, 2.0, "Drag"}, {P::Chirp, 1.0, "Chirp"}, {P::Baby, 0.0, ""}},
          {{P::Scribble, 1.0, "Scribble"}, {P::Transformer, 1.0, "Transformer"}, {P::Crab, 1.0, "Crab"}},
      };
      const auto& routine = kRoutines[static_cast<std::size_t>(request.variation) % std::size(kRoutines)];
      double at = dur - 4.0;
      for (const Move& move : routine) {
        if (move.beats <= 0.0) {
          continue;
        }
        plan.steps.push_back({at, core::Scratch{out, move.pattern, move.beats, wallBeatSec}, move.name});
        at += move.beats;
      }
      plan.steps.push_back({dur - 1.0, core::Scratch{out, P::Backspin, 1.0, wallBeatSec}, "Backspin"});
      plan.steps.push_back({dur - 0.05, core::SetVolume{out, 0.0f}, "Outgoing gone"});
      break;
    }
    case core::TransitionStyle::QuickCut:
      plan.steps.push_back({dur - 0.02, core::SetVolume{out, 0.0f}, "Cut the outgoing on the drop"});
      break;
    case core::TransitionStyle::EchoOut:
    case core::TransitionStyle::ReverbOut: {
      // The effect goes on a bar before the drop, after the channel fader: closing the fader stops feeding it, and the
      // echo (or the reverb) rings on into the incoming drop for two more bars.
      const bool echo = style == core::TransitionStyle::EchoOut;
      const double wallBeatSec = outBeatSec / (request.outgoingSpeed > 0.0 ? request.outgoingSpeed : 1.0);
      if (wallBeatSec > 0.0) {
        plan.steps.push_back({dur - 4.0, core::SetFxTempo{out, wallBeatSec}, "Echo on the beat"});
      }
      plan.steps.push_back({dur - 4.0,
                            core::SetFx{out, 0, echo ? core::FxType::Echo : core::FxType::Reverb, true,
                                        echo ? 0.55f : 0.5f, echo ? 0.6f : 0.85f, true},
                            echo ? "Echo out" : "Reverb out"});
      addEqRamp(plan, out, core::EqBand::Low, dur - 4.0, dur - 1.0, 0.0f, -30.0f, "Thin the bass under the effect");
      addVolumeRamp(plan, out, dur - 2.0, dur - 0.5, false, "Fader closes, the effect rings on");
      break;
    }
    default: {
      // The outgoing track keeps its fader up while its mids are traded away; its highs and fader go last.
      const double from = request.dropAtEnd ? dur * 0.75 : swap;
      const double to = request.dropAtEnd ? dur - 0.5 : dur;
      addEqRamp(plan, out, core::EqBand::High, from, to, 0.0f, -15.0f, "Outgoing highs out");
      addVolumeRamp(plan, out, from, to, false, "Outgoing fader down");
      break;
    }
  }

  plan.steps.push_back({swap, core::SetEq{out, core::EqBand::Low, -60.0f}, "Bass swap: outgoing bass out"});
  plan.steps.push_back({swap, core::SetEq{in, core::EqBand::Low, 0.0f},
                        request.dropAtEnd ? "DROP: incoming bass in" : "Bass swap: incoming bass in"});

  // Battle mode: the drop gets an impact, every other mix an air horn on top; a build-up (loop roll, beat loop) a riser.
  if (request.fxHits && request.dropAtEnd) {
    const double beatSec = request.incomingBpm > 0.0 ? 60.0 / request.incomingBpm : 0.0;
    if (style == core::TransitionStyle::LoopRoll || style == core::TransitionStyle::BeatLoopIn) {
      plan.steps.push_back({dur - 4.0, core::TriggerFxHit{core::FxHitType::Riser, 0.4f, beatSec}, "Riser into the drop"});
    }
    plan.steps.push_back({dur, core::TriggerFxHit{core::FxHitType::Impact, 0.45f, beatSec}, "Impact on the drop"});
    if (request.variation % 2 == 1) {
      plan.steps.push_back({dur, core::TriggerFxHit{core::FxHitType::AirHorn, 0.4f, beatSec}, "Air horn"});
    }
  }

  // The end: stop the outgoing deck, then put back what the transition changed (silently: its fader is down).
  plan.steps.push_back({dur, core::Pause{out}, "Stop outgoing deck"});
  if (style == core::TransitionStyle::EchoOut || style == core::TransitionStyle::ReverbOut) {
    plan.durationBeats = dur + 8.0;  // the tail rings over the incoming drop before the effect is switched off
    plan.steps.push_back({dur + 8.0, core::SetFx{out, 0, core::FxType::None, false, 0.0f, 0.5f, true},
                          "Effect off after its tail"});
  }
  // During the mix both decks run at the outgoing tempo (the sync). Afterwards the incoming track, now alone, settles
  // to its own tempo over about a minute: a change far too slow to hear. The stopped outgoing deck is reset at once.
  if (style != core::TransitionStyle::Brake && request.incomingBpm > 0.0 && request.outgoingBpm > 0.0) {
    plan.steps.push_back({dur, core::GlideTempo{in, 1.0, kTempoSettleSeconds}, "Settle to its own tempo, slowly"});
    plan.steps.push_back({dur, core::SetPlaybackSpeed{out, 1.0}, "Outgoing back to its own tempo (stopped)"});
  }
  if (loopingOut || style == core::TransitionStyle::LoopRoll) {
    plan.steps.push_back({dur, core::SetLoop{out, 0.0, 1.0, false}, "Outgoing loop off"});
  }
  if (style == core::TransitionStyle::Brake) {
    plan.steps.push_back({dur, core::SetPlaybackSpeed{out, request.outgoingSpeed > 0.0 ? request.outgoingSpeed : 1.0},
                          "Restore outgoing speed (stopped)"});
  }
  if (style == core::TransitionStyle::FilterFade) {
    plan.steps.push_back({dur, core::SetFilter{out, 0.0f}, "Outgoing filter off (silent)"});
  }
  for (const core::EqBand band : {core::EqBand::Low, core::EqBand::Mid, core::EqBand::High}) {
    plan.steps.push_back({dur, core::SetEq{out, band, 0.0f}, "Reset outgoing EQ (silent)"});
  }
}

}  // namespace

TransitionPlanner::IncomingCue TransitionPlanner::cueIncoming(const core::TrackMixPoints& points, double bpm,
                                                              double maxBeats) noexcept {
  IncomingCue cue;
  cue.durationBeats = std::min(std::max(maxBeats, kMinDropMixBeats), kMaxMixBeats);
  cue.startSec = std::max(0.0, points.mixInSec);
  if (points.dropSec > 0.0 && bpm > 0.0) {
    const double beatSec = 60.0 / bpm;
    const double earliest = (points.mixInSec >= 0.0 && points.mixInSec < points.dropSec) ? points.mixInSec : 0.0;
    const double introBars = std::floor((points.dropSec - earliest) / beatSec / 4.0 + 1.0e-6);
    const double beats = std::min(cue.durationBeats, introBars * 4.0);
    if (beats >= kMinDropMixBeats) {
      cue.durationBeats = beats;
      cue.startSec = points.dropSec - beats * beatSec;
      cue.dropAtEnd = true;
    }
  }
  return cue;
}

core::TransitionPlan TransitionPlanner::planTransition(const core::TransitionRequest& request) const {
  core::TransitionPlan plan;
  plan.outgoingDeck = request.outgoingDeck;
  plan.incomingDeck = request.incomingDeck;
  plan.style = request.style;
  plan.startBeat = request.startBeat;
  plan.durationBeats = std::max(4.0, request.durationBeats);
  plan.isValid = false;

  const auto outDeck = request.outgoingDeck;
  const auto inDeck = request.incomingDeck;
  const double dur = plan.durationBeats;

  switch (request.style) {
    case core::TransitionStyle::BassSwap:
    case core::TransitionStyle::FilterFade:
    case core::TransitionStyle::LoopRoll:
    case core::TransitionStyle::Brake:
    case core::TransitionStyle::Scratch:
    case core::TransitionStyle::BeatLoopIn:
    case core::TransitionStyle::EchoOut:
    case core::TransitionStyle::ReverbOut:
      buildBlend(plan, request);
      break;

    case core::TransitionStyle::DoubleDrop: {
      // Both drops together: the build-up rises with its bass cut, on the drop the basses swap and both tracks hit
      // for four bars, then the outgoing one fades out under the incoming one.
      const double drop = dur;
      plan.durationBeats = dur + 32.0;
      plan.steps.push_back({0.0, core::SetVolume{inDeck, 0.0f}, "Incoming fader closed"});
      plan.steps.push_back({0.0, core::SetEq{inDeck, core::EqBand::Low, -60.0f}, "Kill incoming bass"});
      plan.steps.push_back({0.0, core::SetEq{inDeck, core::EqBand::Mid, -6.0f}, "Incoming mids down"});
      plan.steps.push_back({0.0, core::Play{inDeck}, "Start incoming build-up in phase"});
      addVolumeRamp(plan, inDeck, 0.0, dur * 0.5, true, "Incoming fader up");
      addEqRamp(plan, inDeck, core::EqBand::Mid, dur * 0.5, drop, -6.0f, 0.0f, "Incoming mids in");
      plan.steps.push_back({drop, core::SetEq{outDeck, core::EqBand::Low, -60.0f}, "DOUBLE DROP: outgoing bass out"});
      plan.steps.push_back({drop, core::SetEq{inDeck, core::EqBand::Low, 0.0f}, "DOUBLE DROP: incoming bass in"});
      plan.steps.push_back({drop, core::SetEq{outDeck, core::EqBand::Mid, -4.0f}, "Make room in the mids"});
      addEqRamp(plan, outDeck, core::EqBand::High, drop + 16.0, drop + 31.5, 0.0f, -15.0f, "Outgoing highs out");
      addVolumeRamp(plan, outDeck, drop + 16.0, drop + 31.5, false, "Outgoing fades under the incoming drop");
      plan.steps.push_back({plan.durationBeats, core::Pause{outDeck}, "Stop outgoing deck"});
      for (const core::EqBand band : {core::EqBand::Low, core::EqBand::Mid, core::EqBand::High}) {
        plan.steps.push_back({plan.durationBeats, core::SetEq{outDeck, band, 0.0f}, "Reset outgoing EQ (silent)"});
      }
      if (request.incomingBpm > 0.0 && request.outgoingBpm > 0.0) {
        plan.steps.push_back({plan.durationBeats, core::GlideTempo{inDeck, 1.0, 60.0}, "Settle to its own tempo, slowly"});
        plan.steps.push_back({plan.durationBeats, core::SetPlaybackSpeed{outDeck, 1.0}, "Outgoing back to its own tempo"});
      }
      break;
    }

    case core::TransitionStyle::StemBlend: {
      // Acapella over the beat: the incoming instrumental comes in under the outgoing track, then the outgoing track
      // loses everything but its voice, which sings over the new beat until the incoming vocals take over.
      using K = core::StemKind;
      plan.steps.push_back({0.0, core::SetVolume{inDeck, 0.0f}, "Incoming fader closed"});
      plan.steps.push_back({0.0, core::SetStemMute{inDeck, K::Vocals, true}, "Incoming: instrumental only"});
      plan.steps.push_back({0.0, core::SetStemMute{inDeck, K::Bass, true}, "Incoming: no bass yet"});
      plan.steps.push_back({0.0, core::Play{inDeck}, "Start incoming instrumental in phase"});
      addVolumeRamp(plan, inDeck, 0.0, dur * 0.25, true, "Incoming fader up");
      const double half = dur * 0.5;
      for (const K stem : {K::Drums, K::Bass, K::Other}) {
        plan.steps.push_back({half, core::SetStemMute{outDeck, stem, true}, "Outgoing: acapella"});
      }
      plan.steps.push_back({half, core::SetStemMute{inDeck, K::Bass, false}, "Incoming bass in under the acapella"});
      addVolumeRamp(plan, outDeck, dur * 0.75, dur - 0.5, false, "The acapella fades");
      plan.steps.push_back({dur, core::SetStemMute{inDeck, K::Vocals, false}, "Incoming vocals take over"});
      plan.steps.push_back({dur, core::Pause{outDeck}, "Stop outgoing deck"});
      for (const K stem : {K::Drums, K::Bass, K::Other}) {
        plan.steps.push_back({dur, core::SetStemMute{outDeck, stem, false}, "Reset outgoing stems (silent)"});
      }
      if (request.incomingBpm > 0.0 && request.outgoingBpm > 0.0) {
        plan.steps.push_back({dur, core::GlideTempo{inDeck, 1.0, 60.0}, "Settle to its own tempo, slowly"});
        plan.steps.push_back({dur, core::SetPlaybackSpeed{outDeck, 1.0}, "Outgoing back to its own tempo"});
      }
      break;
    }

    case core::TransitionStyle::QuickCut: {
      if (request.dropAtEnd) {
        buildBlend(plan, request);  // a build-up under the outgoing track, then a hard cut onto the drop
        break;
      }
      plan.steps.push_back({0.0, core::Pause{outDeck}, "Cut outgoing deck"});
      plan.steps.push_back({0.0, core::SetVolume{inDeck, 1.0f}, "Ensure full volume on incoming"});
      plan.steps.push_back({0.0, core::Play{inDeck}, "Trigger incoming drop downbeat"});
      break;
    }

    case core::TransitionStyle::VolumeCrossfade: {
      // Step 1: Start incoming at 0.0 volume
      plan.steps.push_back({0.0, core::SetVolume{inDeck, 0.0f}, "Prepare incoming at zero volume"});
      plan.steps.push_back({0.0, core::Play{inDeck}, "Start incoming deck"});

      constexpr int kSubSteps = 8;
      for (int i = 1; i <= kSubSteps; ++i) {
        const double b = (dur * static_cast<double>(i)) / static_cast<double>(kSubSteps);
        const float inVol = static_cast<float>(i) / static_cast<float>(kSubSteps);
        const float outVol = 1.0f - inVol;

        plan.steps.push_back({b, core::SetVolume{inDeck, inVol}, "Ramp incoming volume"});
        plan.steps.push_back({b, core::SetVolume{outDeck, outVol}, "Ramp outgoing volume"});
      }

      plan.steps.push_back({dur, core::Pause{outDeck}, "Pause outgoing deck"});
      plan.steps.push_back({dur, core::SetVolume{outDeck, 1.0f}, "Reset outgoing volume"});
      break;
    }
  }

  plan.isValid = !plan.steps.empty();

  std::ostringstream ss;
  ss << core::transitionStyleName(request.style) << " transition ("
     << static_cast<int>(plan.durationBeats) << " beats) from Deck "
     << static_cast<int>(core::index(outDeck)) + 1 << " to Deck "
     << static_cast<int>(core::index(inDeck)) + 1 << ".";
  plan.summary = ss.str();

  return plan;
}

std::size_t TransitionPlanner::scheduleTransition(
    const core::TransitionPlan& plan,
    core::ICommandTimelineScheduler& scheduler) const {
  if (!plan.isValid || plan.steps.empty()) {
    return 0;
  }

  std::size_t count = 0;
  for (const auto& step : plan.steps) {
    const double targetBeat = plan.startBeat + step.beatOffset;
    scheduler.scheduleAtBeat(step.command, targetBeat, core::QuantiseGrid::None, step.description);
    ++count;
  }

  return count;
}

}  // namespace zyron::ai
