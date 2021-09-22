/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/test/dataplane_tests/HwTestPfcUtils.h"
#include <folly/logging/xlog.h>
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/platforms/common/PlatformMode.h"

namespace facebook::fboss::utility {

std::vector<int> kLosslessPgs() {
  return {6, 7};
}

int kPgMinLimitCells() {
  return 19;
}

int kGlobalSharedBufferCells(const HwSwitch* hwSwitch) {
  auto mode = hwSwitch->getPlatform()->getMode();
  switch (mode) {
    case PlatformMode::WEDGE400:
      return 117436;
    // TODO: This check will go away once BCM pending CSP
    // is resolved CS00012200060
    // and we can claim back all cells independent of
    // platform where we run it
    case PlatformMode::MINIPACK:
    case PlatformMode::YAMP:
      return 115196;
    default:
      //  this is for fake bcm
      return 115196;
      break;
  }
}
int kGlobalHeadroomBufferCells() {
  return 12432;
}
int kDownlinkPgHeadroomLimitCells() {
  return 1156;
}
int kUplinkPgHeadroomLimitCells() {
  return 1668;
}
int kPgResumeLimitCells() {
  return 19;
}

void enablePfcConfig(
    cfg::SwitchConfig& cfg,
    const std::vector<PortID>& ports,
    const std::string& pgConfigName) {
  cfg::PortPfc pfc;
  pfc.tx_ref() = true;
  pfc.rx_ref() = true;
  pfc.portPgConfigName_ref() = pgConfigName;

  for (const auto& portID : ports) {
    auto portCfg = utility::findCfgPort(cfg, portID);
    portCfg->pfc_ref() = pfc;
  }
}

void enablePfcMapsConfig(cfg::SwitchConfig& cfg) {
  // update pfc maps
  std::map<int16_t, int16_t> tc2PgId;
  std::map<int16_t, int16_t> pfcPri2PgId;
  std::map<int16_t, int16_t> pfcPri2QueueId;
  // program defaults
  // note PORT_PG_VALUE_MAX, PFC_PRIORITY_VALUE_MAX have same value
  for (auto i = 0; i <= cfg::switch_config_constants::PORT_PG_VALUE_MAX();
       i++) {
    tc2PgId.emplace(i, i);
    pfcPri2PgId.emplace(i, i);
    pfcPri2QueueId.emplace(i, i);
  }

  if (cfg.qosPolicies_ref().is_set() &&
      cfg.qosPolicies_ref()[0].qosMap_ref().has_value()) {
    cfg.qosPolicies_ref()[0].qosMap_ref()->trafficClassToPgId_ref() =
        std::move(tc2PgId);
    cfg.qosPolicies_ref()[0].qosMap_ref()->pfcPriorityToPgId_ref() =
        std::move(pfcPri2PgId);
    cfg.qosPolicies_ref()[0].qosMap_ref()->pfcPriorityToQueueId_ref() =
        std::move(pfcPri2QueueId);
  }
}

void enablePgConfigConfig(
    cfg::SwitchConfig& cfg,
    const int mmuCellBytes,
    const std::map<std::string, bool>& pgProfileMap) {
  std::vector<cfg::PortPgConfig> portPgConfigs;
  std::map<std::string, std::vector<cfg::PortPgConfig>> portPgConfigMap;
  for (const auto& [pgProfileName, useUplinkProfile] : pgProfileMap) {
    for (const auto& pgId : kLosslessPgs()) {
      cfg::PortPgConfig pgConfig;
      CHECK_LE(pgId, cfg::switch_config_constants::PORT_PG_VALUE_MAX());
      pgConfig.id_ref() = pgId;
      pgConfig.bufferPoolName_ref() = "bufferNew";
      // provide atleast 1 cell worth of minLimit
      pgConfig.minLimitBytes_ref() = kPgMinLimitCells() * mmuCellBytes;
      pgConfig.headroomLimitBytes_ref() =
          (useUplinkProfile ? kUplinkPgHeadroomLimitCells()
                            : kDownlinkPgHeadroomLimitCells()) *
          mmuCellBytes;
      pgConfig.scalingFactor_ref() = cfg::MMUScalingFactor::ONE;
      pgConfig.resumeOffsetBytes_ref() = kPgResumeLimitCells() * mmuCellBytes;
      portPgConfigs.emplace_back(pgConfig);
    }
    portPgConfigMap.insert({pgProfileName, portPgConfigs});
  }
  cfg.portPgConfigs_ref() = portPgConfigMap;
}

void enableBufferPoolConfig(
    cfg::SwitchConfig& cfg,
    const int mmuCellBytes,
    const HwSwitch* hwSwitch) {
  // create buffer pool
  std::map<std::string, cfg::BufferPoolConfig> bufferPoolCfgMap;
  cfg::BufferPoolConfig poolConfig;
  poolConfig.sharedBytes_ref() =
      kGlobalSharedBufferCells(hwSwitch) * mmuCellBytes;
  poolConfig.headroomBytes_ref() = kGlobalHeadroomBufferCells() * mmuCellBytes;
  bufferPoolCfgMap.insert({"bufferNew", poolConfig});
  cfg.bufferPoolConfigs_ref() = bufferPoolCfgMap;
}

void addPfcConfig(
    cfg::SwitchConfig& cfg,
    const HwSwitch* hwSwitch,
    const std::vector<PortID>& ports) {
  const std::string kPgConfigName = "foo";
  std::map<std::string, bool> pgProfileMap;
  const int mmuCellBytes = hwSwitch->getPlatform()->getMMUCellBytes();
  enablePfcConfig(cfg, ports, kPgConfigName);
  enablePfcMapsConfig(cfg);

  // create pg profile with specific name, also specify if we want
  // to use uplink/downlink profile for it or not
  pgProfileMap.insert({kPgConfigName, false});
  enablePgConfigConfig(cfg, mmuCellBytes, pgProfileMap);
  enableBufferPoolConfig(cfg, mmuCellBytes, hwSwitch);
}

void addUplinkDownlinkPfcConfig(
    cfg::SwitchConfig& cfg,
    const HwSwitch* hwSwitch,
    const std::vector<PortID>& uplinkPorts,
    const std::vector<PortID>& downllinkPorts) {
  const int mmuCellBytes = hwSwitch->getPlatform()->getMMUCellBytes();
  enablePfcConfig(cfg, uplinkPorts, "uplinks");
  enablePfcConfig(cfg, downllinkPorts, "downlinks");
  enablePfcMapsConfig(cfg);

  std::map<std::string, bool> pgProfileMap;
  // create pg profile with specific names, also specify if we want
  // to use uplink or downlink profiles for it
  pgProfileMap.insert({"uplinks", true});
  pgProfileMap.insert({"downlinks", false});
  enablePgConfigConfig(cfg, mmuCellBytes, pgProfileMap);
  enableBufferPoolConfig(cfg, mmuCellBytes, hwSwitch);
}

}; // namespace facebook::fboss::utility
