/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/qsfp_service/StatsPublisher.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeManager.h"
#include "fboss/qsfp_service/test/hw_test/HwExternalPhyPortTest.h"
#include "fboss/qsfp_service/test/hw_test/HwQsfpEnsemble.h"
#include "fboss/qsfp_service/test/hw_test/HwTest.h"

namespace facebook::fboss {

TEST_F(HwTest, publishStats) {
  StatsPublisher publisher(getHwQsfpEnsemble()->getWedgeManager());
  publisher.init();
  publisher.publishStats(nullptr, 0);
  getHwQsfpEnsemble()->getWedgeManager()->publishI2cTransactionStats();
}

namespace {
int getSleepSeconds(PlatformMode platformMode) {
  // Minipack Xphy stats are relatively slow, one port xphy stat can take
  // about 3s. And because all ports from the same pim need to wait for
  // the pim EventBase, even though we can make the stats collection across
  // all 8 pims in parallel, we might still need about 3X16=48 seconds for
  // all ports. Therefore, use 1 min for all stats to be collected.
  int sleepSeconds = 5;
  if (platformMode == PlatformMode::MINIPACK ||
      platformMode == PlatformMode::FUJI) {
    sleepSeconds = 60;
  }
  return sleepSeconds;
}
} // namespace

class HwXphyPortStatsCollectionTest : public HwExternalPhyPortTest {
 public:
  const std::vector<phy::ExternalPhy::Feature>& neededFeatures()
      const override {
    static const std::vector<phy::ExternalPhy::Feature> kNeededFeatures = {
        phy::ExternalPhy::Feature::PORT_STATS};
    return kNeededFeatures;
  }

  void runTest() {
    const auto& availableXphyPorts = findAvailableXphyPorts();
    auto setup = [this, &availableXphyPorts]() {
      auto* wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
      for (const auto& [port, profile] : availableXphyPorts) {
        // First program the xphy port
        wedgeManager->programXphyPort(port, profile);
      }
    };

    auto verify = [&]() {
      // Change the default xphy port stat collection delay to 1s
      gflags::SetCommandLineOptionWithMode(
          "xphy_port_stat_interval_secs", "0", gflags::SET_FLAGS_DEFAULT);
      getHwQsfpEnsemble()->getWedgeManager()->updateAllXphyPortsStats();
      /* sleep override */
      sleep(getSleepSeconds(
          getHwQsfpEnsemble()->getWedgeManager()->getPlatformMode()));
      // Now check the stats collection future job is done.
      for (const auto& [port, _] : availableXphyPorts) {
        EXPECT_TRUE(
            getHwQsfpEnsemble()->getPhyManager()->isXphyStatsCollectionDone(
                port))
            << "port:" << port << " xphy stats collection is not done";
      }
    };
    verifyAcrossWarmBoots(setup, verify);
  }
};

TEST_F(HwXphyPortStatsCollectionTest, checkXphyStatsCollectionDone) {
  runTest();
}

class HwXphyPrbsStatsCollectionTest : public HwExternalPhyPortTest {
 public:
  const std::vector<phy::ExternalPhy::Feature>& neededFeatures()
      const override {
    static const std::vector<phy::ExternalPhy::Feature> kNeededFeatures = {
        phy::ExternalPhy::Feature::PRBS, phy::ExternalPhy::Feature::PRBS_STATS};
    return kNeededFeatures;
  }

