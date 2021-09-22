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

#include <boost/container/flat_set.hpp>

#include <folly/dynamic.h>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/RouteTypes.h"

DECLARE_uint32(ecmp_width);
DECLARE_bool(optimized_ucmp);

namespace facebook::fboss {

class RouteNextHopEntry {
 public:
  using Action = RouteForwardAction;
  using NextHopSet = boost::container::flat_set<NextHop>;

  RouteNextHopEntry(
      Action action,
      AdminDistance distance,
      std::optional<RouteCounterID> counterID = std::nullopt)
      : adminDistance_(distance), action_(action), counterID_(counterID) {
    CHECK_NE(action_, Action::NEXTHOPS);
  }

  RouteNextHopEntry(
      NextHopSet nhopSet,
      AdminDistance distance,
      std::optional<RouteCounterID> counterID = std::nullopt);

  RouteNextHopEntry(
      NextHop nhop,
      AdminDistance distance,
      std::optional<RouteCounterID> counterID = std::nullopt)
      : adminDistance_(distance),
        action_(Action::NEXTHOPS),
        counterID_(counterID) {
    nhopSet_.emplace(std::move(nhop));
  }

  AdminDistance getAdminDistance() const {
    return adminDistance_;
  }

  Action getAction() const {
    return action_;
  }

  const NextHopSet& getNextHopSet() const {
    return nhopSet_;
  }

  const std::optional<RouteCounterID> getCounterID() const {
    return counterID_;
  }

  NextHopSet normalizedNextHops() const;

  // Get the sum of the weights of all the nexthops in the entry
  NextHopWeight getTotalWeight() const;

  std::string str() const;

  /*
   * Serialize to folly::dynamic
   */
  folly::dynamic toFollyDynamic() const;

  /*
   * Deserialize from folly::dynamic
   */
  static RouteNextHopEntry fromFollyDynamic(const folly::dynamic& entryJson);

  // Methods to manipulate this object
  bool isDrop() const {
    return action_ == Action::DROP;
  }
  bool isToCPU() const {
    return action_ == Action::TO_CPU;
  }
  bool isSame(const RouteNextHopEntry& entry) const {
    return entry.getAdminDistance() == getAdminDistance();
  }

  // Reset the NextHopSet
  void reset() {
    nhopSet_.clear();
    action_ = Action::DROP;
    counterID_ = std::nullopt;
  }

  bool isValid(bool forMplsRoute = false) const;

  static RouteNextHopEntry from(
      const facebook::fboss::UnicastRoute& route,
      AdminDistance defaultAdminDistance,
      std::optional<RouteCounterID> counterID);
  static facebook::fboss::RouteNextHopEntry createDrop(
      AdminDistance adminDistance = AdminDistance::STATIC_ROUTE);
  static facebook::fboss::RouteNextHopEntry createToCpu(
      AdminDistance adminDistance = AdminDistance::STATIC_ROUTE);
  static facebook::fboss::RouteNextHopEntry fromStaticRoute(
      const cfg::StaticRouteWithNextHops& route);
  static facebook::fboss::RouteNextHopEntry fromStaticIp2MplsRoute(
      const cfg::StaticIp2MplsRoute& route);
  static bool isUcmp(const NextHopSet& nhopSet);

 private:
  void normalize(
      std::vector<NextHopWeight>& scaledWeights,
      NextHopWeight totalWeight) const;
  AdminDistance adminDistance_;
  Action action_{Action::DROP};
  std::optional<RouteCounterID> counterID_;
  NextHopSet nhopSet_;
};

/**
 * Comparision operators
 */
bool operator==(const RouteNextHopEntry& a, const RouteNextHopEntry& b);
bool operator<(const RouteNextHopEntry& a, const RouteNextHopEntry& b);

void toAppend(const RouteNextHopEntry& entry, std::string* result);
std::ostream& operator<<(std::ostream& os, const RouteNextHopEntry& entry);

void toAppend(const RouteNextHopEntry::NextHopSet& nhops, std::string* result);
std::ostream& operator<<(
    std::ostream& os,
    const RouteNextHopEntry::NextHopSet& nhops);
NextHopWeight totalWeight(const RouteNextHopEntry::NextHopSet& nhops);

using RouteNextHopSet = RouteNextHopEntry::NextHopSet;

namespace util {

/**
 * Convert thrift representation of nexthops to RouteNextHops.
 */
RouteNextHopSet toRouteNextHopSet(std::vector<NextHopThrift> const& nhts);

/**
 * Convert RouteNextHops to thrift representaion of nexthops
 */
std::vector<NextHopThrift> fromRouteNextHopSet(RouteNextHopSet const& nhs);

UnicastRoute toUnicastRoute(
    const folly::CIDRNetwork& nw,
    const RouteNextHopEntry& nhopEntry);
} // namespace util

} // namespace facebook::fboss
