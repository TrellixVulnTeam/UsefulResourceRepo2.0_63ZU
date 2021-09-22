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

#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclTableGroup.h"
#include "fboss/agent/state/NodeMap.h"

namespace facebook::fboss {

using AclTableGroupMapTraits = NodeMapTraits<cfg::AclStage, AclTableGroup>;
/*
 * A container for the set of tables.
 */
class AclTableGroupMap
    : public NodeMapT<AclTableGroupMap, AclTableGroupMapTraits> {
 public:
  AclTableGroupMap();
  ~AclTableGroupMap() override;

  const std::shared_ptr<AclTableGroup>& getAclTableGroup(
      cfg::AclStage aclStage) const {
    return getNode(aclStage);
  }
  std::shared_ptr<AclTableGroup> getAclTableGroupIf(
      cfg::AclStage aclStage) const {
    return getNodeIf(aclStage);
  }

  /*
   * The following functions modify the static state.
   * These should only be called on unpublished objects which are only visible
   * to a single thread.
   */

  void addAclTableGroup(const std::shared_ptr<AclTableGroup>& aclTableGroup) {
    addNode(aclTableGroup);
  }
  void removeAclTableGroup(cfg::AclStage aclStage) {
    removeAclTableGroup(getNode(aclStage));
  }
  void removeAclTableGroup(
      const std::shared_ptr<AclTableGroup>& aclTableGroup) {
    removeNode(aclTableGroup);
  }

 private:
  // Inherit the constructors required for clone()
  using NodeMapT::NodeMapT;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
