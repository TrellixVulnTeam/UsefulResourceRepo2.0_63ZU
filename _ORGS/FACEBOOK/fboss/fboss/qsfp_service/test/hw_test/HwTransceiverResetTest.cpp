/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/qsfp_service/test/hw_test/HwTest.h"

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"

#include "fboss/qsfp_service/test/hw_test/HwPortUtils.h"
#include "fboss/qsfp_service/test/hw_test/HwQsfpEnsemble.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

TEST_F(HwTest, resetTranscieverAndDetectPresence) {
  auto portMap = std::make_unique<WedgeManager::PortMap>();
  auto agentConfig = getHwQsfpEnsemble()->getWedgeManager()->getAgentConfig();
  auto& swConfig = *agentConfig->thrift.sw_ref();
  for (auto& port : *swConfig.ports_ref()) {
    if (*port.state_ref() != cfg::PortState::ENABLED) {
      continue;
    }
    auto portId = *port.logicalID_ref();
    portMap->emplace(
        portId, utility::getPortStatus(PortID(portId), getHwQsfpEnsemble()));
  }
  auto wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
  std::map<int32_t, TransceiverInfo> transceivers;
  wedgeManager->syncPorts(transceivers, std::move(portMap));

  // Validate that the power control register has been correctly set before we
  // begin resetting the modules
  auto transceiverIds = refreshTransceiversWithRetry();
  auto expectedIds =
      utility::getCabledPortTranceivers(*agentConfig, getHwQsfpEnsemble());
  EXPECT_TRUE(utility::containsSubset(transceiverIds, expectedIds));

  transceivers.clear();
  wedgeManager->getTransceiversInfo(
      transceivers,
      std::make_unique<std::vector<int32_t>>(
          utility::legacyTransceiverIds(expectedIds)));
  for (auto idAndTransceiver : transceivers) {
    if (*idAndTransceiver.second.present_ref()) {
      XLOG(INFO)
          << "Transceiver:" << idAndTransceiver.first
          << " before hard reset, power control="
          << apache::thrift::util::enumNameSafe(
                 *idAndTransceiver.second.settings_ref()->powerControl_ref());
      EXPECT_TRUE(
          *idAndTransceiver.second.settings_ref()->powerControl_ref() ==
              PowerControlState::POWER_OVERRIDE ||
          *idAndTransceiver.second.settings_ref()->powerControl_ref() ==
              PowerControlState::HIGH_POWER_OVERRIDE);
    }
  }

  // Reset all transceivers
  for (auto idAndTransceiver : transceivers) {
    wedgeManager->triggerQsfpHardReset(idAndTransceiver.first);
  }
  // Clear reset on all transceivers
  wedgeManager->clearAllTransceiverReset();
  // Refresh
  refreshTransceiversWithRetry();

  std::map<int32_t, TransceiverInfo> transceiversAfterReset;
  wedgeManager->getTransceiversInfo(
      transceiversAfterReset,
      std::make_unique<std::vector<int32_t>>(
          utility::legacyTransceiverIds(expectedIds)));
  // Assert that we can detect all transceivers again

  for (auto idAndTransceiver : transceivers) {
    if (*idAndTransceiver.second.present_ref()) {
      XLOG(DBG2) << "Checking that transceiver : " << idAndTransceiver.first
                 << " was detected after reset";
      auto titr = transceiversAfterReset.find(idAndTransceiver.first);
      EXPECT_TRUE(titr != transceiversAfterReset.end());
      EXPECT_TRUE(*titr->second.present_ref());

      XLOG(INFO)
          << "Transceiver:" << idAndTransceiver.first
          << " before hard reset, power control="
          << apache::thrift::util::enumNameSafe(
                 *idAndTransceiver.second.settings_ref()->powerControl_ref())
          << ", after hard reset, power control="
          << apache::thrift::util::enumNameSafe(
                 *titr->second.settings_ref()->powerControl_ref());
      EXPECT_EQ(
          *idAndTransceiver.second.settings_ref()->powerControl_ref(),
          *titr->second.settings_ref()->powerControl_ref());
    }
  }
}
} // namespace facebook::fboss
