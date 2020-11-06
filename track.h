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

#define HOURS (60*60*1000ULL)
#define MINUTES (60*1000ULL)
#define SECONDS (1000ULL)


/* Maximum age of tracked aircraft in milliseconds */
#define TRACK_AIRCRAFT_TTL (10 * 24 * HOURS)
// 10 days should cover almost all turn-arounds.
// due to new receiver relative stuff reduce this to 10 days
//
// reduce this to 5h if we don't save state to disk
#define TRACK_AIRCRAFT_NO_STATE_TTL (5 * HOURS)
//
#define TRACK_AIRCRAFT_NO_POS_TTL (5 * MINUTES)
// only 5 minutes for aircraft without position

#define TRACK_AIRCRAFT_NON_ICAO_TTL (5 * HOURS)
// 5 hours for NON_ICAO aircraft

/* Maximum age of a tracked aircraft with only 1 message received, in milliseconds */
#define TRACK_AIRCRAFT_ONEHIT_TTL (1 * MINUTES)

/* Minimum number of repeated Mode A/C replies with a particular Mode A code needed in a
 * 1 second period before accepting that code.
 */
#define TRACK_MODEAC_MIN_MESSAGES 4

/* Special value for Rc unknown */
#define RC_UNKNOWN 0

#define ALTITUDE_BARO_RELIABLE_MAX 20

// 15 seconds
#define TRACK_STALE (15*SECONDS)
// 45 seconds
#define TRACK_EXPIRE (45*SECONDS)
// 90 seconds
#define TRACK_EXPIRE_LONG (90*SECONDS)
// 33 minutes
#define TRACK_EXPIRE_JAERO (33*MINUTES)

// 2.5 seconds maximum between messages used for calculating wind / temperature
#define TRACK_WT_TIMEOUT (2500)

#define RECEIVERIDBUFFER (12)

// data moves through three states:
//  fresh: data is valid. Updates from a less reliable source are not accepted.
//  stale: data is valid. Updates from a less reliable source are accepted.
//  expired: data is not valid.

typedef struct
{
  uint64_t updated; /* when it arrived */
  uint64_t next_reduce_forward; /* when to next forward the data for reduced beast output */
  datasource_t source:8; /* where the data came from */
  datasource_t last_source:8; /* where the data came from */
  int8_t stale; /* if it's stale 1 / 0 */
  unsigned padding:8;
  int padding2;
} data_validity;
// size must be multiple of 64 bits so it can be aligned in struct aircraft.

struct state_flags
{
    unsigned on_ground:1;
    unsigned stale:1;
    unsigned leg_marker:1;
    unsigned altitude_valid:1;
    unsigned gs_valid:1;
    unsigned track_valid:1;
    unsigned rate_valid:1;
    unsigned rate_geom:1;
    unsigned altitude_geom:1;
    int padding:7;
} __attribute__ ((__packed__));

/* Structure representing one point in the aircraft trace */
struct state
{
  uint64_t timestamp:48;
  struct state_flags flags; // 16 bits

  int32_t lat;
  int32_t lon;

  int16_t altitude;
  int16_t gs;
  int16_t track;
  int16_t rate;
} __attribute__ ((__packed__));

struct state_all
{
  char callsign[8]; // Flight number

  int16_t altitude_geom;
  int16_t baro_rate;
  int16_t geom_rate;

  uint16_t squawk; // Squawk
  uint16_t nav_altitude_mcp; // FCU/MCP selected altitude
  uint16_t nav_altitude_fms; // FMS selected altitude

  int16_t nav_qnh; // Altimeter setting (QNH/QFE), millibars
  int16_t nav_heading; // target heading, degrees (0-359)
  int16_t gs;
  int16_t mach;

  int16_t track; // Ground track
  int16_t track_rate; // Rate of change of ground track, degrees/second
  int16_t roll; // Roll angle, degrees right
  int16_t mag_heading; // Magnetic heading
  int16_t true_heading; // True heading

  int wind_direction:10;
  int wind_speed:10;
  int oat:10;
  int tat:10;

  unsigned category:8; // Aircraft category A0 - D7 encoded as a single hex byte. 00 = unset

