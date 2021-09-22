// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/lib/phy/PhyManager.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/types.h"
#include "fboss/lib/config/PlatformConfigUtils.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"

#include <folly/json.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <chrono>
#include <cstdlib>
#include <memory>

DEFINE_int32(
    xphy_port_stat_interval_secs,
    200,
    "Interval to collect xphy port statistics (seconds)");

DEFINE_int32(
    xphy_prbs_stat_interval_secs,
    10,
    "Interval to collect xphy prbs statistics (seconds)");

namespace {
// Key of the portToCacheInfo map in warmboot state cache
constexpr auto kPortToCacheInfoKey = "portToCacheInfo";
constexpr auto kSystemLanesKey = "systemLanes";
constexpr auto kLineLanesKey = "lineLanes";
} // namespace

namespace facebook::fboss {

PhyManager::PhyManager(const PlatformMapping* platformMapping)
    : platformMapping_(platformMapping),
      portToCacheInfo_(setupPortToCacheInfo(platformMapping)) {}

PhyManager::~PhyManager() {}

PhyManager::PortToCacheInfo PhyManager::setupPortToCacheInfo(
    const PlatformMapping* platformMapping) {
  PortToCacheInfo cacheMap;
  const auto& chips = platformMapping->getChips();
  for (const auto& port : platformMapping->getPlatformPorts()) {
    const auto& xphy = utility::getDataPlanePhyChips(
        port.second, chips, phy::DataPlanePhyChipType::XPHY);
    if (!xphy.empty()) {
      auto cache = std::make_unique<folly::Synchronized<PortCacheInfo>>();
      auto wLockedCache = cache->wlock();
      wLockedCache->xphyID =
          GlobalXphyID(*xphy.begin()->second.physicalID_ref());
      cacheMap.emplace(PortID(port.first), std::move(cache));
    }
  }
  return cacheMap;
}

GlobalXphyID PhyManager::getGlobalXphyIDbyPortID(PortID portID) const {
  return getGlobalXphyIDbyPortIDLocked(getRLockedCache(portID));
}

phy::ExternalPhy* PhyManager::getExternalPhy(GlobalXphyID xphyID) {
  auto pimID = getPhyIDInfo(xphyID).pimID;
  const auto& pimXphyMap = xphyMap_.find(pimID);
  if (pimXphyMap == xphyMap_.end()) {
    throw FbossError(
        "getExternalPhy: Invalid Slot Id ", pimID, " is not in xphyMap_");
  }
  const auto& xphyIt = pimXphyMap->second.find(xphyID);
  if (xphyIt == pimXphyMap->second.end()) {
    throw FbossError(
        "getExternalPhy: Invalid Global Xphy Id ",
        xphyID,
        " is not in xphyMap_ for Pim Id ",
        pimID);
  }
  return xphyIt->second.get();
}

phy::PhyPortConfig PhyManager::getDesiredPhyPortConfig(
    PortID portId,
    cfg::PortProfileID portProfileId,
    std::optional<TransceiverInfo> transceiverInfo) {
  auto platformPortEntry = platformMapping_->getPlatformPorts().find(portId);
  if (platformPortEntry == platformMapping_->getPlatformPorts().end()) {
    throw FbossError(
        "Can't find the platform port entry in platform mapping for port:",
        portId);
  }
  const auto& chips = platformMapping_->getChips();
  if (chips.empty()) {
    throw FbossError("No DataPlanePhyChips found");
  }

  std::optional<PlatformPortProfileConfigMatcher> matcher;
  if (transceiverInfo) {
    matcher = PlatformPortProfileConfigMatcher(
        portProfileId, portId, *transceiverInfo);
  } else {
    matcher = PlatformPortProfileConfigMatcher(portProfileId, portId);
  }
  const auto& portPinConfig = platformMapping_->getPortXphyPinConfig(*matcher);
  const auto& portProfileConfig =
      platformMapping_->getPortProfileConfig(*matcher);
  if (!portProfileConfig.has_value()) {
    throw FbossError(
        "No port profile with id ",
        apache::thrift::util::enumNameSafe(portProfileId),
        " found in PlatformConfig for port ",
        portId);
  }

  phy::PhyPortConfig phyPortConfig;
  phyPortConfig.config = phy::ExternalPhyConfig::fromConfigeratorTypes(
      portPinConfig,
      utility::getXphyLinePolaritySwapMap(
          *platformPortEntry->second.mapping_ref()->pins_ref(), chips));
  phyPortConfig.profile =
      phy::ExternalPhyProfileConfig::fromPortProfileConfig(*portProfileConfig);
  return phyPortConfig;
}

phy::PhyPortConfig PhyManager::getHwPhyPortConfig(PortID portID) {
  return getHwPhyPortConfigLocked(getRLockedCache(portID), portID);
}

void PhyManager::programOnePort(
    PortID portId,
    cfg::PortProfileID portProfileId,
    std::optional<TransceiverInfo> transceiverInfo) {
  const auto& wLockedCache = getWLockedCache(portId);

  // This function will call ExternalPhy::programOnePort(phy::PhyPortConfig).
  auto* xphy = getExternalPhyLocked(wLockedCache);
  const auto& desiredPhyPortConfig =
      getDesiredPhyPortConfig(portId, portProfileId, transceiverInfo);
  xphy->programOnePort(desiredPhyPortConfig);

  // Once the port is programmed successfully, update the portToLanesInfo_
  setPortToLanesInfoLocked(wLockedCache, portId, desiredPhyPortConfig);
  if (xphy->isSupported(phy::ExternalPhy::Feature::PORT_STATS)) {
    setPortToExternalPhyPortStatsLocked(
        wLockedCache, createExternalPhyPortStats(PortID(portId)));
  }
}

folly::EventBase* FOLLY_NULLABLE
PhyManager::getPimEventBase(PimID pimID) const {
  if (auto pimEventMultiThread = pimToThread_.find(pimID);
      pimEventMultiThread != pimToThread_.end()) {
    return pimEventMultiThread->second->eventBase.get();
  }
  return nullptr;
}

PhyManager::PimEventMultiThreading::PimEventMultiThreading(PimID pimID) {
  pim = pimID;
  eventBase = std::make_unique<folly::EventBase>();
  // start pim thread
  thread = std::make_unique<std::thread>([=] { eventBase->loopForever(); });
  XLOG(DBG2) << "Created PimEventMultiThreading for pim="
             << static_cast<int>(pimID);
}

PhyManager::PimEventMultiThreading::~PimEventMultiThreading() {
  eventBase->terminateLoopSoon();
  thread->join();
  XLOG(DBG2) << "Terminated multi-threading for pim=" << static_cast<int>(pim);
}

void PhyManager::setupPimEventMultiThreading(PimID pimID) {
  pimToThread_.emplace(pimID, std::make_unique<PimEventMultiThreading>(pimID));
}

void PhyManager::setPortToLanesInfoLocked(
    const PortCacheWLockedPtr& lockedCache,
    PortID portID,
    const phy::PhyPortConfig& portConfig) {
  // First check whether there's a systemLanes and lineLanes cache already
  const auto& systemLanesConfig = portConfig.config.system.lanes;
  const auto& lineLanesConfig = portConfig.config.line.lanes;
  // check whether all the lanes match
  bool matched =
      (lockedCache->systemLanes.size() == systemLanesConfig.size()) &&
      (lockedCache->lineLanes.size() == lineLanesConfig.size());
  // Now check system lane id
  if (matched) {
    for (auto i = 0; i < lockedCache->systemLanes.size(); ++i) {
      if (systemLanesConfig.find(lockedCache->systemLanes[i]) ==
          systemLanesConfig.end()) {
        matched = false;
        break;
      }
    }
  }
  // Now check line lane id
  if (matched) {
    for (auto i = 0; i < lockedCache->lineLanes.size(); ++i) {
      if (lineLanesConfig.find(lockedCache->lineLanes[i]) ==
          lineLanesConfig.end()) {
        matched = false;
        break;
      }
    }
  }
  if (matched) {
    return;
  }

  // Now reset the cached lane info if there's no match
  lockedCache->systemLanes.clear();
  for (const auto& it : portConfig.config.system.lanes) {
    lockedCache->systemLanes.push_back(it.first);
  }
  lockedCache->lineLanes.clear();
  for (const auto& it : portConfig.config.line.lanes) {
    lockedCache->lineLanes.push_back(it.first);
  }
}

void PhyManager::setPortPrbs(
    PortID portID,
    phy::Side side,
    const phy::PortPrbsState& prbs) {
  const auto& wLockedCache = getWLockedCache(portID);

  auto* xphy = getExternalPhyLocked(wLockedCache);
  if (!xphy->isSupported(phy::ExternalPhy::Feature::PRBS)) {
    throw FbossError("Port:", portID, " xphy can't support PRBS");
  }
  if (wLockedCache->systemLanes.empty() || wLockedCache->lineLanes.empty()) {
    throw FbossError(
        "Port:", portID, " is not programmed and can't find cached lanes");
  }
  const auto& sideLanes = side == phy::Side::SYSTEM ? wLockedCache->systemLanes
                                                    : wLockedCache->lineLanes;
  xphy->setPortPrbs(side, sideLanes, prbs);

  if (*prbs.enabled_ref()) {
    const auto& phyPortConfig = getHwPhyPortConfigLocked(wLockedCache, portID);
    wLockedCache->stats->setupPrbsCollection(
        side, sideLanes, phyPortConfig.getLaneSpeedInMb(side));
  } else {
    wLockedCache->stats->disablePrbsCollection(side);
  }
}

phy::PortPrbsState PhyManager::getPortPrbs(PortID portID, phy::Side side) {
  const auto& rLockedCache = getRLockedCache(portID);

  auto* xphy = getExternalPhyLocked(rLockedCache);
  if (!xphy->isSupported(phy::ExternalPhy::Feature::PRBS)) {
    throw FbossError("Port:", portID, " xphy can't support PRBS");
  }
  if (rLockedCache->systemLanes.empty() || rLockedCache->lineLanes.empty()) {
    throw FbossError(
        "Port:", portID, " is not programmed and can't find cached lanes");
  }
  const auto& sideLanes = side == phy::Side::SYSTEM ? rLockedCache->systemLanes
                                                    : rLockedCache->lineLanes;

  return xphy->getPortPrbs(side, sideLanes);
}

std::vector<PrbsLaneStats> PhyManager::getPortPrbsStats(
    PortID portID,
    phy::Side side) {
  const auto& rLockedCache = getRLockedCache(portID);

  auto* xphy = getExternalPhyLocked(rLockedCache);
  if (!xphy->isSupported(phy::ExternalPhy::Feature::PRBS_STATS)) {
    throw FbossError("Port:", portID, " xphy can't support PRBS_STATS");
  }

  return rLockedCache->stats->getPrbsStats(side);
}

void PhyManager::clearPortPrbsStats(PortID portID, phy::Side side) {
  const auto& wLockedCache = getWLockedCache(portID);

  auto* xphy = getExternalPhyLocked(wLockedCache);
  if (!xphy->isSupported(phy::ExternalPhy::Feature::PRBS_STATS)) {
    throw FbossError("Port:", portID, " xphy can't support PRBS_STATS");
  }

  return wLockedCache->stats->clearPrbsStats(side);
}

folly::dynamic PhyManager::getWarmbootState() const {
  folly::dynamic phyState = folly::dynamic::object;
  folly::dynamic portToCacheInfoCache = folly::dynamic::object;
  // For now, we just need to cache the current system/line lanes
  for (const auto& it : portToCacheInfo_) {
    folly::dynamic portCacheInfo = folly::dynamic::object;
    folly::dynamic systemLanes = folly::dynamic::array;
    const auto& rLockedCache = it.second->rlock();
    for (auto lane : rLockedCache->systemLanes) {
      systemLanes.push_back(static_cast<int>(lane));
    }
    folly::dynamic lineLanes = folly::dynamic::array;
    for (auto lane : rLockedCache->lineLanes) {
      lineLanes.push_back(static_cast<int>(lane));
    }
    portCacheInfo[kSystemLanesKey] = systemLanes;
    portCacheInfo[kLineLanesKey] = lineLanes;
    portToCacheInfoCache[folly::to<std::string>(static_cast<int>(it.first))] =
        portCacheInfo;
  }
  phyState[kPortToCacheInfoKey] = portToCacheInfoCache;
  return phyState;
}

void PhyManager::restoreFromWarmbootState(
    const folly::dynamic& phyWarmbootState) {
  if (phyWarmbootState.find(kPortToCacheInfoKey) ==
      phyWarmbootState.items().end()) {
    throw FbossError(
        "Can't find ",
        kPortToCacheInfoKey,
        " in the phy warmboot state. Skip restoring portToCacheInfo_ map");
  }

  const auto& portToCacheInfoCache = phyWarmbootState[kPortToCacheInfoKey];
  for (const auto& it : portToCacheInfo_) {
    auto portID = static_cast<int>(it.first);
    auto portIDStr = folly::to<std::string>(portID);
    if (portToCacheInfoCache.find(portIDStr) ==
        portToCacheInfoCache.items().end()) {
      XLOG(WARN) << "Can't find port=" << portID
                 << " in the phy warmboot portToCacheInfo map.";
      continue;
    }
    const auto& portCacheInfo = portToCacheInfoCache[portIDStr];
    if (portCacheInfo.find(kSystemLanesKey) == portCacheInfo.items().end() ||
        portCacheInfo.find(kLineLanesKey) == portCacheInfo.items().end()) {
      throw FbossError(
          "Can't find the system and line lanes for port=",
          portID,
          " in the phy warmboot portToCacheInfo map.");
    }
    bool isProgrammed = false;
    PortID portIDStrong = PortID(portID);
    const auto& wLockedCache = it.second->wlock();
    for (auto lane : portCacheInfo[kSystemLanesKey]) {
      wLockedCache->systemLanes.push_back(LaneID(lane.asInt()));
      isProgrammed = true;
    }
    for (auto lane : portCacheInfo[kLineLanesKey]) {
      wLockedCache->lineLanes.push_back(LaneID(lane.asInt()));
    }

    if (isProgrammed) {
      auto* xphy = getExternalPhyLocked(wLockedCache);
      // If the port has programmed lane info, we also need to restore the
      // ExternalPhyPortStatsUtils
      if (xphy->isSupported(phy::ExternalPhy::Feature::PORT_STATS)) {
        setPortToExternalPhyPortStatsLocked(
            wLockedCache, createExternalPhyPortStats(portIDStrong));
      }
      // If the prbs is enabled in HW, we also need to setup prbs stats
      if (xphy->isSupported(phy::ExternalPhy::Feature::PRBS_STATS)) {
        const auto& sysPrbsState =
            xphy->getPortPrbs(phy::Side::SYSTEM, wLockedCache->systemLanes);
        const auto& linePrbsState =
            xphy->getPortPrbs(phy::Side::LINE, wLockedCache->lineLanes);
        if (*sysPrbsState.enabled_ref() || *linePrbsState.enabled_ref()) {
          const auto& phyPortConfig =
              getHwPhyPortConfigLocked(wLockedCache, portIDStrong);
          if (*sysPrbsState.enabled_ref()) {
            wLockedCache->stats->setupPrbsCollection(
                phy::Side::SYSTEM,
                wLockedCache->systemLanes,
                phyPortConfig.getLaneSpeedInMb(phy::Side::SYSTEM));
          }
          if (*linePrbsState.enabled_ref()) {
            wLockedCache->stats->setupPrbsCollection(
                phy::Side::LINE,
                wLockedCache->lineLanes,
                phyPortConfig.getLaneSpeedInMb(phy::Side::LINE));
          }
        }
      }
    }

    XLOG(INFO) << "Restore port=" << portID
               << ", systemLanes=" << portCacheInfo[kSystemLanesKey]
               << ", lineLanes=" << portCacheInfo[kLineLanesKey]
               << " from the phy warmboot state";
  }
}

int32_t PhyManager::getXphyPortStatsUpdateIntervalInSec() const {
  return FLAGS_xphy_port_stat_interval_secs;
}

void PhyManager::setPortToExternalPhyPortStatsLocked(
    const PortCacheWLockedPtr& lockedCache,
    std::unique_ptr<ExternalPhyPortStatsUtils> stats) {
  lockedCache->stats = std::move(stats);
}

void PhyManager::updateAllXphyPortsStats() {
  for (const auto& portCacheInfo : portToCacheInfo_) {
    const auto& wLockedCache = portCacheInfo.second->wlock();
    // If the port is not programmed yet, skip updating xphy stats for it
    if (wLockedCache->systemLanes.empty() || wLockedCache->lineLanes.empty()) {
      continue;
    }
    updateStatsLocked(wLockedCache, portCacheInfo.first);
  }
}

using namespace std::chrono;
void PhyManager::updateStatsLocked(
    const PortCacheWLockedPtr& wLockedCache,
    PortID portID) {
  if (wLockedCache->systemLanes.empty() || wLockedCache->lineLanes.empty()) {
    throw FbossError(
        "Port:", portID, " is not programmed and can't find cached lanes");
  }

  auto xphyID = getGlobalXphyIDbyPortIDLocked(wLockedCache);
  auto* xphy = getExternalPhyLocked(wLockedCache);
  // If the xphy doesn't support either port or prbs stats, no-op
  if (!xphy->isSupported(phy::ExternalPhy::Feature::PORT_STATS) &&
      !xphy->isSupported(phy::ExternalPhy::Feature::PRBS_STATS)) {
    return;
  }

  auto pimID = getPhyIDInfo(xphyID).pimID;
  auto evb = getPimEventBase(pimID);
  if (xphy->isSupported(phy::ExternalPhy::Feature::PORT_STATS)) {
    if (wLockedCache->ongoingStatCollection.has_value() &&
        !wLockedCache->ongoingStatCollection->isReady()) {
      XLOG(DBG4) << "XPHY Port Stat collection for Port:" << portID
                 << " still underway...";
    } else {
      // Collect xphy port stats
      steady_clock::time_point begin = steady_clock::now();
      wLockedCache->ongoingStatCollection =
          folly::via(evb)
              .thenValue([this, portID, xphy, begin](auto&&) {
                // Since this is delay future job, we need to fetch the
                // cache with wlock again
                const auto& wCache = getWLockedCache(portID);
                const auto& stats =
                    xphy->getPortStats(wCache->systemLanes, wCache->lineLanes);
                wCache->stats->updateXphyStats(stats);
                XLOG(DBG3) << "Port " << portID
                           << ": xphy port stat collection took "
                           << duration_cast<milliseconds>(
                                  steady_clock::now() - begin)
                                  .count()
                           << "ms";
              })
              .delayed(seconds(getXphyPortStatsUpdateIntervalInSec()));
    }
  }

  if (xphy->isSupported(phy::ExternalPhy::Feature::PRBS_STATS)) {
    // Only needs to update prbs stats as long as there's one side enabled
    if (wLockedCache->stats->isPrbsCollectionEnabled(phy::Side::SYSTEM) ||
        wLockedCache->stats->isPrbsCollectionEnabled(phy::Side::LINE)) {
      if (wLockedCache->ongoingPrbsStatCollection.has_value() &&
          !wLockedCache->ongoingPrbsStatCollection->isReady()) {
        XLOG(DBG4) << "XPHY PRBS Stat collection for Port:" << portID
                   << " still underway...";
      } else {
        // Collect xphy prbs stats
        steady_clock::time_point begin = steady_clock::now();
        wLockedCache->ongoingPrbsStatCollection =
            folly::via(evb)
                .thenValue([this, portID, xphy, begin](auto&&) {
                  // Since this is delay future job, we need to fetch the
                  // cache with wlock again
                  const auto& wCache = getWLockedCache(portID);
                  const auto& stats = xphy->getPortPrbsStats(
                      wCache->systemLanes, wCache->lineLanes);
                  wCache->stats->updateXphyPrbsStats(stats);
                  XLOG(DBG3) << "Port " << portID
                             << ": xphy prbs stat collection took "
                             << duration_cast<milliseconds>(
                                    steady_clock::now() - begin)
                                    .count()
                             << "ms";
                })
                .delayed(seconds(FLAGS_xphy_prbs_stat_interval_secs));
      }
    }
  }
}

const std::string& PhyManager::getPortName(PortID portID) const {
  const auto& portEntry = platformMapping_->getPlatformPorts().find(portID);
  if (portEntry == platformMapping_->getPlatformPorts().end()) {
    throw FbossError(
        "Unrecoginized port=", portID, ", which is not in PlatformMapping");
  }
  return *portEntry->second.mapping_ref()->name_ref();
}

PhyManager::PortCacheRLockedPtr PhyManager::getRLockedCache(
    PortID portID) const {
  const auto& cache = portToCacheInfo_.find(portID);
  if (cache == portToCacheInfo_.end()) {
    throw FbossError(
        "Unrecoginized port=", portID, ", which is not in PlatformMapping");
  }
  return cache->second->rlock();
}
PhyManager::PortCacheWLockedPtr PhyManager::getWLockedCache(
    PortID portID) const {
  const auto& cache = portToCacheInfo_.find(portID);
  if (cache == portToCacheInfo_.end()) {
    throw FbossError(
        "Unrecoginized port=", portID, ", which is not in PlatformMapping");
  }
  return cache->second->wlock();
}

bool PhyManager::isXphyStatsCollectionDone(PortID portID) const {
  const auto& rLockedCache = getRLockedCache(portID);
  if (rLockedCache->systemLanes.empty() || rLockedCache->lineLanes.empty()) {
    throw FbossError(
        "Port:", portID, " is not programmed and can't find cached lanes");
  }
  return rLockedCache->ongoingStatCollection.has_value() &&
      rLockedCache->ongoingStatCollection->isReady();
}

bool PhyManager::isPrbsStatsCollectionDone(PortID portID) const {
  const auto& rLockedCache = getRLockedCache(portID);
  if (rLockedCache->systemLanes.empty() || rLockedCache->lineLanes.empty()) {
    throw FbossError(
        "Port:", portID, " is not programmed and can't find cached lanes");
  }
  return rLockedCache->ongoingPrbsStatCollection.has_value() &&
      rLockedCache->ongoingPrbsStatCollection->isReady();
}

std::vector<PortID> PhyManager::getXphyPorts() const {
  std::vector<PortID> xphyPorts;
  std::for_each(
      portToCacheInfo_.begin(),
      portToCacheInfo_.end(),
      [&xphyPorts](auto& portAndInfo) {
        xphyPorts.push_back(portAndInfo.first);
      });
  return xphyPorts;
}

std::vector<PortID> PhyManager::getPortsSupportingFeature(
    phy::ExternalPhy::Feature feature) const {
  std::vector<PortID> ports;
  auto xphysSupportingFeature = getXphysSupportingFeature(feature);
  std::for_each(
      portToCacheInfo_.begin(),
      portToCacheInfo_.end(),
      [&ports, this, &xphysSupportingFeature](auto& portAndInfo) {
        auto portXphy = getRLockedCache(portAndInfo.first)->xphyID;
        if (xphysSupportingFeature.find(portXphy) !=
            xphysSupportingFeature.end()) {
          ports.push_back(portAndInfo.first);
        }
      });
  return ports;
}

std::set<GlobalXphyID> PhyManager::getXphysSupportingFeature(
    phy::ExternalPhy::Feature feature) const {
  std::set<GlobalXphyID> phys;
  for (const auto& pimAndXphy : xphyMap_) {
    for (const auto& idAndXphy : pimAndXphy.second) {
      if (idAndXphy.second->isSupported(feature)) {
        phys.insert(idAndXphy.first);
      }
    }
  }
  return phys;
}
} // namespace facebook::fboss
