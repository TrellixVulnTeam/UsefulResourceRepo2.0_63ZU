// Copyright 2004-present Facebook. All Rights Reserved.
#include "fboss/qsfp_service/QsfpServiceHandler.h"

#include "fboss/agent/FbossError.h"

#include <fboss/lib/LogThriftCall.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

DEFINE_string(
    sak_list_warmboot_config,
    "/var/facebook/fboss/mka_service/sak_lists/",
    "path to store the physervice SAK config for service restart");
DEFINE_string(
    sak_list_warmboot_filename,
    "sak_config",
    "filename of warmbootconfig");

DEFINE_int64(
    sak_config_validity_in_secs,
    3600,
    "warmboot config validity in secs");

DEFINE_int32(
    phy_service_macsec_port,
    5910,
    "Port for the phy service thrift service");

namespace facebook {
namespace fboss {

template <typename Type>
static void valid(const Type& val) {
  if (!val) {
    throw FbossError("Invalid input");
  }
}

QsfpServiceHandler::QsfpServiceHandler(
    std::unique_ptr<TransceiverManager> manager,
    std::shared_ptr<mka::MacsecHandler> handler)
    : FacebookBase2("QsfpService"),
      manager_(std::move(manager)),
      macsecHandler_(handler) {
  XLOG(INFO) << "FbossPhyMacsecService inside QsfpServiceHandler Started";
}

void QsfpServiceHandler::init() {
  // Initialize the PhyManager all ExternalPhy for the system
  manager_->initExternalPhyMap();
  // Initialize the I2c bus
  manager_->initTransceiverMap();
}

facebook::fb303::cpp2::fb_status QsfpServiceHandler::getStatus() {
  return facebook::fb303::cpp2::fb_status::ALIVE;
}

TransceiverType QsfpServiceHandler::getType(int32_t /* unused */) {
  auto log = LOG_THRIFT_CALL(INFO);
  return TransceiverType::QSFP;
}

void QsfpServiceHandler::getTransceiverInfo(
    std::map<int32_t, TransceiverInfo>& info,
    std::unique_ptr<std::vector<int32_t>> ids) {
  auto log = LOG_THRIFT_CALL(INFO);
  manager_->getTransceiversInfo(info, std::move(ids));
}

void QsfpServiceHandler::customizeTransceiver(
    int32_t idx,
    cfg::PortSpeed speed) {
  auto log = LOG_THRIFT_CALL(INFO);
  XLOG(INFO) << "customizeTransceiver request for " << idx << " to speed "
             << apache::thrift::util::enumNameSafe(speed);
  manager_->customizeTransceiver(idx, speed);
}

void QsfpServiceHandler::getTransceiverRawDOMData(
    std::map<int32_t, RawDOMData>& info,
    std::unique_ptr<std::vector<int32_t>> ids) {
  auto log = LOG_THRIFT_CALL(INFO);
  manager_->getTransceiversRawDOMData(info, std::move(ids));
}

void QsfpServiceHandler::getTransceiverDOMDataUnion(
    std::map<int32_t, DOMDataUnion>& info,
    std::unique_ptr<std::vector<int32_t>> ids) {
  auto log = LOG_THRIFT_CALL(INFO);
  manager_->getTransceiversDOMDataUnion(info, std::move(ids));
}

void QsfpServiceHandler::syncPorts(
    std::map<int32_t, TransceiverInfo>& info,
    std::unique_ptr<std::map<int32_t, PortStatus>> ports) {
  auto log = LOG_THRIFT_CALL(INFO);
  manager_->syncPorts(info, std::move(ports));
}

void QsfpServiceHandler::pauseRemediation(int32_t timeout) {
  auto log = LOG_THRIFT_CALL(INFO);
  manager_->setPauseRemediation(timeout);
}

int32_t QsfpServiceHandler::getRemediationUntilTime() {
  auto log = LOG_THRIFT_CALL(INFO);
  return manager_->getPauseRemediationUntil();
}

void QsfpServiceHandler::readTransceiverRegister(
    std::map<int32_t, ReadResponse>& response,
    std::unique_ptr<ReadRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO);
  auto param = *(request->parameter_ref());
  auto offset = *(param.offset_ref());
  if (offset < 0 || offset > 255) {
    throw FbossError("Offset cannot be < 0 or > 255");
  }
  auto page_ref = param.page_ref();
  if (page_ref.has_value() && *page_ref < 0) {
    throw FbossError("Page cannot be < 0");
  }
  auto length_ref = param.length_ref();
  if (length_ref.has_value()) {
    if (*length_ref < 0 || *length_ref > 255) {
      throw FbossError("Length cannot be < 0 or > 255");
    } else if (*length_ref + offset > 256) {
      throw FbossError("Offset + Length cannot be > 256");
    }
  }
  manager_->readTransceiverRegister(response, std::move(request));
}

void QsfpServiceHandler::writeTransceiverRegister(
    std::map<int32_t, WriteResponse>& response,
    std::unique_ptr<WriteRequest> request) {
  auto log = LOG_THRIFT_CALL(INFO);
  auto param = *(request->parameter_ref());
  auto offset = *(param.offset_ref());
  if (offset < 0 || offset > 255) {
    throw FbossError("Offset cannot be < 0 or > 255");
  }
  auto page_ref = param.page_ref();
  if (page_ref.has_value() && *page_ref < 0) {
    throw FbossError("Page cannot be < 0");
  }
  manager_->writeTransceiverRegister(response, std::move(request));
}

void QsfpServiceHandler::programXphyPort(
    int32_t portId,
    cfg::PortProfileID portProfileId) {
  auto log = LOG_THRIFT_CALL(INFO);
  manager_->programXphyPort(portId, portProfileId);
}

void QsfpServiceHandler::getMacsecCapablePorts(std::vector<int32_t>& ports) {
  auto macsecPorts = manager_->getMacsecCapablePorts();
  std::for_each(macsecPorts.begin(), macsecPorts.end(), [&ports](auto portId) {
    ports.push_back(static_cast<int32_t>(portId));
  });
}

void QsfpServiceHandler::validateHandler() const {
  if (!macsecHandler_) {
    throw FbossError("Macsec handler not initialized");
  }
}

/* TODO(rajank): Move/enable this for mka warmboot support
void QsfpServiceHandler::updateWarmBootConfig() const {
  if (configFile_.empty()) {
    return;
  }
  if (!mka::MKASerializer::getInstance()->writeMKAActiveSakSessionSet(
          configFile_, sakSet_)) {
    XLOG(ERR) << "Failed to update SAK Warmboot Config at: " << configFile_;
  }
}
*/

#if FOLLY_HAS_COROUTINES

folly::coro::Task<bool> QsfpServiceHandler::co_sakInstallRx(
    std::unique_ptr<mka::MKASak> sak,
    std::unique_ptr<mka::MKASci> sciToAdd) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  valid<decltype(sak)>(sak);
  valid<decltype(sciToAdd)>(sciToAdd);
  bool ret = macsecHandler_->sakInstallRx(*sak, *sciToAdd);
  /* TODO(rajank): Move/enable this for mka warmboot support
  if (ret) {
    mka::MKAActiveSakSession sess;
    sess.sak_ref() = std::move(*sak);
    sess.sciList_ref()->emplace_back(std::move(*sciToAdd));
    sakSet_.items.emplace(std::move(sess));
    updateWarmBootConfig();
  }
  */
  co_return ret;
}

