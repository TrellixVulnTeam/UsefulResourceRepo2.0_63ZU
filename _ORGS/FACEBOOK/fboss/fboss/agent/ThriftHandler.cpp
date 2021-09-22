/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/ThriftHandler.h"

#include "common/logging/logging.h"
#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/ArpHandler.h"
#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/IPv6Handler.h"
#include "fboss/agent/LinkAggregationManager.h"
#include "fboss/agent/LldpManager.h"
#include "fboss/agent/NeighborUpdater.h"
#include "fboss/agent/RouteUpdateLogger.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/capture/PktCapture.h"
#include "fboss/agent/capture/PktCaptureManager.h"
#include "fboss/agent/hw/mock/MockRxPacket.h"
#include "fboss/agent/if/gen-cpp2/NeighborListenerClient.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/rib/ForwardingInformationBaseUpdater.h"
#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/AggregatePortMap.h"
#include "fboss/agent/state/ArpEntry.h"
#include "fboss/agent/state/ArpTable.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/LabelForwardingEntry.h"
#include "fboss/agent/state/NdpEntry.h"
#include "fboss/agent/state/NdpTable.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortQueue.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Transceiver.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/VlanMap.h"
#include "fboss/lib/LogThriftCall.h"
#include "fboss/lib/config/PlatformConfigUtils.h"

#include <fb303/ServiceData.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/MoveWrapper.h>
#include <folly/Range.h>
#include <folly/container/F14Map.h>
#include <folly/functional/Partial.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/json_pointer.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <thrift/lib/cpp2/async/DuplexChannel.h>
#include <memory>

#include <limits>

using apache::thrift::ClientReceiveState;
using apache::thrift::server::TConnectionContext;
using facebook::fb303::cpp2::fb_status;
using folly::fbstring;
using folly::IOBuf;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;
using folly::StringPiece;
using folly::io::RWPrivateCursor;
using std::make_unique;
using std::map;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;
using std::chrono::duration_cast;
using std::chrono::seconds;
using std::chrono::steady_clock;

using facebook::network::toAddress;
using facebook::network::toBinaryAddress;
using facebook::network::toIPAddress;

using namespace facebook::fboss;

DEFINE_bool(
    enable_running_config_mutations,
    false,
    "Allow external mutations of running config");

namespace facebook::fboss {

namespace util {

/**
 * Utility function to convert `Nexthops` (resolved ones) to list<BinaryAddress>
 */
std::vector<network::thrift::BinaryAddress> fromFwdNextHops(
    RouteNextHopSet const& nexthops) {
  std::vector<network::thrift::BinaryAddress> nhs;
  nhs.reserve(nexthops.size());
  for (auto const& nexthop : nexthops) {
    auto addr = network::toBinaryAddress(nexthop.addr());
    addr.ifName_ref() = util::createTunIntfName(nexthop.intf());
    nhs.emplace_back(std::move(addr));
  }
  return nhs;
}

} // namespace util

} // namespace facebook::fboss

