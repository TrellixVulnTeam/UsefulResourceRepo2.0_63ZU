/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/String.h>
#include <gflags/gflags.h>

#include "common/network/NetworkUtil.h"
#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/platforms/wedge/WedgePlatformInit.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/MultiNodeTest.h"
#include "fboss/lib/CommonUtils.h"

using namespace facebook::fboss;
using namespace apache::thrift;

DEFINE_string(
    multiNodeTestRemoteSwitchName,
    "",
    "multinode test remote switch name");
DECLARE_bool(run_forever);
DECLARE_bool(nodeZ);

DEFINE_int32(multiNodeTestPort1, 0, "multinode test port 1");
DEFINE_int32(multiNodeTestPort2, 0, "multinode test port 2");

namespace facebook::fboss {

// Construct a config file by combining the hw config passed
// and sw configs from test utility and set config flag to
// point to the test config
void MultiNodeTest::setupConfigFlag() {
  cfg::AgentConfig testConfig;
  utility::setPortToDefaultProfileIDMap(
      std::make_shared<PortMap>(), platform());
  *testConfig.sw_ref() = initialConfig();
  const auto& baseConfig = platform()->config();
  *testConfig.platform_ref() = *baseConfig->thrift.platform_ref();
  auto newcfg = AgentConfig(
      testConfig,
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(testConfig));
  auto testConfigDir = platform()->getPersistentStateDir() + "/multinode_test/";
  auto newCfgFile = testConfigDir + "/agent_multinode_test.conf";
  utilCreateDir(testConfigDir);
  newcfg.dumpConfig(newCfgFile);
  FLAGS_config = newCfgFile;
  // reload config so that test config is loaded
  platform()->reloadConfig();
}

void MultiNodeTest::SetUp() {
  AgentTest::SetUp();

  if (isDUT()) {
    // Tune this if certain tests want to WB while keeping some ports down
    waitForLinkStatus(
        {PortID(FLAGS_multiNodeTestPort1), PortID(FLAGS_multiNodeTestPort2)},
        true /*up*/);
  }

  XLOG(DBG0) << "Multinode setup ready";
}

void MultiNodeTest::checkNeighborResolved(
    const folly::IPAddress& ip,
    VlanID vlanId,
    PortDescriptor port) const {
  auto checkNeighbor = [&](auto neighborEntry) {
    EXPECT_NE(neighborEntry, nullptr);
    EXPECT_EQ(neighborEntry->isPending(), false);
    EXPECT_EQ(neighborEntry->getPort(), port);
  };
  if (ip.isV4()) {
    checkNeighbor(sw()->getState()
                      ->getVlans()
                      ->getVlanIf(vlanId)
                      ->getArpTable()
                      ->getEntryIf(ip.asV4()));
  } else {
    checkNeighbor(sw()->getState()
                      ->getVlans()
                      ->getVlanIf(vlanId)
                      ->getNdpTable()
                      ->getEntryIf(ip.asV6()));
  }
}

std::unique_ptr<FbossCtrlAsyncClient> MultiNodeTest::getRemoteThriftClient()
    const {
  folly::EventBase* eb = folly::EventBaseManager::get()->getEventBase();
  auto remoteSwitchIp = facebook::network::NetworkUtil::getHostByName(
      FLAGS_multiNodeTestRemoteSwitchName);
  folly::SocketAddress agent(remoteSwitchIp, 5909);
  auto socket = folly::AsyncSocket::newSocket(eb, agent);
  auto chan = HeaderClientChannel::newChannel(std::move(socket));
  return std::make_unique<FbossCtrlAsyncClient>(std::move(chan));
}

bool MultiNodeTest::isDUT() const {
  return !FLAGS_nodeZ;
}

std::vector<PortID> MultiNodeTest::testPorts() const {
  return {PortID(FLAGS_multiNodeTestPort1), PortID(FLAGS_multiNodeTestPort2)};
}

std::vector<std::string> MultiNodeTest::testPortNames() const {
  return getPortNames(testPorts());
}

std::map<PortID, PortID> MultiNodeTest::localToRemotePort() const {
  std::map<PortID, PortID> localToRemote;
  auto getNbrs = [&localToRemote, this]() {
    try {
      auto localPorts = testPorts();
      std::sort(localPorts.begin(), localPorts.end());
      std::vector<LinkNeighborThrift> remoteLldpNeighbors;
      getRemoteThriftClient()->sync_getLldpNeighbors(remoteLldpNeighbors);
      for (const auto& nbr : remoteLldpNeighbors) {
        auto localPort = sw()->getState()
                             ->getPorts()
                             ->getPort(*nbr.printablePortId_ref())
                             ->getID();
        if (std::find(localPorts.begin(), localPorts.end(), localPort) !=
            localPorts.end()) {
          localToRemote[localPort] = *nbr.localPort_ref();
        }
      }
      return localToRemote.size() == testPorts().size();
    } catch (const std::exception& ex) {
      XLOG(INFO) << "Failed to get nbrs: " << ex.what();
      return false;
    }
  };

  checkWithRetry(getNbrs);
  return localToRemote;
}

int mulitNodeTestMain(int argc, char** argv, PlatformInitFn initPlatformFn) {
  ::testing::InitGoogleTest(&argc, argv);
  initAgentTest(argc, argv, initPlatformFn);
  return RUN_ALL_TESTS();
}

} // namespace facebook::fboss
