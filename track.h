// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// track.h: aircraft state tracking prototypes
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
//
// Copyright (c) 2014-2016 Oliver Jowett <oliver@mutability.co.uk>
//
// This file is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This file is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// This file incorporates work covered by the following copyright and
// license:
//
// Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//  *  Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//
//  *  Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef DUMP1090_TRACK_H
#define DUMP1090_TRACK_H

/* Minimum number of repeated Mode A/C replies with a particular Mode A code needed in a
 * 1 second period before accepting that code.
 */
#define TRACK_MODEAC_MIN_MESSAGES 4

/* Special value for Rc unknown */
#define RC_UNKNOWN 0

#define ALTITUDE_BARO_RELIABLE_MAX 20

#define POS_RELIABLE_TIMEOUT (60 * MINUTES)

#define TRACK_STALE (15*SECONDS)
#define TRACK_EXPIRE (60*SECONDS)
#define TRACK_EXPIRE_LONG (180*SECONDS)
#define TRACK_EXPIRE_JAERO (33*MINUTES)
#define TRACK_EXPIRE_ROUGH (2 * MINUTES)

#define NOGPS_DWELL (15 * MINUTES)
#define NOGPS_MAX (20)
#define NOGPS_SHOW (17)

int nogps(int64_t now, struct aircraft *a);

// 2.5 seconds maximum between messages used for calculating wind / temperature
#define TRACK_WT_TIMEOUT (2500)

#define RECEIVERIDBUFFER (12)

#define DISCARD_CACHE (4)
#define CPR_CACHE (4)

#define MESSAGE_RATE_CALC_POINTS (2)

typedef enum
{
    REDUCE_RARE,
    REDUCE_OFTEN,
} reduce_freq_t;

// data moves through three states:
//  fresh: data is valid. Updates from a less reliable source are not accepted.
//  stale: data is valid. Updates from a less reliable source are accepted.
//  expired: data is not valid.

typedef struct
{
  int64_t updated; /* when it arrived */
  int64_t next_reduce_forward; /* when to next forward the data for reduced beast output */
  datasource_t source:8; /* where the data came from */
  datasource_t last_source:8; /* where the data came from */
  int8_t stale; /* if it's stale 1 / 0 */
  unsigned padding:8;
  int padding2;
} data_validity;
// size must be multiple of 64 bits so it can be aligned in struct aircraft.

// uint16: 0 to 65535
#define _gs_factor (10.0f) // 6000 to 60000
#define _track_factor (100.0f) // 360 -> 36000

// int16: -32768 to 32767
#define _alt_factor (1/6.25f) // 200000 to 32000
#define _rate_factor (1/8.0f) // 262136 to 32767
#define _roll_factor (100.0f) // 180 to 18000

/* Structure representing one point in the aircraft trace */
struct state
{
  int64_t timestamp:48;
  //struct state_flags flags; // 16 bits

  unsigned on_ground:1;
  unsigned stale:1;
  unsigned leg_marker:1;
  unsigned gs_valid:1;
  unsigned track_valid:1;
  unsigned baro_alt_valid:1;
  unsigned baro_rate_valid:1;
  unsigned geom_alt_valid:1;
  unsigned geom_rate_valid:1;
  unsigned roll_valid:1;
  unsigned ias_valid:1;
  unsigned padding:5;

  int32_t lat;
  int32_t lon;

  uint16_t gs;
  uint16_t track;
  int16_t baro_alt;
  int16_t baro_rate;

  int16_t geom_alt;
  int16_t geom_rate;
  unsigned ias:12;
  int roll:12;
  addrtype_t addrtype:5;
  int padding2:3;
#if defined(TRACKS_UUID)
  uint32_t receiverId;
#endif
} __attribute__ ((__packed__));

struct state_all
{
  char callsign[8]; // Flight number

  uint16_t squawk; // Squawk
  int16_t nav_altitude_mcp; // FCU/MCP selected altitude
  int16_t nav_altitude_fms; // FMS selected altitude

  int16_t nav_qnh; // Altimeter setting (QNH/QFE), millibars
  uint16_t nav_heading; // target heading, degrees (0-359)
  uint16_t mach;

