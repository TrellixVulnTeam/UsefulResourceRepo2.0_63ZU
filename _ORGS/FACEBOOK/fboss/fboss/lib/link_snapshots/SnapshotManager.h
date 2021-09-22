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

#include <stddef.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <chrono>
#include <list>
#include "fboss/lib/link_snapshots/RingBuffer-defs.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "folly/logging/xlog.h"

DECLARE_int32(link_snapshot_publish_interval);
namespace facebook::fboss {

using namespace fboss::phy;

class SnapshotWrapper {
 public:
  explicit SnapshotWrapper(LinkSnapshot snapshot) : snapshot_(snapshot) {}
  void publish(std::set<std::string> portNames);

  LinkSnapshot snapshot_;
  bool published_{false};
};

// length is the number of snapshots we keep stored at any given time
template <size_t length>
class SnapshotManager {
 public:
  explicit SnapshotManager(std::set<std::string> portNames);
  void addSnapshot(LinkSnapshot val);
  void publishAllSnapshots();
  RingBuffer<SnapshotWrapper, length>& getSnapshots();
  void publishFutureSnapshots(int numToPublish);

 private:
  void publishIfNecessary();

  RingBuffer<SnapshotWrapper, length> buf_;
  std::chrono::steady_clock::time_point lastScheduledPublish_;
  int numSnapshotsToPublish_{0};
  std::set<std::string> portNames_;
};

} // namespace facebook::fboss
