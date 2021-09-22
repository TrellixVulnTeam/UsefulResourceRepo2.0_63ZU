// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <folly/IPAddressV4.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>
#include <memory>
#include "fboss/cli/fboss2/CmdGlobalOptions.h"
#include "fboss/cli/fboss2/test/MockClients.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

#pragma once

namespace facebook::fboss {

class CmdHandlerTestBase : public ::testing::Test {
 public:
  void SetUp() override {
    mockedAgent_ = std::make_shared<MockFbossCtrlAgent>();
    localHost_ = std::make_unique<HostInfo>(
        "test.host", "test-oob.host", folly::IPAddressV6("::1"));
  }

  void setupMockedAgentServer() {
    mockedAgentServer_ =
        std::make_unique<apache::thrift::ScopedServerInterfaceThread>(
            mockedAgent_);
    // set global agent thrift port to the fake server created on localhost
    CmdGlobalOptions::getInstance()->setAgentThriftPort(
        mockedAgentServer_->getAddress().getPort());
  }

  void TearDown() override {
    // stop agent servers
    mockedAgentServer_.reset();
  }

  auto& getMockAgent() {
    return *mockedAgent_;
  }

  const auto& localhost() {
    return *localHost_;
  }

  std::unique_ptr<apache::thrift::ScopedServerInterfaceThread>
      mockedAgentServer_;

 private:
  std::shared_ptr<MockFbossCtrlAgent> mockedAgent_;

  std::unique_ptr<HostInfo> localHost_;
};
} // namespace facebook::fboss
