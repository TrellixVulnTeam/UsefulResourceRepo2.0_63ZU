/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/platforms/common/PlatformMapping.h"

#include <gtest/gtest.h>

namespace facebook {
namespace fboss {
namespace test {
class PlatformMappingTest : public ::testing::Test {
 public:
  void SetUp() override {}

  void setExpectation(
      int numPort,
      int numIphy,
      int numXphy,
      int numTcvr,
      std::vector<cfg::PlatformPortConfigFactor>& profilesFactors) {
    expectedNumPort_ = numPort;
    expectedNumIphy_ = numIphy;
    expectedNumXphy_ = numXphy;
    expectedNumTcvr_ = numTcvr;
    expectedProfileFactors_ = std::move(profilesFactors);
  }

  void setExpectation(
      int numPort,
      int numIphy,
      int numXphy,
      int numTcvr,
      std::vector<cfg::PortProfileID>& profiles) {
    std::vector<cfg::PlatformPortConfigFactor> profileFactors;
    for (auto profile : profiles) {
      cfg::PlatformPortConfigFactor factor;
      factor.profileID_ref() = profile;
      profileFactors.push_back(factor);
    }
    setExpectation(numPort, numIphy, numXphy, numTcvr, profileFactors);
  }

  void verify(PlatformMapping* mapping) {
    EXPECT_EQ(expectedNumPort_, mapping->getPlatformPorts().size());
    for (auto factor : expectedProfileFactors_) {
      if (auto pimIDs = factor.pimIDs_ref()) {
        std::optional<phy::PortProfileConfig> prevProfile;
        for (auto pimID : *pimIDs) {
          auto platformSupportedProfile =
              mapping->getPortProfileConfig(PlatformPortProfileConfigMatcher(
                  factor.get_profileID(), PimID(pimID)));
          EXPECT_TRUE(platformSupportedProfile.has_value());
          // compare with profile from previous pim. Since they are in the
          // same factor, the config should be the same
          if (prevProfile.has_value()) {
            EXPECT_EQ(platformSupportedProfile, prevProfile);
          }
          prevProfile = platformSupportedProfile;
        }
      } else {
        auto platformSupportedProfile =
            mapping->getPortProfileConfig(PlatformPortProfileConfigMatcher(
                factor.get_profileID(), std::nullopt));
        EXPECT_TRUE(platformSupportedProfile.has_value());
      }
    }

    int numIphy = 0, numXphy = 0, numTcvr = 0;
    for (const auto& chip : mapping->getChips()) {
      switch (*chip.second.type_ref()) {
        case phy::DataPlanePhyChipType::IPHY:
          numIphy++;
          break;
        case phy::DataPlanePhyChipType::XPHY:
          numXphy++;
          break;
        case phy::DataPlanePhyChipType::TRANSCEIVER:
          numTcvr++;
          break;
        default:
          break;
      }
    }
    EXPECT_EQ(expectedNumIphy_, numIphy);
    EXPECT_EQ(expectedNumXphy_, numXphy);
    EXPECT_EQ(expectedNumTcvr_, numTcvr);
  }

  void verifyTxSettings(
      phy::TxSettings tx,
      std::array<int, 6> expected,
      bool hasDriveCurrent = false) {
    EXPECT_EQ(*tx.pre2_ref(), expected[0]);
    EXPECT_EQ(*tx.pre_ref(), expected[1]);
    EXPECT_EQ(*tx.main_ref(), expected[2]);
    EXPECT_EQ(*tx.post_ref(), expected[3]);
    EXPECT_EQ(*tx.post2_ref(), expected[4]);
    EXPECT_EQ(*tx.post3_ref(), expected[5]);
    EXPECT_EQ(tx.driveCurrent_ref().has_value(), hasDriveCurrent);
  }

  void verifyTxSettingsByProfile(
      PlatformMapping* platformMapping,
      std::map<int32_t, cfg::PlatformPortEntry> platformPorts,
      std::map<cfg::PortProfileID, std::array<int, 6>> txSettingsMap) {
    for (auto& [portID, portEntry] : platformPorts) {
      const auto& profiles = *portEntry.supportedProfiles_ref();
      for (auto [profileID, profileConfig] : profiles) {
        auto pinCfgs = platformMapping->getPortIphyPinConfigs(
            PlatformPortProfileConfigMatcher(profileID, PortID(portID)));
        for (auto pinCfg : pinCfgs) {
          auto tx = pinCfg.tx_ref();
          if (txSettingsMap.find(profileID) != txSettingsMap.end()) {
            EXPECT_TRUE(tx.has_value());
            verifyTxSettings(*tx, txSettingsMap[profileID]);
          } else {
            EXPECT_FALSE(tx.has_value());
          }
        }
      }
    }
  }

 private:
  int expectedNumPort_{0};
  int expectedNumIphy_{0};
  int expectedNumXphy_{0};
  int expectedNumTcvr_{0};
  std::vector<cfg::PlatformPortConfigFactor> expectedProfileFactors_;
};
} // namespace test
} // namespace fboss
} // namespace facebook