  int16_t track_rate; // Rate of change of ground track, degrees/second
  uint16_t mag_heading; // Magnetic heading
  uint16_t true_heading; // True heading

  int wind_direction:10;
  int wind_speed:10;
  int oat:10;
  int tat:10;

  unsigned category:8; // Aircraft category A0 - D7 encoded as a single hex byte. 00 = unset

  unsigned pos_nic:8; // NIC of last computed position
  unsigned pos_rc:16; // Rc of last computed position
  emergency_t emergency:3; // Emergency/priority status
  nav_modes_t nav_modes:7; // enabled modes (autopilot, vnav, etc)
  airground_t airground:2; // air/ground status
  nav_altitude_source_t nav_altitude_src:3;  // source of altitude used by automation
  sil_type_t sil_type:3; // SIL supplement from TSS or opstatus

  unsigned tas:12;

  unsigned adsb_version:4; // ADS-B version (from ADS-B operational status); -1 means no ADS-B messages seen
  unsigned adsr_version:4; // As above, for ADS-R messages
  unsigned tisb_version:4; // As above, for TIS-B messages

  unsigned nic_a : 1; // NIC supplement A from opstatus
  unsigned nic_c : 1; // NIC supplement C from opstatus
  unsigned nic_baro : 1; // NIC baro supplement from TSS or opstatus
  unsigned nac_p : 4; // NACp from TSS or opstatus
  unsigned nac_v : 3; // NACv from airborne velocity or opstatus
  unsigned sil : 2; // SIL from TSS or opstatus
  unsigned gva : 2; // GVA from opstatus
  unsigned sda : 2; // SDA from opstatus
  unsigned alert : 1; // FS Flight status alert bit
  unsigned spi : 1; // FS Flight status SPI (Special Position Identification) bit

  unsigned callsign_valid:1;
  unsigned tas_valid:1;
  unsigned mach_valid:1;
  unsigned track_valid:1;
  unsigned track_rate_valid:1;
  unsigned mag_heading_valid:1;
  unsigned true_heading_valid:1;
  unsigned nic_a_valid:1;
  unsigned nic_c_valid:1;
  unsigned nic_baro_valid:1;
  unsigned nac_p_valid:1;
  unsigned nac_v_valid:1;
  unsigned sil_valid:1;
  unsigned gva_valid:1;
  unsigned sda_valid:1;
  unsigned squawk_valid:1;
  unsigned emergency_valid:1;
  unsigned airground_valid:1;
  unsigned nav_qnh_valid:1;
  unsigned nav_altitude_mcp_valid:1;
  unsigned nav_altitude_fms_valid:1;
  unsigned nav_altitude_src_valid:1;
  unsigned nav_heading_valid:1;
  unsigned nav_modes_valid:1;
  unsigned position_valid:1; // used for position accuracy stuff, position is in small state struct
  unsigned alert_valid:1;
  unsigned spi_valid:1;
  unsigned wind_valid:1;
  unsigned temp_valid:1;

} __attribute__ ((__packed__));

#define SFOUR (4)

typedef struct fourState {
    struct state no[SFOUR];
    struct state_all zeroAll;
} fourState;

typedef struct traceBuffer {
    int len;
    fourState *trace;
} traceBuffer;

static inline int getFourStates(int points) {
    return ((points + SFOUR - 1) / SFOUR);
}

static inline int alignSFOUR(int value) {
    return ((value + SFOUR - 1) / SFOUR) * SFOUR;
}

static inline ssize_t stateBytes(int points) {
    return getFourStates(points) * sizeof(fourState);
}

static inline struct state *getState(fourState *buffer, int position) {
    return &buffer[position / SFOUR].no[position % SFOUR];
}

static inline struct state_all *getStateAll(fourState *buffer, int position) {
    if (position % SFOUR == 0)
        return &buffer[position / SFOUR].zeroAll;
    else
        return NULL;
}
typedef struct stateChunk {
    unsigned char *compressed;
    int compressed_size;
    int numStates;
    int64_t firstTimestamp;
    int64_t lastTimestamp;
} stateChunk;

struct cpr_cache {
  uint32_t cpr_lat;
  uint32_t cpr_lon;
  int64_t ts;
  uint64_t receiverId;
};

