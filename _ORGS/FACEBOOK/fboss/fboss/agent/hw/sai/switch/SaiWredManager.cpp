/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiWredManager.h"

#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/state/PortQueue.h"

namespace facebook::fboss {

constexpr auto kDefaultDropProbability = 100;

std::shared_ptr<SaiWred> SaiWredManager::getOrCreateProfile(
    const PortQueue& queue) {
  if (!queue.getAqms().size()) {
    return nullptr;
  }
  auto attributes = profileCreateAttrs(queue);
  auto& store = saiStore_->get<SaiWredTraits>();
  SaiWredTraits::AdapterHostKey k = tupleProjection<
      SaiWredTraits::CreateAttributes,
      SaiWredTraits::AdapterHostKey>(attributes);
  return store.setObject(k, attributes);
}

SaiWredTraits::CreateAttributes SaiWredManager::profileCreateAttrs(
    const PortQueue& queue) const {
  SaiWredTraits::CreateAttributes attrs;
  using Attributes = SaiWredTraits::Attributes;
  std::get<Attributes::GreenEnable>(attrs) = false;
  std::get<Attributes::EcnMarkMode>(attrs) = SAI_ECN_MARK_MODE_NONE;
  auto& greenMin =
      std::get<std::optional<Attributes::GreenMinThreshold>>(attrs);
  auto& greenMax =
      std::get<std::optional<Attributes::GreenMaxThreshold>>(attrs);
  auto& greenDropProbability =
      std::get<std::optional<Attributes::GreenDropProbability>>(attrs);
  auto& ecnGreenMin =
      std::get<std::optional<Attributes::EcnGreenMinThreshold>>(attrs);
  auto& ecnGreenMax =
      std::get<std::optional<Attributes::EcnGreenMaxThreshold>>(attrs);
  std::tie(greenMin, greenMax, greenDropProbability, ecnGreenMin, ecnGreenMax) =
      std::make_tuple(0, 0, kDefaultDropProbability, 0, 0);
  for (const auto& [type, aqm] : queue.getAqms()) {
    auto thresholds = (*aqm.detection_ref()).get_linear();
    auto [minLen, maxLen] = std::make_pair(
        *thresholds.minimumLength_ref(), *thresholds.maximumLength_ref());
    auto probability = *thresholds.probability_ref();
    switch (type) {
      case cfg::QueueCongestionBehavior::EARLY_DROP:
        std::get<Attributes::GreenEnable>(attrs) = true;
        greenMin = minLen;
        greenMax = maxLen;
        greenDropProbability = probability;
        break;
      case cfg::QueueCongestionBehavior::ECN:
        std::get<Attributes::EcnMarkMode>(attrs) = SAI_ECN_MARK_MODE_GREEN;
        ecnGreenMin = minLen;
        ecnGreenMax = maxLen;
        break;
    }
  }
  return attrs;
}

// CSP CS00012207175
// Currently the sai implementation returns the incorrect default drop
// probability. We workaround by explicitly removing all wred profile and
// recreate them.
void SaiWredManager::removeUnclaimedWredProfile() {
  saiStore_->get<SaiWredTraits>().removeUnclaimedWarmbootHandlesIf(
      [](const auto& wred) {
        wred->release();
        return true;
      });
}

} // namespace facebook::fboss
