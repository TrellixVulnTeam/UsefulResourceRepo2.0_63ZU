/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiDebugCounterManager.h"

#include "fboss/agent/hw/sai/api/DebugCounterApi.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"

namespace facebook::fboss {

void SaiDebugCounterManager::setupDebugCounters() {
  SaiDebugCounterTraits::CreateAttributes attrs{
      SAI_DEBUG_COUNTER_TYPE_PORT_IN_DROP_REASONS,
      SAI_DEBUG_COUNTER_BIND_METHOD_AUTOMATIC,
      SaiDebugCounterTraits::Attributes::InDropReasons{
#if SAI_API_VERSION >= SAI_VERSION(1, 7, 0)
          {SAI_IN_DROP_REASON_FDB_AND_BLACKHOLE_DISCARDS}
#else
          {SAI_IN_DROP_REASON_END + 1}
#endif
      }};

  auto& debugCounterStore = saiStore_->get<SaiDebugCounterTraits>();
  portL3BlackHoleCounter_ = debugCounterStore.setObject(attrs, attrs);
  portL3BlackHoleCounterStatId_ = SAI_PORT_STAT_IN_DROP_REASON_RANGE_BASE +
      SaiApiTable::getInstance()->debugCounterApi().getAttribute(
          portL3BlackHoleCounter_->adapterKey(),
          SaiDebugCounterTraits::Attributes::Index{});
}
} // namespace facebook::fboss