namespace {

void fillPortStats(PortInfoThrift& portInfo, int numPortQs) {
  auto portId = *portInfo.portId_ref();
  auto statMap = facebook::fb303::fbData->getStatMap();

  auto getSumStat = [&](StringPiece prefix, StringPiece name) {
    auto portName = portInfo.name_ref()->empty()
        ? folly::to<std::string>("port", portId)
        : *portInfo.name_ref();
    auto statName = folly::to<std::string>(portName, ".", prefix, name);
    auto statPtr = statMap->getStatPtrNoExport(statName);
    auto lockedStatPtr = statPtr->lock();
    auto numLevels = lockedStatPtr->numLevels();
    // Cumulative (ALLTIME) counters are at (numLevels - 1)
    return lockedStatPtr->sum(numLevels - 1);
  };

  auto fillPortCounters = [&](PortCounters& ctr, StringPiece prefix) {
    *ctr.bytes_ref() = getSumStat(prefix, "bytes");
    *ctr.ucastPkts_ref() = getSumStat(prefix, "unicast_pkts");
    *ctr.multicastPkts_ref() = getSumStat(prefix, "multicast_pkts");
    *ctr.broadcastPkts_ref() = getSumStat(prefix, "broadcast_pkts");
    *ctr.errors_ref()->errors_ref() = getSumStat(prefix, "errors");
    *ctr.errors_ref()->discards_ref() = getSumStat(prefix, "discards");
  };

  fillPortCounters(*portInfo.output_ref(), "out_");
  fillPortCounters(*portInfo.input_ref(), "in_");
  for (int i = 0; i < numPortQs; i++) {
    auto queue = folly::to<std::string>("queue", i, ".");
    QueueStats stats;
    *stats.congestionDiscards_ref() =
        getSumStat(queue, "out_congestion_discards_bytes");
    *stats.outBytes_ref() = getSumStat(queue, "out_bytes");
    portInfo.output_ref()->unicast_ref()->push_back(stats);
  }
}

void getPortInfoHelper(
    const SwSwitch& sw,
    PortInfoThrift& portInfo,
    const std::shared_ptr<Port> port) {
  *portInfo.portId_ref() = port->getID();
  *portInfo.name_ref() = port->getName();
  *portInfo.description_ref() = port->getDescription();
  *portInfo.speedMbps_ref() = static_cast<int>(port->getSpeed());
  for (auto entry : port->getVlans()) {
    portInfo.vlans_ref()->push_back(entry.first);
  }

  std::shared_ptr<QosPolicy> qosPolicy;
  auto state = sw.getState();
  if (port->getQosPolicy().has_value()) {
    auto appliedPolicyName = port->getQosPolicy();
    qosPolicy =
        *appliedPolicyName == state->getDefaultDataPlaneQosPolicy()->getName()
        ? state->getDefaultDataPlaneQosPolicy()
        : state->getQosPolicy(*appliedPolicyName);
    if (!qosPolicy) {
      throw std::runtime_error("qosPolicy state is null");
    }
  }

  for (const auto& queue : port->getPortQueues()) {
    PortQueueThrift pq;
    *pq.id_ref() = queue->getID();
    *pq.mode_ref() =
        apache::thrift::TEnumTraits<cfg::QueueScheduling>::findName(
            queue->getScheduling());
    if (queue->getScheduling() ==
        facebook::fboss::cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN) {
      pq.weight_ref() = queue->getWeight();
    }
    if (queue->getReservedBytes()) {
      pq.reservedBytes_ref() = queue->getReservedBytes().value();
    }
    if (queue->getScalingFactor()) {
      pq.scalingFactor_ref() =
          apache::thrift::TEnumTraits<cfg::MMUScalingFactor>::findName(
              queue->getScalingFactor().value());
    }
    if (!queue->getAqms().empty()) {
      std::vector<ActiveQueueManagement> aqms;
      for (const auto& aqm : queue->getAqms()) {
        ActiveQueueManagement aqmThrift;
        switch (aqm.second.detection.getType()) {
          case facebook::fboss::cfg::QueueCongestionDetection::Type::linear:
            aqmThrift.detection_ref()->linear_ref() =
                LinearQueueCongestionDetection();
            *aqmThrift.detection_ref()->linear_ref()->minimumLength_ref() =
                aqm.second.detection.get_linear().minimumLength;
            *aqmThrift.detection_ref()->linear_ref()->maximumLength_ref() =
                aqm.second.detection.get_linear().maximumLength;
            break;
          case facebook::fboss::cfg::QueueCongestionDetection::Type::__EMPTY__:
            XLOG(WARNING) << "Invalid queue congestion detection config";
            break;
        }
        *aqmThrift.behavior_ref() = QueueCongestionBehavior(aqm.first);
        aqms.push_back(aqmThrift);
      }
      pq.aqms_ref() = {};
      pq.aqms_ref()->swap(aqms);
    }
    if (queue->getName()) {
      *pq.name_ref() = queue->getName().value();
    }

    if (queue->getPortQueueRate().has_value()) {
      if (queue->getPortQueueRate().value().getType() ==
          cfg::PortQueueRate::Type::pktsPerSec) {
        Range range;
        range.minimum_ref() =
            *queue->getPortQueueRate().value().get_pktsPerSec().minimum_ref();
        range.maximum_ref() =
            *queue->getPortQueueRate().value().get_pktsPerSec().maximum_ref();
        PortQueueRate portQueueRate;
        portQueueRate.pktsPerSec_ref() = range;

        pq.portQueueRate_ref() = portQueueRate;
      } else if (
          queue->getPortQueueRate().value().getType() ==
          cfg::PortQueueRate::Type::kbitsPerSec) {
        Range range;
        range.minimum_ref() =
            *queue->getPortQueueRate().value().get_kbitsPerSec().minimum_ref();
        range.maximum_ref() =
            *queue->getPortQueueRate().value().get_kbitsPerSec().maximum_ref();
        PortQueueRate portQueueRate;
        portQueueRate.kbitsPerSec_ref() = range;

        pq.portQueueRate_ref() = portQueueRate;
      }
    }

    if (queue->getBandwidthBurstMinKbits()) {
      pq.bandwidthBurstMinKbits_ref() =
          queue->getBandwidthBurstMinKbits().value();
    }

    if (queue->getBandwidthBurstMaxKbits()) {
      pq.bandwidthBurstMaxKbits_ref() =
          queue->getBandwidthBurstMaxKbits().value();
    }

    if (!port->getLookupClassesToDistributeTrafficOn().empty()) {
      // On MH-NIC setup, RSW downlinks implement queue-pe-host.
      // For such configurations traffic goes to queue corresponding
      // to host regardless of DSCP value
      auto kMaxDscp = 64;
      std::vector<signed char> dscps(kMaxDscp);
      std::iota(dscps.begin(), dscps.end(), 0);
      pq.dscps_ref() = dscps;
    } else if (qosPolicy) {
      std::vector<signed char> dscps;
      auto tcToDscp = qosPolicy->getDscpMap().from();
      auto tcToQueueId = qosPolicy->getTrafficClassToQueueId();
      for (const auto& entry : tcToDscp) {
        if (tcToQueueId[entry.trafficClass()] == queue->getID()) {
          dscps.push_back(entry.attr());
        }
      }
      pq.dscps_ref() = dscps;
    }

    portInfo.portQueues_ref()->push_back(pq);
  }

  *portInfo.adminState_ref() = PortAdminState(
      port->getAdminState() == facebook::fboss::cfg::PortState::ENABLED);
  *portInfo.operState_ref() =
      PortOperState(port->getOperState() == Port::OperState::UP);

  *portInfo.profileID_ref() =
      apache::thrift::util::enumName(port->getProfileID());

  if (port->isEnabled()) {
    const auto platformPort = sw.getPlatform()->getPlatformPort(port->getID());
    PortHardwareDetails hw;
    hw.profile_ref() = port->getProfileID();
    hw.profileConfig_ref() =
        platformPort->getPortProfileConfigFromCache(*hw.profile_ref());
    hw.pinConfig_ref() = platformPort->getPortPinConfigs(*hw.profile_ref());
    hw.chips_ref() = platformPort->getPortDataplaneChips(*hw.profile_ref());
    portInfo.hw_ref() = hw;

    auto fec = hw.profileConfig_ref()->iphy_ref()->fec_ref().value();
    portInfo.fecEnabled_ref() = fec != phy::FecMode::NONE;
    portInfo.fecMode_ref() = apache::thrift::util::enumName(fec);
  }

  auto pause = port->getPause();
  *portInfo.txPause_ref() = *pause.tx_ref();
  *portInfo.rxPause_ref() = *pause.rx_ref();

  if (port->getPfc().has_value()) {
    PfcConfig pc;
    auto pfc = port->getPfc();
    pc.tx_ref() = *pfc->tx_ref();
    pc.rx_ref() = *pfc->rx_ref();
    pc.watchdog_ref() = pfc->watchdog_ref().has_value();
    portInfo.pfc_ref() = pc;
  }
  try {
    portInfo.transceiverIdx_ref() =
        sw.getPlatform()->getPortMapping(port->getID(), port->getSpeed());
  } catch (const facebook::fboss::FbossError& err) {
    // No problem, we just don't set the other info
  }

  fillPortStats(portInfo, portInfo.portQueues_ref()->size());
}

LacpPortRateThrift fromLacpPortRate(facebook::fboss::cfg::LacpPortRate rate) {
  switch (rate) {
    case facebook::fboss::cfg::LacpPortRate::SLOW:
      return LacpPortRateThrift::SLOW;
    case facebook::fboss::cfg::LacpPortRate::FAST:
      return LacpPortRateThrift::FAST;
  }
  throw FbossError("Unknown LACP port rate: ", rate);
}

LacpPortActivityThrift fromLacpPortActivity(
    facebook::fboss::cfg::LacpPortActivity activity) {
  switch (activity) {
    case facebook::fboss::cfg::LacpPortActivity::ACTIVE:
      return LacpPortActivityThrift::ACTIVE;
    case facebook::fboss::cfg::LacpPortActivity::PASSIVE:
      return LacpPortActivityThrift::PASSIVE;
  }
  throw FbossError("Unknown LACP port activity: ", activity);
}

void populateAggregatePortThrift(
    const std::shared_ptr<AggregatePort>& aggregatePort,
    AggregatePortThrift& aggregatePortThrift) {
  *aggregatePortThrift.key_ref() =
      static_cast<uint32_t>(aggregatePort->getID());
  *aggregatePortThrift.name_ref() = aggregatePort->getName();
  *aggregatePortThrift.description_ref() = aggregatePort->getDescription();
  *aggregatePortThrift.systemPriority_ref() =
      aggregatePort->getSystemPriority();
  *aggregatePortThrift.systemID_ref() = aggregatePort->getSystemID().toString();
  *aggregatePortThrift.minimumLinkCount_ref() =
      aggregatePort->getMinimumLinkCount();
  *aggregatePortThrift.isUp_ref() = aggregatePort->isUp();

  // Since aggregatePortThrift.memberPorts is being push_back'ed to, but is an
  // out parameter, make sure it's clear() first
  aggregatePortThrift.memberPorts_ref()->clear();

  aggregatePortThrift.memberPorts_ref()->reserve(
      aggregatePort->subportsCount());

  for (const auto& subport : aggregatePort->sortedSubports()) {
    bool isEnabled = aggregatePort->getForwardingState(subport.portID) ==
        AggregatePort::Forwarding::ENABLED;
    AggregatePortMemberThrift aggPortMember;
    *aggPortMember.memberPortID_ref() = static_cast<int32_t>(subport.portID),
    *aggPortMember.isForwarding_ref() = isEnabled,
    *aggPortMember.priority_ref() = static_cast<int32_t>(subport.priority),
    *aggPortMember.rate_ref() = fromLacpPortRate(subport.rate),
    *aggPortMember.activity_ref() = fromLacpPortActivity(subport.activity);
    aggregatePortThrift.memberPorts_ref()->push_back(aggPortMember);
  }
}

AclEntryThrift populateAclEntryThrift(const AclEntry& aclEntry) {
  AclEntryThrift aclEntryThrift;
  *aclEntryThrift.priority_ref() = aclEntry.getPriority();
  *aclEntryThrift.name_ref() = aclEntry.getID();
  *aclEntryThrift.srcIp_ref() = toBinaryAddress(aclEntry.getSrcIp().first);
  *aclEntryThrift.srcIpPrefixLength_ref() = aclEntry.getSrcIp().second;
  *aclEntryThrift.dstIp_ref() = toBinaryAddress(aclEntry.getDstIp().first);
  *aclEntryThrift.dstIpPrefixLength_ref() = aclEntry.getDstIp().second;
  *aclEntryThrift.actionType_ref() =
      aclEntry.getActionType() == facebook::fboss::cfg::AclActionType::DENY
      ? "deny"
      : "permit";
  if (aclEntry.getProto()) {
    aclEntryThrift.proto_ref() = aclEntry.getProto().value();
  }
  if (aclEntry.getSrcPort()) {
    aclEntryThrift.srcPort_ref() = aclEntry.getSrcPort().value();
  }
  if (aclEntry.getDstPort()) {
    aclEntryThrift.dstPort_ref() = aclEntry.getDstPort().value();
  }
  if (aclEntry.getIcmpCode()) {
    aclEntryThrift.icmpCode_ref() = aclEntry.getIcmpCode().value();
  }
  if (aclEntry.getIcmpType()) {
    aclEntryThrift.icmpType_ref() = aclEntry.getIcmpType().value();
  }
  if (aclEntry.getDscp()) {
    aclEntryThrift.dscp_ref() = aclEntry.getDscp().value();
  }
  if (aclEntry.getTtl()) {
    aclEntryThrift.ttl_ref() = aclEntry.getTtl().value().getValue();
  }
  if (aclEntry.getL4SrcPort()) {
    aclEntryThrift.l4SrcPort_ref() = aclEntry.getL4SrcPort().value();
  }
  if (aclEntry.getL4DstPort()) {
    aclEntryThrift.l4DstPort_ref() = aclEntry.getL4DstPort().value();
  }
  if (aclEntry.getDstMac()) {
    aclEntryThrift.dstMac_ref() = aclEntry.getDstMac().value().toString();
  }
  return aclEntryThrift;
}

LinkNeighborThrift thriftLinkNeighbor(
    const SwSwitch& sw,
    const LinkNeighbor& n,
    steady_clock::time_point now) {
  LinkNeighborThrift tn;
  *tn.localPort_ref() = n.getLocalPort();
  *tn.localVlan_ref() = n.getLocalVlan();
  *tn.srcMac_ref() = n.getMac().toString();
  *tn.chassisIdType_ref() = static_cast<int32_t>(n.getChassisIdType());
  *tn.chassisId_ref() = n.getChassisId();
  *tn.printableChassisId_ref() = n.humanReadableChassisId();
  *tn.portIdType_ref() = static_cast<int32_t>(n.getPortIdType());
  *tn.portId_ref() = n.getPortId();
  *tn.printablePortId_ref() = n.humanReadablePortId();
  *tn.originalTTL_ref() = duration_cast<seconds>(n.getTTL()).count();
  *tn.ttlSecondsLeft_ref() =
      duration_cast<seconds>(n.getExpirationTime() - now).count();
  if (!n.getSystemName().empty()) {
    tn.systemName_ref() = n.getSystemName();
  }
  if (!n.getSystemDescription().empty()) {
    tn.systemDescription_ref() = n.getSystemDescription();
  }
  if (!n.getPortDescription().empty()) {
    tn.portDescription_ref() = n.getPortDescription();
  }
  const auto port = sw.getState()->getPorts()->getPortIf(n.getLocalPort());
  if (port) {
    tn.localPortName_ref() = port->getName();
  }
  return tn;
}
template <typename AddrT>
IpPrefix getIpPrefix(const Route<AddrT>& route) {
  IpPrefix pfx;
  pfx.ip_ref() = toBinaryAddress(route.prefix().network);
  pfx.prefixLength_ref() = route.prefix().mask;
  return pfx;
}

void translateToFibError(const FbossHwUpdateError& updError) {
  StateDelta delta(updError.appliedState, updError.desiredState);
  FbossFibUpdateError fibError;
  forEachChangedRoute(
      delta,
      [&](RouterID rid, const auto& removed, const auto& added) {
        if (!removed->isSame(added.get())) {
          fibError.vrf2failedAddUpdatePrefixes_ref()[rid].push_back(
              getIpPrefix(*added));
        }
      },
      [&](RouterID rid, const auto& added) {
        fibError.vrf2failedAddUpdatePrefixes_ref()[rid].push_back(
            getIpPrefix(*added));
      },
      [&](RouterID rid, const auto& removed) {
        fibError.vrf2failedDeletePrefixes_ref()[rid].push_back(
            getIpPrefix(*removed));
      });

  DeltaFunctions::forEachChanged(
      delta.getLabelForwardingInformationBaseDelta(),
      [&](const auto& removed, const auto& added) {
        if (!(*added == *removed)) {
          fibError.failedAddUpdateMplsLabels_ref()->push_back(added->getID());
        }
      },
      [&](const auto& added) {
        fibError.failedAddUpdateMplsLabels_ref()->push_back(added->getID());
      },
      [&](const auto& removed) {
        fibError.failedDeleteMplsLabels_ref()->push_back(removed->getID());
      });
  throw fibError;
}
cfg::PortLoopbackMode toLoopbackMode(PortLoopbackMode mode) {
  switch (mode) {
    case PortLoopbackMode::NONE:
      return cfg::PortLoopbackMode::NONE;
    case PortLoopbackMode::MAC:
      return cfg::PortLoopbackMode::MAC;
    case PortLoopbackMode::PHY:
      return cfg::PortLoopbackMode::PHY;
  }
  throw FbossError("Bogus loopback mode: ", mode);
}
PortLoopbackMode toThriftLoopbackMode(cfg::PortLoopbackMode mode) {
  switch (mode) {
    case cfg::PortLoopbackMode::NONE:
      return PortLoopbackMode::NONE;
    case cfg::PortLoopbackMode::MAC:
      return PortLoopbackMode::MAC;
    case cfg::PortLoopbackMode::PHY:
      return PortLoopbackMode::PHY;
  }
  throw FbossError("Bogus loopback mode: ", mode);
}
} // namespace