folly::coro::Task<bool> QsfpServiceHandler::co_sakInstallTx(
    std::unique_ptr<mka::MKASak> sak) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  valid<decltype(sak)>(sak);
  bool ret = macsecHandler_->sakInstallTx(*sak);
  /* TODO(rajank): Move/enable this for mka warmboot support
  if (ret) {
    updateWarmBootConfig();
  }
  */
  co_return ret;
}

folly::coro::Task<bool> QsfpServiceHandler::co_sakDeleteRx(
    std::unique_ptr<mka::MKASak> sak,
    std::unique_ptr<mka::MKASci> sciToRemove) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  valid<decltype(sak)>(sak);
  valid<decltype(sciToRemove)>(sciToRemove);
  bool ret = macsecHandler_->sakDeleteRx(*sak, *sciToRemove);
  /* TODO(rajank): Move/enable this for mka warmboot support
  if (ret) {
    mka::MKAActiveSakSession sess;
    sess.sak_ref() = std::move(*sak);
    std::unordered_set<mka::MKAActiveSakSession>::iterator iter =
        sakSet_.items.find(sess);
    if (iter == sakSet_.items.end()) {
      co_return ret;
    }
    // Expected copy. The unordered set values are const and can't be
    // modified, so we copy and replace it.
    std::vector<mka::MKASci> list = *iter->sciList_ref();
    for (auto sciIter = list.begin(); sciIter != list.end(); sciIter++) {
      auto& sci = *sciIter;
      if (*sci.macAddress_ref() == *sciToRemove->macAddress_ref() &&
          *sci.port_ref() == *sciToRemove->port_ref()) {
        list.erase(sciIter);
        break;
      }
    }
    sess.sciList_ref() = std::move(list);
    sakSet_.items.erase(iter);
    sakSet_.items.emplace(sess);
    updateWarmBootConfig();
  }
  */
  co_return ret;
}

