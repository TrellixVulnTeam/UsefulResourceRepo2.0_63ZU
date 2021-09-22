/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "RouteNextHopEntry.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/NexthopUtils.h"
#include "fboss/agent/state/RouteNextHop.h"

#include <folly/logging/xlog.h>
#include <gflags/gflags.h>
#include <numeric>
#include "folly/IPAddress.h"

namespace {
constexpr auto kNexthops = "nexthops";
constexpr auto kAction = "action";
constexpr auto kAdminDistance = "adminDistance";
constexpr auto kCounterID = "counterID";
static constexpr int kMinSizeForWideEcmp{128};

std::vector<facebook::fboss::NextHopThrift> thriftNextHopsFromAddresses(
    const std::vector<facebook::network::thrift::BinaryAddress>& addrs) {
  std::vector<facebook::fboss::NextHopThrift> nhs;
  nhs.reserve(addrs.size());
  for (const auto& addr : addrs) {
    facebook::fboss::NextHopThrift nh;
    *nh.address_ref() = addr;
    *nh.weight_ref() = 0;
    nhs.emplace_back(std::move(nh));
  }
  return nhs;
}
} // namespace

DEFINE_bool(wide_ecmp, false, "Enable fixed width wide ECMP feature");
DEFINE_bool(optimized_ucmp, false, "Enable UCMP normalization optimizations");
DEFINE_double(ucmp_max_error, 0.05, "Max UCMP normalization error");

