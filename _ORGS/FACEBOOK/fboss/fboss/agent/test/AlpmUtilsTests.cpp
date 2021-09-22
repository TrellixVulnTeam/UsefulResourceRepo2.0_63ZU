/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>

#include "fboss/agent/AlpmUtils.h"
#include "fboss/agent/GtestDefs.h"
#include "fboss/agent/state/Route.h"

#include "fboss/agent/state/RouteTypes.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/types.h"

using folly::IPAddressV4;
using folly::IPAddressV6;

namespace facebook::fboss {

namespace {
template <typename AddrType>
std::shared_ptr<ForwardingInformationBase<AddrType>> getStandAloneFib(
    const std::shared_ptr<SwitchState>& state) {
  auto fibContainer = state->getFibs()->getFibContainer(RouterID(0));
  return fibContainer->template getFib<AddrType>();
}

template <typename AddrType>
std::shared_ptr<Route<AddrType>> getDefaultRouteStandAloneRib(
    const std::shared_ptr<SwitchState>& state) {
  auto fib = getStandAloneFib<AddrType>(state);
  return fib->getRouteIf(RoutePrefix<AddrType>{AddrType(), 0});
}

template <typename AddrType>
std::shared_ptr<Route<AddrType>> getDefaultRoute(
    const std::shared_ptr<SwitchState>& state) {
  return getDefaultRouteStandAloneRib<AddrType>(state);
}

template <typename AddrType>
size_t getFibSize(const std::shared_ptr<SwitchState>& state) {
  return getStandAloneFib<AddrType>(state)->size();
}
} // namespace

TEST(AlpmUtilsTest, DefaultNullRoutesAddedOnEmptyState) {
  auto emptyState = std::make_shared<SwitchState>();
  auto alpmInitState = setupMinAlpmRouteState(emptyState);
  EXPECT_NE(nullptr, alpmInitState);
  auto v4RibSize = getFibSize<folly::IPAddressV4>(alpmInitState);
  EXPECT_EQ(1, v4RibSize);
  auto v6RibSize = getFibSize<folly::IPAddressV6>(alpmInitState);
  EXPECT_EQ(1, v6RibSize);

  auto v4Default = getDefaultRoute<IPAddressV4>(alpmInitState);
  auto v6Default = getDefaultRoute<IPAddressV6>(alpmInitState);
  auto assertNullRoute = [=](const auto& route) {
    EXPECT_NE(nullptr, route);
    EXPECT_TRUE(route->prefix().network.isZero());
    EXPECT_EQ(0, route->prefix().mask);
    EXPECT_TRUE(route->isDrop());
    const auto& fwd = route->getForwardInfo();
    EXPECT_EQ(AdminDistance::MAX_ADMIN_DISTANCE, fwd.getAdminDistance());
    EXPECT_EQ(0, fwd.getNextHopSet().size());
  };
  assertNullRoute(v4Default);
  assertNullRoute(v6Default);
}

TEST(AlpmUtilsTest, DefaultNullRoutesNotReAdded) {
  auto emptyState = std::make_shared<SwitchState>();
  auto alpmInitState = setupMinAlpmRouteState(emptyState);
  auto alpmAgainState = setupMinAlpmRouteState(alpmInitState);

  auto v4RibSize = getFibSize<folly::IPAddressV4>(alpmAgainState);
  EXPECT_EQ(1, v4RibSize);
  auto v6RibSize = getFibSize<folly::IPAddressV6>(alpmAgainState);
  EXPECT_EQ(1, v6RibSize);
}

TEST(AlpmUtilsTest, minAlpmRouteCount) {
  EXPECT_EQ(2, numMinAlpmRoutes());
}

TEST(AlpmUtilsTest, minAlpmV4RouteCount) {
  EXPECT_EQ(1, numMinAlpmV4Routes());
}

TEST(AlpmUtilsTest, minAlpmV6RouteCount) {
  EXPECT_EQ(1, numMinAlpmV6Routes());
}

} // namespace facebook::fboss
