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

#include <folly/IPAddress.h>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {
class SwitchState;

/*
 * Wrapper class to handle route updates and programming across both
 * stand alone RIB and legacy setups
 */
class RouteUpdateWrapper {
  struct AddDelRoutes {
    std::vector<UnicastRoute> toAdd;
    std::vector<IpPrefix> toDel;
  };

 public:
  using PrefixToInterfaceIDAndIP = boost::container::
      flat_map<folly::CIDRNetwork, std::pair<InterfaceID, folly::IPAddress>>;
  using RouterIDAndNetworkToInterfaceRoutes =
      boost::container::flat_map<RouterID, PrefixToInterfaceIDAndIP>;

  struct ConfigRoutes {
    RouterIDAndNetworkToInterfaceRoutes configRouterIDToInterfaceRoutes;
    std::vector<cfg::StaticRouteWithNextHops> staticRoutesWithNextHops;
    std::vector<cfg::StaticRouteNoNextHops> staticRoutesToNull;
    std::vector<cfg::StaticRouteNoNextHops> staticRoutesToCpu;
    std::vector<cfg::StaticIp2MplsRoute> staticIp2MplsRoutes;
  };
  using RouterIDAndClient = std::pair<RouterID, ClientID>;
  using SyncFibFor = std::unordered_set<RouterIDAndClient>;
  virtual ~RouteUpdateWrapper() = default;
  using UpdateStatistics = RoutingInformationBase::UpdateStatistics;
  void addRoute(
      RouterID id,
      const folly::IPAddress& network,
      uint8_t mask,
      ClientID clientId,
      RouteNextHopEntry entry);

  void addRoute(RouterID id, ClientID clientId, const UnicastRoute& route);
  void delRoute(
      RouterID id,
      const folly::IPAddress& network,
      uint8_t mask,
      ClientID clientId);

  void delRoute(RouterID id, const IpPrefix& pfx, ClientID clientId);
  void setRoutesToConfig(
      const RouterIDAndNetworkToInterfaceRoutes&
          _configRouterIDToInterfaceRoutes,
      const std::vector<cfg::StaticRouteWithNextHops>&
          _staticRoutesWithNextHops,
      const std::vector<cfg::StaticRouteNoNextHops>& _staticRoutesToNull,
      const std::vector<cfg::StaticRouteNoNextHops>& _staticRoutesToCpu,
      const std::vector<cfg::StaticIp2MplsRoute>& _staticIp2MplsRoutes);
  void program(const SyncFibFor& syncFibFor = {});
  void programMinAlpmState();
  void programClassID(
      RouterID rid,
      const std::vector<folly::CIDRNetwork>& prefixes,
      std::optional<cfg::AclLookupClass> classId,
      bool async);

 private:
  RoutingInformationBase* getRib() {
    return rib_;
  }
  void printStats(const UpdateStatistics& stats) const;
  void programStandAloneRib(const SyncFibFor& syncFibFor);
  virtual void updateStats(const UpdateStatistics& stats) = 0;
  virtual AdminDistance clientIdToAdminDistance(ClientID clientID) const = 0;

 protected:
  RouteUpdateWrapper(
      RoutingInformationBase* rib,
      std::optional<FibUpdateFunction> fibUpdateFn,
      void* fibUpdateCookie)
      : rib_(rib),
        fibUpdateFn_(fibUpdateFn),
        fibUpdateCookie_(fibUpdateCookie) {
    CHECK(rib_ && fibUpdateFn_ && fibUpdateCookie_);
  }

  RouteUpdateWrapper(RouteUpdateWrapper&&) = default;
  RouteUpdateWrapper& operator=(RouteUpdateWrapper&&) = default;
  std::unordered_map<std::pair<RouterID, ClientID>, AddDelRoutes>
      ribRoutesToAddDel_;
  RoutingInformationBase* rib_{nullptr};
  std::optional<FibUpdateFunction> fibUpdateFn_;
  void* fibUpdateCookie_{nullptr};
  std::unique_ptr<ConfigRoutes> configRoutes_{nullptr};
};
} // namespace facebook::fboss