  void runTest(phy::Side side) {
    const auto& availableXphyPorts = findAvailableXphyPorts();
    auto setup = [this, &availableXphyPorts, side]() {
      auto* wedgeManager = getHwQsfpEnsemble()->getWedgeManager();
      for (const auto& [port, profile] : availableXphyPorts) {
        // First program the xphy port
        wedgeManager->programXphyPort(port, profile);
        // Then program this xphy port with a common prbs polynominal
        phy::PortPrbsState prbs;
        prbs.enabled_ref() = true;
        static constexpr auto kCommonPolynominal = 9;
        prbs.polynominal_ref() = kCommonPolynominal;
        wedgeManager->programXphyPortPrbs(port, side, prbs);
      }
    };

    auto verify = [&]() {
      // Change the default xphy port stat collection delay to no delay
      gflags::SetCommandLineOptionWithMode(
          "xphy_port_stat_interval_secs", "0", gflags::SET_FLAGS_DEFAULT);
      gflags::SetCommandLineOptionWithMode(
          "xphy_prbs_stat_interval_secs", "0", gflags::SET_FLAGS_DEFAULT);

      auto platformMode =
          getHwQsfpEnsemble()->getWedgeManager()->getPlatformMode();
      int sleepSeconds = getSleepSeconds(platformMode);

      // The first update stats will set the prbs stats to lock and accumulated
      // errors to 0
      getHwQsfpEnsemble()->getWedgeManager()->updateAllXphyPortsStats();
      /* sleep override */
      sleep(sleepSeconds);
      // The second update stats will provide the prbs stats since the first
      // update, which should see some bers
      getHwQsfpEnsemble()->getWedgeManager()->updateAllXphyPortsStats();
      /* sleep override */
      sleep(sleepSeconds);

      // Now check the stats collection future job is done.
      for (const auto& [port, _] : availableXphyPorts) {
        EXPECT_TRUE(
            getHwQsfpEnsemble()->getPhyManager()->isPrbsStatsCollectionDone(
                port))
            << "port:" << port << " prbs stats collection is not done";

        // For Minipack family we need to enable both sides prbs for actually
        // seeing any error bits. Like asic and xphy system side.
        // Since this is only qsfp_hw_test, it won't be able to trigger
        // asic prbs programming without using wedge_agent.
        // Skip the prbs stats check for Minipack family
        if (platformMode == PlatformMode::MINIPACK ||
            platformMode == PlatformMode::FUJI) {
          continue;
        }

        // Also try to get prbs stats
        const auto& prbsStats =
            getHwQsfpEnsemble()->getPhyManager()->getPortPrbsStats(port, side);
        EXPECT_TRUE(!prbsStats.empty());
        const auto& phyPortConfig =
            getHwQsfpEnsemble()->getPhyManager()->getHwPhyPortConfig(port);
        auto modulation =
            (side == phy::Side::SYSTEM
                 ? *phyPortConfig.profile.system.modulation_ref()
                 : *phyPortConfig.profile.line.modulation_ref());
        for (const auto& lanePrbsStats : prbsStats) {
          EXPECT_TRUE(*lanePrbsStats.timeSinceLastLocked_ref() >= sleepSeconds);
          EXPECT_TRUE(*lanePrbsStats.timeSinceLastClear_ref() >= sleepSeconds);
          // NRZ on short channels can have 0 errors.
          // Expecting PAM4 to be BER 0 is not reasonable.
          // xphy and optics are both on the pim so the channel is short
          // The system side link from ASIC<->XPHY goes through a connector and
          // that adds loss
          if (modulation == phy::IpModulation::PAM4 &&
              side == phy::Side::SYSTEM) {
            EXPECT_TRUE(*lanePrbsStats.ber_ref() > 0);
          }
          EXPECT_TRUE(*lanePrbsStats.maxBer_ref() >= *lanePrbsStats.ber_ref());
          EXPECT_TRUE(*lanePrbsStats.locked_ref());
          EXPECT_TRUE(*lanePrbsStats.numLossOfLock_ref() == 0);
        }
      }
    };
    verifyAcrossWarmBoots(setup, verify);
  }
};

TEST_F(HwXphyPrbsStatsCollectionTest, getSystemPrbsStats) {
  runTest(phy::Side::SYSTEM);
}
TEST_F(HwXphyPrbsStatsCollectionTest, getLinePrbsStats) {
  runTest(phy::Side::LINE);
}
} // namespace facebook::fboss