namespace facebook::fboss {

class RouteUpdateStats {
 public:
  RouteUpdateStats(SwSwitch* sw, const std::string& func, uint32_t routes)
      : sw_(sw),
        func_(func),
        routes_(routes),
        start_(std::chrono::steady_clock::now()) {}
  ~RouteUpdateStats() {
    auto end = std::chrono::steady_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
    sw_->stats()->routeUpdate(duration, routes_);
    XLOG(DBG0) << func_ << " " << routes_ << " routes took " << duration.count()
               << "us";
  }

 private:
  SwSwitch* sw_;
  const std::string func_;
  uint32_t routes_;
  std::chrono::time_point<std::chrono::steady_clock> start_;
};

ThriftHandler::ThriftHandler(SwSwitch* sw) : FacebookBase2("FBOSS"), sw_(sw) {
  if (sw) {
    sw->registerNeighborListener([=](const std::vector<std::string>& added,
                                     const std::vector<std::string>& deleted) {
      for (auto& listener : listeners_.accessAllThreads()) {
        XLOG(INFO) << "Sending notification to bgpD";
        auto listenerPtr = &listener;
        listener.eventBase->runInEventBaseThread([=] {
          XLOG(INFO) << "firing off notification";
          invokeNeighborListeners(listenerPtr, added, deleted);
        });
      }
    });
  }
}

fb_status ThriftHandler::getStatus() {
  if (sw_->isExiting()) {
    return fb_status::STOPPING;
  }

  auto bootType = sw_->getBootType();
  switch (bootType) {
    case BootType::UNINITIALIZED:
      return fb_status::STARTING;
    case BootType::COLD_BOOT:
      return (sw_->isFullyConfigured()) ? fb_status::ALIVE
                                        : fb_status::STARTING;
    case BootType::WARM_BOOT:
      return (sw_->isFullyInitialized()) ? fb_status::ALIVE
                                         : fb_status::STARTING;
  }

  throw FbossError("Unknown bootType", bootType);
}

void ThriftHandler::async_tm_getStatus(ThriftCallback<fb_status> callback) {
  callback->result(getStatus());
}

void ThriftHandler::flushCountersNow() {
  auto log = LOG_THRIFT_CALL(DBG1);
  // Currently SwSwitch only contains thread local stats.
  //
  // Depending on how we design the HW-specific stats interface,
  // we may also need to make a separate call to force immediate collection of
  // hardware stats.
  fb303::ThreadCachedServiceData::get()->publishStats();
}

void ThriftHandler::addUnicastRouteInVrf(
    int16_t client,
    std::unique_ptr<UnicastRoute> route,
    int32_t vrf) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL(DBG1, clientName);
  auto routes = std::make_unique<std::vector<UnicastRoute>>();
  routes->emplace_back(std::move(*route));
  addUnicastRoutesInVrf(client, std::move(routes), vrf);
}

void ThriftHandler::addUnicastRoute(
    int16_t client,
    std::unique_ptr<UnicastRoute> route) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL(DBG1, clientName);
  addUnicastRouteInVrf(client, std::move(route), 0);
}

void ThriftHandler::deleteUnicastRouteInVrf(
    int16_t client,
    std::unique_ptr<IpPrefix> prefix,
    int32_t vrf) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL(DBG1, clientName);
  auto prefixes = std::make_unique<std::vector<IpPrefix>>();
  prefixes->emplace_back(std::move(*prefix));
  deleteUnicastRoutesInVrf(client, std::move(prefixes), vrf);
}

void ThriftHandler::deleteUnicastRoute(
    int16_t client,
    std::unique_ptr<IpPrefix> prefix) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL(DBG1, clientName);
  deleteUnicastRouteInVrf(client, std::move(prefix), 0);
}

void ThriftHandler::addUnicastRoutesInVrf(
    int16_t client,
    std::unique_ptr<std::vector<UnicastRoute>> routes,
    int32_t vrf) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL(DBG1, clientName);
  ensureConfigured(__func__);
  updateUnicastRoutesImpl(vrf, client, routes, "addUnicastRoutesInVrf", false);
}

void ThriftHandler::addUnicastRoutes(
    int16_t client,
    std::unique_ptr<std::vector<UnicastRoute>> routes) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL(DBG1, clientName);
  ensureConfigured(__func__);
  addUnicastRoutesInVrf(client, std::move(routes), 0);
}

void ThriftHandler::getProductInfo(ProductInfo& productInfo) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  sw_->getProductInfo(productInfo);
}

void ThriftHandler::deleteUnicastRoutesInVrf(
    int16_t client,
    std::unique_ptr<std::vector<IpPrefix>> prefixes,
    int32_t vrf) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL(DBG1, clientName);
  ensureConfigured(__func__);

  auto updater = sw_->getRouteUpdater();
  auto routerID = RouterID(vrf);
  auto clientID = ClientID(client);
  for (const auto& prefix : *prefixes) {
    updater.delRoute(routerID, prefix, clientID);
  }
  updater.program();
}

void ThriftHandler::deleteUnicastRoutes(
    int16_t client,
    std::unique_ptr<std::vector<IpPrefix>> prefixes) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL(DBG1, clientName);
  ensureConfigured(__func__);
  deleteUnicastRoutesInVrf(client, std::move(prefixes), 0);
}

void ThriftHandler::syncFibInVrf(
    int16_t client,
    std::unique_ptr<std::vector<UnicastRoute>> routes,
    int32_t vrf) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);

  // Only route updates in first syncFib for each client are logged
  auto firstClientSync =
      syncedFibClients.find(client) == syncedFibClients.end();
  auto clientId = static_cast<ClientID>(client);
  auto clientName = apache::thrift::TEnumTraits<ClientID>::findName(clientId);
  auto clientIdentifier =
      "fboss-agent-warmboot-" + (clientName ? string(clientName) : "DEFAULT");
  if (firstClientSync && sw_->getBootType() == BootType::WARM_BOOT) {
    sw_->logRouteUpdates("::", 0, clientIdentifier);
    sw_->logRouteUpdates("0.0.0.0", 0, clientIdentifier);
  }
  SCOPE_EXIT {
    if (firstClientSync && sw_->getBootType() == BootType::WARM_BOOT) {
      sw_->stopLoggingRouteUpdates(clientIdentifier);
    }
  };
  updateUnicastRoutesImpl(vrf, client, routes, "syncFibInVrf", true);

  if (firstClientSync) {
    sw_->setFibSyncTimeForClient(clientId);
  }

  syncedFibClients.emplace(client);
}

void ThriftHandler::syncFib(
    int16_t client,
    std::unique_ptr<std::vector<UnicastRoute>> routes) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  syncFibInVrf(client, std::move(routes), 0);
}

void ThriftHandler::updateUnicastRoutesImpl(
    int32_t vrf,
    int16_t client,
    const std::unique_ptr<std::vector<UnicastRoute>>& routes,
    const std::string& updType,
    bool sync) {
  auto updater = sw_->getRouteUpdater();
  auto routerID = RouterID(vrf);
  auto clientID = ClientID(client);
  for (const auto& route : *routes) {
    updater.addRoute(routerID, clientID, route);
  }
  RouteUpdateWrapper::SyncFibFor syncFibs;

  if (sync) {
    syncFibs.insert({routerID, clientID});
  }
  try {
    updater.program(syncFibs);
  } catch (const FbossHwUpdateError& ex) {
    translateToFibError(ex);
  }
}

static void populateInterfaceDetail(
    InterfaceDetail& interfaceDetail,
    const std::shared_ptr<Interface> intf) {
  *interfaceDetail.interfaceName_ref() = intf->getName();
  *interfaceDetail.interfaceId_ref() = intf->getID();
  *interfaceDetail.vlanId_ref() = intf->getVlanID();
  *interfaceDetail.routerId_ref() = intf->getRouterID();
  *interfaceDetail.mtu_ref() = intf->getMtu();
  *interfaceDetail.mac_ref() = intf->getMac().toString();
  interfaceDetail.address_ref()->clear();
  interfaceDetail.address_ref()->reserve(intf->getAddresses().size());
  for (const auto& addrAndMask : intf->getAddresses()) {
    IpPrefix temp;
    temp.ip = toBinaryAddress(addrAndMask.first);
    temp.prefixLength = addrAndMask.second;
    interfaceDetail.address_ref()->push_back(temp);
  }
}

