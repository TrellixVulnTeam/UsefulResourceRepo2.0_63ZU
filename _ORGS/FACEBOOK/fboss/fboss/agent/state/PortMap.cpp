/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/PortMap.h"

#include "fboss/agent/state/NodeMap-defs.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

PortMap::PortMap() {}

PortMap::~PortMap() {}

void PortMap::registerPort(PortID id, const std::string& name) {
  addNode(std::make_shared<Port>(id, name));
}

void PortMap::addPort(const std::shared_ptr<Port>& port) {
  addNode(port);
}

void PortMap::updatePort(const std::shared_ptr<Port>& port) {
  updateNode(port);
}

PortMap* PortMap::modify(std::shared_ptr<SwitchState>* state) {
  if (!isPublished()) {
    CHECK(!(*state)->isPublished());
    return this;
  }

  SwitchState::modify(state);
  auto newPorts = clone();
  auto* ptr = newPorts.get();
  (*state)->resetPorts(std::move(newPorts));
  return ptr;
}

std::shared_ptr<Port> PortMap::getPort(const std::string& name) const {
  auto port = getPortIf(name);
  if (!port) {
    throw FbossError("Port with name: ", name, " not found");
  }
  return port;
}

std::shared_ptr<Port> PortMap::getPortIf(const std::string& name) const {
  for (auto port : *this) {
    if (name == port->getName()) {
      return port;
    }
  }
  return nullptr;
}

FBOSS_INSTANTIATE_NODE_MAP(PortMap, PortMapTraits);

} // namespace facebook::fboss
