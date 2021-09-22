/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwLinkStateDependentTest.h"

#include "fboss/agent/hw/test/HwTestProdConfigUtils.h"

#include "fboss/agent/hw/test/dataplane_tests/HwEcmpDataPlaneTestUtil.h"

namespace facebook::fboss {

/*
 * Defines an enum to pass test options to functions - specifically, to check
 * which features a given switch has enabled.
 * For a function that takes such options, define an argument of type
 * HwInvariantBitmask which can then be bit-masked with different HwInvariantOpt
 * values.
 */
typedef uint32_t HwInvariantBitmask;
enum HwInvariantOpt : HwInvariantBitmask {
  EMPTY_INVARIANT = 0x0000,
  MMU_LOSSLESS_INVARIANT = 0x0001,
  MHNIC_INVARIANT = 0x0002,
  SFLOW_INVARIANT = 0x0004,
  COPP_INVARIANT = 0x0008,
  LOAD_BALANCER_INVARIANT = 0x0010,
  OLYMPIC_QOS_INVARIANT = 0x0020,
  DIAG_CMDS_INVARIANT = 0x0040,
};
// Useful/common combinations can be defined here as one sees fit
const HwInvariantBitmask DEFAULT_INVARIANTS = DIAG_CMDS_INVARIANT |
    COPP_INVARIANT | LOAD_BALANCER_INVARIANT | OLYMPIC_QOS_INVARIANT;

/*
 * Convenience function that checks if field `opt` is set within `opts`.
 * Useful to avoid human error in checking bitmasks.
 */
inline bool isHwInvariantOptSet(HwInvariantBitmask opts, HwInvariantOpt opt) {
  return ((opts & opt) == opt);
}

class HwProdInvariantHelper {
 public:
  HwProdInvariantHelper(
      HwSwitchEnsemble* ensemble,
      const cfg::SwitchConfig& initialCfg)
      : ensemble_(ensemble), initialCfg_(initialCfg) {}

  void setupEcmp();
  void setupEcmpWithNextHopMac(const folly::MacAddress& nextHop);
  void disableTtl();
  void verifyInvariants(HwInvariantBitmask options) {
    // No point if we're gonna skip all the tests, should receive at least one
    // field in 'options'.
    if (options == EMPTY_INVARIANT) {
      throw FbossError(
          "no options passed to verifyInvariants, got EMPTY_INVARIANT");
    }
    if (isHwInvariantOptSet(options, DIAG_CMDS_INVARIANT)) {
      verifySafeDiagCmds();
    }
    if (isHwInvariantOptSet(options, COPP_INVARIANT)) {
      verifyCopp();
    }
    if (isHwInvariantOptSet(options, LOAD_BALANCER_INVARIANT)) {
      verifyLoadBalacing();
    }
    if (isHwInvariantOptSet(options, OLYMPIC_QOS_INVARIANT)) {
      verifyDscpToQueueMapping();
    }
    if (isHwInvariantOptSet(options, MHNIC_INVARIANT)) {
      verifyQueuePerHostMapping();
    }
  }
  void sendTraffic();
  static HwSwitchEnsemble::Features featuresDesired() {
    return {
        HwSwitchEnsemble::LINKSCAN,
        HwSwitchEnsemble::PACKET_RX,
        HwSwitchEnsemble::STATS_COLLECTION};
  }
  void verifyNoDiscards();

 private:
  cfg::SwitchConfig initialConfig() const {
    return initialCfg_;
  }
  std::shared_ptr<SwitchState> getProgrammedState() const;
  std::vector<std::string> getBcmCommandsToTest() const;
  void verifySafeDiagCmds();
  void verifyDscpToQueueMapping();
  void verifyCopp();
  void verifyLoadBalacing();
  void sendAndVerifyPkts(uint16_t destPort, uint8_t queueId);
  void verifyQueuePerHostMapping();

  std::unique_ptr<utility::HwIpV6EcmpDataPlaneTestUtil> ecmpHelper_;
  HwSwitchEnsemble* ensemble_;
  cfg::SwitchConfig initialCfg_;
};

} // namespace facebook::fboss
