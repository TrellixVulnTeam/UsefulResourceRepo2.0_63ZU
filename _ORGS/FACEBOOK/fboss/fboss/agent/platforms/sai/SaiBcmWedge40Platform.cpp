/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/sai/SaiBcmWedge40Platform.h"

#include "fboss/agent/hw/switch_asics/Trident2Asic.h"
#include "fboss/agent/platforms/common/wedge40/Wedge40PlatformMapping.h"

#include "fboss/agent/platforms/common/utils/Wedge40LedUtils.h"

#include <cstdio>
#include <cstring>
namespace facebook::fboss {

SaiBcmWedge40Platform::SaiBcmWedge40Platform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    folly::MacAddress localMac)
    : SaiBcmPlatform(
          std::move(productInfo),
          std::make_unique<Wedge40PlatformMapping>(),
          localMac) {
  asic_ = std::make_unique<Trident2Asic>();
}

HwAsic* SaiBcmWedge40Platform::getAsic() const {
  return asic_.get();
}

SaiBcmWedge40Platform::~SaiBcmWedge40Platform() {}

void SaiBcmWedge40Platform::initLEDs() {
  initWedgeLED(0, Wedge40LedUtils::defaultLedCode());
  initWedgeLED(1, Wedge40LedUtils::defaultLedCode());
}

} // namespace facebook::fboss
