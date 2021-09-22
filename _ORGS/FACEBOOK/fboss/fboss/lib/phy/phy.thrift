#
# Copyright 2004-present Facebook. All Rights Reserved.
#
namespace py neteng.fboss.phy
namespace py3 neteng.fboss.phy
namespace py.asyncio neteng.fboss.asyncio.phy
namespace cpp2 facebook.fboss.phy
namespace go neteng.fboss.phy
namespace php fboss_phy

include "fboss/agent/switch_config.thrift"
include "fboss/qsfp_service/if/transceiver.thrift"

enum IpModulation {
  NRZ = 1,
  PAM4 = 2,
}

enum FecMode {
  NONE = 1,
  CL74 = 74,
  CL91 = 91,
  RS528 = 528,
  RS544 = 544,
  RS544_2N = 11,
}

struct VCOFrequencyFactor {
  1: switch_config.PortSpeed speed;
  2: FecMode fecMode;
}

enum VCOFrequency {
  UNKNOWN = 0,
  VCO_10_3125GHZ = 1,
  VCO_20_625GHZ = 2,
  VCO_25_78125GHZ = 3,
  VCO_26_5625GHZ = 4,
}

// [DEPRECATED] Unfortunately this should be InterfaceType instead of
// InterfaceMode, as interface mode means something else. Otherwise this will
// be very confusing for xphy programming.
enum InterfaceMode {
  // Backplane
  KR = 1,
  KR2 = 2,
  KR4 = 3,
  KR8 = 4,
  // Copper
  CR = 10,
  CR2 = 11,
  CR4 = 12,
  // Optics
  SR = 20,
  SR4 = 21,
  // CAUI
  CAUI = 30,
  CAUI4 = 31,
  CAUI4_C2C = 32,
  CAUI4_C2M = 33,
  // Other
  XLAUI = 40,
  SFI = 41,
  GMII = 42,
  XLPPI = 44,
  AUI_C2C = 46,
  AUI_C2M = 47,
}

enum InterfaceType {
  // Backplane
  KR = 1,
  KR2 = 2,
  KR4 = 3,
  KR8 = 4,
  // Copper
  CR = 10,
  CR2 = 11,
  CR4 = 12,
  // Optics
  SR = 20,
  SR4 = 21,
  // CAUI
  CAUI = 30,
  CAUI4 = 31,
  CAUI4_C2C = 32,
  CAUI4_C2M = 33,
  // Other
  XLAUI = 40,
  SFI = 41,
  GMII = 42,
  XLPPI = 44,
  AUI_C2C = 46,
  AUI_C2M = 47,
}

enum Side {
  SYSTEM = 1,
  LINE = 2,
}

enum Loopback {
  ON = 1,
  OFF = 2,
}

struct TxSettings {
  1: i16 pre = 0;
  2: i16 pre2 = 0;
  3: i16 main = 0;
  4: i16 post = 0;
  5: i16 post2 = 0;
  6: i16 post3 = 0;
  8: optional i16 driveCurrent;
}

struct RxSettings {
  1: optional i16 ctlCode;
  2: optional i16 dspMode;
  3: optional i16 afeTrim;
  4: optional i16 acCouplingBypass;
}

struct LaneMap {
  1: byte rx = 0;
  2: byte tx = 0;
}

struct PolaritySwap {
  1: bool rx = 0;
  2: bool tx = 0;
}

const PolaritySwap NO_POLARITY_SWAP = {};

struct PhyFwVersion {
  1: i32 version;
  2: i32 crc;
  3: optional string versionStr;
  4: optional i32 dateCode;
  5: optional i32 minorVersion;
}

/*
 * ===== Port speed profile configs =====
 * The following structs are used to define the profile configs for a port
 * speed profile. Usually these config should be static across all ports in
 * the same platform. Each platform supported PortProfileID should map to a
 * PortProfileConfig.
 */
struct ProfileSideConfig {
  1: i16 numLanes;
  2: IpModulation modulation;
  3: FecMode fec;
  4: optional transceiver.TransmitterTechnology medium;
  // [DEPRECATED] Replace interfaceMode with interfaceType
  5: optional InterfaceMode interfaceMode;
  6: optional InterfaceType interfaceType;
}