folly::coro::Task<bool> QsfpServiceHandler::co_sakDelete(
    std::unique_ptr<mka::MKASak> sak) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  valid<decltype(sak)>(sak);
  bool ret = macsecHandler_->sakDelete(*sak);
  /* TODO(rajank): Move/enable this for mka warmboot support
  if (ret) {
    mka::MKAActiveSakSession sess;
    sess.sak_ref() = std::move(*sak);
    sakSet_.items.erase(sess);
    updateWarmBootConfig();
  }
  */
  co_return ret;
}

folly::coro::Task<std::unique_ptr<mka::MKASakHealthResponse>>
QsfpServiceHandler::co_sakHealthCheck(std::unique_ptr<mka::MKASak> sak) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  valid<decltype(sak)>(sak);
  // TODO:(shankaran) - when healthcheck fails remove the sak from set.
  co_return std::make_unique<mka::MKASakHealthResponse>(
      macsecHandler_->sakHealthCheck(*sak));
}

folly::coro::Task<std::unique_ptr<mka::MacsecPortPhyMap>>
QsfpServiceHandler::co_macsecGetPhyPortInfo() {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  co_return std::make_unique<mka::MacsecPortPhyMap>(
      macsecHandler_->macsecGetPhyPortInfo());
}

folly::coro::Task<PortOperState> QsfpServiceHandler::co_macsecGetPhyLinkInfo(
    std::unique_ptr<std::string> portName) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  co_return macsecHandler_->macsecGetPhyLinkInfo(*portName);
}

folly::coro::Task<bool> QsfpServiceHandler::co_deleteAllSc(
    std::unique_ptr<std::string> portName) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  co_return macsecHandler_->deleteAllSc(*portName);
}

folly::coro::Task<std::unique_ptr<mka::MacsecAllScInfo>>
QsfpServiceHandler::co_macsecGetAllScInfo(
    std::unique_ptr<std::string> portName) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  co_return std::make_unique<mka::MacsecAllScInfo>(
      macsecHandler_->macsecGetAllScInfo(*portName));
}

folly::coro::Task<std::unique_ptr<mka::MacsecPortStats>>
QsfpServiceHandler::co_macsecGetPortStats(
    std::unique_ptr<std::string> portName,
    bool directionIngress) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  co_return std::make_unique<mka::MacsecPortStats>(
      macsecHandler_->macsecGetPortStats(*portName, directionIngress));
}

folly::coro::Task<std::unique_ptr<mka::MacsecFlowStats>>
QsfpServiceHandler::co_macsecGetFlowStats(
    std::unique_ptr<std::string> portName,
    bool directionIngress) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  co_return std::make_unique<mka::MacsecFlowStats>(
      macsecHandler_->macsecGetFlowStats(*portName, directionIngress));
}

folly::coro::Task<std::unique_ptr<mka::MacsecSaStats>>
QsfpServiceHandler::co_macsecGetSaStats(
    std::unique_ptr<std::string> portName,
    bool directionIngress) {
  auto log = LOG_THRIFT_CALL(INFO);
  validateHandler();
  co_return std::make_unique<mka::MacsecSaStats>(
      macsecHandler_->macsecGetSaStats(*portName, directionIngress));
}

#endif

} // namespace fboss
} // namespace facebook