struct traceCacheEntry {
    int64_t ts;
    int32_t offset;
    int32_t len;
    int32_t leg_marker;
};

struct traceCache {
    int32_t entriesLen;
    int32_t json_max;
    int32_t firstRecentCache;
    int32_t totalAlloc;
    int64_t referenceTs;
    struct traceCacheEntry *entries;
    char *json;
};

struct seenByReceiverIdLlEntry {
    struct seenByReceiverIdLlEntry *next; // Next entry
    uint64_t receiverId;
    uint64_t receiverId2;
    int64_t lastTimestamp;
};

/* Structure used to describe the state of one tracked aircraft */
struct aircraft
{
  struct aircraft *next; // Next aircraft in our linked list

  struct seenByReceiverIdLlEntry *seenByReceiverIds;

  uint32_t addr; // ICAO address
  addrtype_t addrtype; // highest priority address type seen for this aircraft
  int64_t seen; // Time (millis) at which the last packet with reliable address was received
  int64_t seen_pos; // Time (millis) at which the last position was received

  uint32_t messages; // Number of Mode S messages received
  int32_t onActiveList;
  uint32_t receiverCount;
  uint32_t category; // Aircraft category A0 - D7 encoded as a single hex byte. 00 = unset
                     //
  int64_t category_updated;

  // ----

  int64_t trace_next_mw; // timestamp for next full trace write to /run (tmpfs)
  int64_t trace_next_perm; // timestamp for next trace write to history_dir (disk)
  int64_t lastSignalTimestamp; // timestamp the last message with RSSI was received
  int64_t trace_perm_last_timestamp; // timestamp for last trace point written to disk

  fourState *trace_current; // uncompressed most recent points in the trace
  stateChunk *trace_chunks; // compressed chunks of trace

  int32_t trace_current_max;
  int32_t trace_current_len; // number of points in our uncompressed most recent trace portion
  int32_t trace_len; // total number of points in the trace
  int32_t trace_chunk_len; // how many stateChunks are saved for this aircraft
  int32_t trace_write; // signal for writing the trace

  int32_t trace_writeCounter; // how many points where added since the complete trace was written to memory
  int32_t baro_alt; // Altitude (Baro)
  int32_t alt_reliable;
  int32_t geom_alt; // Altitude (Geometric)

  int32_t geom_delta; // Difference between Geometric and Baro altitudes
  uint32_t signalNext; // next index of signalLevel to use

  // ----

  double signalLevel[8]; // Last 8 Signal Amplitudes

  // ----

  float rr_lat; // very rough receiver latitude
  float rr_lon; // very rough receiver longitude
  int64_t rr_seen; // when we noted this rough position


  int64_t seenAdsbReliable; // last time we saw a reliable SOURCE_ADSB positions from this aircraft
  int64_t addrtype_updated;
  float tat;
  uint16_t nogpsCounter;
  uint16_t receiverIdsNext;
  int64_t seenPosReliable; // last time we saw a reliable position
  int64_t lastPosReceiverId;

  // ---- the following section has 9 instead of 8 times 8 bytes. but that's not critical as long as the 8 byte alignment is ok

  uint32_t pos_nic; // NIC of last computed position
  uint32_t pos_rc; // Rc of last computed position
  double lat; // Coordinates obtained from CPR encoded data
  double lon; // Coordinates obtained from CPR encoded data
  float pos_reliable_odd; // Number of good global CPRs, indicates position reliability
  float pos_reliable_even;
  int16_t traceWrittenForYesterday; // the permanent trace has been written for the previous day
  uint16_t mlatEPU;
  float gs_last_pos; // Save a groundspeed associated with the last position

  float wind_speed;
  float wind_direction;
  int32_t wind_altitude;
  float oat;
  int64_t wind_updated;
  int64_t oat_updated;

  // ----

  int32_t baro_rate; // Vertical rate (barometric)
  int32_t geom_rate; // Vertical rate (geometric)
  uint32_t ias;
  uint32_t tas;
  uint32_t squawk; // Squawk
  uint32_t squawkTentative; // require the same squawk code twice to accept it
  uint32_t nav_altitude_mcp; // FCU/MCP selected altitude
  uint32_t nav_altitude_fms; // FMS selected altitude
  uint32_t cpr_odd_lat;
  uint32_t cpr_odd_lon;
  uint32_t cpr_odd_nic;
  uint32_t cpr_odd_rc;
  uint32_t cpr_even_lat;
  uint32_t cpr_even_lon;
  uint32_t cpr_even_nic;
  uint32_t cpr_even_rc;

