// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/test/HwTestTrunkUtils.h"

#include "fboss/agent/hw/test/HwSwitchEnsemble.h"

#include "fboss/agent/hw/sai/hw_test/SaiSwitchEnsemble.h"
#include "fboss/agent/hw/sai/switch/SaiLagManager.h"
#include "fboss/agent/hw/sai/switch/SaiRxPacket.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"

#include <gtest/gtest.h>

namespace facebook::fboss::utility {

void verifyAggregatePortCount(const HwSwitchEnsemble* ensemble, uint8_t count) {
  auto* saiSwitchEnsemble = static_cast<const SaiSwitchEnsemble*>(ensemble);
  auto* saiSwitch = saiSwitchEnsemble->getHwSwitch();
  EXPECT_EQ(saiSwitch->managerTable()->lagManager().getLagCount(), count);
}

void verifyAggregatePort(
    const HwSwitchEnsemble* ensemble,
    AggregatePortID aggregatePortID) {
  auto* saiSwitchEnsemble = static_cast<const SaiSwitchEnsemble*>(ensemble);
  auto* saiSwitch = saiSwitchEnsemble->getHwSwitch();
  auto& lagManager = saiSwitch->managerTable()->lagManager();
  EXPECT_NO_THROW(lagManager.getLagHandle(aggregatePortID));
}

void verifyAggregatePortMemberCount(
    const HwSwitchEnsemble* ensemble,
    AggregatePortID aggregatePortID,
    uint8_t totalCount,
    uint8_t currentCount) {
  auto* saiSwitchEnsemble = static_cast<const SaiSwitchEnsemble*>(ensemble);
  auto* saiSwitch = saiSwitchEnsemble->getHwSwitch();
  auto& lagManager = saiSwitch->managerTable()->lagManager();
  EXPECT_EQ(lagManager.getLagMemberCount(aggregatePortID), totalCount);
  EXPECT_EQ(lagManager.getActiveMemberCount(aggregatePortID), currentCount);
}

void verifyPktFromAggregatePort(
    const HwSwitchEnsemble* /*ensemble*/,
    AggregatePortID aggregatePortID) {
  std::array<char, 8> data{};
  auto rxPacket = std::make_unique<SaiRxPacket>(
      data.size(), data.data(), aggregatePortID, VlanID(1));
  EXPECT_TRUE(rxPacket->isFromAggregatePort());
}

} // namespace facebook::fboss::utility
