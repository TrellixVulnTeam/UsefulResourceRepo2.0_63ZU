// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/Main.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

namespace facebook::fboss {

class AgentTest : public ::testing::Test, public AgentInitializer {
 public:
  virtual ~AgentTest();
  void SetUp() override {
    setupAgent();
  }
  void TearDown() override;

 protected:
  void setupAgent();
  void runForever() const;
  virtual void setupConfigFlag();
  virtual void setupFlags() const;
  std::map<std::string, HwPortStats> getPortStats(
      const std::vector<std::string>& ports) const;

  void resolveNeighbor(
      PortDescriptor port,
      const folly::IPAddress& ip,
      folly::MacAddress mac);
  bool waitForSwitchStateCondition(
      std::function<bool(const std::shared_ptr<SwitchState>&)> conditionFn,
      uint32_t retries = 10,
      std::chrono::duration<uint32_t, std::milli> msBetweenRetry =
          std::chrono::milliseconds(1000)) const;
  void setPortStatus(PortID port, bool up);
  void setPortLoopbackMode(PortID port, cfg::PortLoopbackMode mode);
  std::vector<std::string> getPortNames(const std::vector<PortID>& ports) const;
  void waitForLinkStatus(
      const std::vector<PortID>& portsToCheck,
      bool up,
      uint32_t retries = 60,
      std::chrono::duration<uint32_t, std::milli> msBetweenRetry =
          std::chrono::milliseconds(1000)) const;

  template <
      typename SETUP_FN,
      typename VERIFY_FN,
      typename SETUP_POSTWB_FN,
      typename VERIFY_POSTWB_FN>
  void verifyAcrossWarmBoots(
      SETUP_FN setup,
      VERIFY_FN verify,
      SETUP_POSTWB_FN setupPostWarmboot,
      VERIFY_POSTWB_FN verifyPostWarmboot) {
    if (platform()->getHwSwitch()->getBootType() != BootType::WARM_BOOT) {
      XLOG(INFO) << "cold boot setup()";
      setup();
    }

    if (runVerification()) {
      XLOG(INFO) << "verify()";
      verify();
    }

    if (platform()->getHwSwitch()->getBootType() == BootType::WARM_BOOT) {
      // If we did a warm boot, do post warmboot actions now
      XLOG(INFO) << "setupPostWarmboot()";
      setupPostWarmboot();

      if (runVerification()) {
        XLOG(INFO) << "verifyPostWarmboot()";
        verifyPostWarmboot();
      }
    }
  }

  template <typename SETUP_FN, typename VERIFY_FN>
  void verifyAcrossWarmBoots(SETUP_FN setup, VERIFY_FN verify) {
    verifyAcrossWarmBoots(
        setup, verify, []() {}, []() {});
  }
  template <typename VERIFY_FN>
  void verifyAcrossWarmBoots(VERIFY_FN verify) {
    verifyAcrossWarmBoots([]() {}, verify, []() {}, []() {});
  }

 private:
  template <typename AddrT>
  void resolveNeighbor(
      PortDescriptor port,
      const AddrT& ip,
      VlanID vlan,
      folly::MacAddress mac);
  /*
   * Derived classes have the option to not run verify no
   * certain DUTs. E.g. non controlling nodes in Multinode setups
   */
  virtual bool runVerification() const {
    return true;
  }
  std::unique_ptr<std::thread> asyncInitThread_{nullptr};
};

void initAgentTest(int argc, char** argv, PlatformInitFn initPlatformFn);
} // namespace facebook::fboss
