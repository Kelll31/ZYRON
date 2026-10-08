// SPDX-License-Identifier: AGPL-3.0-only
#include "AI/Transition/TransitionPlanner.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace zyron::ai {

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
    case core::TransitionStyle::BassSwap: {
      // Step 1 (Beat 0): Start incoming deck, set low kill, set volume 1.0
      plan.steps.push_back({0.0, core::SetEq{inDeck, core::EqBand::Low, -60.0f}, "Kill incoming bass"});
      plan.steps.push_back({0.0, core::SetEq{inDeck, core::EqBand::Mid, -2.0f}, "Dip incoming mid slightly"});
      plan.steps.push_back({0.0, core::SetVolume{inDeck, 1.0f}, "Open incoming channel fader"});
      plan.steps.push_back({0.0, core::Play{inDeck}, "Start incoming deck playback"});

      // Step 2 (Quarter way, e.g. beat 16 in 64-beat mix): Bring incoming mids to neutral
      const double q1 = dur * 0.25;
      plan.steps.push_back({q1, core::SetEq{inDeck, core::EqBand::Mid, 0.0f}, "Restore incoming mid"});

      // Step 3 (Midpoint / Drop, e.g. beat 32): Bass swap!
      const double mid = dur * 0.50;
      plan.steps.push_back({mid, core::SetEq{outDeck, core::EqBand::Low, -60.0f}, "Kill outgoing bass"});
      plan.steps.push_back({mid, core::SetEq{inDeck, core::EqBand::Low, 0.0f}, "Bring in incoming bass punch"});

      // Step 4 (Three-quarters, e.g. beat 48): Fade outgoing highs and mids
      const double q3 = dur * 0.75;
      plan.steps.push_back({q3, core::SetEq{outDeck, core::EqBand::Mid, -8.0f}, "Duck outgoing mid"});
      plan.steps.push_back({q3, core::SetEq{outDeck, core::EqBand::High, -6.0f}, "Duck outgoing high"});
      plan.steps.push_back({q3, core::SetVolume{outDeck, 0.5f}, "Lower outgoing volume fader"});

      // Step 5 (Final beat): Outgoing silence and pause
      plan.steps.push_back({dur - 1.0, core::SetVolume{outDeck, 0.0f}, "Fade out outgoing channel"});
      plan.steps.push_back({dur, core::Pause{outDeck}, "Pause outgoing deck"});
      plan.steps.push_back({dur, core::SetEq{outDeck, core::EqBand::Low, 0.0f}, "Reset outgoing EQ Low"});
      plan.steps.push_back({dur, core::SetVolume{outDeck, 1.0f}, "Reset outgoing volume"});
      break;
    }

    case core::TransitionStyle::StemBlend: {
      // Step 1 (Beat 0): Start incoming deck with vocal and bass muted
      plan.steps.push_back({0.0, core::SetStemMute{inDeck, core::StemKind::Vocals, true}, "Mute incoming vocals"});
      plan.steps.push_back({0.0, core::SetStemMute{inDeck, core::StemKind::Bass, true}, "Mute incoming bass"});
      plan.steps.push_back({0.0, core::SetVolume{inDeck, 1.0f}, "Open incoming channel fader"});
      plan.steps.push_back({0.0, core::Play{inDeck}, "Start incoming deck playback"});

      // Step 2 (Quarter way): Outgoing vocal mute, incoming vocal unmute
      const double q1 = dur * 0.25;
      plan.steps.push_back({q1, core::SetStemMute{outDeck, core::StemKind::Vocals, true}, "Mute outgoing vocals"});
      plan.steps.push_back({q1, core::SetStemMute{inDeck, core::StemKind::Vocals, false}, "Unmute incoming vocals"});

      // Step 3 (Midpoint): Swap bass stems
      const double mid = dur * 0.50;
      plan.steps.push_back({mid, core::SetStemMute{outDeck, core::StemKind::Bass, true}, "Mute outgoing bass stem"});
      plan.steps.push_back({mid, core::SetStemMute{inDeck, core::StemKind::Bass, false}, "Unmute incoming bass stem"});

      // Step 4 (Three-quarters): Mute outgoing other / melody
      const double q3 = dur * 0.75;
      plan.steps.push_back({q3, core::SetStemMute{outDeck, core::StemKind::Other, true}, "Mute outgoing other stem"});

      // Step 5 (Final beat): Pause outgoing and unmute stems for next load
      plan.steps.push_back({dur, core::Pause{outDeck}, "Pause outgoing deck"});
      plan.steps.push_back({dur, core::SetStemMute{outDeck, core::StemKind::Vocals, false}, "Reset outgoing vocal mute"});
      plan.steps.push_back({dur, core::SetStemMute{outDeck, core::StemKind::Bass, false}, "Reset outgoing bass mute"});
      plan.steps.push_back({dur, core::SetStemMute{outDeck, core::StemKind::Other, false}, "Reset outgoing other mute"});
      break;
    }

    case core::TransitionStyle::QuickCut: {
      plan.steps.push_back({0.0, core::Pause{outDeck}, "Cut outgoing deck"});
      plan.steps.push_back({0.0, core::SetVolume{inDeck, 1.0f}, "Ensure full volume on incoming"});
      plan.steps.push_back({0.0, core::Play{inDeck}, "Trigger incoming drop downbeat"});
      break;
    }

    case core::TransitionStyle::VolumeCrossfade:
    case core::TransitionStyle::FilterFade: {
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
