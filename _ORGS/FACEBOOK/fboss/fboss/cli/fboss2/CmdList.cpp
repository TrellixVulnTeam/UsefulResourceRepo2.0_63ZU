/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/CmdList.h"

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/clear/CmdClearArp.h"
#include "fboss/cli/fboss2/commands/clear/CmdClearNdp.h"
#include "fboss/cli/fboss2/commands/show/CmdShowInterface.h"
#include "fboss/cli/fboss2/commands/show/CmdShowPortQueue.h"
#include "fboss/cli/fboss2/commands/show/acl/CmdShowAcl.h"
#include "fboss/cli/fboss2/commands/show/arp/CmdShowArp.h"
#include "fboss/cli/fboss2/commands/show/lldp/CmdShowLldp.h"
#include "fboss/cli/fboss2/commands/show/ndp/CmdShowNdp.h"
#include "fboss/cli/fboss2/commands/show/port/CmdShowPort.h"
#include "fboss/cli/fboss2/commands/show/transceiver/CmdShowTransceiver.h"
#include "fboss/cli/fboss2/commands/show/mka/CmdShowMka.h"

namespace facebook::fboss {

const CommandTree& kCommandTree() {
  const static CommandTree root = {
      {"show",
       "acl",
       utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE,
       "Show ACL information",
       commandHandler<CmdShowAcl>},

      {"show",
       "arp",
       utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE,
       "Show ARP information",
       commandHandler<CmdShowArp>},

      {"show",
       "lldp",
       utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE,
       "Show LLDPinformation",
       commandHandler<CmdShowLldp>},

      {"show",
       "ndp",
       utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IPV6_LIST,
       "Show NDP information",
       commandHandler<CmdShowNdp>},

      {"show",
       "port",
       utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PORT_LIST,
       "Show Port queue information",
       commandHandler<CmdShowPort>,
       {

           {"queue",
            utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE,
            "Show Port queue information",
            commandHandler<CmdShowPortQueue>}}},

      {"show",
       "interface",
       utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PORT_LIST,
       "Show Interface information",
       commandHandler<CmdShowInterface>},

      {"show",
       "transceiver",
       utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PORT_LIST,
       "Show Transceiver information",
       commandHandler<CmdShowTransceiver>},
      {"show",
       "mka",
       utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE,
       "Show mka session information",
       commandHandler<CmdShowMka>},

      {"clear",
       "arp",
       utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE,
       "Clear ARP information",
       commandHandler<CmdClearArp>},

      {"clear",
       "ndp",
       utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE,
       "Clear NDP information",
       commandHandler<CmdClearNdp>},
  };
  return root;
}

} // namespace facebook::fboss
