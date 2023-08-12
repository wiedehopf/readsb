// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// comm_b.c: Comm-B message decoding
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
//
// Copyright (c) 2017 FlightAware, LLC
// Copyright (c) 2017 Oliver Jowett <oliver@mutability.co.uk>
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

#include "readsb.h"
#include "ais_charset.h"

typedef int (*CommBDecoderFn)(struct modesMessage *, bool);

static int decodeEmptyResponse(struct modesMessage *mm, bool store);
static int decodeBDS10(struct modesMessage *mm, bool store);
static int decodeBDS17(struct modesMessage *mm, bool store);
static int decodeBDS20(struct modesMessage *mm, bool store);
static int decodeBDS30(struct modesMessage *mm, bool store);
static int decodeBDS40(struct modesMessage *mm, bool store);
static int decodeBDS50(struct modesMessage *mm, bool store);
static int decodeBDS60(struct modesMessage *mm, bool store);
static int decodeBDS44(struct modesMessage *mm, bool store);

static CommBDecoderFn comm_b_decoders[] = {
    &decodeEmptyResponse,
    &decodeBDS10,
    &decodeBDS20,
    &decodeBDS30,
    &decodeBDS17,
    &decodeBDS40,
    &decodeBDS50,
    &decodeBDS60,
    &decodeBDS44
};

void decodeCommB(struct modesMessage *mm) {
    mm->commb_format = COMMB_UNKNOWN;

    // If DR or UM are set, this message is _probably_ noise
    // as nothing really seems to use the multisite broadcast stuff?
    // Also skip anything that had errors corrected
    if (mm->DR != 0 || mm->UM != 0 || mm->correctedbits > 0) {
        return;
    }

    // This is a bit hairy as we don't know what the requested register was
    int bestScore = 0;
    CommBDecoderFn bestDecoder = NULL;
    int ambiguous = 0;

    for (unsigned i = 0; i < (sizeof (comm_b_decoders) / sizeof (comm_b_decoders[0])); ++i) {
        int score = comm_b_decoders[i](mm, false);
        if (score > bestScore) {
            bestScore = score;
            bestDecoder = comm_b_decoders[i];
            ambiguous = 0;
        } else if (score == bestScore) {
            ambiguous = 1;
        }
    }

    if (bestDecoder) {
        if (ambiguous) {
            mm->commb_format = COMMB_AMBIGUOUS;
        } else {
            // decode it
            bestDecoder(mm, true);
        }
    }
}

static int decodeEmptyResponse(struct modesMessage *mm, bool store) {
    for (unsigned i = 0; i < 7; ++i) {
        if (mm->MB[i] != 0) {
            return 0;
        }
    }

    if (store) {
        mm->commb_format = COMMB_EMPTY_RESPONSE;
    }

    return 56;
}

// BDS1,0 Datalink capabilities

static int decodeBDS10(struct modesMessage *mm, bool store) {
    unsigned char *msg = mm->MB;

    // BDS identifier
    if (msg[0] != 0x10) {
        return 0;
    }

    // Reserved bits
    if (getbits(msg, 10, 14) != 0) {
        return 0;
    }

    // Looks plausible.

    if (store) {
        mm->commb_format = COMMB_DATALINK_CAPS;
    }

    return 56;
}

// BDS1,7 Common usage GICB capability report

static int decodeBDS17(struct modesMessage *mm, bool store) {
    unsigned char *msg = mm->MB;

    // reserved bits
    if (getbits(msg, 25, 56) != 0) {
        return 0;
    }

    int score = 0;
    if (getbit(msg, 7)) {
        score += 1; // 2,0 aircraft identification
    } else {
        // BDS2,0 is on almost everything
        score -= 2;
    }

    // unlikely bits
    if (getbit(msg, 10)) { // 4,1 next waypoint identifier
        score -= 2;
    }
    if (getbit(msg, 11)) { // 4,2 next waypoint position
        score -= 2;
    }
    if (getbit(msg, 12)) { // 4,3 next waypoint information
        score -= 2;
    }
    if (getbit(msg, 13)) { // 4,4 meterological routine report
        score -= 2;
    }
    if (getbit(msg, 14)) { // 4,4 meterological hazard report
        score -= 2;
    }
    if (getbit(msg, 20)) { // 5,4 waypoint 1
        score -= 2;
    }
    if (getbit(msg, 21)) { // 5,5 waypoint 2
        score -= 2;
    }
    if (getbit(msg, 22)) { // 5,6 waypoint 3
        score -= 2;
    }

    if (getbit(msg, 1) && getbit(msg, 2) && getbit(msg, 3) && getbit(msg, 4) && getbit(msg, 5)) {
        // looks like ES capable
        score += 5;
        if (getbit(msg, 6)) {
            // ES EDI
            score += 1;
        }
    } else if (!getbit(msg, 1) && !getbit(msg, 2) && !getbit(msg, 3) && !getbit(msg, 4) && !getbit(msg, 5) && !getbit(msg, 6)) {
        // not ES capable
        score += 1;
    } else {
        // partial ES support, unlikely
        score -= 12;
    }

    if (getbit(msg, 16) && getbit(msg, 24)) {
        // track/turn, heading/speed
        score += 2;
        if (getbit(msg, 9)) {
            // vertical intent
            score += 1;
        }
    } else if (!getbit(msg, 16) && !getbit(msg, 24) && !getbit(msg, 9)) {
        // neither
        score += 1;
    } else {
        // unlikely
        score -= 6;
    }

    if (store) {
        mm->commb_format = COMMB_GICB_CAPS;
    }

    return score;
}

