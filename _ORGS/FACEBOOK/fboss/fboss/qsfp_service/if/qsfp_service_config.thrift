#
# Copyright 2004-present Facebook. All Rights Reserved.
#
namespace cpp2 facebook.fboss.cfg
namespace go neteng.fboss.qsfp_service_config
namespace py neteng.fboss.qsfp_service_config
namespace py3 neteng.fboss
namespace rust facebook.fboss

include "fboss/qsfp_service/if/transceiver.thrift"

enum TransceiverPartNumber {
  UNKNOWN = 1,
}

// The matching logic will treat the absent of an optional field as match all.
struct TransceiverConfigOverrideFactor {
  1: optional TransceiverPartNumber transceiverPartNumber;
  2: optional transceiver.SMFMediaInterfaceCode applicationCode;
}

struct Sff8636Overrides {
  1: optional i16 rxPreemphasis;
}

struct CmisOverrides {
  1: optional transceiver.RxEqualizerSettings rxEqualizerSettings;
}

union TransceiverOverrides {
  1: Sff8636Overrides sff;
  2: CmisOverrides cmis;
}

struct TransceiverConfigOverride {
  1: TransceiverConfigOverrideFactor factor;
  2: TransceiverOverrides config;
}

struct QsfpServiceConfig {
  // This is used to override the default command line arguments we
  // pass to qsfp service.
  1: map<string, string> defaultCommandLineArgs = {};

  // This has a list of overrides of settings on the optic together with the
  // factor that specify the condition to apply.
  2: list<TransceiverConfigOverride> transceiverConfigOverrides = [];
}
