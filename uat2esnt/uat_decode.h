// Part of dump978, a UAT decoder.
//
// Copyright 2015, Oliver Jowett <oliver@mutability.co.uk>
//
// This file is free software: you may copy, redistribute and/or modify it  
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 2 of the License, or (at your  
// option) any later version.  
//
// This file is distributed in the hope that it will be useful, but  
// WITHOUT ANY WARRANTY; without even the implied warranty of  
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU  
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License  
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef UAT_DECODE_H
#define UAT_DECODE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "uat.h"

//
// Datatypes
//

typedef enum { AQ_ADSB_ICAO=0, AQ_NATIONAL=1, AQ_TISB_ICAO=2, AQ_TISB_OTHER=3, AQ_VEHICLE=4,
               AQ_FIXED_BEACON=5, AQ_RESERVED_6=6, AQ_RESERVED_7=7 } address_qualifier_t;
typedef enum { ALT_INVALID=0, ALT_BARO, ALT_GEO } altitude_type_t;
typedef enum { AG_SUBSONIC=0, AG_SUPERSONIC=1, AG_GROUND=2, AG_RESERVED=3 } airground_state_t;
typedef enum { TT_INVALID=0, TT_TRACK, TT_MAG_HEADING, TT_TRUE_HEADING } track_type_t;
typedef enum { HT_INVALID=0, HT_MAGNETIC, HT_TRUE } heading_type_t;
typedef enum { CS_INVALID=0, CS_CALLSIGN, CS_SQUAWK } callsign_type_t;

struct uat_adsb_mdb {
    // presence bits
    uint32_t has_sv : 1;
    uint32_t has_ms : 1;
    uint32_t has_auxsv : 1;

    uint32_t position_valid : 1;
    uint32_t ns_vel_valid : 1;
    uint32_t ew_vel_valid : 1;
    uint32_t speed_valid : 1;
    uint32_t dimensions_valid : 1;

    //
    // HDR
    //
    uint8_t mdb_type;
    address_qualifier_t address_qualifier;
    uint32_t address;

    //
    // SV
    //

    // if position_valid:
    double lat;
    double lon;

    altitude_type_t altitude_type;
    int32_t altitude; // in feet
    
    uint8_t nic;

    airground_state_t airground_state;

    // if ns_vel_valid:
    int16_t ns_vel; // in kts
    // if ew_vel_valid:
    int16_t ew_vel; // in kts
    
    track_type_t track_type;
    uint16_t track;

    // if speed_valid:
    uint16_t speed; // in kts

    altitude_type_t vert_rate_source;
    int16_t vert_rate; // in ft/min

    // if lengthwidth_valid:
    double length; // in meters (just to be different)
    double width;  // in meters (just to be different)
    uint32_t position_offset : 1;  // true if Position Offset Applied

    uint32_t utc_coupled : 1;      // true if UTC Coupled flag is set (ADS-B)
    uint8_t tisb_site_id;     // TIS-B site ID, or zero in ADS-B messages
    
    //
    // MS
    //

    uint8_t emitter_category;
    callsign_type_t callsign_type;
    char callsign[9];
    uint8_t emergency_status;
    uint8_t uat_version;
    uint8_t sil;
    uint8_t transmit_mso;
    uint8_t nac_p;
    uint8_t nac_v;
    uint8_t nic_baro;
  
    // capabilities:
    uint32_t has_cdti : 1;
    uint32_t has_acas : 1;
    // operational modes:
    uint32_t acas_ra_active : 1;
    uint32_t ident_active : 1;
    uint32_t atc_services : 1;

    heading_type_t heading_type;

    //
    // AUXSV

    altitude_type_t sec_altitude_type;
    int32_t sec_altitude; // in feet
};


static inline int64_t mstime(void) {
    struct timeval tv;
    int64_t mst;

    gettimeofday(&tv, NULL);
    mst = ((int64_t) tv.tv_sec)*1000;
    mst += tv.tv_usec / 1000;
    return mst;
}

//
// Decode/display prototypes
//

void uat_decode_adsb_mdb(uint8_t *frame, struct uat_adsb_mdb *mdb);
void uat_display_adsb_mdb(const struct uat_adsb_mdb *mdb, FILE *to);

//
// UPLINK 
//

// assume 6 byte frames: 2 header bytes, 4 byte payload
// (TIS-B heartbeat with one address, or empty FIS-B APDU)
#define UPLINK_MAX_INFO_FRAMES (424/6)

struct fisb_apdu {
    uint32_t a_flag : 1;
    uint32_t g_flag : 1;
    uint32_t p_flag : 1;
    uint32_t s_flag : 1;
    uint32_t monthday_valid : 1;
    uint32_t seconds_valid : 1;

    uint16_t product_id;
    uint8_t month;   // if monthday_valid
    uint8_t day;     // if monthday_valid
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds; // if seconds_valid

    uint16_t length;
    uint8_t *data;
};

struct uat_uplink_info_frame {
    uint32_t is_fisb : 1;

    uint16_t length;
    uint8_t type;
    uint8_t *data; // points within the containing appdata

    // if is_fisb:
    struct fisb_apdu fisb;
};

struct uat_uplink_mdb {
    uint32_t position_valid : 1;
    uint32_t utc_coupled : 1;
    uint32_t app_data_valid : 1;

    // if position_valid:
    double lat;
    double lon;

    uint8_t slot_id;
    uint8_t tisb_site_id;

    // if app_data_valid:
    uint8_t app_data[424];
    unsigned num_info_frames;
    struct uat_uplink_info_frame info_frames[UPLINK_MAX_INFO_FRAMES];    
};

void uat_decode_uplink_mdb(uint8_t *frame, struct uat_uplink_mdb *mdb);
void uat_display_uplink_mdb(const struct uat_uplink_mdb *mdb, FILE *to);

#endif
