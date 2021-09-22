/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/CmdSubcommands.h"
#include "fboss/cli/fboss2/CmdList.h"
#include "fboss/cli/fboss2/utils/CLIParserUtils.h"

#include <folly/Singleton.h>
#include <stdexcept>

namespace {
struct singleton_tag_type {};

const std::map<std::string, std::string>& kSupportedVerbs() {
  static const std::map<std::string, std::string> supportedVerbs = {
      {"show", "Show object info"},
      {"clear", "Clear object info"},
      {"create", "Create object"},
  };

  return supportedVerbs;
};

} // namespace

using facebook::fboss::CmdSubcommands;
static folly::Singleton<CmdSubcommands, singleton_tag_type> cmdSubcommands{};
std::shared_ptr<CmdSubcommands> CmdSubcommands::getInstance() {
  return cmdSubcommands.try_get();
}

namespace facebook::fboss {

void CmdSubcommands::addCommandBranch(CLI::App& app, const Command& cmd) {
  // Command should not already exists since we only traverse the tree once
  if (utils::getSubcommandIf(app, cmd.name)) {
    // TODO explore moving this check to a compile time check
    std::runtime_error("Command already exists, command tree must be invalid");
  }
  auto* subCmd = app.add_subcommand(cmd.name, cmd.help);
  if (auto& handler = cmd.handler) {
    subCmd->callback(*handler);

    if (cmd.argType == utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IPV6_LIST) {
      subCmd->add_option("ipv6Addrs", ipv6Addrs_, "IPv6 addr(s)");
    } else if (
        cmd.argType == utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PORT_LIST) {
      subCmd->add_option("ports", ports_, "Port(s)");
    }
  }

  for (const auto& child : cmd.subcommands) {
    addCommandBranch(*subCmd, child);
  }
}

void CmdSubcommands::initCommandTree(
    CLI::App& app,
    const CommandTree& cmdTree) {
  for (const auto& cmd : cmdTree) {
    auto& verb = cmd.verb;
    auto* verbCmd = utils::getSubcommandIf(app, verb);
    // TODO explore moving this check to a compile time check
    if (!verbCmd) {
      throw std::runtime_error("unsupported verb " + verb);
    }

    addCommandBranch(*verbCmd, cmd);
  }
}

void CmdSubcommands::init(CLI::App& app) {
  for (const auto& [verb, helpMsg] : kSupportedVerbs()) {
    app.add_subcommand(verb, helpMsg);
  }

  initCommandTree(app, kCommandTree());
  initCommandTree(app, kAdditionalCommandTree());
}

} // namespace facebook::fboss
