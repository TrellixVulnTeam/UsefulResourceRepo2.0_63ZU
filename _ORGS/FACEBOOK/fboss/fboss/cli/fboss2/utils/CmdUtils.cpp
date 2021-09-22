/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/cli/fboss2/utils/CmdUtils.h"

#include <folly/logging/LogConfig.h>
#include <folly/logging/LoggerDB.h>
#include <folly/logging/xlog.h>

#include <chrono>
#include <fstream>
#include <string>

using namespace std::chrono;

using folly::ByteRange;
using folly::IPAddress;
using folly::IPAddressV6;

namespace facebook::fboss::utils {

const folly::IPAddress getIPFromHost(const std::string& hostname) {
  if (IPAddress::validate(hostname)) {
    return IPAddress(hostname);
  }
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* result = nullptr;
  auto rv = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);

  SCOPE_EXIT {
    freeaddrinfo(result);
  };
  if (rv < 0) {
    XLOG(ERR) << "Could not get an IP for: " << hostname;
    return IPAddress();
  }
  struct addrinfo* res;
  for (res = result; res != nullptr; res = res->ai_next) {
    if (res->ai_addr->sa_family == AF_INET) { // IPV4
      struct sockaddr_in* sp = (struct sockaddr_in*)res->ai_addr;
      return IPAddress::fromLong(sp->sin_addr.s_addr);
    } else if (res->ai_addr->sa_family == AF_INET6) { // IPV6
      struct sockaddr_in6* sp = (struct sockaddr_in6*)res->ai_addr;
      return IPAddress::fromBinary(ByteRange(
          static_cast<unsigned char*>(&(sp->sin6_addr.s6_addr[0])),
          IPAddressV6::byteCount()));
    }
  }
  XLOG(ERR) << "Could not get an IP for: " << hostname;
  return IPAddress();
}

std::vector<std::string> getHostsFromFile(const std::string& filename) {
  std::vector<std::string> hosts;
  std::ifstream in(filename);
  std::string str;

  while (std::getline(in, str)) {
    hosts.push_back(str);
  }

  return hosts;
}

void setLogLevel(std::string logLevelStr) {
  if (logLevelStr == "DBG0") {
    return;
  }

  auto logLevel = folly::stringToLogLevel(logLevelStr);

  auto logConfig = folly::LoggerDB::get().getConfig();
  auto& categoryMap = logConfig.getCategoryConfigs();

  for (auto& p : categoryMap) {
    auto category = folly::LoggerDB::get().getCategory(p.first);
    if (category != nullptr) {
      folly::LoggerDB::get().setLevel(category, logLevel);
    }
  }

  XLOG(DBG1) << "Setting loglevel to " << logLevelStr;
}

const std::string getPrettyElapsedTime(const int64_t& start_time) {
  /* borrowed from "pretty_timedelta function" in fb_toolkit
     takes an epoch time and returns a pretty string of elapsed time
  */
  auto startTime = seconds(start_time);
  auto time_now = system_clock::now();
  auto elapsed_time = time_now - startTime;

  time_t elapsed_convert = system_clock::to_time_t(elapsed_time);

  int days = 0, hours = 0, minutes = 0;

  days = elapsed_convert / 86400;

  int64_t leftover = elapsed_convert % 86400;

  hours = leftover / 3600;
  leftover %= 3600;

  minutes = leftover / 60;
  leftover %= 60;

  std::string pretty_output = "";
  if (days != 0) {
    pretty_output += std::to_string(days) + "d ";
  }
  pretty_output += std::to_string(hours) + "h ";
  pretty_output += std::to_string(minutes) + "m ";
  pretty_output += std::to_string(leftover) + "s";

  return pretty_output;
}

} // namespace facebook::fboss::utils
