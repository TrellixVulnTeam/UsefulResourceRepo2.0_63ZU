/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/switch/SaiFdbManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopManager.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/types.h"

#include "folly/container/F14Map.h"

#include <memory>
#include <mutex>

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
class SaiNeighborManager;
class SaiStore;

using SaiNeighbor = SaiObject<SaiNeighborTraits>;

struct SaiNeighborHandle {
  const SaiNeighbor* neighbor;
  const SaiFdbEntry* fdbEntry;
};

class ManagedNeighbor : public SaiObjectEventAggregateSubscriber<
                            ManagedNeighbor,
                            SaiNeighborTraits,
                            SaiVlanRouterInterfaceTraits,
                            SaiFdbTraits> {
 public:
  using Base = SaiObjectEventAggregateSubscriber<
      ManagedNeighbor,
      SaiNeighborTraits,
      SaiVlanRouterInterfaceTraits,
      SaiFdbTraits>;
  using RouterInterfaceWeakPtr =
      std::weak_ptr<const SaiObject<SaiVlanRouterInterfaceTraits>>;
  using FdbWeakptr = std::weak_ptr<const SaiObject<SaiFdbTraits>>;
  using PublisherObjects = std::tuple<RouterInterfaceWeakPtr, FdbWeakptr>;

  ManagedNeighbor(
      SaiNeighborManager* manager,
      SaiPortDescriptor port,
      InterfaceID interfaceId,
      folly::IPAddress ip,
      folly::MacAddress mac,
      std::optional<sai_uint32_t> metadata,
      std::optional<bool> noHostRoute)
      : Base(interfaceId, std::make_tuple(interfaceId, mac)),
        manager_(manager),
        port_(port),
        ip_(ip),
        handle_(std::make_unique<SaiNeighborHandle>()),
        metadata_(metadata),
        noHostRoute_(noHostRoute) {}

  void createObject(PublisherObjects objects);
  void removeObject(size_t index, PublisherObjects objects);
  void handleLinkDown();

  SaiNeighborHandle* getHandle() const {
    return handle_.get();
  }

  void notifySubscribers() const;

 private:
  std::string toString() const;

  SaiNeighborManager* manager_;
  SaiPortDescriptor port_;
  folly::IPAddress ip_;
  std::unique_ptr<SaiNeighborHandle> handle_;
  std::optional<sai_uint32_t> metadata_;
  std::optional<bool> noHostRoute_;
};

class SaiNeighborManager {
 public:
  SaiNeighborManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      const SaiPlatform* platform);

  // Helper function to create a SAI NeighborEntry from an FBOSS SwitchState
  // NeighborEntry (e.g., NeighborEntry<IPAddressV6, NDPTable>)
  template <typename NeighborEntryT>
  SaiNeighborTraits::NeighborEntry saiEntryFromSwEntry(
      const std::shared_ptr<NeighborEntryT>& swEntry);

  template <typename NeighborEntryT>
  void changeNeighbor(
      const std::shared_ptr<NeighborEntryT>& oldSwEntry,
      const std::shared_ptr<NeighborEntryT>& newSwEntry);

  template <typename NeighborEntryT>
  void addNeighbor(const std::shared_ptr<NeighborEntryT>& swEntry);

  template <typename NeighborEntryT>
  void removeNeighbor(const std::shared_ptr<NeighborEntryT>& swEntry);

  SaiNeighborHandle* getNeighborHandle(
      const SaiNeighborTraits::NeighborEntry& entry);
  const SaiNeighborHandle* getNeighborHandle(
      const SaiNeighborTraits::NeighborEntry& entry) const;

  void clear();

  std::shared_ptr<SaiNeighbor> createSaiObject(
      const SaiNeighborTraits::AdapterHostKey& key,
      const SaiNeighborTraits::CreateAttributes& attributes,
      bool notify);

  bool isLinkUp(SaiPortDescriptor port);

 private:
  SaiNeighborHandle* getNeighborHandleImpl(
      const SaiNeighborTraits::NeighborEntry& entry) const;

  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;
  folly::F14FastMap<
      SaiNeighborTraits::NeighborEntry,
      std::shared_ptr<ManagedNeighbor>>
      managedNeighbors_;
};

} // namespace facebook::fboss