void ThriftHandler::getAllInterfaces(
    std::map<int32_t, InterfaceDetail>& interfaces) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  for (const auto& intf : (*sw_->getState()->getInterfaces())) {
    auto& interfaceDetail = interfaces[intf->getID()];
    populateInterfaceDetail(interfaceDetail, intf);
  }
}

void ThriftHandler::getInterfaceList(std::vector<std::string>& interfaceList) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  for (const auto& intf : (*sw_->getState()->getInterfaces())) {
    interfaceList.push_back(intf->getName());
  }
}

void ThriftHandler::getInterfaceDetail(
    InterfaceDetail& interfaceDetail,
    int32_t interfaceId) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  const auto& intf = sw_->getState()->getInterfaces()->getInterfaceIf(
      InterfaceID(interfaceId));

  if (!intf) {
    throw FbossError("no such interface ", interfaceId);
  }
  populateInterfaceDetail(interfaceDetail, intf);
}

void ThriftHandler::getNdpTable(std::vector<NdpEntryThrift>& ndpTable) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto entries = sw_->getNeighborUpdater()->getNdpCacheData().get();
  ndpTable.reserve(entries.size());
  ndpTable.insert(
      ndpTable.begin(),
      std::make_move_iterator(std::begin(entries)),
      std::make_move_iterator(std::end(entries)));
}

void ThriftHandler::getArpTable(std::vector<ArpEntryThrift>& arpTable) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto entries = sw_->getNeighborUpdater()->getArpCacheData().get();
  arpTable.reserve(entries.size());
  arpTable.insert(
      arpTable.begin(),
      std::make_move_iterator(std::begin(entries)),
      std::make_move_iterator(std::end(entries)));
}

void ThriftHandler::getL2Table(std::vector<L2EntryThrift>& l2Table) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  sw_->getHw()->fetchL2Table(&l2Table);
  XLOG(DBG6) << "L2 Table size:" << l2Table.size();
}

void ThriftHandler::getAclTable(std::vector<AclEntryThrift>& aclTable) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  aclTable.reserve(sw_->getState()->getAcls()->numEntries());
  for (const auto& aclEntry : *(sw_->getState()->getAcls())) {
    aclTable.push_back(populateAclEntryThrift(*aclEntry));
  }
}

void ThriftHandler::getAggregatePort(
    AggregatePortThrift& aggregatePortThrift,
    int32_t aggregatePortIDThrift) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);

  if (aggregatePortIDThrift < 0 ||
      aggregatePortIDThrift > std::numeric_limits<uint16_t>::max()) {
    throw FbossError(
        "AggregatePort ID ", aggregatePortIDThrift, " is out of range");
  }
  auto aggregatePortID = static_cast<AggregatePortID>(aggregatePortIDThrift);

  auto aggregatePort =
      sw_->getState()->getAggregatePorts()->getAggregatePortIf(aggregatePortID);

  if (!aggregatePort) {
    throw FbossError(
        "AggregatePort with ID ", aggregatePortIDThrift, " not found");
  }

  populateAggregatePortThrift(aggregatePort, aggregatePortThrift);
}

void ThriftHandler::getAggregatePortTable(
    std::vector<AggregatePortThrift>& aggregatePortsThrift) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);

  // Since aggregatePortsThrift is being push_back'ed to, but is an out
  // parameter, make sure it's clear() first
  aggregatePortsThrift.clear();

  aggregatePortsThrift.reserve(sw_->getState()->getAggregatePorts()->size());

  for (const auto& aggregatePort : *(sw_->getState()->getAggregatePorts())) {
    aggregatePortsThrift.emplace_back();

    populateAggregatePortThrift(aggregatePort, aggregatePortsThrift.back());
  }
}

void ThriftHandler::getPortInfo(PortInfoThrift& portInfo, int32_t portId) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);

  const auto port = sw_->getState()->getPorts()->getPortIf(PortID(portId));
  if (!port) {
    throw FbossError("no such port ", portId);
  }

  getPortInfoHelper(*sw_, portInfo, port);
}

void ThriftHandler::getAllPortInfo(map<int32_t, PortInfoThrift>& portInfoMap) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);

  // NOTE: important to take pointer to switch state before iterating over
  // list of ports
  std::shared_ptr<SwitchState> swState = sw_->getState();
  for (const auto& port : *(swState->getPorts())) {
    auto portId = port->getID();
    auto& portInfo = portInfoMap[portId];
    getPortInfoHelper(*sw_, portInfo, port);
  }
}

void ThriftHandler::clearPortStats(unique_ptr<vector<int32_t>> ports) {
  auto log = LOG_THRIFT_CALL(DBG1, *ports);
  ensureConfigured(__func__);
  sw_->clearPortStats(ports);

  auto getPortCounterKeys = [&](std::vector<std::string>& portKeys,
                                const StringPiece prefix,
                                const std::shared_ptr<Port> port) {
    auto portId = port->getID();
    auto portName = port->getName().empty()
        ? folly::to<std::string>("port", portId)
        : port->getName();
    auto portNameWithPrefix = folly::to<std::string>(portName, ".", prefix);
    portKeys.emplace_back(
        folly::to<std::string>(portNameWithPrefix, "unicast_pkts"));
    portKeys.emplace_back(folly::to<std::string>(portNameWithPrefix, "bytes"));
    portKeys.emplace_back(
        folly::to<std::string>(portNameWithPrefix, "multicast_pkts"));
    portKeys.emplace_back(
        folly::to<std::string>(portNameWithPrefix, "broadcast_pkts"));
    portKeys.emplace_back(folly::to<std::string>(portNameWithPrefix, "errors"));
    portKeys.emplace_back(
        folly::to<std::string>(portNameWithPrefix, "discards"));
  };

  auto getQueueCounterKeys = [&](std::vector<std::string>& portKeys,
                                 const std::shared_ptr<Port> port) {
    auto portId = port->getID();
    auto portName = port->getName().empty()
        ? folly::to<std::string>("port", portId)
        : port->getName();
    for (int i = 0; i < port->getPortQueues().size(); ++i) {
      auto portQueue = folly::to<std::string>(portName, ".", "queue", i, ".");
      portKeys.emplace_back(
          folly::to<std::string>(portQueue, "out_congestion_discards_bytes"));
      portKeys.emplace_back(folly::to<std::string>(portQueue, "out_bytes"));
    }
  };

  auto getPortPfcCounterKeys = [&](std::vector<std::string>& portKeys,
                                   const std::shared_ptr<Port> port) {
    auto portId = port->getID();
    auto portName = port->getName().empty()
        ? folly::to<std::string>("port", portId)
        : port->getName();
    auto portNameExt = folly::to<std::string>(portName, ".");
    std::array<int, 2> enabledPfcPriorities_{0, 7};
    for (auto pri : enabledPfcPriorities_) {
      portKeys.emplace_back(
          folly::to<std::string>(portNameExt, "in_pfc_frames.priority", pri));
      portKeys.emplace_back(folly::to<std::string>(
          portNameExt, "in_pfc_xon_frames.priority", pri));
      portKeys.emplace_back(
          folly::to<std::string>(portNameExt, "out_pfc_frames.priority", pri));
    }
    portKeys.emplace_back(folly::to<std::string>(portNameExt, "in_pfc_frames"));
    portKeys.emplace_back(
        folly::to<std::string>(portNameExt, "out_pfc_frames"));
  };

  auto getPortLinkStateCounterKey = [&](std::vector<std::string>& portKeys,
                                        const std::shared_ptr<Port> port) {
    auto portId = port->getID();
    auto portName = port->getName().empty()
        ? folly::to<std::string>("port", portId)
        : port->getName();
    portKeys.emplace_back(
        folly::to<std::string>(portName, ".", "link_state.flap"));
  };

  auto getLinkStateCounterKey = [&](std::vector<std::string>& globalKeys) {
    globalKeys.emplace_back("link_state.flap");
  };

  auto statsMap = facebook::fb303::fbData->getStatMap();
  for (const auto& portId : *ports) {
    const auto port = sw_->getState()->getPorts()->getPortIf(PortID(portId));
    std::vector<std::string> portKeys;
    getPortCounterKeys(portKeys, "out_", port);
    getPortCounterKeys(portKeys, "in_", port);
    getQueueCounterKeys(portKeys, port);
    getPortLinkStateCounterKey(portKeys, port);
    if (port->getPfc().has_value()) {
      getPortPfcCounterKeys(portKeys, port);
    }
    for (const auto& key : portKeys) {
      // this API locks statistics for the key
      // ensuring no race condition with update/delete
      // in different thread
      statsMap->clearValue(key);
    }
  }

  std::vector<std::string> globalKeys;
  getLinkStateCounterKey(globalKeys);
  for (const auto& key : globalKeys) {
    // this API locks statistics for the key
    // ensuring no race condition with update/delete
    // in different thread
    statsMap->clearValue(key);
  }
}

void ThriftHandler::getPortStats(PortInfoThrift& portInfo, int32_t portId) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  getPortInfo(portInfo, portId);
}

void ThriftHandler::getAllPortStats(map<int32_t, PortInfoThrift>& portInfoMap) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  getAllPortInfo(portInfoMap);
}

void ThriftHandler::getRunningConfig(std::string& configStr) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  configStr = sw_->getConfigStr();
}

void ThriftHandler::getCurrentStateJSON(
    std::string& ret,
    std::unique_ptr<std::string> jsonPointerStr) {
  auto log = LOG_THRIFT_CALL(DBG1, *jsonPointerStr);
  if (!jsonPointerStr) {
    return;
  }
  ensureConfigured(__func__);
  auto const jsonPtr = folly::json_pointer::try_parse(*jsonPointerStr);
  if (!jsonPtr) {
    throw FbossError("Malformed JSON Pointer");
  }
  auto swState = sw_->getState()->toFollyDynamic();
  auto dyn = swState.get_ptr(jsonPtr.value());
  ret = folly::json::serialize(*dyn, folly::json::serialization_opts{});
}