  // ----

  float nav_qnh; // Altimeter setting (QNH/QFE), millibars
  float nav_heading; // target heading, degrees (0-359)
  float gs;
  float mach;
  float track; // Ground track
  float track_rate; // Rate of change of ground track, degrees/second
  float roll; // Roll angle, degrees right
  float mag_heading; // Magnetic heading

  float true_heading; // True heading
  float calc_track; // Calculated Ground track
  int64_t next_reduce_forward_DF11;
  char callsign[16]; // Flight number

  // ----

  emergency_t emergency; // Emergency/priority status
  airground_t airground; // air/ground status
  nav_modes_t nav_modes; // enabled modes (autopilot, vnav, etc)
  cpr_type_t cpr_odd_type;
  cpr_type_t cpr_even_type;
  nav_altitude_source_t nav_altitude_src;  // source of altitude used by automation
  int32_t modeA_hit; // did our squawk match a possible mode A reply in the last check period?
  int32_t modeC_hit; // did our altitude match a possible mode C reply in the last check period?

  // data extracted from opstatus etc
  int32_t adsb_version; // ADS-B version (from ADS-B operational status); -1 means no ADS-B messages seen
  int32_t adsr_version; // As above, for ADS-R messages
  int32_t tisb_version; // As above, for TIS-B messages
  heading_type_t adsb_hrd; // Heading Reference Direction setting (from ADS-B operational status)
  heading_type_t adsb_tah; // Track Angle / Heading setting (from ADS-B operational status)
  int32_t globe_index; // custom index of the planes area on the globe
  sil_type_t sil_type; // SIL supplement from TSS or opstatus

  uint32_t nic_a : 1; // nic supplement a from opstatus
  uint32_t nic_c : 1; // nic supplement c from opstatus
  uint32_t nic_baro : 1; // nic baro supplement from tss or opstatus
  uint32_t nac_p : 4; // nacp from tss or opstatus
  uint32_t nac_v : 3; // nacv from airborne velocity or opstatus
  uint32_t sil : 2; // sil from tss or opstatus
  uint32_t gva : 2; // gva from opstatus
  uint32_t sda : 2; // sda from opstatus
  // 16 bit
  uint32_t alert : 1; // fs flight status alert bit
  uint32_t spi : 1; // fs flight status spi (special position identification) bit
  uint32_t pos_surface : 1; // (a->airground == ag_ground) associated with current position
  uint32_t last_cpr_type : 2; // mm->cpr_type associated with current position
  uint32_t tracePosBuffered : 1; // denotes if a->trace[a->trace_len] has a valid state buffered in it
  uint32_t surfaceCPR_allow_ac_rel : 1; // allow surface cpr relative to last known aircraft location
  uint32_t localCPR_allow_ac_rel : 1; // allow local cpr relative to last known aircraft location
  // 24 bit
  uint32_t padding_b : 8;
  // 32 bit !!

  // ----

  data_validity callsign_valid;
  data_validity baro_alt_valid;
  data_validity geom_alt_valid;
  data_validity geom_delta_valid;
  data_validity gs_valid;
  data_validity ias_valid;
  data_validity tas_valid;
  data_validity mach_valid;

  data_validity track_valid;
  data_validity track_rate_valid;
  data_validity roll_valid;
  data_validity mag_heading_valid;
  data_validity true_heading_valid;
  data_validity baro_rate_valid;
  data_validity geom_rate_valid;
  data_validity nic_a_valid;

  data_validity nic_c_valid;
  data_validity nic_baro_valid;
  data_validity nac_p_valid;
  data_validity nac_v_valid;
  data_validity sil_valid;
  data_validity gva_valid;
  data_validity sda_valid;
  data_validity squawk_valid;

  data_validity emergency_valid;
  data_validity airground_valid;
  data_validity nav_qnh_valid;
  data_validity nav_altitude_mcp_valid;
  data_validity nav_altitude_fms_valid;
  data_validity nav_altitude_src_valid;
  data_validity nav_heading_valid;
  data_validity nav_modes_valid;