  unsigned pos_nic:8; // NIC of last computed position
  unsigned pos_rc:16; // Rc of last computed position
  emergency_t emergency:4; // Emergency/priority status
  addrtype_t addrtype:4; // highest priority address type seen for this aircraft
  nav_modes_t nav_modes:7; // enabled modes (autopilot, vnav, etc)
  airground_t airground:2; // air/ground status
  nav_altitude_source_t nav_altitude_src:3;  // source of altitude used by automation
  sil_type_t sil_type:3; // SIL supplement from TSS or opstatus

  unsigned tas:12;
  unsigned ias:12;

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
  unsigned altitude_baro_valid:1;
  unsigned altitude_geom_valid:1;
  unsigned geom_delta_valid:1;
  unsigned gs_valid:1;
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
  unsigned position_valid:1;
  unsigned alert_valid:1;
  unsigned spi_valid:1;
  unsigned wind_valid:1;
  unsigned temp_valid:1;

  unsigned padding:22;
} __attribute__ ((__packed__));

/* Structure used to describe the state of one tracked aircraft */
struct aircraft
{
  struct aircraft *next; // Next aircraft in our linked list
  uint32_t addr; // ICAO address
  addrtype_t addrtype; // highest priority address type seen for this aircraft
  uint64_t seen; // Time (millis) at which the last packet was received
  uint64_t seen_pos; // Time (millis) at which the last position was received

  uint32_t size_struct_aircraft; // size of this struct
  uint32_t messages; // Number of Mode S messages received
  int trace_len; // current number of points in the trace
  int trace_write; // signal for writing the trace
  int trace_full_write; // signal for writing the complete trace
  int trace_alloc; // current number of allocated points
  int destroy; // aircraft is being deleted
  int signalNext; // next index of signalLevel to use

  // ----

  struct state *trace; // array of positions representing the aircrafts trace/trail
  struct state_all *trace_all;
  int altitude_baro; // Altitude (Baro)
  int alt_reliable;
  int altitude_geom; // Altitude (Geometric)
  int geom_delta; // Difference between Geometric and Baro altitudes

  uint64_t trace_next_mw; // timestamp for next full trace write to /run (tmpfs)
  uint64_t trace_next_fw; // timestamp for next full trace write to history_dir (disk)
  double trace_llat; // last saved lat
  double trace_llon; // last saved lon

  // ----

  double signalLevel[8]; // Last 8 Signal Amplitudes

  // ----

  float rr_lat; // very rough receiver latitude
  float rr_lon; // very rough receiver longitude
  uint64_t rr_seen; // when we noted this rough position
  uint64_t category_updated;
  unsigned category; // Aircraft category A0 - D7 encoded as a single hex byte. 00 = unset
  uint16_t receiverCountMlat;
  uint16_t paddingabc;


  uint64_t padding23;
  uint64_t addrtype_updated;
  float tat;
  uint16_t no_signal_count; // consecutive messages without signal strength specified
  uint16_t receiverIdsNext;
  uint64_t seenPosReliable; // last time we saw a reliable position
  uint64_t lastPosReceiverId;

  // ---- the following section has 9 instead of 8 times 8 bytes. but that's not critical as long as the 8 byte alignment is ok

  unsigned pos_nic; // NIC of last computed position
  unsigned pos_rc; // Rc of last computed position
  double lat; // Coordinates obtained from CPR encoded data
  double lon; // Coordinates obtained from CPR encoded data
  int pos_reliable_odd; // Number of good global CPRs, indicates position reliability
  int pos_reliable_even;
  int padding1234; // unused
  float gs_last_pos; // Save a groundspeed associated with the last position

  float wind_speed;
  float wind_direction;
  int wind_altitude;
  float oat;
  uint64_t wind_updated;
  uint64_t oat_updated;

  // ----

  int baro_rate; // Vertical rate (barometric)
  int geom_rate; // Vertical rate (geometric)
  unsigned ias;
  unsigned tas;
  unsigned squawk; // Squawk
  unsigned padding2344;
  unsigned nav_altitude_mcp; // FCU/MCP selected altitude
  unsigned nav_altitude_fms; // FMS selected altitude
  unsigned cpr_odd_lat;
  unsigned cpr_odd_lon;
  unsigned cpr_odd_nic;
  unsigned cpr_odd_rc;
  unsigned cpr_even_lat;
  unsigned cpr_even_lon;
  unsigned cpr_even_nic;
  unsigned cpr_even_rc;

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
  uint64_t next_reduce_forward_DF11;
  char callsign[16]; // Flight number

