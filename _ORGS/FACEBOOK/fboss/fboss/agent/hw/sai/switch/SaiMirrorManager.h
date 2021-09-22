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

#include "fboss/agent/hw/sai/api/MirrorApi.h"
#include "fboss/agent/hw/sai/api/Types.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/state/Mirror.h"
#include "fboss/agent/types.h"

#include <memory>

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
class SaiStore;

using SaiMirror2Port = SaiObject<SaiLocalMirrorTraits>;
using SaiMirror2GreTunnel = SaiObject<SaiEnhancedRemoteMirrorTraits>;
#if SAI_API_VERSION >= SAI_VERSION(1, 7, 0)
using SaiMirrorSflowTunnel = SaiObject<SaiSflowMirrorTraits>;
#endif

struct SaiMirrorHandle {
  SaiMirrorHandle(std::string mirrorId, SaiManagerTable* managerTable)
      : mirrorId(mirrorId), managerTable(managerTable) {}
  ~SaiMirrorHandle();
  using SaiMirror = std::variant<
      std::shared_ptr<SaiMirror2Port>,
      std::shared_ptr<SaiMirror2GreTunnel>
#if SAI_API_VERSION >= SAI_VERSION(1, 7, 0)
      ,
      std::shared_ptr<SaiMirrorSflowTunnel>
#endif
      >;
  std::string mirrorId;
  SaiManagerTable* managerTable;
  SaiMirror mirror;
  MirrorSaiId adapterKey() {
    return std::visit(
        [](auto& handle) { return handle->adapterKey(); }, mirror);
  }
};

class SaiMirrorManager {
 public:
  SaiMirrorManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      SaiPlatform* platform);
  void addMirror(const std::shared_ptr<Mirror>& mirror);
  void removeMirror(const std::shared_ptr<Mirror>& mirror);
  void changeMirror(
      const std::shared_ptr<Mirror>& oldMirror,
      const std::shared_ptr<Mirror>& newMirror);
  const SaiMirrorHandle* FOLLY_NULLABLE
  getMirrorHandle(const std::string& mirrorId) const;
  SaiMirrorHandle* FOLLY_NULLABLE getMirrorHandle(const std::string& mirrorId);
  std::vector<MirrorSaiId> getAllMirrorSessionOids() const;

 private:
  SaiMirrorHandle* FOLLY_NULLABLE
  getMirrorHandleImpl(const std::string& mirrorId) const;
  SaiMirrorHandle::SaiMirror addMirrorSpan(PortSaiId monitorPort);
  SaiMirrorHandle::SaiMirror addMirrorErSpan(
      const std::shared_ptr<Mirror>& mirror,
      PortSaiId monitorPort);
  SaiMirrorHandle::SaiMirror addMirrorSflow(
      const std::shared_ptr<Mirror>& mirror,
      PortSaiId monitorPort);

  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  SaiPlatform* platform_;
  folly::F14FastMap<std::string, std::unique_ptr<SaiMirrorHandle>>
      mirrorHandles_;
};

} // namespace facebook::fboss
