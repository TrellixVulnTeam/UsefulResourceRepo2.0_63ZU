/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/platforms/sai/SaiPlatform.h"

#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/hw/HwSwitchWarmBootHelper.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiBcmGalaxyPlatformPort.h"
#include "fboss/agent/platforms/sai/SaiBcmMinipackPlatformPort.h"
#include "fboss/agent/platforms/sai/SaiBcmPlatformPort.h"
#include "fboss/agent/platforms/sai/SaiBcmWedge100PlatformPort.h"
#include "fboss/agent/platforms/sai/SaiBcmWedge400PlatformPort.h"
#include "fboss/agent/platforms/sai/SaiBcmWedge40PlatformPort.h"
#include "fboss/agent/platforms/sai/SaiBcmYampPlatformPort.h"
#include "fboss/agent/platforms/sai/SaiCloudRipperPlatformPort.h"
#include "fboss/agent/platforms/sai/SaiElbert8DDPhyPlatformPort.h"
#include "fboss/agent/platforms/sai/SaiFakePlatformPort.h"
#include "fboss/agent/platforms/sai/SaiWedge400CPlatformPort.h"
#include "fboss/agent/state/Port.h"
#include "fboss/lib/config/PlatformConfigUtils.h"
#include "fboss/qsfp_service/lib/QsfpCache.h"

#include "fboss/agent/hw/sai/switch/SaiHandler.h"

namespace {

std::unordered_map<std::string, std::string> kSaiProfileValues;

const char* saiProfileGetValue(
    sai_switch_profile_id_t /*profile_id*/,
    const char* variable) {
  auto saiProfileValItr = kSaiProfileValues.find(variable);
  return saiProfileValItr != kSaiProfileValues.end()
      ? saiProfileValItr->second.c_str()
      : nullptr;
}

int saiProfileGetNextValue(
    sai_switch_profile_id_t /* profile_id */,
    const char** variable,
    const char** value) {
  static auto saiProfileValItr = kSaiProfileValues.begin();
  if (!value) {
    saiProfileValItr = kSaiProfileValues.begin();
    return 0;
  }
  if (saiProfileValItr == kSaiProfileValues.end()) {
    return -1;
  }
  *variable = saiProfileValItr->first.c_str();
  *value = saiProfileValItr->second.c_str();
  ++saiProfileValItr;
  return 0;
}

sai_service_method_table_t kSaiServiceMethodTable = {
    .profile_get_value = saiProfileGetValue,
    .profile_get_next_value = saiProfileGetNextValue,
};

using namespace facebook::fboss;
SaiSwitchTraits::Attributes::HwInfo getHwInfo(SaiPlatform* platform) {
  std::vector<int8_t> connectionHandle;
  auto connStr = platform->getPlatformAttribute(
      cfg::PlatformAttributes::CONNECTION_HANDLE);
  if (connStr.has_value()) {
    std::copy(
        connStr->c_str(),
        connStr->c_str() + connStr->size() + 1,
        std::back_inserter(connectionHandle));
  }
  return connectionHandle;
}
} // namespace

namespace facebook::fboss {

SaiPlatform::SaiPlatform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    std::unique_ptr<PlatformMapping> platformMapping,
    folly::MacAddress localMac)
    : Platform(std::move(productInfo), std::move(platformMapping), localMac),
      qsfpCache_(std::make_unique<AutoInitQsfpCache>()) {
  const auto& portsByMasterPort =
      utility::getSubsidiaryPortIDs(getPlatformPorts());
  CHECK(portsByMasterPort.size() > 1);
  for (auto itPort : portsByMasterPort) {
    masterLogicalPortIds_.push_back(itPort.first);
  }
}

SaiPlatform::~SaiPlatform() {}

void SaiPlatform::updatePorts(const StateDelta& delta) {
  for (const auto& entry : delta.getPortsDelta()) {
    const auto newPort = entry.getNew();
    if (newPort) {
      getPort(newPort->getID())->portChanged(newPort, entry.getOld());
    }
  }
}

HwSwitch* SaiPlatform::getHwSwitch() const {
  return saiSwitch_.get();
}

