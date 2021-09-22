namespace cpp2 facebook.fboss.mka
namespace py neteng.fboss.mka.mka_structs
namespace py3 neteng.fboss.mka
namespace py.asyncio neteng.fboss.asyncio.mka_structs
namespace go neteng.fboss.mka.mka_structs

// All the timeouts are expected in milliseconds
const i64 DEFAULT_CAK_TIMEOUT_IN_MSEC = 0; // never expires

enum MKACipherSuite {
  GCM_AES_128 = 0,
  GCM_AES_256 = 1,
  GCM_AES_XPN_128 = 2,
  GCM_AES_XPN_256 = 3,
}

struct Cak {
  1: string key;
  2: string ckn;
  3: i64 expiration = DEFAULT_CAK_TIMEOUT_IN_MSEC;
}

// IEEE 8021X.2010 TABLE 11.6
enum MKAConfidentialityOffset {
  CONFIDENTIALITY_NOT_USED = 0,
  CONFIDENTIALITY_NO_OFFSET = 1,
  CONFIDENTIALITY_OFFSET_30 = 2,
  CONFIDENTIALITY_OFFSET_50 = 3,
}

struct MKASci {
  1: string macAddress;
  2: i32 port = 1;
}

struct MKASak {
  1: MKASci sci;
  2: string l2Port;
  3: i16 assocNum = 0;
  4: string keyHex;
  5: string keyIdHex;
  6: mka_structs.MKAConfidentialityOffset confidentOffset = mka_structs.MKAConfidentialityOffset.CONFIDENTIALITY_NO_OFFSET;
  7: bool primary = false;
}

struct MKAActiveSakSession {
  1: MKASak sak;
  2: list<MKASci> sciList;
}
