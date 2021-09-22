/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/platforms/sai/SaiPlatformInit.h"
#include "fboss/agent/platforms/common/PlatformProductInfo.h"

#include <memory>

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/Platform.h"
#include "fboss/agent/platforms/sai/SaiBcmGalaxyFCPlatform.h"
#include "fboss/agent/platforms/sai/SaiBcmGalaxyLCPlatform.h"
#include "fboss/agent/platforms/sai/SaiBcmMinipackPlatform.h"
#include "fboss/agent/platforms/sai/SaiBcmWedge100Platform.h"
#include "fboss/agent/platforms/sai/SaiBcmWedge400Platform.h"
#include "fboss/agent/platforms/sai/SaiBcmWedge40Platform.h"
#include "fboss/agent/platforms/sai/SaiBcmYampPlatform.h"
#include "fboss/agent/platforms/sai/SaiCloudRipperPlatform.h"
#include "fboss/agent/platforms/sai/SaiWedge400CPlatform.h"

namespace facebook::fboss {

std::unique_ptr<SaiPlatform> chooseSaiPlatform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    folly::MacAddress localMac) {
  if (productInfo->getMode() == PlatformMode::WEDGE100) {
    return std::make_unique<SaiBcmWedge100Platform>(
        std::move(productInfo), localMac);
  } else if (productInfo->getMode() == PlatformMode::WEDGE) {
    return std::make_unique<SaiBcmWedge40Platform>(
        std::move(productInfo), localMac);
  } else if (productInfo->getMode() == PlatformMode::GALAXY_FC) {
    return std::make_unique<SaiBcmGalaxyFCPlatform>(
        std::move(productInfo), localMac);
  } else if (productInfo->getMode() == PlatformMode::GALAXY_LC) {
    return std::make_unique<SaiBcmGalaxyLCPlatform>(
        std::move(productInfo), localMac);
  } else if (productInfo->getMode() == PlatformMode::WEDGE400) {
    return std::make_unique<SaiBcmWedge400Platform>(
        std::move(productInfo), localMac);
  } else if (productInfo->getMode() == PlatformMode::MINIPACK) {
    return std::make_unique<SaiBcmMinipackPlatform>(
        std::move(productInfo), localMac);
  } else if (productInfo->getMode() == PlatformMode::YAMP) {
    return std::make_unique<SaiBcmYampPlatform>(
        std::move(productInfo), localMac);
  } else if (productInfo->getMode() == PlatformMode::WEDGE400C) {
    if (isLEB()) {
      return getLEBPlatform(std::move(productInfo), localMac);
    }
    return std::make_unique<SaiWedge400CPlatform>(
        std::move(productInfo), localMac);
  } else if (productInfo->getMode() == PlatformMode::CLOUDRIPPER) {
    return std::make_unique<SaiCloudRipperPlatform>(
        std::move(productInfo), localMac);
  }

  return nullptr;
} // namespace facebook::fboss

std::unique_ptr<Platform> initSaiPlatform(
    std::unique_ptr<AgentConfig> config,
    uint32_t hwFeaturesDesired) {
  auto productInfo =
      std::make_unique<PlatformProductInfo>(FLAGS_fruid_filepath);
  productInfo->initialize();
  auto localMac = getLocalMacAddress();

  auto platform = chooseSaiPlatform(std::move(productInfo), localMac);
  platform->init(std::move(config), hwFeaturesDesired);
  return std::move(platform);
}

} // namespace facebook::fboss