void SaiPlatform::onHwInitialized(SwSwitch* sw) {
  initLEDs();
  sw->registerStateObserver(this, "SaiPlatform");
}

void SaiPlatform::updateQsfpCache(const StateDelta& delta) {
  QsfpCache::PortMapThrift changedPorts;
  auto portsDelta = delta.getPortsDelta();
  for (const auto& entry : portsDelta) {
    auto port = entry.getNew();
    if (port) {
      auto platformPort = getPort(port->getID());
      PortStatus portStatus;
      *portStatus.enabled_ref() = port->isEnabled();
      *portStatus.up_ref() = port->isUp();
      *portStatus.speedMbps_ref() = static_cast<int64_t>(port->getSpeed());
      portStatus.transceiverIdx_ref() =
          platformPort->getTransceiverMapping(port->getSpeed());
      changedPorts.insert(std::make_pair(port->getID(), portStatus));
    }
  }
  qsfpCache_->portsChanged(changedPorts);
}

void SaiPlatform::stateUpdated(const StateDelta& delta) {
  updatePorts(delta);
  updateQsfpCache(delta);
}

void SaiPlatform::onInitialConfigApplied(SwSwitch* /* sw */) {}

void SaiPlatform::stop() {}

std::unique_ptr<ThriftHandler> SaiPlatform::createHandler(SwSwitch* sw) {
  return std::make_unique<SaiHandler>(sw, saiSwitch_.get());
}

TransceiverIdxThrift SaiPlatform::getPortMapping(
    PortID portId,
    cfg::PortSpeed speed) const {
  return getPort(portId)->getTransceiverMapping(speed);
}

std::string SaiPlatform::getHwConfigDumpFile() {
  return getVolatileStateDir() + "/" + FLAGS_hw_config_file;
}

void SaiPlatform::generateHwConfigFile() {
  auto hwConfig = getHwConfig();
  if (!folly::writeFile(hwConfig, getHwConfigDumpFile().c_str())) {
    throw FbossError(errno, "failed to generate hw config file. write failed");
  }
}

void SaiPlatform::initSaiProfileValues() {
  kSaiProfileValues.insert(
      std::make_pair(SAI_KEY_INIT_CONFIG_FILE, getHwConfigDumpFile()));
  kSaiProfileValues.insert(std::make_pair(
      SAI_KEY_WARM_BOOT_READ_FILE, getWarmBootHelper()->warmBootDataPath()));
  kSaiProfileValues.insert(std::make_pair(
      SAI_KEY_WARM_BOOT_WRITE_FILE, getWarmBootHelper()->warmBootDataPath()));
  kSaiProfileValues.insert(std::make_pair(
      SAI_KEY_BOOT_TYPE, getWarmBootHelper()->canWarmBoot() ? "1" : "0"));
  kSaiProfileValues.insert(std::make_pair(SAI_KEY_BOOT_TYPE, "0"));
}

void SaiPlatform::initImpl(uint32_t hwFeaturesDesired) {
  initSaiProfileValues();
  SaiApiTable::getInstance()->queryApis(
      getServiceMethodTable(), getSupportedApiList());
  saiSwitch_ = std::make_unique<SaiSwitch>(this, hwFeaturesDesired);
  generateHwConfigFile();
}

