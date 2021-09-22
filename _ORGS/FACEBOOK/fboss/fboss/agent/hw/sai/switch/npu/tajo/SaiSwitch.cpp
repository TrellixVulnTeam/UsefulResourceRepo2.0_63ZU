// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/switch/SaiSwitch.h"

#include "fboss/agent/hw/sai/api/TamApi.h"

extern "C" {
#include <experimental/sai_attr_ext.h>
}

namespace {
std::string eventName(sai_switch_event_type_t type) {
  switch (type) {
    case SAI_SWITCH_EVENT_TYPE_NONE:
      return "SAI_SWITCH_EVENT_TYPE_NONE";
    case SAI_SWITCH_EVENT_TYPE_ALL:
      return "SAI_SWITCH_EVENT_TYPE_ALL";
    case SAI_SWITCH_EVENT_TYPE_STABLE_FULL:
      return "SAI_SWITCH_EVENT_TYPE_STABLE_FULL";
    case SAI_SWITCH_EVENT_TYPE_STABLE_ERROR:
      return "SAI_SWITCH_EVENT_TYPE_STABLE_ERROR";
    case SAI_SWITCH_EVENT_TYPE_UNCONTROLLED_SHUTDOWN:
      return "SAI_SWITCH_EVENT_TYPE_UNCONTROLLED_SHUTDOWN";
    case SAI_SWITCH_EVENT_TYPE_WARM_BOOT_DOWNGRADE:
      return "SAI_SWITCH_EVENT_TYPE_UNCONTROLLED_SHUTDOWN";
    case SAI_SWITCH_EVENT_TYPE_PARITY_ERROR:
      return "SAI_SWITCH_EVENT_TYPE_PARITY_ERROR";
  }
  return folly::to<std::string>("unknown event type: ", type);
}

std::string correctionType(sai_tam_switch_event_ecc_err_type_e type) {
  switch (type) {
    case ECC_COR:
      return "ECC_COR";
    case ECC_UNCOR:
      return "ECC_UNCOR";
    case PARITY:
      return "PARITY";
  }
  return "correction-type-unknown";
}
} // namespace

namespace facebook::fboss {

void SaiSwitch::tamEventCallback(
    sai_object_id_t /*tam_event_id*/,
    sai_size_t /*buffer_size*/,
    const void* buffer,
    uint32_t /*attr_count*/,
    const sai_attribute_t* /*attr_list*/) {
  auto eventDesc = static_cast<const sai_tam_event_desc_t*>(buffer);
  if (eventDesc->type != SAI_TAM_EVENT_TYPE_SWITCH) {
    // not a switch type event
    return;
  }
  auto eventType = eventDesc->event.switch_event.type;
  std::stringstream sstream;
  sstream << "received switch event=" << eventName(eventType);
  switch (eventType) {
    case SAI_SWITCH_EVENT_TYPE_PARITY_ERROR: {
      auto errorType = eventDesc->event.switch_event.data.parity_error.err_type;
      switch (errorType) {
        case ECC_COR:
          getSwitchStats()->corrParityError();
          break;
        case ECC_UNCOR:
        case PARITY:
          getSwitchStats()->uncorrParityError();
          break;
      }
      sstream << ", correction type=" << correctionType(errorType);
    } break;
    case SAI_SWITCH_EVENT_TYPE_ALL:
    case SAI_SWITCH_EVENT_TYPE_STABLE_FULL:
    case SAI_SWITCH_EVENT_TYPE_STABLE_ERROR:
    case SAI_SWITCH_EVENT_TYPE_UNCONTROLLED_SHUTDOWN:
    case SAI_SWITCH_EVENT_TYPE_WARM_BOOT_DOWNGRADE:
      getSwitchStats()->asicError();
      break;
    case SAI_SWITCH_EVENT_TYPE_NONE:
      // no-op
      break;
  }
  XLOG(WARNING) << sstream.str();
}

void SaiSwitch::parityErrorSwitchEventCallback(
    sai_size_t /*buffer_size*/,
    const void* /*buffer*/,
    uint32_t /*event_type*/) {
  // noop;
}
} // namespace facebook::fboss
