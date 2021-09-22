// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/qsfp_service/module/sff/Sff8472Module.h"

#include <string>
#include "fboss/agent/FbossError.h"
#include "fboss/lib/usb/TransceiverI2CApi.h"
#include "fboss/qsfp_service/module/TransceiverImpl.h"
#include "fboss/qsfp_service/module/sff/Sff8472FieldInfo.h"

#include <folly/logging/xlog.h>

#include <thrift/lib/cpp/util/EnumUtils.h>

namespace {}

namespace facebook {
namespace fboss {

enum Sff8472Pages {
  LOWER,
};

static std::map<Ethernet10GComplianceCode, MediaInterfaceCode>
    mediaInterfaceMapping = {
        {Ethernet10GComplianceCode::LR_10G, MediaInterfaceCode::LR_10G},
};

// As per SFF-8472
static Sff8472FieldInfo::Sff8472FieldMap sfpFields = {
    // Address A0h fields
    {Sff8472Field::IDENTIFIER,
     {TransceiverI2CApi::ADDR_QSFP, Sff8472Pages::LOWER, 0, 1}},
    {Sff8472Field::ETHERNET_10G_COMPLIANCE_CODE,
     {TransceiverI2CApi::ADDR_QSFP, Sff8472Pages::LOWER, 3, 1}},

    // Address A2h fields
    {Sff8472Field::ALARM_WARNING_THRESHOLDS,
     {TransceiverI2CApi::ADDR_QSFP_A2, Sff8472Pages::LOWER, 0, 40}},
};

void getSfpFieldAddress(
    Sff8472Field field,
    int& dataAddress,
    int& offset,
    int& length,
    uint8_t& transceiverI2CAddress) {
  auto info = Sff8472FieldInfo::getSff8472FieldAddress(sfpFields, field);
  dataAddress = info.dataAddress;
  offset = info.offset;
  length = info.length;
  transceiverI2CAddress = info.transceiverI2CAddress;
}

const uint8_t* Sff8472Module::getSfpValuePtr(
    int dataAddress,
    int offset,
    int length,
    uint8_t transceiverI2CAddress) const {
  /* if the cached values are not correct */
  if (!cacheIsValid()) {
    throw FbossError("Sfp is either not present or the data is not read");
  }
  if (dataAddress == Sff8472Pages::LOWER) {
    if (transceiverI2CAddress == TransceiverI2CApi::ADDR_QSFP) {
      CHECK_LE(offset + length, sizeof(a0LowerPage_));
      return (a0LowerPage_ + offset);
    } else if (transceiverI2CAddress == TransceiverI2CApi::ADDR_QSFP_A2) {
      CHECK_LE(offset + length, sizeof(a2LowerPage_));
      return (a2LowerPage_ + offset);
    }
  }
  throw FbossError(
      "Invalid Data Address 0x%d, transceiverI2CAddress 0x%d",
      dataAddress,
      transceiverI2CAddress);
}

void Sff8472Module::getSfpValue(
    int dataAddress,
    int offset,
    int length,
    uint8_t transceiverI2CAddress,
    uint8_t* data) const {
  const uint8_t* ptr =
      getSfpValuePtr(dataAddress, offset, length, transceiverI2CAddress);

  memcpy(data, ptr, length);
}

uint8_t Sff8472Module::getSettingsValue(Sff8472Field field, uint8_t mask)
    const {
  int offset;
  int length;
  int dataAddress;
  uint8_t transceiverI2CAddress;

  getSfpFieldAddress(field, dataAddress, offset, length, transceiverI2CAddress);
  const uint8_t* data =
      getSfpValuePtr(dataAddress, offset, length, transceiverI2CAddress);

  return data[0] & mask;
}

Sff8472Module::Sff8472Module(
    TransceiverManager* transceiverManager,
    std::unique_ptr<TransceiverImpl> qsfpImpl,
    unsigned int portsPerTransceiver)
    : QsfpModule(transceiverManager, std::move(qsfpImpl), portsPerTransceiver) {
}

Sff8472Module::~Sff8472Module() {}

void Sff8472Module::updateQsfpData(bool allPages) {
  // expects the lock to be held
  if (!present_) {
    return;
  }
  try {
    XLOG(DBG2) << "Performing " << ((allPages) ? "full" : "partial")
               << " sfp data cache refresh for transceiver "
               << folly::to<std::string>(qsfpImpl_->getName());
    qsfpImpl_->readTransceiver(
        TransceiverI2CApi::ADDR_QSFP_A2, 0, sizeof(a2LowerPage_), a2LowerPage_);
    lastRefreshTime_ = std::time(nullptr);
    dirty_ = false;

    if (!allPages) {
      // Only address A2h lower page0 has diagnostic data that changes very
      // often. Avoid reading the static data frequently.
      return;
    }

    qsfpImpl_->readTransceiver(
        TransceiverI2CApi::ADDR_QSFP, 0, sizeof(a0LowerPage_), a0LowerPage_);
  } catch (const std::exception& ex) {
    // No matter what kind of exception throws, we need to set the dirty_ flag
    // to true.
    dirty_ = true;
    XLOG(ERR) << "Error update data for transceiver:"
              << folly::to<std::string>(qsfpImpl_->getName()) << ": "
              << ex.what();
    throw;
  }
}

TransceiverSettings Sff8472Module::getTransceiverSettingsInfo() {
  TransceiverSettings settings = TransceiverSettings();
  settings.mediaInterface_ref() =
      std::vector<MediaInterfaceId>(numMediaLanes());
  if (!getMediaInterfaceId(*(settings.mediaInterface_ref()))) {
    settings.mediaInterface_ref().reset();
  }

  return settings;
}

bool Sff8472Module::getMediaInterfaceId(
    std::vector<MediaInterfaceId>& mediaInterface) {
  CHECK_EQ(mediaInterface.size(), numMediaLanes());

  // TODO: Read this from the module
  auto ethernet10GCompliance = getEthernet10GComplianceCode();
  for (int lane = 0; lane < mediaInterface.size(); lane++) {
    mediaInterface[lane].lane_ref() = lane;
    MediaInterfaceUnion media;
    media.ethernet10GComplianceCode_ref() = ethernet10GCompliance;
    if (auto it = mediaInterfaceMapping.find(ethernet10GCompliance);
        it != mediaInterfaceMapping.end()) {
      mediaInterface[lane].code_ref() = it->second;
    } else {
      XLOG(ERR) << folly::sformat(
          "Module {:s}, Unable to find MediaInterfaceCode for {:s}",
          qsfpImpl_->getName(),
          apache::thrift::util::enumNameSafe(ethernet10GCompliance));
      mediaInterface[lane].code_ref() = MediaInterfaceCode::UNKNOWN;
    }
    mediaInterface[lane].media_ref() = media;
  }

  return true;
}

Ethernet10GComplianceCode Sff8472Module::getEthernet10GComplianceCode() const {
  return Ethernet10GComplianceCode(
      getSettingsValue(Sff8472Field::ETHERNET_10G_COMPLIANCE_CODE));
}

} // namespace fboss
} // namespace facebook