void SaiPlatform::initPorts() {
  auto platformMode = getMode();
  for (auto& port : getPlatformPorts()) {
    std::unique_ptr<SaiPlatformPort> saiPort;
    PortID portId(port.first);
    if (platformMode == PlatformMode::WEDGE400C) {
      saiPort = std::make_unique<SaiWedge400CPlatformPort>(portId, this);
    } else if (platformMode == PlatformMode::CLOUDRIPPER) {
      saiPort = std::make_unique<SaiCloudRipperPlatformPort>(portId, this);
    } else if (platformMode == PlatformMode::WEDGE) {
      saiPort = std::make_unique<SaiBcmWedge40PlatformPort>(portId, this);
    } else if (platformMode == PlatformMode::WEDGE100) {
      saiPort = std::make_unique<SaiBcmWedge100PlatformPort>(portId, this);
    } else if (
        platformMode == PlatformMode::GALAXY_LC ||
        platformMode == PlatformMode::GALAXY_FC) {
      saiPort = std::make_unique<SaiBcmGalaxyPlatformPort>(portId, this);
    } else if (platformMode == PlatformMode::WEDGE400) {
      saiPort = std::make_unique<SaiBcmWedge400PlatformPort>(portId, this);
    } else if (platformMode == PlatformMode::MINIPACK) {
      saiPort = std::make_unique<SaiBcmMinipackPlatformPort>(portId, this);
    } else if (platformMode == PlatformMode::YAMP) {
      saiPort = std::make_unique<SaiBcmYampPlatformPort>(portId, this);
    } else if (platformMode == PlatformMode::ELBERT) {
      saiPort = std::make_unique<SaiElbert8DDPhyPlatformPort>(portId, this);
    } else {
      saiPort = std::make_unique<SaiFakePlatformPort>(portId, this);
    }
    portMapping_.insert(std::make_pair(portId, std::move(saiPort)));
  }
}

SaiPlatformPort* SaiPlatform::getPort(PortID id) const {
  auto saiPortIter = portMapping_.find(id);
  if (saiPortIter == portMapping_.end()) {
    throw FbossError("failed to find port: ", id);
  }
  return saiPortIter->second.get();
}

PlatformPort* SaiPlatform::getPlatformPort(PortID port) const {
  return getPort(port);
}

std::optional<std::string> SaiPlatform::getPlatformAttribute(
    cfg::PlatformAttributes platformAttribute) {
  auto& platform = *config()->thrift.platform_ref();

  if (auto platformSettings = platform.platformSettings_ref()) {
    auto platformIter = platformSettings->find(platformAttribute);
    if (platformIter == platformSettings->end()) {
      return std::nullopt;
    }
    return platformIter->second;
  } else {
    return std::nullopt;
  }
}

sai_service_method_table_t* SaiPlatform::getServiceMethodTable() const {
  return &kSaiServiceMethodTable;
}

HwSwitchWarmBootHelper* SaiPlatform::getWarmBootHelper() {
  if (!wbHelper_) {
    wbHelper_ = std::make_unique<HwSwitchWarmBootHelper>(
        0, getWarmBootDir(), "sai_adaptor_state_");
  }
  return wbHelper_.get();
}

QsfpCache* SaiPlatform::getQsfpCache() const {
  return qsfpCache_.get();
}

PortID SaiPlatform::findPortID(
    cfg::PortSpeed speed,
    std::vector<uint32_t> lanes) const {
  for (const auto& portMapping : portMapping_) {
    const auto& platformPort = portMapping.second;
    if (!platformPort->getProfileIDBySpeedIf(speed) ||
        platformPort->getHwPortLanes(speed) != lanes) {
      continue;
    }
    return platformPort->getPortID();
  }
  throw FbossError("platform port not found");
}

std::vector<phy::TxSettings> SaiPlatform::getPlatformPortTxSettings(
    PortID port,
    cfg::PortProfileID profile) {
  auto platformPort = getPlatformPort(port);
  CHECK(platformPort);
  const auto& iphyPinConfigs = platformPort->getIphyPinConfigs(profile);
  std::vector<phy::TxSettings> txSettings;
  for (auto& pinConfig : iphyPinConfigs) {
    auto tx = pinConfig.tx_ref();
    if (!tx) {
      continue;
    }
    txSettings.push_back(tx.value());
  }
  return txSettings;
}

std::vector<phy::RxSettings> SaiPlatform::getPlatformPortRxSettings(
    PortID port,
    cfg::PortProfileID profile) {
  auto platformPort = getPlatformPort(port);
  CHECK(platformPort);
  const auto& iphyPinConfigs = platformPort->getIphyPinConfigs(profile);
  std::vector<phy::RxSettings> rxSettings;
  for (auto& pinConfig : iphyPinConfigs) {
    auto rx = pinConfig.rx_ref();
    if (!rx) {
      continue;
    }
    rxSettings.push_back(rx.value());
  }
  return rxSettings;
}