namespace facebook::fboss {

namespace util {

RouteNextHopSet toRouteNextHopSet(std::vector<NextHopThrift> const& nhs) {
  RouteNextHopSet rnhs;
  rnhs.reserve(nhs.size());
  for (auto const& nh : nhs) {
    rnhs.emplace(fromThrift(nh));
  }
  return rnhs;
}

std::vector<NextHopThrift> fromRouteNextHopSet(RouteNextHopSet const& nhs) {
  std::vector<NextHopThrift> nhts;
  nhts.reserve(nhs.size());
  for (const auto& nh : nhs) {
    nhts.emplace_back(nh.toThrift());
  }
  return nhts;
}

UnicastRoute toUnicastRoute(
    const folly::CIDRNetwork& nw,
    const RouteNextHopEntry& nhopEntry) {
  UnicastRoute thriftRoute;
  thriftRoute.dest_ref()->ip_ref() = network::toBinaryAddress(nw.first);
  thriftRoute.dest_ref()->prefixLength_ref() = nw.second;
  thriftRoute.action_ref() = nhopEntry.getAction();
  if (nhopEntry.getCounterID().has_value()) {
    thriftRoute.counterID_ref() = nhopEntry.getCounterID().value();
  }
  if (nhopEntry.getAction() == RouteForwardAction::NEXTHOPS) {
    thriftRoute.nextHops_ref() = fromRouteNextHopSet(nhopEntry.getNextHopSet());
  }
  return thriftRoute;
}

} // namespace util

RouteNextHopEntry::RouteNextHopEntry(
    NextHopSet nhopSet,
    AdminDistance distance,
    std::optional<RouteCounterID> counterID)
    : adminDistance_(distance),
      action_(Action::NEXTHOPS),
      counterID_(counterID),
      nhopSet_(std::move(nhopSet)) {
  if (nhopSet_.size() == 0) {
    throw FbossError("Empty nexthop set is passed to the RouteNextHopEntry");
  }
}

NextHopWeight RouteNextHopEntry::getTotalWeight() const {
  return totalWeight(getNextHopSet());
}

std::string RouteNextHopEntry::str() const {
  std::string result;
  switch (action_) {
    case Action::DROP:
      result = "DROP";
      break;
    case Action::TO_CPU:
      result = "To_CPU";
      break;
    case Action::NEXTHOPS:
      toAppend(getNextHopSet(), &result);
      break;
    default:
      CHECK(0);
      break;
  }
  result +=
      folly::to<std::string>(";admin=", static_cast<int32_t>(adminDistance_));
  result += folly::to<std::string>(
      ";counterID=", counterID_.has_value() ? *counterID_ : "none");
  return result;
}

bool operator==(const RouteNextHopEntry& a, const RouteNextHopEntry& b) {
  return (
      a.getAction() == b.getAction() and
      a.getNextHopSet() == b.getNextHopSet() and
      a.getAdminDistance() == b.getAdminDistance() and
      a.getCounterID() == b.getCounterID());
}

bool operator<(const RouteNextHopEntry& a, const RouteNextHopEntry& b) {
  if (a.getAdminDistance() != b.getAdminDistance()) {
    return a.getAdminDistance() < b.getAdminDistance();
  }
  return (
      (a.getAction() == b.getAction()) ? (a.getNextHopSet() < b.getNextHopSet())
                                       : a.getAction() < b.getAction());
}

// Methods for RouteNextHopEntry
void toAppend(const RouteNextHopEntry& entry, std::string* result) {
  result->append(entry.str());
}
std::ostream& operator<<(std::ostream& os, const RouteNextHopEntry& entry) {
  return os << entry.str();
}

// Methods for NextHopSet
void toAppend(const RouteNextHopEntry::NextHopSet& nhops, std::string* result) {
  for (const auto& nhop : nhops) {
    result->append(folly::to<std::string>(nhop.str(), " "));
  }
}

std::ostream& operator<<(
    std::ostream& os,
    const RouteNextHopEntry::NextHopSet& nhops) {
  for (const auto& nhop : nhops) {
    os << nhop.str() << " ";
  }
  return os;
}

NextHopWeight totalWeight(const RouteNextHopEntry::NextHopSet& nhops) {
  uint32_t result = 0;
  for (const auto& nh : nhops) {
    result += nh.weight();
  }
  return result;
}

folly::dynamic RouteNextHopEntry::toFollyDynamic() const {
  folly::dynamic entry = folly::dynamic::object;
  entry[kAction] = forwardActionStr(action_);
  folly::dynamic nhops = folly::dynamic::array;
  for (const auto& nhop : nhopSet_) {
    nhops.push_back(nhop.toFollyDynamic());
  }
  entry[kNexthops] = std::move(nhops);
  entry[kAdminDistance] = static_cast<int32_t>(adminDistance_);
  if (counterID_.has_value()) {
    entry[kCounterID] = counterID_.value();
  }
  return entry;
}

RouteNextHopEntry RouteNextHopEntry::fromFollyDynamic(
    const folly::dynamic& entryJson) {
  Action action = str2ForwardAction(entryJson[kAction].asString());
  auto it = entryJson.find(kAdminDistance);
  AdminDistance adminDistance = (it == entryJson.items().end())
      ? AdminDistance::MAX_ADMIN_DISTANCE
      : AdminDistance(entryJson[kAdminDistance].asInt());
  RouteNextHopEntry entry(Action::DROP, adminDistance);
  entry.action_ = action;
  for (const auto& nhop : entryJson[kNexthops]) {
    entry.nhopSet_.insert(util::nextHopFromFollyDynamic(nhop));
  }
  if (entryJson.find(kCounterID) != entryJson.items().end()) {
    entry.counterID_ = RouteCounterID(entryJson[kCounterID].asString());
  }
  return entry;
}

bool RouteNextHopEntry::isValid(bool forMplsRoute) const {
  bool valid = true;
  if (!forMplsRoute) {
    /* for ip2mpls routes, next hop label forwarding action must be push */
    for (const auto& nexthop : nhopSet_) {
      if (action_ != Action::NEXTHOPS) {
        continue;
      }
      if (nexthop.labelForwardingAction() &&
          nexthop.labelForwardingAction()->type() !=
              LabelForwardingAction::LabelForwardingType::PUSH) {
        return !valid;
      }
    }
  }
  return valid;
}

// A greedy algorithm to determine UCMP weight distribution when total
// weight is greater than ecmp_width.
// Input - [w(1), w(2), .., w(n)] represents weights of each path
// Output - [w'(1), w'(2), .., w'(n)]
// Constraints
// w'(i) is an Integer
// Sum[w'(i)] <= ecmp_width // Fits within ECMP Width, 64 or 128
// Minimize - Max(ErrorDeviation(i)) for i = 1 to n
// where ErrorDeviation(i) is percentage deviation of weight for index i
// from optimal allocation (without constraints)

//  a) Compute gcd and reduce weights by gcd
//  b) Calculate the scaled factor FLAGS_ecmp_width/totalWeight.
//     Without rounding, multiplying each weight by this will still yield
//     correct weight ratios between the next hops.
//  c) Scale each next hop by the scaling factor, rounding up.
//  d) We might have exceeded ecmp_width at this point due to rounding up.
//     while total weight > ecmp_width
//           1) Find entry with maximum deviation from ideal distribution
//           2) reduce the weight of max error entry
//  e) we have a potential solution. Continue searching for an optimal solution.
//     while total weight is not zero or tolernce not achieved
//           1) Find entry with maximum deviation from ideal distribution
//           2) reduce the weight of max error entry
//           3) If newly created set has lower max deviation, record this
//              as new optimal solution.

void RouteNextHopEntry::normalize(
    std::vector<NextHopWeight>& scaledWeights,
    NextHopWeight totalWeight) const {
  // This is the weight distribution without constraints
  std::vector<double> idealWeights;

  // compute normalization factor
  double factor = FLAGS_ecmp_width / static_cast<double>(totalWeight);
  NextHopWeight scaledTotalWeight = 0;
  for (auto& entry : scaledWeights) {
    // Compute the ideal distribution
    idealWeights.emplace_back(entry / static_cast<double>(totalWeight));
    // compute scaled weights by rounding up
    entry = static_cast<NextHopWeight>(std::ceil(entry * factor));
    scaledTotalWeight += entry;
  }

  // find entry with max error
  auto findMaxErrorEntry = [&idealWeights,
                            &scaledTotalWeight](const auto& scaledWeights) {
    double maxError = 0;
    int maxErrorIndex = 0;
    auto index = 0;
    for (auto entry : scaledWeights) {
      // percentage weight of total weight allocation for this member
      auto allocation = (double)entry / scaledTotalWeight;
      // measure of percentage weight deviation from ideal
      auto error = (allocation - idealWeights[index]) / idealWeights[index];
      // record the max error entry and it's key
      if (error >= maxError) {
        maxErrorIndex = index;
        maxError = error;
      }
      index++;
    }
    return std::tuple(maxErrorIndex, maxError);
  };

  // current solution is not feasible till it can fit in ecmp_width value
  while (scaledTotalWeight > FLAGS_ecmp_width) {
    auto key = std::get<0>(findMaxErrorEntry(scaledWeights));
    scaledWeights.at(key)--;
    scaledTotalWeight--;
  }

  // we have a potential solution now
  auto [maxErrorIndex, maxErrorVal] = findMaxErrorEntry(scaledWeights);
  auto bestMaxError = maxErrorVal;

  // Do not optimize further for wide ecmp
  bool wideEcmp = FLAGS_wide_ecmp && scaledTotalWeight > kMinSizeForWideEcmp;
  if (!wideEcmp) {
    // map to continue searching for best result
    auto newWeights = scaledWeights;

    while (scaledTotalWeight > 1 && bestMaxError > FLAGS_ucmp_max_error) {
      // reduce weight for entry with max error deviation
      newWeights.at(maxErrorIndex)--;
      scaledTotalWeight--;
      // recompute the new error deviation
      std::tie(maxErrorIndex, maxErrorVal) = findMaxErrorEntry(newWeights);
      // save new solution if it reduces max eror deviation
      if (maxErrorVal <= bestMaxError) {
        scaledWeights = newWeights;
        bestMaxError = maxErrorVal;
      }
    };
  }

  int commonFactor{0};
  // reduce weights proportionaly by gcd if needed
  for (const auto entry : scaledWeights) {
    if (entry) {
      commonFactor = std::gcd(commonFactor, entry);
    }
  }
  commonFactor = std::max(commonFactor, 1);

  // reduce weights by gcd if needed
  for (auto& entry : scaledWeights) {
    entry = entry / commonFactor;
  }
}

RouteNextHopEntry::NextHopSet RouteNextHopEntry::normalizedNextHops() const {
  NextHopSet normalizedNextHops;
  // 1)
  for (const auto& nhop : getNextHopSet()) {
    normalizedNextHops.insert(ResolvedNextHop(
        nhop.addr(),
        nhop.intf(),
        std::max(nhop.weight(), NextHopWeight(1)),
        nhop.labelForwardingAction()));
  }
  // 2)
  // Calculate the totalWeight. If that exceeds the max ecmp width, we use the
  // following heuristic algorithm:
  // 2a) Calculate the scaled factor FLAGS_ecmp_width/totalWeight.
  //     Without rounding, multiplying each weight by this will still yield
  //     correct weight ratios between the next hops.
  // 2b) Scale each next hop by the scaling factor, rounding down by default
  //     except for when weights go below 1. In that case, add them in as
  //     weight 1. At this point, we might _still_ be above FLAGS_ecmp_width,
  //     because we could have rounded too many 0s up to 1.
  // 2c) Do a final pass where we make up any remaining excess weight above
  //     FLAGS_ecmp_width by iteratively decrementing the max weight. If there
  //     are more than FLAGS_ecmp_width next hops, this cannot possibly succeed.
  NextHopWeight totalWeight = std::accumulate(
      normalizedNextHops.begin(),
      normalizedNextHops.end(),
      0,
      [](NextHopWeight w, const NextHop& nh) { return w + nh.weight(); });
  // Total weight after applying the scaling factor
  // FLAGS_ecmp_width/totalWeight to all next hops.
  NextHopWeight scaledTotalWeight = 0;
  if (totalWeight > FLAGS_ecmp_width) {
    XLOG(DBG3) << "Total weight of next hops exceeds max ecmp width: "
               << totalWeight << " > " << FLAGS_ecmp_width << " ("
               << normalizedNextHops << ")";

    NextHopSet scaledNextHops;
    if (FLAGS_optimized_ucmp) {
      std::vector<NextHopWeight> scaledWeights;
      for (const auto& nhop : normalizedNextHops) {
        scaledWeights.emplace_back(nhop.weight());
      }
      normalize(scaledWeights, totalWeight);

      auto index = 0;
      for (const auto& nhop : normalizedNextHops) {
        auto weight = scaledWeights.at(index);
        if (weight) {
          scaledNextHops.insert(ResolvedNextHop(
              nhop.addr(), nhop.intf(), weight, nhop.labelForwardingAction()));
          scaledTotalWeight += weight;
        }
        index++;
      }
    } else {
      // 2a)
      double factor = FLAGS_ecmp_width / static_cast<double>(totalWeight);
      // 2b)
      for (const auto& nhop : normalizedNextHops) {
        NextHopWeight w = std::max(
            static_cast<NextHopWeight>(nhop.weight() * factor),
            NextHopWeight(1));
        scaledNextHops.insert(ResolvedNextHop(
            nhop.addr(), nhop.intf(), w, nhop.labelForwardingAction()));
        scaledTotalWeight += w;
      }
      // 2c)
      if (scaledTotalWeight > FLAGS_ecmp_width) {
        XLOG(DBG3) << "Total weight of scaled next hops STILL exceeds max "
                   << "ecmp width: " << scaledTotalWeight << " > "
                   << FLAGS_ecmp_width << " (" << scaledNextHops << ")";
        // calculate number of times we need to decrement the max next hop
        NextHopWeight overflow = scaledTotalWeight - FLAGS_ecmp_width;
        for (int i = 0; i < overflow; ++i) {
          // find the max weight next hop
          auto maxItr = std::max_element(
              scaledNextHops.begin(),
              scaledNextHops.end(),
              [](const NextHop& n1, const NextHop& n2) {
                return n1.weight() < n2.weight();
              });
          XLOG(DBG3) << "Decrementing the weight of next hop: " << *maxItr;
          // create a clone of the max weight next hop with weight decremented
          ResolvedNextHop decMax = ResolvedNextHop(
              maxItr->addr(),
              maxItr->intf(),
              maxItr->weight() - 1,
              maxItr->labelForwardingAction());
          // remove the max weight next hop and replace with the
          // decremented version, if the decremented version would
          // not have weight 0. If it would have weight 0, that means
          // that we have > FLAGS_ecmp_width next hops.
          scaledNextHops.erase(maxItr);
          if (decMax.weight() > 0) {
            scaledNextHops.insert(decMax);
          }
        }
        scaledTotalWeight = FLAGS_ecmp_width;
      }
    }
    XLOG(DBG3) << "Scaled next hops from " << getNextHopSet() << " to "
               << scaledNextHops;
    normalizedNextHops = scaledNextHops;
  } else {
    scaledTotalWeight = totalWeight;
  }

  if (FLAGS_wide_ecmp && scaledTotalWeight > kMinSizeForWideEcmp &&
      scaledTotalWeight < FLAGS_ecmp_width) {
    std::map<folly::IPAddress, uint64_t> nhopAndWeights;
    for (const auto& nhop : normalizedNextHops) {
      nhopAndWeights.insert(std::make_pair(nhop.addr(), nhop.weight()));
    }
    normalizeNextHopWeightsToMaxPaths<folly::IPAddress>(
        nhopAndWeights, FLAGS_ecmp_width);
    NextHopSet normalizedToMaxPathNextHops;
    for (const auto& nhop : normalizedNextHops) {
      normalizedToMaxPathNextHops.insert(ResolvedNextHop(
          nhop.addr(),
          nhop.intf(),
          nhopAndWeights[nhop.addr()],
          nhop.labelForwardingAction()));
    }
    XLOG(DBG3) << "Scaled next hops from " << getNextHopSet() << " to "
               << normalizedToMaxPathNextHops;
    return normalizedToMaxPathNextHops;
  }
  return normalizedNextHops;
}

RouteNextHopEntry RouteNextHopEntry::from(
    const facebook::fboss::UnicastRoute& route,
    AdminDistance defaultAdminDistance,
    std::optional<RouteCounterID> counterID) {
  std::vector<NextHopThrift> nhts;
  if (route.nextHops_ref()->empty() && !route.nextHopAddrs_ref()->empty()) {
    nhts = thriftNextHopsFromAddresses(*route.nextHopAddrs_ref());
  } else {
    nhts = *route.nextHops_ref();
  }

  RouteNextHopSet nexthops = util::toRouteNextHopSet(nhts);

  auto adminDistance = route.adminDistance_ref().value_or(defaultAdminDistance);

  if (nexthops.size()) {
    if (route.action_ref() &&
        *route.action_ref() != facebook::fboss::RouteForwardAction::NEXTHOPS) {
      throw FbossError(
          "Nexthops specified, but action is set to : ", *route.action_ref());
    }
    return RouteNextHopEntry(std::move(nexthops), adminDistance, counterID);
  }

  if (!route.action_ref() ||
      *route.action_ref() == facebook::fboss::RouteForwardAction::DROP) {
    return RouteNextHopEntry(
        RouteForwardAction::DROP, adminDistance, counterID);
  }
  return RouteNextHopEntry(
      RouteForwardAction::TO_CPU, adminDistance, counterID);
}

RouteNextHopEntry RouteNextHopEntry::createDrop(AdminDistance adminDistance) {
  return RouteNextHopEntry(RouteForwardAction::DROP, adminDistance);
}

RouteNextHopEntry RouteNextHopEntry::createToCpu(AdminDistance adminDistance) {
  return RouteNextHopEntry(RouteForwardAction::TO_CPU, adminDistance);
}

RouteNextHopEntry RouteNextHopEntry::fromStaticRoute(
    const cfg::StaticRouteWithNextHops& route) {
  RouteNextHopSet nhops;

  // NOTE: Static routes use the default UCMP weight so that they can be
  // compatible with UCMP, i.e., so that we can do ucmp where the next
  // hops resolve to a static route.  If we define recursive static
  // routes, that may lead to unexpected behavior where some interface
  // gets more traffic.  If necessary, in the future, we can make it
  // possible to configure strictly ECMP static routes
  for (auto& nhopStr : *route.nexthops_ref()) {
    nhops.emplace(
        UnresolvedNextHop(folly::IPAddress(nhopStr), UCMP_DEFAULT_WEIGHT));
  }

  return RouteNextHopEntry(std::move(nhops), AdminDistance::STATIC_ROUTE);
}

RouteNextHopEntry RouteNextHopEntry::fromStaticIp2MplsRoute(
    const cfg::StaticIp2MplsRoute& route) {
  RouteNextHopSet nhops;

  for (auto& nexthop : *route.nexthops_ref()) {
    nhops.emplace(util::fromThrift(nexthop));
  }
  return RouteNextHopEntry(std::move(nhops), AdminDistance::STATIC_ROUTE);
}

bool RouteNextHopEntry::isUcmp(const NextHopSet& nhopSet) {
  return totalWeight(nhopSet) != nhopSet.size();
}

} // namespace facebook::fboss