// BDS2,0 Aircraft identification

static int decodeBDS20(struct modesMessage *mm, bool store) {
    char callsign[sizeof(mm->callsign)];
    unsigned char *msg = mm->MB;

    // BDS identifier
    if (msg[0] != 0x20) {
        return 0;
    }

    callsign[0] = ais_charset[getbits(msg, 9, 14)];
    callsign[1] = ais_charset[getbits(msg, 15, 20)];
    callsign[2] = ais_charset[getbits(msg, 21, 26)];
    callsign[3] = ais_charset[getbits(msg, 27, 32)];
    callsign[4] = ais_charset[getbits(msg, 33, 38)];
    callsign[5] = ais_charset[getbits(msg, 39, 44)];
    callsign[6] = ais_charset[getbits(msg, 45, 50)];
    callsign[7] = ais_charset[getbits(msg, 51, 56)];
    callsign[8] = 0;

    // score based on number of valid characters
    int score = 8;
    int valid = 1;
    for (int i = 0; i < 8; ++i) {
        if (
                (callsign[i] >= 'A' && callsign[i] <= 'Z')
                // -./0123456789
                || (callsign[i] >= '-' && callsign[i] <= '9')
                || callsign[i] == ' '
                || callsign[i] == '@'
           ) {
            // valid chars
            score += 6;
        } else {
            // Invalid
            if (Modes.debug_callsign) {
                fprintf(stderr, "%06x %8s (len: %d)\n", mm->addr, callsign, (int) strlen(callsign));
            }
            return 0;
        }
    }

    if (store) {
        mm->commb_format = COMMB_AIRCRAFT_IDENT;
        if (valid) {
            memcpy(mm->callsign, callsign, sizeof(mm->callsign));
            mm->callsign_valid = 1;
        }
    }

    return score;
}

// check if the payload is a valid ACAS payload
// https://mode-s.org/decode/book-the_1090mhz_riddle-junzi_sun.pdf
int checkAcasRaValid(unsigned char *msg, struct modesMessage *mm, int debug) {

    bool ara = getbit(msg, 9);
    bool rat = getbit(msg, 27);
    bool mte = getbit(msg, 28);

    // not a valid RA if none of the bits are set
    if (!ara && !rat && !mte)
        return 0;

    if (getbits(msg, 9, 28) == 0)
        return 0; // these are the bits that contain the info, all zero it's not an RA
    if (getbit(msg, 23) && getbit(msg, 24))
        return 0; // complementary bits, both set is invalid (above / below)
    if (getbit(msg, 25) && getbit(msg, 26))
        return 0; // complementary bits, both set is invalid (left / right)

    if (mm->msgtype == 16) {
        if (getbits(msg, 29, 56) != 0)
            return 0; // in DF16 messages msg bits 29 to 56 are reserved

        return 1;
    }

    // some extra restrictions for DF != 16 below

    if (getbit(msg, 25) || getbit(msg, 26))
        return 0; // left / right isn't used, require zero

    // for COMMB messages let's check if the thread indicator makes sense

    int tti = getbits(msg, 29, 30);
    // thread type indicator
    // 00 No identity data in threat identity data
    if (tti == 0) {
        if (getbits(msg, 31, 56) != 0)
            return 0;

        return 1;
    }
    // When the threat type indicator is 01 , MB bits 31-54 contain the 24-bit Mode S transponder address and the last two bits are set to zero.
    if (tti == 1) {
        if (getbits(msg, 55, 56) != 0)
            return 0;

        uint32_t addr = getbits(msg, 31, 54);
        if (icaoFilterTest(addr))
            return 1;

        return debug; // zero unless debug
    }

    // 10 Threat identity data contains altitude, range, and bearing
    if (tti == 2) {
        if (mm->metype == 28)// allow for DF17
            return 1;
        // hard to tell if used and separate from garbage, don't mark valid for the moment
        return debug; // zero unless debug
    }

    // 11 Not assigned
    if (tti == 3) {
        return 0;
    }

    return 0;
}

