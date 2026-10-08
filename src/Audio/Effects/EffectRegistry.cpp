// SPDX-License-Identifier: AGPL-3.0-only
#include "Audio/Effects/EffectRegistry.hpp"

#include "Audio/Effects/DelayEffect.hpp"
#include "Audio/Effects/EchoEffect.hpp"
#include "Audio/Effects/FlangerEffect.hpp"
#include "Audio/Effects/PhaserEffect.hpp"
#include "Audio/Effects/ReverbEffect.hpp"

namespace zyron::audio {

EffectRegistry& EffectRegistry::instance() {
  static EffectRegistry registry;
  return registry;
}

void EffectRegistry::registerEffect(std::string_view id, Factory factory) {
  std::lock_guard<std::mutex> lock(mutex_);
  factories_[std::string(id)] = std::move(factory);
}

std::unique_ptr<Effect> EffectRegistry::create(std::string_view id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = factories_.find(std::string(id));
  if (it != factories_.end()) {
    return it->second();
  }
  return nullptr;
}

bool EffectRegistry::hasEffect(std::string_view id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return factories_.find(std::string(id)) != factories_.end();
}

std::vector<std::string> EffectRegistry::registeredIds() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> ids;
  ids.reserve(factories_.size());
  for (const auto& [id, _] : factories_) {
    ids.push_back(id);
  }
  return ids;
}

void EffectRegistry::registerBuiltins() {
  registerEffect("delay", []() { return std::make_unique<DelayEffect>(); });
  registerEffect("echo", []() { return std::make_unique<EchoEffect>(); });
  registerEffect("reverb", []() { return std::make_unique<ReverbEffect>(); });
  registerEffect("flanger", []() { return std::make_unique<FlangerEffect>(); });
  registerEffect("phaser", []() { return std::make_unique<PhaserEffect>(); });
}

void EffectRegistry::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  factories_.clear();
}

}  // namespace zyron::audio