void ThriftHandler::patchCurrentStateJSON(
    std::unique_ptr<std::string> jsonPointerStr,
    std::unique_ptr<std::string> jsonPatchStr) {
  auto log = LOG_THRIFT_CALL(DBG1, *jsonPointerStr, *jsonPatchStr);
  if (!FLAGS_enable_running_config_mutations) {
    throw FbossError("Running config mutations are not allowed");
  }
  ensureConfigured(__func__);
  auto const jsonPtr = folly::json_pointer::try_parse(*jsonPointerStr);
  if (!jsonPtr) {
    throw FbossError("Malformed JSON Pointer");
  }
  // OK to capture by reference because the update call below is blocking
  auto updateFn = [&](const shared_ptr<SwitchState>& oldState) {
    auto fullDynamic = oldState->toFollyDynamic();
    auto* partialDynamic = fullDynamic.get_ptr(jsonPtr.value());
    if (!partialDynamic) {
      throw FbossError("JSON Pointer does not address proper object");
    }
    // mutates in place, i.e. modifies fullDynamic too
    partialDynamic->merge_patch(folly::parseJson(*jsonPatchStr));
    return SwitchState::fromFollyDynamic(fullDynamic);
  };
  sw_->updateStateBlocking("JSON patch", std::move(updateFn));
}

void ThriftHandler::getPortStatusImpl(
    std::map<int32_t, PortStatus>& statusMap,
    const std::unique_ptr<std::vector<int32_t>>& ports) const {
  ensureConfigured(__func__);
  if (ports->empty()) {
    statusMap = sw_->getPortStatus();
  } else {
    for (auto port : *ports) {
      statusMap[port] = sw_->getPortStatus(PortID(port));
    }
  }
}

void ThriftHandler::getPortStatus(
    map<int32_t, PortStatus>& statusMap,
    unique_ptr<vector<int32_t>> ports) {
  auto log = LOG_THRIFT_CALL(DBG1);
  getPortStatusImpl(statusMap, ports);
}

void ThriftHandler::clearPortPrbsStats(
    int32_t portId,
    PrbsComponent component) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  if (component == PrbsComponent::ASIC) {
    sw_->clearPortAsicPrbsStats(portId);
  } else if (
      component == PrbsComponent::GB_SYSTEM ||
      component == PrbsComponent::GB_LINE) {
    phy::Side side = (component == PrbsComponent::GB_SYSTEM) ? phy::Side::SYSTEM
                                                             : phy::Side::LINE;
    sw_->clearPortGearboxPrbsStats(portId, side);
  } else {
    XLOG(INFO) << "Unrecognized component to ClearPortPrbsStats: "
               << apache::thrift::util::enumNameSafe(component);
  }
}

void ThriftHandler::getPortPrbsStats(
    PrbsStats& prbsStats,
    int32_t portId,
    PrbsComponent component) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);

  if (component == PrbsComponent::ASIC) {
    auto asicPrbsStats = sw_->getPortAsicPrbsStats(portId);
    *prbsStats.portId_ref() = portId;
    *prbsStats.component_ref() = PrbsComponent::ASIC;
    for (const auto& lane : asicPrbsStats) {
      prbsStats.laneStats_ref()->push_back(lane);
    }
  } else if (
      component == PrbsComponent::GB_SYSTEM ||
      component == PrbsComponent::GB_LINE) {
    phy::Side side = (component == PrbsComponent::GB_SYSTEM) ? phy::Side::SYSTEM
                                                             : phy::Side::LINE;
    auto gearboxPrbsStats = sw_->getPortGearboxPrbsStats(portId, side);
    *prbsStats.portId_ref() = portId;
    *prbsStats.component_ref() = component;
    for (const auto& lane : gearboxPrbsStats) {
      prbsStats.laneStats_ref()->push_back(lane);
    }
  } else {
    XLOG(INFO) << "Unrecognized component to GetPortPrbsStats: "
               << apache::thrift::util::enumNameSafe(component);
  }
}

void ThriftHandler::setPortPrbs(
    int32_t portNum,
    PrbsComponent component,
    bool enable,
    int32_t polynominal) {
  auto log = LOG_THRIFT_CALL(DBG1, portNum, enable);
  ensureConfigured(__func__);
  PortID portId = PortID(portNum);
  const auto port = sw_->getState()->getPorts()->getPortIf(portId);
  if (!port) {
    throw FbossError("no such port ", portNum);
  }

  phy::PortPrbsState newPrbsState;
  *newPrbsState.enabled_ref() = enable;
  *newPrbsState.polynominal_ref() = polynominal;

  if (component == PrbsComponent::ASIC) {
    auto updateFn = [=](const shared_ptr<SwitchState>& state) {
      shared_ptr<SwitchState> newState{state};
      auto newPort = port->modify(&newState);
      newPort->setAsicPrbs(newPrbsState);
      return newState;
    };
    sw_->updateStateBlocking("set port asic prbs", updateFn);
  } else if (component == PrbsComponent::GB_SYSTEM) {
    auto updateFn = [=](const shared_ptr<SwitchState>& state) {
      shared_ptr<SwitchState> newState{state};
      auto newPort = port->modify(&newState);
      newPort->setGbSystemPrbs(newPrbsState);
      return newState;
    };
    sw_->updateStateBlocking("set port gearbox system side prbs", updateFn);
  } else if (component == PrbsComponent::GB_LINE) {
    auto updateFn = [=](const shared_ptr<SwitchState>& state) {
      shared_ptr<SwitchState> newState{state};
      auto newPort = port->modify(&newState);
      newPort->setGbLinePrbs(newPrbsState);
      return newState;
    };
    sw_->updateStateBlocking("set port gearbox line side prbs", updateFn);
  } else {
    XLOG(INFO) << "Unrecognized component to setPortPrbs: "
               << apache::thrift::util::enumNameSafe(component);
  }
}

void ThriftHandler::setPortState(int32_t portNum, bool enable) {
  auto log = LOG_THRIFT_CALL(DBG1, portNum, enable);
  ensureConfigured(__func__);
  PortID portId = PortID(portNum);
  const auto port = sw_->getState()->getPorts()->getPortIf(portId);
  if (!port) {
    throw FbossError("no such port ", portNum);
  }

  cfg::PortState newPortState =
      enable ? cfg::PortState::ENABLED : cfg::PortState::DISABLED;

  if (port->getAdminState() == newPortState) {
    XLOG(DBG2) << "setPortState: port already in state "
               << (enable ? "ENABLED" : "DISABLED");
    return;
  }

  auto updateFn = [portId, newPortState](const shared_ptr<SwitchState>& state) {
    const auto oldPort = state->getPorts()->getPortIf(portId);
    shared_ptr<SwitchState> newState{state};
    auto newPort = oldPort->modify(&newState);
    newPort->setAdminState(newPortState);
    return newState;
  };
  sw_->updateStateBlocking("set port state", updateFn);
}

void ThriftHandler::setPortLoopbackMode(
    int32_t portNum,
    PortLoopbackMode mode) {
  auto log = LOG_THRIFT_CALL(DBG1, portNum, mode);
  ensureConfigured(__func__);
  PortID portId = PortID(portNum);
  const auto port = sw_->getState()->getPorts()->getPortIf(portId);
  if (!port) {
    throw FbossError("no such port ", portNum);
  }

  auto newLoopbackMode = toLoopbackMode(mode);

  if (port->getLoopbackMode() == newLoopbackMode) {
    XLOG(DBG2) << "setPortState: port already set to lb mode : "
               << static_cast<int>(newLoopbackMode);
    return;
  }

  auto updateFn = [portId,
                   newLoopbackMode](const shared_ptr<SwitchState>& state) {
    const auto oldPort = state->getPorts()->getPortIf(portId);
    shared_ptr<SwitchState> newState{state};
    auto newPort = oldPort->modify(&newState);
    newPort->setLoopbackMode(newLoopbackMode);
    return newState;
  };
  sw_->updateStateBlocking("set port loopback mode", updateFn);
}

void ThriftHandler::getAllPortLoopbackMode(
    std::map<int32_t, PortLoopbackMode>& port2LbMode) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  for (auto& port : *sw_->getState()->getPorts()) {
    port2LbMode[port->getID()] = toThriftLoopbackMode(port->getLoopbackMode());
  }
}