// BDS3,0 ACAS RA

static int decodeBDS30(struct modesMessage *mm, bool store) {
    unsigned char *msg = mm->MB;

    // BDS identifier
    if (msg[0] != 0x30) {
        return 0;
    }

    if (store) {
        mm->commb_format = COMMB_ACAS_RA;
        mm->acas_ra_valid = 1;
    }

    // just accept it.
    return 56;
}

// BDS4,0 Selected vertical intention

static int decodeBDS40(struct modesMessage *mm, bool store) {
    unsigned char *msg = mm->MB;

    unsigned mcp_valid = getbit(msg, 1);
    unsigned mcp_raw = getbits(msg, 2, 13);
    unsigned fms_valid = getbit(msg, 14);
    unsigned fms_raw = getbits(msg, 15, 26);
    unsigned baro_valid = getbit(msg, 27);
    unsigned baro_raw = getbits(msg, 28, 39);
    unsigned reserved_1 = getbits(msg, 40, 47);
    unsigned mode_valid = getbit(msg, 48);
    unsigned mode_raw = getbits(msg, 49, 51);
    unsigned reserved_2 = getbits(msg, 52, 53);
    unsigned source_valid = getbit(msg, 54);
    unsigned source_raw = getbits(msg, 55, 56);

    if (!mcp_valid && !fms_valid && !baro_valid && !mode_valid && !source_valid) {
        return 0;
    }

    int score = 0;

    unsigned mcp_alt = 0;
    if (mcp_valid && mcp_raw != 0) {
        mcp_alt = mcp_raw * 16;
        if (mcp_alt >= 1000 && mcp_alt <= 50000) {
            score += 13;
        } else {
            // unlikely altitude
            return 0;
        }
    } else if (!mcp_valid && mcp_raw == 0) {
        score += 1;
    } else {
        return 0;
    }

    unsigned fms_alt = 0;
    if (fms_valid && fms_raw != 0) {
        fms_alt = fms_raw * 16;
        if (fms_alt >= 1000 && fms_alt <= 50000) {
            score += 13;
        } else {
            // unlikely altitude
            return 0;
        }
    } else if (!fms_valid && fms_raw == 0) {
        score += 1;
    } else {
        return 0;
    }

    float baro_setting = 0;
    if (baro_valid && baro_raw != 0) {
        baro_setting = 800 + baro_raw * 0.1;
        if (baro_setting >= 900 && baro_setting <= 1100) {
            score += 13;
        } else {
            // unlikely pressure setting
            return 0;
        }
    } else if (!baro_valid && baro_raw == 0) {
        score += 1;
    } else {
        return 0;
    }

    if (reserved_1 != 0) {
        return 0;
    }

    if (mode_valid) {
        score += 4;
    } else if (!mode_valid && mode_raw == 0) {
        score += 1;
    } else {
        return 0;
    }

    if (reserved_2 != 0) {
        return 0;
    }

    if (source_valid) {
        score += 3;
    } else if (!source_valid && source_raw == 0) {
        score += 1;
    } else {
        return 0;
    }

    // small penalty for inconsistent data
    if (mcp_valid && fms_valid && mcp_alt != fms_alt) {
        score -= 4;
    }

    if (mcp_valid) {
        unsigned remainder = mcp_alt % 500;
        if (!(remainder < 16 || remainder > 484)) {
            // mcp altitude is not a multiple of 500
            score -= 4;
        }
    }

    if (fms_valid) {
        unsigned remainder = fms_alt % 500;
        if (!(remainder < 16 || remainder > 484)) {
            // fms altitude is not a multiple of 500
            score -= 4;
        }
    }

    if (store) {
        mm->commb_format = COMMB_VERTICAL_INTENT;

        if (mcp_valid) {
            mm->nav.mcp_altitude_valid = 1;
            mm->nav.mcp_altitude = mcp_alt;
        }

        if (fms_valid) {
            mm->nav.fms_altitude_valid = 1;
            mm->nav.fms_altitude = fms_alt;
        }

        if (baro_valid) {
            mm->nav.qnh_valid = 1;
            mm->nav.qnh = baro_setting;
        }

        if (mode_valid) {
            mm->nav.modes_valid = 1;
            mm->nav.modes =
                ((mode_raw & 4) ? NAV_MODE_VNAV : 0) |
                ((mode_raw & 2) ? NAV_MODE_ALT_HOLD : 0) |
                ((mode_raw & 1) ? NAV_MODE_APPROACH : 0);
        }

        if (source_valid) {
            switch (source_raw) {
                case 0:
                    mm->nav.altitude_source = NAV_ALT_UNKNOWN;
                    break;
                case 1:
                    mm->nav.altitude_source = NAV_ALT_AIRCRAFT;
                    break;
                case 2:
                    mm->nav.altitude_source = NAV_ALT_MCP;
                    break;
                case 3:
                    mm->nav.altitude_source = NAV_ALT_FMS;
                    break;
                default:
                    mm->nav.altitude_source = NAV_ALT_INVALID;
                    break;
            }
        } else {
            mm->nav.altitude_source = NAV_ALT_INVALID;
        }
    }

    return score;
}