  // ----

  emergency_t emergency; // Emergency/priority status
  airground_t airground; // air/ground status
  nav_modes_t nav_modes; // enabled modes (autopilot, vnav, etc)
  cpr_type_t cpr_odd_type;
  cpr_type_t cpr_even_type;
  nav_altitude_source_t nav_altitude_src;  // source of altitude used by automation
  int modeA_hit; // did our squawk match a possible mode A reply in the last check period?
  int modeC_hit; // did our altitude match a possible mode C reply in the last check period?

  // data extracted from opstatus etc
  int adsb_version; // ADS-B version (from ADS-B operational status); -1 means no ADS-B messages seen
  int adsr_version; // As above, for ADS-R messages
  int tisb_version; // As above, for TIS-B messages
  heading_type_t adsb_hrd; // Heading Reference Direction setting (from ADS-B operational status)
  heading_type_t adsb_tah; // Track Angle / Heading setting (from ADS-B operational status)
  int globe_index; // custom index of the planes area on the globe
  sil_type_t sil_type; // SIL supplement from TSS or opstatus

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
  unsigned pos_surface : 1; // (a->airground == AG_GROUND) associated with current position
  unsigned last_cpr_type : 2; // mm->cpr_type associated with current position
  // 20 bit ??
  unsigned padding_b : 11;
  // 32 bit !!

  // ----

  data_validity callsign_valid;
  data_validity altitude_baro_valid;
  data_validity altitude_geom_valid;
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

  uint64_t seenPosGlobal; // seen global CPR or other hopefully reliable position
  double latReliable; // last reliable position based on json_reliable threshold
  double lonReliable; // last reliable position based on json_reliable threshold
  char typeCode[4];
  char registration[12];
  char typeLong[63];
  uint8_t dbFlags;
  uint16_t receiverIds[RECEIVERIDBUFFER]; // RECEIVERIDBUFFER = 12

  struct modesMessage *first_message; // A copy of the first message we received for this aircraft.
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
updateValidity (data_validity *v, uint64_t now, uint64_t expiration_timeout)
{
    if (v->source == SOURCE_INVALID)
        return;
    v->stale = (now > v->updated + TRACK_STALE);
    if (v->source == SOURCE_JAERO) {
        if (now > v->updated + TRACK_EXPIRE_JAERO)
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


static inline int
trackVState (uint64_t now, const data_validity *v, const data_validity *pos_valid)
{
    // source is valid, allow normal expiration time for shitty position sources
    if (pos_valid->source > SOURCE_JAERO)
        return (v->source != SOURCE_INVALID && now < v->updated + TRACK_EXPIRE_LONG);

    return (v->source != SOURCE_INVALID);
}

/* what's the age of this data, in milliseconds? */
static inline uint64_t
trackDataAge (uint64_t now, const data_validity *v)
{
  if (v->updated >= now)
    return 0;
  return (now - v->updated);
}

// calculate great circle distance in meters
double greatcircle(double lat0, double lon0, double lat1, double lon1);
void to_state_all(struct aircraft *a, struct state_all *new, uint64_t now);

/* Update aircraft state from data in the provided mesage.
 * Return the tracked aircraft.
 */
struct modesMessage;
struct aircraft *trackUpdateFromMessage (struct modesMessage *mm);

/* Call periodically */
void trackPeriodicUpdate ();

void trackForceStats();

void updateValidities(struct aircraft *a, uint64_t now);

void from_state_all(struct state_all *in, struct aircraft *a , uint64_t ts);
void freeAircraft(struct aircraft *a);
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

static inline int
min (int a, int b)
{
  if (a < b)
    return a;
  else
    return b;
}

static inline int
max (int a, int b)
{
  if (a > b)
    return a;
  else
    return b;
}

static inline double
norm_diff (double a, double pi)
{
    if (a < -pi)
        a +=  2 * pi;
    if (a > pi)
        a -=  2 * pi;

    return a;
}
static inline double
norm_angle (double a, double pi)
{
    if (a < 0)
        a +=  2 * pi;
    if (a >= 2 * pi)
        a -=  2 * pi;

    return a;
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
    return nearbyint(signal);
}
#endif