  data_validity cpr_odd_valid; // Last seen even CPR message
  data_validity cpr_even_valid; // Last seen odd CPR message
  data_validity position_valid;
  data_validity alert_valid;
  data_validity spi_valid;

  int64_t seenPosGlobal; // seen global CPR or other hopefully reliable position
  double latReliable; // last reliable position based on json_reliable threshold
  double lonReliable; // last reliable position based on json_reliable threshold
  char typeCode[4];
  char registration[12];
  char typeLong[63];
  uint8_t dbFlags;
  uint16_t receiverIds[RECEIVERIDBUFFER]; // RECEIVERIDBUFFER = 12

  int64_t next_reduce_forward_status;
  unsigned char acas_ra[7]; // mm->MV from last acas RA message
  unsigned char acas_flags; // maybe use for some flags, would be padding otherwise
  data_validity acas_ra_valid;
  float gs_reliable;
  float track_reliable;
  uint64_t canary1;
  int64_t squawkTentativeChanged;
  double magneticDeclination;
  int64_t updatedDeclination;

  uint16_t pos_nic_reliable;
  uint16_t pos_rc_reliable;
  int32_t trackUnreliable;

  uint64_t receiverId;

  // previous position and timestamp
  double prev_lat; // previous latitude
  double prev_lon; // previous longitude
  int64_t prev_pos_time; // time the previous position was received

  int32_t speedUnreliable;
  uint32_t lastStatusDiscarded;

  int64_t nextJsonPortOutput;

  float receiver_distance;
  float receiver_direction;

  data_validity mlat_pos_valid;
  double mlat_lat;
  double mlat_lon;

  data_validity pos_reliable_valid;

  // last reliable SOURCE_ADSB positions from this aircraft
  double seenAdsbLat;
  double seenAdsbLon;

  int64_t lastStatusTs;
  int64_t lastOverrideTs;

  // DANGER, this section is zeroed when saving and loading data

  char zeroStart;

  float messageRate;
  uint16_t messageRateAcc[MESSAGE_RATE_CALC_POINTS];
  int64_t nextMessageRateCalc;

  uint32_t disc_cache_index;
  uint32_t cpr_cache_index;
  struct cpr_cache disc_cache[DISCARD_CACHE];
  struct cpr_cache cpr_cache[CPR_CACHE];

  // keep trace cache after cpr / disc cache
  // stuff above is put into

  struct traceCache traceCache;

  uint32_t trace_chunk_overall_bytes;

  int8_t initialTraceWriteDone;

  char zeroEnd;
};

/* Mode A/C tracking is done separately, not via the aircraft list,
 * and via a flat array rather than a list since there are only 4k possible values
 * (nb: we ignore the ident/SPI bit when tracking)
 */
extern uint32_t modeAC_count[4096];
extern uint32_t modeAC_match[4096];
extern uint32_t modeAC_age[4096];

/* is this bit of data valid? */
static inline void
updateValidity (data_validity *v, int64_t now, int64_t expiration_timeout)
{
    if (v->source == SOURCE_INVALID)
        return;
    int stale = (now > v->updated + TRACK_STALE);
    if (stale != v->stale)
        v->stale = stale;

    if (v->source == SOURCE_JAERO) {
        if (now > v->updated + Modes.trackExpireJaero)
            v->source = SOURCE_INVALID;
    } else if (v->source == SOURCE_INDIRECT && Modes.debug_rough_receiver_location) {
        if (now > v->updated + TRACK_EXPIRE_ROUGH)
            v->source = SOURCE_INVALID;
    } else {
        if (now > v->updated + expiration_timeout)
            v->source = SOURCE_INVALID;
    }
}

/* is this bit of data valid? */
static inline int
trackDataValid (const data_validity *v)
{
  return (v->source != SOURCE_INVALID);
}

