// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/agent/PhySnapshotManager.h"
#include "fboss/agent/state/Port.h"

namespace facebook::fboss {

void PhySnapshotManager::updateIPhyInfo(
    const std::map<PortID, phy::PhyInfo>& phyInfo) {
  auto lockedSnapshotMap = snapshots_.wlock();
  for (auto info : phyInfo) {
    phy::LinkSnapshot snapshot;
    snapshot.phyInfo_ref() = info.second;
    if (auto it = lockedSnapshotMap->find(info.first);
        it != lockedSnapshotMap->end()) {
      it->second.addSnapshot(snapshot);
    } else {
      IPhySnapshotCache cache{{*info.second.name_ref()}};
      cache.addSnapshot(snapshot);
      lockedSnapshotMap->emplace(info.first, cache);
    }
  }
}

std::map<PortID, const phy::PhyInfo> PhySnapshotManager::getIPhyInfo(
    const std::vector<PortID>& portIDs) {
  std::map<PortID, const phy::PhyInfo> infoMap;
  auto lockedSnapshotMap = snapshots_.rlock();
  for (auto portID : portIDs) {
    try {
      if (auto it = lockedSnapshotMap->find(portID);
          it != lockedSnapshotMap->end()) {
        auto snapshot = it->second;
        phy::PhyInfo info =
            snapshot.getSnapshots().last().snapshot_.get_phyInfo();
        infoMap.insert(std::pair<PortID, const phy::PhyInfo>(portID, info));
      }
    } catch (std::exception& e) {
      XLOG(ERR) << "Fetching snapshot for " << portID << " failed with "
                << e.what();
    }
  }
  return infoMap;
}

void PhySnapshotManager::publishSnapshots(PortID port) {
  auto lockedSnapshotMap = snapshots_.wlock();
  if (auto it = lockedSnapshotMap->find(port); it != lockedSnapshotMap->end()) {
    it->second.publishAllSnapshots();
    it->second.publishFutureSnapshots(kNumCachedSnapshots);
  }
}

} // namespace facebook::fboss