void ThriftHandler::programInternalPhyPorts(
    std::map<int32_t, cfg::PortProfileID>& programmedPorts,
    std::unique_ptr<TransceiverInfo> transceiver,
    bool force) {
  int32_t id = *transceiver->port_ref();
  auto log = LOG_THRIFT_CALL(DBG1, id, force);
  ensureConfigured(__func__);

  // Check whether the transceiver has valid id
  std::optional<phy::DataPlanePhyChip> tcvrChip;
  for (const auto& chip : sw_->getPlatform()->getDataPlanePhyChips()) {
    if (*chip.second.type_ref() == phy::DataPlanePhyChipType::TRANSCEIVER &&
        *chip.second.physicalID_ref() == id) {
      tcvrChip = chip.second;
      break;
    }
  }
  if (!tcvrChip) {
    throw FbossError("Can't find transceiver:", id, " from PlatformMapping");
  }

  TransceiverID tcvrID = TransceiverID(id);
  std::shared_ptr<Transceiver> newTransceiver;
  if (*transceiver->present_ref()) {
    newTransceiver = std::make_shared<Transceiver>(tcvrID);
    if (transceiver->cable_ref() && transceiver->cable_ref()->length_ref()) {
      newTransceiver->setCableLength(*transceiver->cable_ref()->length_ref());
    }
    if (auto settings = transceiver->settings_ref();
        settings && settings->mediaInterface_ref()) {
      const auto& interface = (*settings->mediaInterface_ref())[0];
      newTransceiver->setMediaInterface(*interface.code_ref());
    }
    if (auto interface = transceiver->transceiverManagementInterface_ref()) {
      newTransceiver->setManagementInterface(*interface);
    }
  }

  const auto tcvr =
      sw_->getState()->getTransceivers()->getTransceiverIf(tcvrID);
  // Check whether the current Transceiver in the SwitchState matches the
  // input TransceiverInfo
  if (!tcvr && !newTransceiver) {
    XLOG(DBG2) << "programInternalPhyPorts for not present Transceiver:"
               << tcvrID
               << " which doesn't exist in SwitchState. Skip re-programming";
  } else if (!force && tcvr && newTransceiver && *tcvr == *newTransceiver) {
    XLOG(DBG2) << "programInternalPhyPorts for present Transceiver:" << tcvrID
               << " matches current SwitchState. Skip re-programming";
  } else {
    auto updateFn = [tcvrID,
                     newTransceiver](const shared_ptr<SwitchState>& state) {
      auto newState = state->clone();
      auto newTransceiverMap = newState->getTransceivers()->modify(&newState);
      if (!newTransceiver) {
        newTransceiverMap->removeTransceiver(tcvrID);
      } else if (newTransceiverMap->getTransceiverIf(tcvrID)) {
        newTransceiverMap->updateTransceiver(newTransceiver);
      } else {
        newTransceiverMap->addTransceiver(newTransceiver);
      }
      return newState;
    };
    sw_->updateStateBlocking(
        fmt::format("program iphy ports for transceiver: {}", id), updateFn);
  }

  const auto& platformPorts = utility::getPlatformPortsByChip(
      sw_->getPlatform()->getPlatformPorts(), *tcvrChip);
  // fetch the programmed profiles
  for (const auto& platformPort : platformPorts) {
    const auto port = sw_->getState()->getPorts()->getPortIf(
        PortID(*platformPort.mapping_ref()->id_ref()));
    if (port && port->isEnabled()) {
      // Only return ports actually exist and are enabled
      programmedPorts.emplace(port->getID(), port->getProfileID());
    }
  }
}

void ThriftHandler::getRouteTable(std::vector<UnicastRoute>& routes) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto state = sw_->getState();
  forAllRoutes(state, [&routes](RouterID /*rid*/, const auto& route) {
    UnicastRoute tempRoute;
    if (!route->isResolved()) {
      XLOG(INFO) << "Skipping unresolved route: " << route->toFollyDynamic();
      return;
    }
    auto fwdInfo = route->getForwardInfo();
    tempRoute.dest_ref()->ip_ref() = toBinaryAddress(route->prefix().network);
    tempRoute.dest_ref()->prefixLength_ref() = route->prefix().mask;
    tempRoute.nextHopAddrs_ref() =
        util::fromFwdNextHops(fwdInfo.getNextHopSet());
    tempRoute.nextHops_ref() =
        util::fromRouteNextHopSet(fwdInfo.normalizedNextHops());
    if (fwdInfo.getCounterID().has_value()) {
      tempRoute.counterID_ref() = *fwdInfo.getCounterID();
    }
    routes.emplace_back(std::move(tempRoute));
  });
}

void ThriftHandler::getRouteTableByClient(
    std::vector<UnicastRoute>& routes,
    int16_t client) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto state = sw_->getState();
  forAllRoutes(state, [&routes, client](RouterID /*rid*/, const auto& route) {
    auto entry = route->getEntryForClient(ClientID(client));
    if (not entry) {
      return;
    }
    UnicastRoute tempRoute;
    tempRoute.dest_ref()->ip_ref() = toBinaryAddress(route->prefix().network);
    tempRoute.dest_ref()->prefixLength_ref() = route->prefix().mask;
    tempRoute.nextHops_ref() =
        util::fromRouteNextHopSet(entry->getNextHopSet());
    if (entry->getCounterID().has_value()) {
      tempRoute.counterID_ref() = *entry->getCounterID();
    }
    for (const auto& nh : *tempRoute.nextHops_ref()) {
      tempRoute.nextHopAddrs_ref()->emplace_back(*nh.address_ref());
    }
    routes.emplace_back(std::move(tempRoute));
  });
}

void ThriftHandler::getRouteTableDetails(std::vector<RouteDetails>& routes) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  forAllRoutes(sw_->getState(), [&routes](RouterID /*rid*/, const auto& route) {
    routes.emplace_back(route->toRouteDetails(true));
  });
}

void ThriftHandler::getIpRoute(
    UnicastRoute& route,
    std::unique_ptr<Address> addr,
    int32_t vrfId) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  folly::IPAddress ipAddr = toIPAddress(*addr);

  auto state = sw_->getState();
  if (ipAddr.isV4()) {
    auto match = sw_->longestMatch(state, ipAddr.asV4(), RouterID(vrfId));
    if (!match || !match->isResolved()) {
      route.dest.ip = toBinaryAddress(IPAddressV4("0.0.0.0"));
      route.dest.prefixLength = 0;
      return;
    }
    const auto fwdInfo = match->getForwardInfo();
    route.dest.ip = toBinaryAddress(match->prefix().network);
    route.dest.prefixLength = match->prefix().mask;
    *route.nextHopAddrs_ref() = util::fromFwdNextHops(fwdInfo.getNextHopSet());
    auto counterID = fwdInfo.getCounterID();
    if (counterID.has_value()) {
      route.counterID_ref() = *counterID;
    }
  } else {
    auto match = sw_->longestMatch(state, ipAddr.asV6(), RouterID(vrfId));
    if (!match || !match->isResolved()) {
      route.dest.ip = toBinaryAddress(IPAddressV6("::0"));
      route.dest.prefixLength = 0;
      return;
    }
    const auto fwdInfo = match->getForwardInfo();
    route.dest.ip = toBinaryAddress(match->prefix().network);
    route.dest.prefixLength = match->prefix().mask;
    *route.nextHopAddrs_ref() = util::fromFwdNextHops(fwdInfo.getNextHopSet());
    auto counterID = fwdInfo.getCounterID();
    if (counterID.has_value()) {
      route.counterID_ref() = *counterID;
    }
  }
}

void ThriftHandler::getIpRouteDetails(
    RouteDetails& route,
    std::unique_ptr<Address> addr,
    int32_t vrfId) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  folly::IPAddress ipAddr = toIPAddress(*addr);
  auto state = sw_->getState();

  if (ipAddr.isV4()) {
    auto match = sw_->longestMatch(state, ipAddr.asV4(), RouterID(vrfId));
    if (match && match->isResolved()) {
      route = match->toRouteDetails(true);
    }
  } else {
    auto match = sw_->longestMatch(state, ipAddr.asV6(), RouterID(vrfId));
    if (match && match->isResolved()) {
      route = match->toRouteDetails(true);
    }
  }
}

void ThriftHandler::getRouteCounterBytes(
    std::map<std::string, std::int64_t>& routeCounters,
    std::unique_ptr<std::vector<std::string>> counters) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto statMap = facebook::fb303::fbData->getStatMap();
  for (const auto& statName : *counters) {
    // returns default stat if statName does not exists
    auto statPtr = statMap->getStatPtrNoExport(statName);
    auto lockedStatPtr = statPtr->lock();
    auto numLevels = lockedStatPtr->numLevels();
    // Cumulative (ALLTIME) counters are at (numLevels - 1)
    auto value = lockedStatPtr->sum(numLevels - 1);
    routeCounters.insert(make_pair(statName, value));
  }
}

void ThriftHandler::getAllRouteCounterBytes(
    std::map<std::string, std::int64_t>& routeCounters) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto state = sw_->getState();
  std::unordered_set<std::string> countersUsed;
  forAllRoutes(state, [&countersUsed](RouterID /*rid*/, const auto& route) {
    if (route->isResolved()) {
      auto counterID = route->getForwardInfo().getCounterID();
      if (counterID.has_value()) {
        std::string statName = counterID.value();
        countersUsed.emplace(statName);
      }
    }
  });
  auto counters = std::make_unique<std::vector<std::string>>();
  for (const auto& counter : countersUsed) {
    counters->emplace_back(counter);
  }
  return getRouteCounterBytes(routeCounters, std::move(counters));
}

void ThriftHandler::getLldpNeighbors(vector<LinkNeighborThrift>& results) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto lldpMgr = sw_->getLldpMgr();
  if (lldpMgr == nullptr) {
    throw std::runtime_error("lldpMgr is not configured");
  }

  auto* db = lldpMgr->getDB();
  // Do an immediate check for expired neighbors
  db->pruneExpiredNeighbors();
  auto neighbors = db->getNeighbors();
  results.reserve(neighbors.size());
  auto now = steady_clock::now();
  for (const auto& entry : db->getNeighbors()) {
    results.push_back(thriftLinkNeighbor(*sw_, entry, now));
  }
}

void ThriftHandler::invokeNeighborListeners(
    ThreadLocalListener* listener,
    std::vector<std::string> added,
    std::vector<std::string> removed) {
  // Collect the iterators to avoid erasing and potentially reordering
  // the iterators in the list.
  for (const auto& ctx : brokenClients_) {
    listener->clients.erase(ctx);
  }
  brokenClients_.clear();
  for (auto& client : listener->clients) {
    auto clientDone = [&](ClientReceiveState&& state) {
      try {
        NeighborListenerClientAsyncClient::recv_neighborsChanged(state);
      } catch (const std::exception& ex) {
        XLOG(ERR) << "Exception in neighbor listener: " << ex.what();
        brokenClients_.push_back(client.first);
      }
    };
    client.second->neighborsChanged(clientDone, added, removed);
  }
}

void ThriftHandler::async_eb_registerForNeighborChanged(
    ThriftCallback<void> cb) {
  auto ctx = cb->getRequestContext()->getConnectionContext();
  auto client = ctx->getDuplexClient<NeighborListenerClientAsyncClient>();
  auto info = listeners_.get();
  CHECK(cb->getEventBase()->isInEventBaseThread());
  if (!info) {
    info = new ThreadLocalListener(cb->getEventBase());
    listeners_.reset(info);
  }
  DCHECK_EQ(info->eventBase, cb->getEventBase());
  if (!info->eventBase) {
    info->eventBase = cb->getEventBase();
  }
  info->clients.emplace(ctx, client);
  cb->done();
}

