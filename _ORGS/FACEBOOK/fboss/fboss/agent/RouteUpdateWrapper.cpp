/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/RouteUpdateWrapper.h"

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/rib/ForwardingInformationBaseUpdater.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/NodeBase-defs.h"

#include "fboss/agent/state/SwitchState.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

void RouteUpdateWrapper::addRoute(
    RouterID vrf,
    const folly::IPAddress& network,
    uint8_t mask,
    ClientID clientId,
    RouteNextHopEntry nhop) {
  UnicastRoute tempRoute;
  tempRoute.dest_ref()->ip_ref() = network::toBinaryAddress(network);
  tempRoute.dest_ref()->prefixLength_ref() = mask;
  if (nhop.getAction() == RouteForwardAction::NEXTHOPS) {
    tempRoute.nextHops_ref() = util::fromRouteNextHopSet(nhop.getNextHopSet());
    tempRoute.action_ref() = RouteForwardAction::NEXTHOPS;
  } else {
    tempRoute.action_ref() = nhop.getAction() == RouteForwardAction::DROP
        ? RouteForwardAction::DROP
        : RouteForwardAction::TO_CPU;
  }
  if (nhop.getCounterID().has_value()) {
    tempRoute.counterID_ref() = *nhop.getCounterID();
  }
  addRoute(vrf, clientId, std::move(tempRoute));
}

void RouteUpdateWrapper::addRoute(
    RouterID vrf,
    ClientID clientId,
    const UnicastRoute& route) {
  ribRoutesToAddDel_[std::make_pair(vrf, clientId)].toAdd.emplace_back(route);
}

void RouteUpdateWrapper::delRoute(
    RouterID vrf,
    const folly::IPAddress& network,
    uint8_t mask,
    ClientID clientId) {
  IpPrefix pfx;
  pfx.ip_ref() = network::toBinaryAddress(network);
  pfx.prefixLength_ref() = mask;
  delRoute(vrf, pfx, clientId);
}

void RouteUpdateWrapper::delRoute(
    RouterID vrf,
    const IpPrefix& pfx,
    ClientID clientId) {
  ribRoutesToAddDel_[std::make_pair(vrf, clientId)].toDel.emplace_back(
      std::move(pfx));
}

void RouteUpdateWrapper::program(const SyncFibFor& syncFibFor) {
  for (const auto& ridAndClient : syncFibFor) {
    if (ribRoutesToAddDel_.find(ridAndClient) == ribRoutesToAddDel_.end()) {
      ribRoutesToAddDel_[ridAndClient] = AddDelRoutes{};
    }
  }
  programStandAloneRib(syncFibFor);
  ribRoutesToAddDel_.clear();
  configRoutes_.reset();
}

void RouteUpdateWrapper::programMinAlpmState() {
  getRib()->ensureVrf(RouterID(0));
  addRoute(
      RouterID(0),
      folly::IPAddressV4("0.0.0.0"),
      0,
      ClientID::STATIC_INTERNAL,
      RouteNextHopEntry(
          RouteForwardAction::DROP, AdminDistance::MAX_ADMIN_DISTANCE));
  addRoute(
      RouterID(0),
      folly::IPAddressV6("::"),
      0,
      ClientID::STATIC_INTERNAL,
      RouteNextHopEntry(
          RouteForwardAction::DROP, AdminDistance::MAX_ADMIN_DISTANCE));
  program();
}

void RouteUpdateWrapper::printStats(const UpdateStatistics& stats) const {
  XLOG(DBG0) << " Routes added: " << stats.v4RoutesAdded + stats.v6RoutesAdded
             << " Routes deleted: "
             << stats.v4RoutesDeleted + stats.v6RoutesDeleted << " Duration "
             << stats.duration.count() << " us ";
}

void RouteUpdateWrapper::programStandAloneRib(const SyncFibFor& syncFibFor) {
  if (configRoutes_) {
    getRib()->reconfigure(
        configRoutes_->configRouterIDToInterfaceRoutes,
        configRoutes_->staticRoutesWithNextHops,
        configRoutes_->staticRoutesToNull,
        configRoutes_->staticRoutesToCpu,
        configRoutes_->staticIp2MplsRoutes,
        *fibUpdateFn_,
        fibUpdateCookie_);
  }
  for (auto [ridClientId, addDelRoutes] : ribRoutesToAddDel_) {
    auto stats = getRib()->update(
        ridClientId.first,
        ridClientId.second,
        clientIdToAdminDistance(ridClientId.second),
        addDelRoutes.toAdd,
        addDelRoutes.toDel,
        syncFibFor.find(ridClientId) != syncFibFor.end(),
        "RIB update",
        *fibUpdateFn_,
        fibUpdateCookie_);
    printStats(stats);
    updateStats(stats);
  }
}

void RouteUpdateWrapper::programClassID(
    RouterID rid,
    const std::vector<folly::CIDRNetwork>& prefixes,
    std::optional<cfg::AclLookupClass> classId,
    bool async) {
  if (async) {
    getRib()->setClassIDAsync(
        rid, prefixes, *fibUpdateFn_, classId, fibUpdateCookie_);
  } else {
    getRib()->setClassID(
        rid, prefixes, *fibUpdateFn_, classId, fibUpdateCookie_);
  }
}

void RouteUpdateWrapper::setRoutesToConfig(
    const RouterIDAndNetworkToInterfaceRoutes& _configRouterIDToInterfaceRoutes,
    const std::vector<cfg::StaticRouteWithNextHops>& _staticRoutesWithNextHops,
    const std::vector<cfg::StaticRouteNoNextHops>& _staticRoutesToNull,
    const std::vector<cfg::StaticRouteNoNextHops>& _staticRoutesToCpu,
    const std::vector<cfg::StaticIp2MplsRoute>& _staticIp2MplsRoutes) {
  configRoutes_ = std::make_unique<ConfigRoutes>(ConfigRoutes{
      _configRouterIDToInterfaceRoutes,
      _staticRoutesWithNextHops,
      _staticRoutesToNull,
      _staticRoutesToCpu,
      _staticIp2MplsRoutes});
}
} // namespace facebook::fboss
