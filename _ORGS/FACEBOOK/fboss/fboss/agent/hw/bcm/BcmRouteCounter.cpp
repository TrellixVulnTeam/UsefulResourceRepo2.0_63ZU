/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
extern "C" {
#include <bcm/l3.h>
}

#include <folly/logging/xlog.h>
#include "fboss/agent/Constants.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmPlatform.h"
#include "fboss/agent/hw/bcm/BcmRouteCounter.h"
#include "fboss/agent/hw/bcm/BcmStatUpdater.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/bcm/BcmWarmBootCache.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"

namespace facebook::fboss {

BcmRouteCounter::BcmRouteCounter(
    BcmSwitch* hw,
    RouteCounterID counterID,
    int modeId)
    : hw_(hw) {
  uint32 numCounters;
  const auto counterFromWBCache =
      hw_->getWarmBootCache()->findRouteCounterID(counterID);
  if (counterFromWBCache != hw_->getWarmBootCache()->RouteCounterIDMapEnd()) {
    hwCounterId_ = counterFromWBCache->second;
    XLOG(DBG2) << "Restored route counter id " << counterID << " hwId "
               << hwCounterId_;
    hw_->getWarmBootCache()->programmed(counterFromWBCache);
  } else {
    /* Attach stat to customized group mode */
    auto rc = bcm_stat_custom_group_create(
        hw_->getUnit(),
        modeId,
        bcmStatObjectIngL3Route,
        &hwCounterId_,
        &numCounters);
    bcmCheckError(rc, "failed to create bcm stat custom group ");
    XLOG(DBG2) << "Allocated route counter id " << hwCounterId_;
  }
  hw_->getStatUpdater()->toBeAddedRouteCounter(hwCounterId_, counterID);
}

BcmRouteCounter::~BcmRouteCounter() {
  XLOG(DBG2) << "Destroying route counter id " << hwCounterId_;
  auto rc = bcm_stat_group_destroy(hw_->getUnit(), hwCounterId_);
  bcmLogFatal(rc, hw_, "failed to destroy counter id ", hwCounterId_);
  hw_->getStatUpdater()->toBeRemovedRouteCounter(hwCounterId_);
}

BcmRouteCounterTable::BcmRouteCounterTable(BcmSwitch* hw) : hw_(hw) {}

uint32_t BcmRouteCounterTable::createStatGroupModeId() {
  uint32_t totalCounters = 1;
  int numSelectors = 1;
  bcm_stat_group_mode_attr_selector_t attrSelectors[1];
  uint32 modeId = 0;

  if (!hw_->getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::ROUTE_COUNTERS)) {
    throw FbossError("Route counters are not supported on this platform");
  }

  /* Selector for non-drop packets  */
  bcm_stat_group_mode_attr_selector_t_init(&attrSelectors[0]);
  attrSelectors[0].counter_offset = 0;
  attrSelectors[0].attr = bcmStatGroupModeAttrDrop;
  attrSelectors[0].attr_value = 0;

  auto rc = bcm_stat_group_mode_id_create(
      hw_->getUnit(),
      BCM_STAT_GROUP_MODE_INGRESS,
      totalCounters,
      numSelectors,
      attrSelectors,
      &modeId);
  bcmCheckError(rc, "failed to create bcm stat group mode id");
  return modeId;
}

// used for testing purpose
std::optional<BcmRouteCounterID> BcmRouteCounterTable::getHwCounterID(
    std::optional<RouteCounterID> counterID) const {
  if (!counterID.has_value()) {
    return std::nullopt;
  }
  auto hwCounter = counterIDs_.get(*counterID);
  return hwCounter
      ? std::optional<BcmRouteCounterID>(hwCounter->getHwCounterID())
      : std::nullopt;
}

std::shared_ptr<BcmRouteCounter>
BcmRouteCounterTable::referenceOrEmplaceCounterID(RouteCounterID id) {
  if (globalIngressModeId_ == STAT_MODEID_INVALID) {
    const auto idFromWBCache = hw_->getWarmBootCache()->getRouteCounterModeId();
    if (idFromWBCache != STAT_MODEID_INVALID) {
      globalIngressModeId_ = idFromWBCache;
      XLOG(DBG2) << "Restored route counter global mode id "
                 << globalIngressModeId_ << " from warmboot cache";
    } else {
      globalIngressModeId_ = createStatGroupModeId();
      XLOG(DBG2) << "Allocated route counter global mode id "
                 << globalIngressModeId_;
    }
  }
  std::shared_ptr<BcmRouteCounter> counterRef =
      counterIDs_.refOrEmplace(id, hw_, id, globalIngressModeId_).first;
  if (counterIDs_.size() > maxRouteCounterIDs_) {
    XLOG(ERR) << "RouteCounterIDs in use " << counterIDs_.size()
              << " exceed max count " << maxRouteCounterIDs_;
    // throw Bcm full error so that overflow error handling kicks in
    throw BcmError(
        BCM_E_FULL,
        "RouteCounterIDs in use ",
        counterIDs_.size(),
        " exceed max count ",
        maxRouteCounterIDs_);
  }
  return counterRef;
}

folly::dynamic BcmRouteCounterTable::toFollyDynamic() const {
  folly::dynamic routeCounterIDs = folly::dynamic::object;
  for (const auto entry : counterIDs_) {
    auto counter = entry.second.lock();
    routeCounterIDs[entry.first] = counter->getHwCounterID();
  }
  folly::dynamic routeCounterInfo = folly::dynamic::object;
  routeCounterInfo[kRouteCounterIDs] = std::move(routeCounterIDs);
  routeCounterInfo[kGlobalModeId] = std::to_string(globalIngressModeId_);
  return routeCounterInfo;
}

BcmRouteCounterTable::~BcmRouteCounterTable() {
  if (globalIngressModeId_ != STAT_MODEID_INVALID) {
    XLOG(DBG2) << "Destroying stat group mode id " << globalIngressModeId_;
    auto rc =
        bcm_stat_group_mode_id_destroy(hw_->getUnit(), globalIngressModeId_);
    bcmLogFatal(rc, hw_, "failed to destroy bcm stat group mode id");
  }
}
} // namespace facebook::fboss