static inline int posReliable(struct aircraft *a) {
    if (!trackDataValid(&a->position_valid)) {
        return 0;
    }
    if (a->position_valid.source == SOURCE_JAERO
            || a->position_valid.source == SOURCE_MLAT
            || a->position_valid.source == SOURCE_INDIRECT) {
        return 1;
    }
    int reliable = Modes.json_reliable;
    // disable this extra requirement for the moment:
    if (0 && Modes.position_persistence > reliable && reliable > 1 && (a->addr & MODES_NON_ICAO_ADDRESS || a->addrtype == ADDR_TISB_ICAO || a->addrtype == ADDR_ADSR_ICAO)) {
        reliable += 1; // require additional reliability for non-icao hex addresses
    }

    if (a->pos_reliable_odd >= reliable && a->pos_reliable_even >= reliable) {
        return 1;
    }

    return 0;
}
static inline int altBaroReliable(struct aircraft *a) {
    if (!trackDataValid(&a->baro_alt_valid))
        return 0;
    if (a->position_valid.source == SOURCE_JAERO)
        return 1;
    if (a->alt_reliable >= 2 * Modes.json_reliable)
        return 1;

    return 0;
}

static inline int
trackVState (int64_t now, const data_validity *v, const data_validity *pos_valid)
{
    if (pos_valid->source <= SOURCE_JAERO) {
        // allow normal expiration time for shitty position sources
        return (v->source != SOURCE_INVALID);
    }
    // reduced expiration time for good sources
    return (v->source != SOURCE_INVALID && now < v->updated + 35 * SECONDS);

}

static inline int altBaroReliableTrace(int64_t now, struct aircraft *a) {
    if (altBaroReliable(a) && trackVState(now, &a->baro_alt_valid, &a->pos_reliable_valid))
        return 1;
    else
        return 0;
}

/* what's the age of this data, in milliseconds? */
static inline int64_t
trackDataAge (int64_t now, const data_validity *v)
{
  if (v->updated >= now)
    return 0;
  return (now - v->updated);
}

static inline double toRad(double degrees) {
    return degrees * (M_PI / 180.0);
}
static inline double toDeg(double radians) {
    return radians * (180.0 / M_PI);
}
// calculate great circle distance in meters
//
void to_state(struct aircraft *a, struct state *new, int64_t now, int on_ground, float track, int stale);
void to_state_all(struct aircraft *a, struct state_all *new, int64_t now);
void from_state_all(struct state_all *in, struct state *in2, struct aircraft *a , int64_t ts);

/* Update aircraft state from data in the provided mesage.
 * Return the tracked aircraft.
 */
struct modesMessage;
struct aircraft *trackUpdateFromMessage (struct modesMessage *mm);

void trackMatchAC(int64_t now);
void trackRemoveStale(int64_t now);

void updateValidities(struct aircraft *a, int64_t now);

struct aircraft *trackFindAircraft(uint32_t addr);

/* Convert from a (hex) mode A value to a 0-4095 index */
static inline unsigned
modeAToIndex (unsigned modeA)
{
  return (modeA & 0x0007) | ((modeA & 0x0070) >> 1) | ((modeA & 0x0700) >> 2) | ((modeA & 0x7000) >> 3);
}

/* Convert from a 0-4095 index to a (hex) mode A value */
static inline unsigned
indexToModeA (unsigned index)
{
  return (index & 0007) | ((index & 0070) << 1) | ((index & 0700) << 2) | ((index & 07000) << 3);
}

static inline int bogus_lat_lon(double lat, double lon) {
    if (fabs(lat) >= 90.0 || fabs(lon) >= 180.0)
        return 1;
    if (lat == 0 && (lon == -90 || lon == 90 || lon == 0))
        return 1;
    if (fabs(lat) < 0.01 && fabs(lon) < 0.01)
        return 1;
    return 0;
}
static inline int get8bitSignal(struct aircraft *a) {
    double signal = (a->signalLevel[0] + a->signalLevel[1] + a->signalLevel[2] + a->signalLevel[3] +
            a->signalLevel[4] + a->signalLevel[5] + a->signalLevel[6] + a->signalLevel[7]) / 8.0;
    signal = sqrt(signal) * 255.0;
    if (signal > 255) signal = 255;
    if (signal < 1 && signal > 0) signal = 1;
    return (int) nearbyint(signal);
}

static inline const char *nonIcaoSpace(struct aircraft *a) {
    if (a->addr & MODES_NON_ICAO_ADDRESS) {
        return "";
    } else {
        return " ";
    }
}

#endif
