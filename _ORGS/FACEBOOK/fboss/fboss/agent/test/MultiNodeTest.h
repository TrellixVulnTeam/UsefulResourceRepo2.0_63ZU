/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/Main.h"
#include "fboss/agent/test/AgentTest.h"

DECLARE_string(config);
DECLARE_int32(multiNodeTestPort1);
DECLARE_int32(multiNodeTestPort2);

namespace facebook::fboss {

class MultiNodeTest : public AgentTest {
 protected:
  void SetUp() override;
  void setupConfigFlag() override;

  std::unique_ptr<FbossCtrlAsyncClient> getRemoteThriftClient() const;

  void checkNeighborResolved(
      const folly::IPAddress& ip,
      VlanID vlan,
      PortDescriptor port) const;
  bool isDUT() const;
  std::vector<PortID> testPorts() const;
  std::vector<std::string> testPortNames() const;
  std::map<PortID, PortID> localToRemotePort() const;

 private:
  bool runVerification() const override {
    return isDUT();
  }
  virtual cfg::SwitchConfig initialConfig() const = 0;
};
int mulitNodeTestMain(int argc, char** argv, PlatformInitFn initPlatformFn);
} // namespace facebook::fboss