// BDS5,0 Track and turn report

static int decodeBDS50(struct modesMessage *mm, bool store) {
    unsigned char *msg = mm->MB;

    unsigned roll_valid = getbit(msg, 1);
    unsigned roll_sign = getbit(msg, 2);
    unsigned roll_raw = getbits(msg, 3, 11);

    unsigned track_valid = getbit(msg, 12);
    unsigned track_sign = getbit(msg, 13);
    unsigned track_raw = getbits(msg, 14, 23);

    unsigned gs_valid = getbit(msg, 24);
    unsigned gs_raw = getbits(msg, 25, 34);

    unsigned track_rate_valid = getbit(msg, 35);
    unsigned track_rate_sign = getbit(msg, 36);
    unsigned track_rate_raw = getbits(msg, 37, 45);

    unsigned tas_valid = getbit(msg, 46);
    unsigned tas_raw = getbits(msg, 47, 56);

    if (!roll_valid || !track_valid || !gs_valid || !tas_valid) {
        return 0;
    }

    int score = 0;

    float roll = 0;
    if (roll_valid) {
        roll = roll_raw * 45.0 / 256.0;
        if (roll_sign) {
            roll -= 90.0;
        }

        if (roll >= -40 && roll < 40) {
            score += 11;
        } else {
            return 0;
        }
    } else if (!roll_valid && roll_raw == 0 && !roll_sign) {
        score += 1;
    } else {
        return 0;
    }

    float track = 0;
    if (track_valid) {
        score += 12;
        track = track_raw * 90.0 / 512.0;
        if (track_sign) {
            track += 180.0;
        }
    } else if (!track_valid && track_raw == 0 && !track_sign) {
        score += 1;
    } else {
        return 0;
    }

    unsigned gs = 0;
    if (gs_valid && gs_raw != 0) {
        gs = gs_raw * 2;

        if (gs >= 50 && gs <= 700) {
            score += 11;
        } else {
            return 0;
        }
    } else if (!gs_valid && gs_raw == 0) {
        score += 1;
    } else {
        return 0;
    }

    float track_rate = 0;
    if (track_rate_valid) {
        track_rate = track_rate_raw * 8.0 / 256.0;
        if (track_rate_sign) {
            track_rate -= 16;
        }

        if (track_rate >= -10.0 && track_rate <= 10.0) {
            score += 11;
        } else {
            return 0;
        }
    } else if (!track_rate_valid && track_rate_raw == 0 && !track_rate_sign) {
        score += 1;
    } else {
        return 0;
    }

    unsigned tas = 0;
    if (tas_valid && tas_raw != 0) {
        tas = tas_raw * 2;

        if (tas >= 50 && tas <= 700) {
            score += 11;
        } else {
            return 0;
        }
    } else if (!tas_valid && tas_raw == 0) {
        score += 1;
    } else {
        return 0;
    }

    // small penalty for inconsistent data
    if (gs_valid && tas_valid) {
        int delta = abs((int)gs_valid - (int)tas_valid);
        if (delta > 150) {
            score -= 6;
        }
    }

    // compute the theoretical turn rate and compare to track angle rate
    if (roll_valid && tas_valid && tas > 0 && track_rate_valid) {
        double turn_rate = 68625 * tan(roll * M_PI / 180.0) / (tas * 20 * M_PI);
        double delta = fabs(turn_rate - track_rate);
        if (delta > 2.0) {
            score -= 6;
        }
    }

    if (store) {
        mm->commb_format = COMMB_TRACK_TURN;

        if (roll_valid) {
            mm->roll_valid = 1;
            mm->roll = roll;
        }

        if (track_valid) {
            mm->heading_valid = 1;
            mm->heading = track;
            mm->heading_type = HEADING_GROUND_TRACK;
        }

        if (gs_valid) {
            mm->gs_valid = 1;
            mm->gs.v0 = mm->gs.v2 = mm->gs.selected = gs;
        }

        if (track_rate_valid) {
            mm->track_rate_valid = 1;
            mm->track_rate = track_rate;
        }

        if (tas_valid) {
            mm->tas_valid = 1;
            mm->tas = tas;
        }
    }

    return score;
}