void ThriftHandler::startPktCapture(unique_ptr<CaptureInfo> info) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto* mgr = sw_->getCaptureMgr();
  auto capture = make_unique<PktCapture>(
      *info->name_ref(),
      *info->maxPackets_ref(),
      *info->direction_ref(),
      *info->filter_ref());
  mgr->startCapture(std::move(capture));
}

void ThriftHandler::stopPktCapture(unique_ptr<std::string> name) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto* mgr = sw_->getCaptureMgr();
  mgr->forgetCapture(*name);
}

void ThriftHandler::stopAllPktCaptures() {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto* mgr = sw_->getCaptureMgr();
  mgr->forgetAllCaptures();
}

void ThriftHandler::startLoggingRouteUpdates(
    std::unique_ptr<RouteUpdateLoggingInfo> info) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  folly::IPAddress addr = toIPAddress(info->prefix_ref()->ip);
  uint8_t mask = static_cast<uint8_t>(info->prefix_ref()->prefixLength);
  RouteUpdateLoggingInstance loggingInstance{
      RoutePrefix<folly::IPAddress>{addr, mask},
      *info->identifier_ref(),
      *info->exact_ref()};
  routeUpdateLogger->startLoggingForPrefix(loggingInstance);
}

void ThriftHandler::startLoggingMplsRouteUpdates(
    std::unique_ptr<MplsRouteUpdateLoggingInfo> info) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  routeUpdateLogger->startLoggingForLabel(
      *info->label_ref(), *info->identifier_ref());
}

void ThriftHandler::stopLoggingRouteUpdates(
    std::unique_ptr<IpPrefix> prefix,
    std::unique_ptr<std::string> identifier) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  folly::IPAddress addr = toIPAddress(prefix->ip);
  uint8_t mask = static_cast<uint8_t>(prefix->prefixLength);
  routeUpdateLogger->stopLoggingForPrefix(addr, mask, *identifier);
}

void ThriftHandler::stopLoggingAnyRouteUpdates(
    std::unique_ptr<std::string> identifier) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  routeUpdateLogger->stopLoggingForIdentifier(*identifier);
}

void ThriftHandler::stopLoggingAnyMplsRouteUpdates(
    std::unique_ptr<std::string> identifier) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  routeUpdateLogger->stopLabelLoggingForIdentifier(*identifier);
}

void ThriftHandler::stopLoggingMplsRouteUpdates(
    std::unique_ptr<MplsRouteUpdateLoggingInfo> info) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  routeUpdateLogger->stopLoggingForLabel(
      *info->label_ref(), *info->identifier_ref());
}

void ThriftHandler::getRouteUpdateLoggingTrackedPrefixes(
    std::vector<RouteUpdateLoggingInfo>& infos) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  for (const auto& tracked : routeUpdateLogger->getTrackedPrefixes()) {
    RouteUpdateLoggingInfo info;
    IpPrefix prefix;
    prefix.ip = toBinaryAddress(tracked.prefix.network);
    prefix.prefixLength = tracked.prefix.mask;
    *info.prefix_ref() = prefix;
    *info.identifier_ref() = tracked.identifier;
    *info.exact_ref() = tracked.exact;
    infos.push_back(info);
  }
}

void ThriftHandler::getMplsRouteUpdateLoggingTrackedLabels(
    std::vector<MplsRouteUpdateLoggingInfo>& infos) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  for (const auto& tracked : routeUpdateLogger->gettTrackedLabels()) {
    MplsRouteUpdateLoggingInfo info;
    *info.identifier_ref() = tracked.first;
    *info.label_ref() = tracked.second;
    infos.push_back(info);
  }
}

void ThriftHandler::sendPkt(
    int32_t port,
    int32_t vlan,
    unique_ptr<fbstring> data) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto buf = IOBuf::copyBuffer(
      reinterpret_cast<const uint8_t*>(data->data()), data->size());
  auto pkt = make_unique<MockRxPacket>(std::move(buf));
  pkt->setSrcPort(PortID(port));
  pkt->setSrcVlan(VlanID(vlan));
  sw_->packetReceived(std::move(pkt));
}

void ThriftHandler::sendPktHex(
    int32_t port,
    int32_t vlan,
    unique_ptr<fbstring> hex) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto pkt = MockRxPacket::fromHex(StringPiece(*hex));
  pkt->setSrcPort(PortID(port));
  pkt->setSrcVlan(VlanID(vlan));
  sw_->packetReceived(std::move(pkt));
}

void ThriftHandler::txPkt(int32_t port, unique_ptr<fbstring> data) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);

  unique_ptr<TxPacket> pkt = sw_->allocatePacket(data->size());
  RWPrivateCursor cursor(pkt->buf());
  cursor.push(StringPiece(*data));

  sw_->sendPacketOutOfPortAsync(std::move(pkt), PortID(port));
}

void ThriftHandler::txPktL2(unique_ptr<fbstring> data) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);

  unique_ptr<TxPacket> pkt = sw_->allocatePacket(data->size());
  RWPrivateCursor cursor(pkt->buf());
  cursor.push(StringPiece(*data));

  sw_->sendPacketSwitchedAsync(std::move(pkt));
}

void ThriftHandler::txPktL3(unique_ptr<fbstring> payload) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);

  unique_ptr<TxPacket> pkt = sw_->allocateL3TxPacket(payload->size());
  RWPrivateCursor cursor(pkt->buf());
  cursor.push(StringPiece(*payload));

  sw_->sendL3Packet(std::move(pkt));
}

Vlan* ThriftHandler::getVlan(int32_t vlanId) {
  ensureConfigured(__func__);
  return sw_->getState()->getVlans()->getVlan(VlanID(vlanId)).get();
}

Vlan* ThriftHandler::getVlan(const std::string& vlanName) {
  ensureConfigured(__func__);
  return sw_->getState()->getVlans()->getVlanSlow(vlanName).get();
}

int32_t ThriftHandler::flushNeighborEntry(
    unique_ptr<BinaryAddress> ip,
    int32_t vlan) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);

  auto parsedIP = toIPAddress(*ip);
  VlanID vlanID(vlan);
  return sw_->getNeighborUpdater()->flushEntry(vlanID, parsedIP).get();
}

void ThriftHandler::getVlanAddresses(Addresses& addrs, int32_t vlan) {
  auto log = LOG_THRIFT_CALL(DBG1);
  getVlanAddresses(getVlan(vlan), addrs, toAddress);
}

void ThriftHandler::getVlanAddressesByName(
    Addresses& addrs,
    unique_ptr<string> vlan) {
  auto log = LOG_THRIFT_CALL(DBG1);
  getVlanAddresses(getVlan(*vlan), addrs, toAddress);
}

void ThriftHandler::getVlanBinaryAddresses(
    BinaryAddresses& addrs,
    int32_t vlan) {
  auto log = LOG_THRIFT_CALL(DBG1);
  getVlanAddresses(getVlan(vlan), addrs, toBinaryAddress);
}

void ThriftHandler::getVlanBinaryAddressesByName(
    BinaryAddresses& addrs,
    const std::unique_ptr<std::string> vlan) {
  auto log = LOG_THRIFT_CALL(DBG1);
  getVlanAddresses(getVlan(*vlan), addrs, toBinaryAddress);
}

template <typename ADDR_TYPE, typename ADDR_CONVERTER>
void ThriftHandler::getVlanAddresses(
    const Vlan* vlan,
    std::vector<ADDR_TYPE>& addrs,
    ADDR_CONVERTER& converter) {
  ensureConfigured(__func__);
  CHECK(vlan);
  for (auto intf : (*sw_->getState()->getInterfaces())) {
    if (intf->getVlanID() == vlan->getID()) {
      for (const auto& addrAndMask : intf->getAddresses()) {
        addrs.push_back(converter(addrAndMask.first));
      }
    }
  }
}

BootType ThriftHandler::getBootType() {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  return sw_->getBootType();
}

void ThriftHandler::ensureConfigured(StringPiece function) const {
  if (sw_->isFullyConfigured()) {
    return;
  }

  if (!function.empty()) {
    XLOG(DBG1) << "failing thrift prior to switch configuration: " << function;
  }
  throw FbossError(
      "switch is still initializing or is exiting and is not "
      "fully configured yet");
}

// If this is a premature client disconnect from a duplex connection, we need to
// clean up state.  Failure to do so may allow the server's duplex clients to
// use the destroyed context => segfaults.
void ThriftHandler::connectionDestroyed(TConnectionContext* ctx) {
  // Port status notifications
  if (listeners_) {
    listeners_->clients.erase(ctx);
  }
}

int32_t ThriftHandler::getIdleTimeout() {
  auto log = LOG_THRIFT_CALL(DBG1);
  if (thriftIdleTimeout_ < 0) {
    throw FbossError("Idle timeout has not been set");
  }
  return thriftIdleTimeout_;
}

void ThriftHandler::reloadConfig() {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  return sw_->applyConfig("reload config initiated by thrift call", true);
}

void ThriftHandler::getLacpPartnerPair(
    LacpPartnerPair& lacpPartnerPair,
    int32_t portID) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);

  auto lagManager = sw_->getLagManager();
  if (!lagManager) {
    throw FbossError("LACP not enabled");
  }

  lagManager->populatePartnerPair(static_cast<PortID>(portID), lacpPartnerPair);
}

void ThriftHandler::getAllLacpPartnerPairs(
    std::vector<LacpPartnerPair>& lacpPartnerPairs) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);

  auto lagManager = sw_->getLagManager();
  if (!lagManager) {
    throw FbossError("LACP not enabled");
  }

  lagManager->populatePartnerPairs(lacpPartnerPairs);
}

SwitchRunState ThriftHandler::getSwitchRunState() {
  auto log = LOG_THRIFT_CALL(DBG3);
  ensureConfigured(__func__);
  return sw_->getSwitchRunState();
}

