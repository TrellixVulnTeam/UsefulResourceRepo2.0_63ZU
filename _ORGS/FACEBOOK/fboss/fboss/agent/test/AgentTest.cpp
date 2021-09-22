// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/agent/test/AgentTest.h"
#include <folly/gen/Base.h>
#include "fboss/agent/Main.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/qsfp_service/lib/QsfpClient.h"

namespace {
int argCount{0};
char** argVec{nullptr};
facebook::fboss::PlatformInitFn initPlatform{nullptr};
} // unnamed namespace

DEFINE_bool(setup_for_warmboot, false, "Set up test for warmboot");
DEFINE_bool(run_forever, false, "run the test forever");

namespace facebook::fboss {

void AgentTest::setupAgent() {
  AgentInitializer::createSwitch(
      argCount,
      argVec,
      (HwSwitch::FeaturesDesired::PACKET_RX_DESIRED |
       HwSwitch::FeaturesDesired::LINKSCAN_DESIRED),
      initPlatform);
  setupConfigFlag();
  setupFlags();
  asyncInitThread_.reset(
      new std::thread([this] { AgentInitializer::initAgent(); }));
  // Cannot join the thread because initAgent starts a thrift server in the main
  // thread and that runs for lifetime of the application.
  asyncInitThread_->detach();
  initializer()->waitForInitDone();
  XLOG(INFO) << "Agent has been setup and ready for the test";
}

void AgentTest::TearDown() {
  if (FLAGS_run_forever) {
    runForever();
  }
  AgentInitializer::stopAgent(FLAGS_setup_for_warmboot);
}

std::map<std::string, HwPortStats> AgentTest::getPortStats(
    const std::vector<std::string>& ports) const {
  auto allPortStats = sw()->getHw()->getPortStats();
  std::map<std::string, HwPortStats> portStats;
  std::for_each(ports.begin(), ports.end(), [&](const auto& portName) {
    portStats.insert({portName, allPortStats[portName]});
  });
  return portStats;
}

void AgentTest::resolveNeighbor(
    PortDescriptor portDesc,
    const folly::IPAddress& ip,
    folly::MacAddress mac) {
  // TODO support agg ports as well.
  CHECK(portDesc.isPhysicalPort());
  auto port = sw()->getState()->getPort(portDesc.phyPortID());
  auto vlan = port->getVlans().begin()->first;
  if (ip.isV4()) {
    resolveNeighbor(portDesc, ip.asV4(), vlan, mac);
  } else {
    resolveNeighbor(portDesc, ip.asV6(), vlan, mac);
  }
}

template <typename AddrT>
void AgentTest::resolveNeighbor(
    PortDescriptor port,
    const AddrT& ip,
    VlanID vlanId,
    folly::MacAddress mac) {
  auto resolveNeighborFn = [=](const std::shared_ptr<SwitchState>& state) {
    auto outputState{state->clone()};
    auto vlan = outputState->getVlans()->getVlan(vlanId);
    auto nbrTable = vlan->template getNeighborEntryTable<AddrT>()->modify(
        vlanId, &outputState);
    if (nbrTable->getEntryIf(ip)) {
      nbrTable->updateEntry(ip, mac, port, vlan->getInterfaceID());
    } else {
      nbrTable->addEntry(ip, mac, port, vlan->getInterfaceID());
    }
    return outputState;
  };
  sw()->updateStateBlocking("resolve nbr", resolveNeighborFn);
}

void AgentTest::runForever() const {
  XLOG(DBG2) << "AgentTest run forever...";
  while (true) {
    sleep(1);
    XLOG_EVERY_MS(DBG2, 5000) << "AgentTest running forever";
  }
}

bool AgentTest::waitForSwitchStateCondition(
    std::function<bool(const std::shared_ptr<SwitchState>&)> conditionFn,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) const {
  auto newState = sw()->getState();
  while (retries--) {
    if (conditionFn(newState)) {
      return true;
    }
    std::this_thread::sleep_for(msBetweenRetry);
    newState = sw()->getState();
  }
  XLOG(DBG3) << "Awaited state condition was never satisfied";
  return false;
}

void AgentTest::setPortStatus(PortID portId, bool up) {
  auto configFnLinkDown = [=](const std::shared_ptr<SwitchState>& state) {
    auto newState = state->clone();
    auto ports = newState->getPorts()->clone();
    auto port = ports->getPort(portId)->clone();
    port->setAdminState(
        up ? cfg::PortState::ENABLED : cfg::PortState::DISABLED);
    ports->updateNode(port);
    newState->resetPorts(ports);
    return newState;
  };
  sw()->updateStateBlocking("set port state", configFnLinkDown);
}

void AgentTest::setPortLoopbackMode(PortID portId, cfg::PortLoopbackMode mode) {
  auto setLbMode = [=](const std::shared_ptr<SwitchState>& state) {
    auto newState = state->clone();
    auto ports = newState->getPorts()->clone();
    auto port = ports->getPort(portId)->clone();
    port->setLoopbackMode(mode);
    ports->updateNode(port);
    newState->resetPorts(ports);
    return newState;
  };
  sw()->updateStateBlocking("set port loopback mode", setLbMode);
}

// Returns the port names for a given list of portIDs
std::vector<std::string> AgentTest::getPortNames(
    const std::vector<PortID>& ports) const {
  return folly::gen::from(ports) | folly::gen::map([&](PortID port) {
           return sw()->getState()->getPort(port)->getName();
         }) |
      folly::gen::as<std::vector<std::string>>();
}

// Waits till the link status of the passed in ports reaches
// the expected state
void AgentTest::waitForLinkStatus(
    const std::vector<PortID>& portsToCheck,
    bool up,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) const {
  XLOG(INFO) << "Checking link status on "
             << folly::join(",", getPortNames(portsToCheck));
  auto portStatus = sw()->getPortStatus();
  std::vector<PortID> badPorts;
  while (retries--) {
    badPorts.clear();
    for (const auto& port : portsToCheck) {
      if (*portStatus[port].up_ref() != up) {
        std::this_thread::sleep_for(msBetweenRetry);
        portStatus = sw()->getPortStatus();
        badPorts.push_back(port);
      }
    }
    if (badPorts.empty()) {
      return;
    }
  }

  auto msg = folly::format(
      "Unexpected Link status {:d} for {:s}",
      !up,
      folly::join(",", getPortNames(badPorts)));
  throw FbossError(msg);
}
void AgentTest::setupConfigFlag() {
  // Nothing to setup by default
}

void AgentTest::setupFlags() const {
  // Nothing to setup by default
}

AgentTest::~AgentTest() {}

void initAgentTest(int argc, char** argv, PlatformInitFn initPlatformFn) {
  argCount = argc;
  argVec = argv;
  initPlatform = initPlatformFn;
}

} // namespace facebook::fboss