// BDS6,0 Heading and speed report

static int decodeBDS60(struct modesMessage *mm, bool store) {
    unsigned char *msg = mm->MB;

    unsigned heading_valid = getbit(msg, 1);
    unsigned heading_sign = getbit(msg, 2);
    unsigned heading_raw = getbits(msg, 3, 12);

    unsigned ias_valid = getbit(msg, 13);
    unsigned ias_raw = getbits(msg, 14, 23);

    unsigned mach_valid = getbit(msg, 24);
    unsigned mach_raw = getbits(msg, 25, 34);

    unsigned baro_rate_valid = getbit(msg, 35);
    unsigned baro_rate_sign = getbit(msg, 36);
    unsigned baro_rate_raw = getbits(msg, 37, 45);

    unsigned inertial_rate_valid = getbit(msg, 46);
    unsigned inertial_rate_sign = getbit(msg, 47);
    unsigned inertial_rate_raw = getbits(msg, 48, 56);

    if (!heading_valid || !ias_valid || !mach_valid || (!baro_rate_valid && !inertial_rate_valid)) {
        return 0;
    }

    int score = 0;

    float heading = 0;
    if (heading_valid) {
        heading = heading_raw * 90.0 / 512.0;
        if (heading_sign) {
            heading += 180.0;
        }
        score += 12;
    } else if (!heading_valid && heading_raw == 0 && !heading_sign) {
        score += 1;
    } else {
        return 0;
    }

    unsigned ias = 0;
    if (ias_valid && ias_raw != 0) {
        ias = ias_raw;
        if (ias >= 50 && ias <= 700) {
            score += 11;
        } else {
            return 0;
        }
    } else if (!ias_valid && ias_raw == 0) {
        score += 1;
    } else {
        return 0;
    }

    float mach = 0;
    if (mach_valid && mach_raw != 0) {
        mach = mach_raw * 2.048 / 512;
        if (mach >= 0.1 && mach <= 0.9) {
            score += 11;
        } else {
            return 0;
        }
    } else if (!mach_valid && mach_raw == 0) {
        score += 1;
    } else {
        return 0;
    }

    int baro_rate = 0;
    if (baro_rate_valid) {
        baro_rate = baro_rate_raw * 32;
        if (baro_rate_sign) {
            baro_rate -= 16384;
        }

        if (baro_rate >= -6000 && baro_rate <= 6000) {
            score += 11;
        } else {
            return 0;
        }
    } else if (!baro_rate_valid && baro_rate_raw == 0) {
        score += 1;
    } else {
        return 0;
    }

    int inertial_rate = 0;
    if (inertial_rate_valid) {
        inertial_rate = inertial_rate_raw * 32;
        if (inertial_rate_sign) {
            inertial_rate -= 16384;
        }

        if (inertial_rate >= -6000 && inertial_rate <= 6000) {
            score += 11;
        } else {
            return 0;
        }
    } else if (!inertial_rate_valid && inertial_rate_raw == 0) {
        score += 1;
    } else {
        return 0;
    }

    // small penalty for inconsistent data

    // Should check IAS vs Mach at given altitude, but the maths is a little involved

    if (baro_rate_valid && inertial_rate_valid) {
        int delta = abs(baro_rate - inertial_rate);
        if (delta > 2000) {
            score -= 12;
        }
    }

    if (store) {
        mm->commb_format = COMMB_HEADING_SPEED;

        if (heading_valid) {
            mm->heading_valid = 1;
            mm->heading = heading;
            mm->heading_type = HEADING_MAGNETIC;
        }

        if (ias_valid) {
            mm->ias_valid = 1;
            mm->ias = ias;
        }

        if (mach_valid) {
            mm->mach_valid = 1;
            mm->mach = mach;
        }

        if (baro_rate_valid) {
            mm->baro_rate_valid = 1;
            mm->baro_rate = baro_rate;
        }

        if (inertial_rate_valid) {
            // INS-derived data is treated as a "geometric rate" / "geometric altitude"
            // elsewhere, so do the same here.
            mm->geom_rate_valid = 1;
            mm->geom_rate = inertial_rate;
        }
    }

    return score;
}