SSLType ThriftHandler::getSSLPolicy() {
  auto log = LOG_THRIFT_CALL(DBG1);
  SSLType sslType = SSLType::PERMITTED;

  if (sslPolicy_ == apache::thrift::SSLPolicy::DISABLED) {
    sslType = SSLType::DISABLED;
  } else if (sslPolicy_ == apache::thrift::SSLPolicy::PERMITTED) {
    sslType = SSLType::PERMITTED;
  } else if (sslPolicy_ == apache::thrift::SSLPolicy::REQUIRED) {
    sslType = SSLType::REQUIRED;
  } else {
    throw FbossError("Invalid SSL Policy");
  }

  return sslType;
}

void ThriftHandler::setExternalLedState(
    int32_t portNum,
    PortLedExternalState ledState) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  PortID portId = PortID(portNum);

  const auto plport = sw_->getPlatform()->getPlatformPort(portId);

  if (!plport) {
    throw FbossError("No such port ", portNum);
  }
  plport->externalState(ledState);
}

void ThriftHandler::addMplsRoutes(
    int16_t clientId,
    std::unique_ptr<std::vector<MplsRoute>> mplsRoutes) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(clientId));
  auto log = LOG_THRIFT_CALL(DBG1, clientName);
  ensureConfigured(__func__);
  auto updateFn = [=, routes = std::move(*mplsRoutes)](
                      const std::shared_ptr<SwitchState>& state) {
    auto newState = state->clone();

    addMplsRoutesImpl(&newState, ClientID(clientId), routes);
    if (!sw_->isValidStateUpdate(StateDelta(state, newState))) {
      throw FbossError("Invalid MPLS routes");
    }
    return newState;
  };
  try {
    sw_->updateStateWithHwFailureProtection("addMplsRoutes", updateFn);
  } catch (const FbossHwUpdateError& ex) {
    translateToFibError(ex);
  }
}

void ThriftHandler::addMplsRoutesImpl(
    std::shared_ptr<SwitchState>* state,
    ClientID clientId,
    const std::vector<MplsRoute>& mplsRoutes) const {
  /* cache to return interface for non-link local but directly connected next
   * hop address for label fib entry  */
  folly::F14FastMap<std::pair<RouterID, folly::IPAddress>, InterfaceID>
      labelFibEntryNextHopAddress2Interface;

  auto labelFib =
      (*state)->getLabelForwardingInformationBase().get()->modify(state);
  for (const auto& mplsRoute : mplsRoutes) {
    auto topLabel = mplsRoute.topLabel;
    if (topLabel > mpls_constants::MAX_MPLS_LABEL_) {
      throw FbossError("invalid value for label ", topLabel);
    }
    auto adminDistance = mplsRoute.adminDistance_ref().has_value()
        ? mplsRoute.adminDistance_ref().value()
        : sw_->clientIdToAdminDistance(static_cast<int>(clientId));
    // check for each next hop if these are resolved, if not resolve them
    // unresolved next hop must always be directly connected for MPLS
    // so unresolved next hops must be directly reachable via one of the
    // interface
    LabelNextHopSet nexthops;
    for (auto& nexthop : util::toRouteNextHopSet(*mplsRoute.nextHops_ref())) {
      if (nexthop.isResolved() || nexthop.isPopAndLookup()) {
        nexthops.emplace(nexthop);
        continue;
      }
      if (nexthop.addr().isV6() && nexthop.addr().isLinkLocal()) {
        throw FbossError(
            "v6 link-local nexthop: ",
            nexthop.addr().str(),
            " must have interface id");
      }
      // BGP leaks MPLS routes to OpenR which then sends routes to agent
      // In such routes, interface id information is absent, because neither
      // BGP nor OpenR has enough information (in different scenarios) to
      // resolve this interface ID. Consequently doing this in agent. Each such
      // unresolved next hop will always be in the subnet of one of the
      // interface routes. look for all interfaces of a router to find an
      // interface which can reach this next hop. searching interfaces of a
      // default router, in future if multiple routers are to be supported,
      // router id must either be part of MPLS route or some configured router
      // id for unresolved MPLS next hops.
      // router id of MPLS route is to be used ONLY for resolving unresolved
      // next hop addresses. MPLS has no notion of multiple switching domains
      // within the same switch, all the labels must be unique.
      // So router ID has no relevance to label switching.
      auto iter = labelFibEntryNextHopAddress2Interface.find(
          std::make_pair(RouterID(0), nexthop.addr()));
      if (iter == labelFibEntryNextHopAddress2Interface.end()) {
        auto result = (*state)->getInterfaces()->getIntfAddrToReach(
            RouterID(0), nexthop.addr());
        if (!result.intf) {
          throw FbossError(
              "nexthop : ", nexthop.addr().str(), " is not connected");
        }
        if (result.intf->hasAddress(nexthop.addr())) {
          // attempt to program local interface address as next hop
          throw FbossError(
              "invalid next hop, nexthop : ",
              nexthop.addr().str(),
              " is same as interface address");
        }

        std::tie(iter, std::ignore) =
            labelFibEntryNextHopAddress2Interface.emplace(
                std::make_pair<RouterID, folly::IPAddress>(
                    RouterID(0), nexthop.addr()),
                result.intf->getID());
      }

      nexthops.emplace(ResolvedNextHop(
          nexthop.addr(),
          iter->second,
          nexthop.weight(),
          nexthop.labelForwardingAction()));
    }

    // validate top label
    labelFib = labelFib->programLabel(
        state,
        topLabel,
        ClientID(clientId),
        adminDistance,
        std::move(nexthops));
  }
}

void ThriftHandler::deleteMplsRoutes(
    int16_t clientId,
    std::unique_ptr<std::vector<int32_t>> topLabels) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(clientId));
  auto log = LOG_THRIFT_CALL(DBG1, clientName);
  ensureConfigured(__func__);
  auto updateFn = [=, topLabels = std::move(*topLabels)](
                      const std::shared_ptr<SwitchState>& state) {
    auto newState = state->clone();
    auto labelFib = state->getLabelForwardingInformationBase().get();
    for (const auto topLabel : topLabels) {
      if (topLabel > mpls_constants::MAX_MPLS_LABEL_) {
        throw FbossError("invalid value for label ", topLabel);
      }
      labelFib =
          labelFib->unprogramLabel(&newState, topLabel, ClientID(clientId));
    }
    return newState;
  };
  sw_->updateStateBlocking("deleteMplsRoutes", updateFn);
}

void ThriftHandler::syncMplsFib(
    int16_t clientId,
    std::unique_ptr<std::vector<MplsRoute>> mplsRoutes) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto updateFn = [=, routes = std::move(*mplsRoutes)](
                      const std::shared_ptr<SwitchState>& state) {
    auto newState = state->clone();
    auto labelFib = newState->getLabelForwardingInformationBase();

    labelFib->purgeEntriesForClient(&newState, ClientID(clientId));
    addMplsRoutesImpl(&newState, ClientID(clientId), routes);
    if (!sw_->isValidStateUpdate(StateDelta(state, newState))) {
      throw FbossError("Invalid MPLS routes");
    }
    return newState;
  };
  try {
    sw_->updateStateWithHwFailureProtection("syncMplsFib", updateFn);
  } catch (const FbossHwUpdateError& ex) {
    translateToFibError(ex);
  }
}

void ThriftHandler::getMplsRouteTableByClient(
    std::vector<MplsRoute>& mplsRoutes,
    int16_t clientId) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  auto labelFib = sw_->getState()->getLabelForwardingInformationBase();
  for (const auto& entry : *labelFib) {
    auto* labelNextHopEntry = entry->getEntryForClient(ClientID(clientId));
    if (!labelNextHopEntry) {
      continue;
    }
    MplsRoute mplsRoute;
    mplsRoute.topLabel = entry->getID();
    mplsRoute.adminDistance_ref() = labelNextHopEntry->getAdminDistance();
    *mplsRoute.nextHops_ref() =
        util::fromRouteNextHopSet(labelNextHopEntry->getNextHopSet());
    mplsRoutes.emplace_back(std::move(mplsRoute));
  }
}

void ThriftHandler::getAllMplsRouteDetails(
    std::vector<MplsRouteDetails>& mplsRouteDetails) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  const auto labelFib = sw_->getState()->getLabelForwardingInformationBase();
  for (const auto& entry : *labelFib) {
    MplsRouteDetails details;
    getMplsRouteDetails(details, entry->getID());
    mplsRouteDetails.push_back(details);
  }
}

void ThriftHandler::getMplsRouteDetails(
    MplsRouteDetails& mplsRouteDetail,
    MplsLabel topLabel) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  const auto entry = sw_->getState()
                         ->getLabelForwardingInformationBase()
                         ->getLabelForwardingEntry(topLabel);
  *mplsRouteDetail.topLabel_ref() = entry->getID();
  *mplsRouteDetail.nextHopMulti_ref() =
      entry->getLabelNextHopsByClient().toThrift();
  const auto& fwd = entry->getLabelNextHop();
  for (const auto& nh : fwd.getNextHopSet()) {
    mplsRouteDetail.nextHops_ref()->push_back(nh.toThrift());
  }
  *mplsRouteDetail.adminDistance_ref() = fwd.getAdminDistance();
  *mplsRouteDetail.action_ref() = forwardActionStr(fwd.getAction());
}

void ThriftHandler::getHwDebugDump(std::string& out) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  out = sw_->getHw()->getDebugDump();
}

void ThriftHandler::getPlatformMapping(cfg::PlatformMapping& ret) {
  ret = sw_->getPlatform()->getPlatformMapping()->toThrift();
}

void ThriftHandler::listHwObjects(
    std::string& out,
    std::unique_ptr<std::vector<HwObjectType>> hwObjects,
    bool cached) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  out = sw_->getHw()->listObjects(*hwObjects, cached);
}
} // namespace facebook::fboss
