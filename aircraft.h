#ifndef AIRCRAFT_H
#define AIRCRAFT_H

static inline uint32_t addrHash(uint32_t addr, uint32_t bits) {
    const uint64_t m = 0x880355f21e6d1965ULL;
    const uint64_t seed = 0x30732349f7810465ULL;
    uint64_t h = seed ^ (4 * m);

    uint64_t v = addr;
    h ^= mix_fasthash(v);
    h *= m;
    h = mix_fasthash(h);

    // collapse to required bit width while retaining as much info as possible

    uint64_t res = h ^ (h >> 32);

    if (bits < 16)
        res ^= (res >> 16);

    res ^= (res >> bits);

    // mask to fit the requested bit width
    res &= (((uint64_t) 1) << bits) - 1;

    return (uint32_t) res;
}

void quickInit();
void quickDestroy();
void quickAdd(struct aircraft *a);
void quickRemove(struct aircraft *a);

void aircraftZeroTail(struct aircraft *a);
struct aircraft *aircraftGet(uint32_t addr);
struct aircraft *aircraftCreate(uint32_t addr);
void freeAircraft(struct aircraft *a);

void clearAircraftSeenByList(struct aircraft *a);

typedef struct dbEntry {
    struct dbEntry *next;
    uint32_t addr;
    uint16_t dbFlags;
    char typeCode[4];
    char registration[12];
    char typeLong[64];
    char ownOp[64];
    char year[4];
} dbEntry;

dbEntry *dbGet(uint32_t addr, dbEntry **index);
void dbPut(uint32_t addr, dbEntry **index, dbEntry *d);

#define BINCRAFT_ALT_FACTOR (1.0f/25.0f)

struct binCraft {
  uint32_t hex;
  uint16_t seen_pos;
  uint16_t seen;
  // 8
  int32_t lon;
  int32_t lat;
  // 16
  int16_t baro_rate;
  int16_t geom_rate;
  int16_t baro_alt;
  int16_t geom_alt;
  // 24
  uint16_t nav_altitude_mcp; // FCU/MCP selected altitude
  uint16_t nav_altitude_fms; // FMS selected altitude
  int16_t nav_qnh; // Altimeter setting (QNH/QFE), millibars
  int16_t nav_heading; // target heading, degrees (0-359)
  // 32
  uint16_t squawk; // Squawk
  int16_t gs;
  int16_t mach;
  int16_t roll; // Roll angle, degrees right
  // 40
  int16_t track; // Ground track
  int16_t track_rate; // Rate of change of ground track, degrees/second
  int16_t mag_heading; // Magnetic heading
  int16_t true_heading; // True heading
  // 48
  int16_t wind_direction;
  int16_t wind_speed;
  int16_t oat;
  int16_t tat;
  // 56
  uint16_t tas;
  uint16_t ias;
  uint16_t pos_rc; // Rc of last computed position
  uint16_t messages;
  // 64
  unsigned category:8; // Aircraft category A0 - D7 encoded as a single hex byte. 00 = unset

  unsigned pos_nic:8; // NIC of last computed position
  // 66
  nav_modes_t nav_modes:8; // enabled modes (autopilot, vnav, etc)

  emergency_t emergency:4; // Emergency/priority status
  addrtype_t addrtype:4; // highest priority address type seen for this aircraft
  // 68
  airground_t airground:4; // air/ground status
  nav_altitude_source_t nav_altitude_src:4;  // source of altitude used by automation

  sil_type_t sil_type:4; // SIL supplement from TSS or opstatus
  unsigned adsb_version:4; // ADS-B version (from ADS-B operational status); -1 means no ADS-B messages seen
  // 70
  unsigned adsr_version:4; // As above, for ADS-R messages
  unsigned tisb_version:4; // As above, for TIS-B messages

  unsigned nac_p : 4; // NACp from TSS or opstatus
  unsigned nac_v : 4; // NACv from airborne velocity or opstatus
  // 72
  unsigned sil : 2; // SIL from TSS or opstatus
  unsigned gva : 2; // GVA from opstatus
  unsigned sda : 2; // SDA from opstatus
  unsigned nic_a : 1; // NIC supplement A from opstatus
  unsigned nic_c : 1; // NIC supplement C from opstatus

  unsigned nic_baro : 1; // NIC baro supplement from TSS or opstatus
  unsigned alert : 1; // FS Flight status alert bit
  unsigned spi : 1; // FS Flight status SPI (Special Position Identification) bit
  unsigned callsign_valid:1;
  unsigned baro_alt_valid:1;
  unsigned geom_alt_valid:1;
  unsigned position_valid:1;
  unsigned gs_valid:1;
  // 74
  unsigned ias_valid:1;
  unsigned tas_valid:1;
  unsigned mach_valid:1;
  unsigned track_valid:1;
  unsigned track_rate_valid:1;
  unsigned roll_valid:1;
  unsigned mag_heading_valid:1;
  unsigned true_heading_valid:1;

  unsigned baro_rate_valid:1;
  unsigned geom_rate_valid:1;
  unsigned nic_a_valid:1;
  unsigned nic_c_valid:1;
  unsigned nic_baro_valid:1;
  unsigned nac_p_valid:1;
  unsigned nac_v_valid:1;
  unsigned sil_valid:1;
  // 76
  unsigned gva_valid:1;
  unsigned sda_valid:1;
  unsigned squawk_valid:1;
  unsigned emergency_valid:1;
  unsigned spi_valid:1;
  unsigned nav_qnh_valid:1;
  unsigned nav_altitude_mcp_valid:1;
  unsigned nav_altitude_fms_valid:1;

  unsigned nav_altitude_src_valid:1;
  unsigned nav_heading_valid:1;
  unsigned nav_modes_valid:1;
  unsigned alert_valid:1;
  unsigned wind_valid:1;
  unsigned temp_valid:1;
  unsigned unused_1:1;
  unsigned unused_2:1;
  // 78
  char callsign[8]; // Flight number
  // 86
  uint16_t dbFlags;
  // 88
  char typeCode[4];
  // 92
  char registration[12];
  // 104
  uint8_t receiverCount;
  uint8_t signal;
  uint8_t extraFlags;
  uint8_t reserved;
  // 108
  // javascript sucks, this must be a multiple of 4 bytes for Int32Array to work correctly
#if defined(TRACKS_UUID)
  uint32_t receiverId;
#endif
} __attribute__ ((__packed__));

void toBinCraft(struct aircraft *a, struct binCraft *new, int64_t now);
int dbUpdate(int64_t now);
int dbFinishUpdate();

void updateTypeReg(struct aircraft *a);

#endif
