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

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/mka/gen-cpp2/model_types.h"
#include "fboss/mka_service/if/gen-cpp2/MKAService.h"

namespace facebook::fboss {

struct CmdShowMkaTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = cli::ShowMkaModel;
};

class CmdShowMka : public CmdHandler<CmdShowMka, CmdShowMkaTraits> {
 public:
  using RetType = CmdShowMkaTraits::RetType;
  RetType queryClient(const HostInfo& hostInfo) {
    auto client =
        utils::createClient<facebook::fboss::mka::MKAServiceAsyncClient>(hostInfo);

    std::vector<::facebook::fboss::mka::MKASessionInfo> mkaEntries;
    client->sync_getSessions(mkaEntries);
    return createModel(mkaEntries);
  }
  RetType createModel(
      std::vector<facebook::fboss::mka::MKASessionInfo> mkaEntries) {
    RetType model;
    for (const auto& entry : mkaEntries) {
      cli::MkaEntry modelEntry;
      auto& participantCtx = *entry.participantCtx_ref();
      modelEntry.localPort_ref() = *participantCtx.l2Port_ref();
      modelEntry.srcMac_ref() = *participantCtx.srcMac_ref();
      modelEntry.ckn_ref() = *participantCtx.cak_ref()->ckn_ref();
      modelEntry.keyServerElected_ref() = *participantCtx.elected_ref();
      auto strTime = [](auto secsSinceEpoch) -> std::string {
        if (!secsSinceEpoch) {
          return "--";
        }
        time_t t(secsSinceEpoch);
        std::string timeStr = std::ctime(&t);
        // Trim trailing newline from ctime output
        auto it = std::find_if(timeStr.rbegin(), timeStr.rend(), [](char c) {
          return !std::isspace(c);
        });
        timeStr.erase(it.base(), timeStr.end());
        return timeStr;
      };
      modelEntry.sakRxInstalledSince_ref() =
          strTime(*participantCtx.sakEnabledRxSince_ref());
      modelEntry.sakTxInstalledSince_ref() =
          strTime(*participantCtx.sakEnabledTxSince_ref());
      model.mkaEntries_ref()->emplace_back(modelEntry);
    }
    return model;
  }
  void printOutput(const RetType& model, std::ostream& out = std::cout) {

    for (auto const& entry : model.get_mkaEntries()) {
      out << "Port: " << entry.get_localPort() << std::endl;
      out << std::string(20, '=') << std::endl;
      out << " MAC: " << entry.get_srcMac() << std::endl;
      out << " CKN: " << entry.get_ckn() << std::endl;
      out << " Keyserver elected: "
          << (entry.get_keyServerElected() ? "Y" : "N") << std::endl;
      out << " SAK installed since: "
          << " rx: " << entry.get_sakRxInstalledSince()
          << " tx: " << entry.get_sakTxInstalledSince() << std::endl;
    }
  }
};

} // namespace facebook::fboss