std::vector<SaiPlatformPort*> SaiPlatform::getPortsWithTransceiverID(
    TransceiverID id) const {
  std::vector<SaiPlatformPort*> ports;
  for (const auto& port : portMapping_) {
    if (auto tcvrID = port.second->getTransceiverID()) {
      if (tcvrID == id &&
          port.second->getCurrentProfile() !=
              cfg::PortProfileID::PROFILE_DEFAULT) {
        ports.push_back(port.second.get());
      }
    }
  }
  return ports;
}

SaiSwitchTraits::CreateAttributes SaiPlatform::getSwitchAttributes(
    bool mandatoryOnly) {
  SaiSwitchTraits::Attributes::InitSwitch initSwitch(true);

  std::optional<SaiSwitchTraits::Attributes::HwInfo> hwInfo = getHwInfo(this);
  std::optional<SaiSwitchTraits::Attributes::SrcMac> srcMac;
  std::optional<SaiSwitchTraits::Attributes::MacAgingTime> macAgingTime;
  if (!mandatoryOnly) {
    srcMac = getLocalMac();
    macAgingTime = getDefaultMacAgingTime();
  }

  std::optional<SaiSwitchTraits::Attributes::AclFieldList> aclFieldList =
      getAclFieldList();

  std::optional<SaiSwitchTraits::Attributes::UseEcnThresholds> useEcnThresholds{
      std::nullopt};
  if (getAsic()->isSupported(HwAsic::Feature::SAI_ECN_WRED)) {
    useEcnThresholds = true;
  }

  return {
      initSwitch,
      hwInfo, // hardware info
      srcMac, // source mac
      std::nullopt, // shell
      std::nullopt, // ecmp hash v4
      std::nullopt, // ecmp hash v6
      std::nullopt, // lag hash v4
      std::nullopt, // lag hash v6
      std::nullopt, // ecmp hash seed
      std::nullopt, // lag hash seed
      std::nullopt, // ecmp hash algo
      std::nullopt, // lag hash algo
      std::nullopt, // restart warm
      std::nullopt, // qos dscp to tc map
      std::nullopt, // qos tc to queue map
      std::nullopt, // qos exp to tc map
      std::nullopt, // qos tc to exp map
      macAgingTime,
      std::nullopt, // ingress acl
      aclFieldList,
      std::nullopt, // tam object list
      useEcnThresholds,
      std::nullopt, // counter refresh interval
      std::nullopt, // Firmware path name
      std::nullopt, // Firmware load method
      std::nullopt, // Firmware load type
      std::nullopt, // Hardware access bus
      std::nullopt, // Platform context
      std::nullopt, // Switch profile id
      std::nullopt, // Switch id
      std::nullopt, // Max system cores
      std::nullopt, // System port config list
      std::nullopt, // Switch type
      std::nullopt, // Read function
      std::nullopt, // Write function
  };
}

uint32_t SaiPlatform::getDefaultMacAgingTime() const {
  static auto constexpr kL2AgeTimerSeconds = 300;
  return kL2AgeTimerSeconds;
}

const std::set<sai_api_t>& SaiPlatform::getDefaultSwitchAsicSupportedApis()
    const {
  // For now, most of the apis are supported for switch asic.
  // Therefore we can just reuse SaiApiTable::getFullApiList()
  // But in case in the future we have some special phy api which won't be
  // supported in the switch asic, we can also exclude those apis ad-hoc.
  static auto apis = SaiApiTable::getInstance()->getFullApiList();
  // Macsec is not currently supported in the broadcom sai sdk
  apis.erase(facebook::fboss::MacsecApi::ApiType);
  return apis;
}
const std::set<sai_api_t>& SaiPlatform::getDefaultPhyAsicSupportedApis() const {
  static const std::set<sai_api_t> kSupportedPhyApiList = {
      facebook::fboss::AclApi::ApiType,
      facebook::fboss::PortApi::ApiType,
      facebook::fboss::SwitchApi::ApiType,
  };
  return kSupportedPhyApiList;
}

const std::set<sai_api_t>& SaiPlatform::getSupportedApiList() const {
  return getDefaultSwitchAsicSupportedApis();
}

} // namespace facebook::fboss
