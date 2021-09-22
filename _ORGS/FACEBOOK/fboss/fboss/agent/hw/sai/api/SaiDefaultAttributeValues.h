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

#include "fboss/agent/hw/sai/api/SaiVersion.h"
#include "fboss/agent/hw/sai/api/Traits.h"
#include "fboss/agent/hw/sai/api/Types.h"

#include <folly/MacAddress.h>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

struct SaiObjectIdDefault {
  sai_object_id_t operator()() const {
    return SAI_NULL_OBJECT_ID;
  }
};

template <typename SaiIntT>
struct SaiIntDefault {
  SaiIntT operator()() const {
    return 0;
  }
};

struct SaiInt1Default {
  sai_uint32_t operator()() const {
    return 1;
  }
};

template <typename SaiIntT>
struct SaiInt100Default {
  SaiIntT operator()() const {
    return 100;
  }
};

struct SaiBoolDefaultFalse {
  bool operator()() const {
    return false;
  }
};

struct SaiBoolDefaultTrue {
  bool operator()() const {
    return true;
  }
};
struct SaiPortInterfaceTypeDefault {
  sai_port_interface_type_t operator()() const {
    return SAI_PORT_INTERFACE_TYPE_NONE;
  }
};

struct SaiMacAddressDefault {
  folly::MacAddress operator()() const {
    return folly::MacAddress{};
  }
};

template <typename SaiIntRangeT>
struct SaiIntRangeDefault {
  SaiIntRangeT operator()() const {
    return {0, 0};
  }
};

template <typename SaiListT>
struct SaiListDefault {
  SaiListT operator()() const {
    return SaiListT{0, nullptr};
  }
};

struct SaiPointerDefault {
  sai_pointer_t operator()() const {
    return SAI_NULL_OBJECT_ID;
  }
};

// using AclEntryActionSaiObjectIdT = AclEntryAction<sai_object_id_t>;
struct SaiAclEntryActionSaiObjectDefault {
  AclEntryActionSaiObjectIdT operator()() const {
    return AclEntryActionSaiObjectIdT(SAI_NULL_OBJECT_ID);
  }
};

using SaiObjectIdListDefault = SaiListDefault<sai_object_list_t>;
using SaiU32ListDefault = SaiListDefault<sai_u32_list_t>;
using SaiS8ListDefault = SaiListDefault<sai_s8_list_t>;

} // namespace facebook::fboss
