/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/LoadBalancerUtils.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/test/MultiNodeTest.h"
#include "fboss/agent/test/TestUtils.h"

#include "common/process/Process.h"
#include "fboss/agent/RouteUpdateWrapper.h"

using namespace facebook::fboss;

class MultiNodeLoadBalancerTest : public MultiNodeTest {
 public:
  void SetUp() override {
    MultiNodeTest::SetUp();
    if (isDUT()) {
      // TOOD - call verify reachability once configs are
      // fixed to generate the right intf IPs
      // verifyReachability();
    }
    auto addRoute = [this](auto ip, const auto& nhops) {
      auto updater = sw()->getRouteUpdater();
      updater.addRoute(
          RouterID(0),
          ip,
          0,
          ClientID::BGPD,
          RouteNextHopEntry(nhops, AdminDistance::EBGP));
      updater.program();
    };

    addRoute(folly::IPAddress("0.0.0.0"), makeNextHops(getV4Neighbors()));
    addRoute(folly::IPAddress("::"), makeNextHops(getV6Neighbors()));
  }
  std::map<PortID, std::vector<folly::IPAddress>> getPort2DesiredNeighbors()
      const {
    std::map<PortID, std::vector<folly::IPAddress>> port2Nbrs;
    for (auto nbrIp : getNeighbors()) {
      auto state = sw()->getState();
      auto intfToReach = state->getInterfaces()
                             ->getIntfAddrToReach(RouterID(0), nbrIp)
                             .intf->getID();
      for (auto portId : testPorts()) {
        auto vlan = state->getPort(portId)->getVlans().begin()->first;
        if (intfToReach ==
            state->getInterfaces()->getInterfaceInVlan(vlan)->getID()) {
          port2Nbrs[portId].push_back(nbrIp);
        }
      }
    }
    CHECK_EQ(port2Nbrs.size(), testPorts().size());
    return port2Nbrs;
  }

 private:
  std::vector<folly::IPAddress> getNeighbors() const {
    std::vector<folly::IPAddress> nbrs;
    auto makeIps = [&nbrs](const std::vector<std::string>& nbrStrs) {
      for (auto nbr : nbrStrs) {
        nbrs.push_back(folly::IPAddress(nbr));
      }
    };
    // TODO - figure these out from config
    if (isDUT()) {
      makeIps({"1::1", "2::1", "1.0.0.1", "2.0.0.1"});
    } else {
      makeIps({"1::0", "2::0", "1.0.0.0", "2.0.0.0"});
    }
    return nbrs;
  }
  template <typename AddrT>
  std::vector<AddrT> getNeighbors() const {
    std::vector<AddrT> addrs;
    for (auto& nbr : getNeighbors()) {
      if (nbr.version() == AddrT().version()) {
        addrs.push_back(AddrT(nbr.str()));
      }
    }
    return addrs;
  }
  std::vector<folly::IPAddressV6> getV6Neighbors() const {
    return getNeighbors<folly::IPAddressV6>();
  }
  std::vector<folly::IPAddressV4> getV4Neighbors() const {
    return getNeighbors<folly::IPAddressV4>();
  }
  void verifyReachability() const {
    for (auto dstIp : getNeighbors()) {
      std::string pingCmd = "ping -c 5 ";
      std::string resultStr;
      std::string errStr;
      EXPECT_TRUE(facebook::process::Process::execShellCmd(
          pingCmd + dstIp.str(), &resultStr, &errStr));
    }
  }
  cfg::SwitchConfig initialConfig() const override {
    auto config = utility::onePortPerVlanConfig(
        platform()->getHwSwitch(),
        testPorts(),
        cfg::PortLoopbackMode::NONE,
        true /*interfaceHasSubnet*/,
        false /*setInterfaceMac*/);
    config.loadBalancers_ref()->push_back(
        facebook::fboss::utility::getEcmpFullHashConfig(sw()->getPlatform()));
    return config;
  }
};

TEST_F(MultiNodeLoadBalancerTest, verifyFullHashLoadBalance) {
  auto verify = [this]() {
    auto state = sw()->getState();
    auto vlan = (*state->getVlans()->begin())->getID();
    auto localMac = state->getInterfaces()->getInterfaceInVlan(vlan)->getMac();
    for (auto isV6 : {true, false}) {
      facebook::fboss::utility::pumpTraffic(
          isV6, sw()->getHw(), localMac, vlan);
    }
    // TODO - verify LB
  };
  verifyAcrossWarmBoots(verify);
}