// BDS 4,4 Meteorological routine air report

static int decodeBDS44(struct modesMessage *mm, bool store) {
    unsigned char *msg = mm->MB;

    unsigned source = getbits(msg, 1, 4);

    unsigned wind_valid = getbit(msg, 5);
    unsigned wind_speed_raw = getbits(msg, 6, 14);
    unsigned wind_direction_raw = getbits(msg, 15, 23);

    unsigned temperature_sign = getbit(msg, 24);
    unsigned static_air_temperature_raw = getbits(msg, 25, 34);

    unsigned pressure_valid = getbit(msg, 35);
    unsigned static_pressure_raw = getbits(msg, 36, 46);

    unsigned turbulence_valid = getbit(msg, 47);
    unsigned turbulence_raw = getbits(msg, 48, 49);

    unsigned humidity_valid = getbit(msg, 50);
    unsigned humidity_raw = getbits(msg, 51, 56);

    /*
    if (!wind_valid || !temperature_valid || !pressure_valid || !turbulence_valid && !humidity_valid){
       return 0;
    }
    */
    int met_source = source;
    int score = 0;
    int wind_speed = 0;
    float wind_direction = 0;
    float temperature = 0;
    int static_pressure = 0;
    int turbulence = 0;
    float humidity = 0;
    if (met_source >= 0 && met_source <= 6) {
        score += 4;
    }
    else {
        return 0;
    }
    if (wind_valid){
        wind_speed = (int)wind_speed_raw;
        if (wind_speed <= 511 && wind_speed >= 0){
            score += 9;
        }
        else {
            return 0;
        }
        wind_direction = wind_direction_raw * (180 / 256);
        if (wind_direction >= 0 && wind_direction <= 360){
            score += 9;
        }
        else {
            return 0;
        }
    }
    else if (wind_speed == 0) {
        score += 2;
    }
    if (temperature_sign){
        temperature = (static_air_temperature_raw - pow(2, 10)) * 0.25;
    }
    else {
        temperature = static_air_temperature_raw * 0.25;
    }
    if (temperature >= -128 && temperature <= 128){
        score += 10;
    }
    else {
        return 0;
    }
    if (pressure_valid){
        static_pressure = (int)static_pressure_raw;
        if (static_pressure >= 0 && static_pressure <= 2048){
            score += 11;
            return 0;
        }
        else {
        }
    }
    else if (static_pressure == 0) {
        score += 1;
    }
    if (turbulence_valid){
        turbulence = (int)turbulence_raw;
        if (turbulence >= 0 && turbulence <= 3) {
            score += 2;
        }
        else {
            return 0;
        }
    }
    else if (turbulence == 0) {
        score += 1;
    }
    if (humidity_valid) {
        humidity = humidity_raw * (100.0f / 64);
        if (humidity >= 0 && humidity <= 100){
            score += 6;
        }
        else {
            return 0;
        }
    }
    else if (humidity == 0) {
        score += 1;
    }
    if (store) {
        mm->commb_format = COMMB_METEOROLOGICAL_ROUTINE;
        mm->met_source_valid = 1;
        mm->met_source = met_source;
        if (wind_valid) { 
            mm->wind_valid = 1;
            mm->wind_speed = wind_speed;
            mm->wind_direction = wind_direction;
        }
        mm->oat_valid = 1;
        mm->oat = temperature;
        if (pressure_valid) {
            mm->static_pressure_valid = 1;
            mm->static_pressure = static_pressure;
        }
        if (turbulence_valid) {
            mm->turbulence_valid = 1;
            mm->turbulence = turbulence;
        }
        if (humidity_valid) {
            mm->humidity_valid = 1;
            mm->humidity = humidity;
        }
    }
    return score;
}