struct PortProfileConfig {
  1: switch_config.PortSpeed speed;
  2: ProfileSideConfig iphy;
  3: optional ProfileSideConfig xphyLine;
  // We used to think xphy system should be identical to iphy based on their
  // direct connection. However, it's not always true in some platform.
  // For example, Minipack might use KR interface mode in iphy but acutually
  // use AUI_C2C on xphy system for new firmware.
  // Hence, introducing xphy system to distinguish the difference.
  4: optional ProfileSideConfig xphySystem;
}

/*
 * ===== Port PinConnection configs =====
 * The following structs are used to define the fixed port PinConnection of the
 * PlatformPortMapping for a port. Usually these configs should be static
 * for any single port, and it won't change because of speed, medium or etc.
 * Here, we break down a platform port to a list of PinConnection, which can
 * start from ASIC(iphy), and then connect either directly to transceiver(
 * non-xphy case) or to xphy system side. And then xphy system can connect to
 * one or multiple line ends and finally the line end will connect to the
 * transceiver.
 */
enum DataPlanePhyChipType {
  IPHY = 1,
  XPHY = 2,
  TRANSCEIVER = 3,
}

struct DataPlanePhyChip {
  // Global id and used by the phy::PinID
  1: string name;
  2: DataPlanePhyChipType type;
  // Physical id which is used to identify the chip for the phy service.
  // type=DataPlanePhyChipType::IPHY, which means the ASIC core id
  // type=DataPlanePhyChipType::XPHY, which means the xphy(gearbox) id
  // type=DataPlanePhyChipType::TRANSCEIVER, which means the transceiver id
  3: i32 physicalID;
}

struct PinID {
  // Match to the name of one chip in the global chip list in platform_config
  1: string chip;
  2: i32 lane;
}

struct PinJunction {
  1: PinID system;
  2: list<PinConnection> line;
}

union Pin {
  1: PinID end;
  2: PinJunction junction;
}

struct PinConnection {
  1: PinID a;
  // Some platform might not have z side, like Galaxy Fabric Card
  2: optional Pin z;
  3: optional PolaritySwap polaritySwap;
}

/*
 * ===== Port dynamic Pin configs =====
 * The following structs are used to define the dynamic pin config of the
 * PlatformPortConfig for a port. Usually these configs will change accordingly
 * based on the speed, medium or any other factors.
 */
struct PinConfig {
  1: PinID id;
  2: optional TxSettings tx;
  3: optional RxSettings rx;
  4: optional LaneMap laneMap;
  5: optional PolaritySwap polaritySwap;
}

struct PortPinConfig {
  1: list<PinConfig> iphy;
  2: optional list<PinConfig> transceiver;
  3: optional list<PinConfig> xphySys;
  4: optional list<PinConfig> xphyLine;
}

struct PortPrbsState {
  1: bool enabled = false;
  2: i32 polynominal;
}

// structs for Phy(both IPHY and XPHY) diagnostic info
struct PhyInfo {
  1: DataPlanePhyChip phyChip;
  2: optional PhyFwVersion fwVersion;
  3: switch_config.PortSpeed speed;
  4: string name; // port name
  10: optional PhySideInfo system;
  11: PhySideInfo line;
  12: i32 timeCollected; // Time the diagnostic info was collected at
}

struct PhySideInfo {
  1: Side side;
  2: optional PcsInfo pcs;
  3: PmdInfo pmd;
  4: optional RsInfo rs; // Reconciliation sub-layer
}

struct PcsInfo {
  1: bool pcsRxStatusLive;
  2: bool pcsRxStatusLatched;
  20: optional RsFecInfo rsFec;
}

struct RsFecInfo {
  1: i64 correctedCodewords;
  2: i64 uncorrectedCodewords;
}

struct PmdInfo {
  1: map<i16, LaneInfo> lanes;
}

struct LaneInfo {
  1: i16 lane;
  2: bool signalDetectLive;
  3: bool signalDetectLatched;
  4: bool cdrLockLive;
  5: bool cdrLockLatched;
  6: optional list<i32> eyeMargins;
  7: TxSettings txSettings;
}

struct RsInfo {
  1: bool localFault;
  2: bool remoteFault;
}

union LinkSnapshot {
  1: transceiver.TransceiverInfo transceiverInfo;
  2: PhyInfo phyInfo;
}
