#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../readsb.h"

#include "uat.h"
#include "uat2mm.h"
#include "uat_decode.h"

#define _UNUSED(V) ((void)V)

static int hexbyte(char *buf) {
    int i;
    char c;

    c = buf[0];
    if (c >= '0' && c <= '9')
        i = (c - '0');
    else if (c >= 'a' && c <= 'f')
        i = (c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
        i = (c - 'A' + 10);
    else
        return -1;

    i <<= 4;
    c = buf[1];
    if (c >= '0' && c <= '9')
        return i | (c - '0');
    else if (c >= 'a' && c <= 'f')
        return i | (c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
        return i | (c - 'A' + 10);
    else
        return -1;
}

int uat2mm(frame_type_t type, uint8_t *frame, float ss, int64_t now, struct modesMessage *mm) {
    struct uat_adsb_mdb mdb;
    uat_decode_adsb_mdb(frame, &mdb);

    if (type == UAT_DOWNLINK) {
        if (mdb.mdb_type == 0) { // only type that is a basic (short) ads-b message that i can find
            type = UAT_SHORT;
        } else {
            type = UAT_LONG;
        }
    }

    if (type == UAT_SHORT) {
        memcpy(mm->msg, frame, SHORT_FRAME_BYTES);
        mm->msgbits = SHORT_FRAME_BITS;
    } else if (type == UAT_LONG) {
        memcpy(mm->msg, frame, LONG_FRAME_BYTES);
        mm->msgbits = LONG_FRAME_BITS;
    } else {
        return 0;
    }

    mm->sysTimestamp = now;
    mm->signalLevel = ss;
    mm->msgtype = 33;

    mm->source = SOURCE_UAT;
    mm->addr = mdb.address;

    switch (mdb.address_qualifier) {
    case AQ_ADSB_ICAO:
        mm->addrtype = ADDR_UAT_ICAO;
        break;
    case AQ_NATIONAL:
        mm->addrtype = ADDR_UAT_OTHER;
        mm->addr |= MODES_NON_ICAO_ADDRESS;
        break;
    case AQ_TISB_ICAO:
        mm->addrtype = ADDR_TISB_ICAO;
        break;
    case AQ_TISB_OTHER:
        mm->addrtype = ADDR_TISB_OTHER;
        mm->addr |= MODES_NON_ICAO_ADDRESS;
        break;
    case AQ_VEHICLE:
    case AQ_FIXED_BEACON:
        mm->addrtype = ADDR_UAT_OTHER;
        mm->addr |= MODES_NON_ICAO_ADDRESS;
        break;
    case AQ_RESERVED_6:
    case AQ_RESERVED_7:
        return 0;
    }

    if (mdb.has_sv) {
        mm->msgtype = 34; // has position

        switch (mdb.nic) {
        case 1:
            mm->decoded_rc = 37040;
            break;
        case 2:
            mm->decoded_rc = 14816;
            break;
        case 3:
            mm->decoded_rc = 7408;
            break;
        case 4:
            mm->decoded_rc = 3704;
            break;
        case 5:
            mm->decoded_rc = 1852;
            break;
        case 6:
            mm->decoded_rc = 1112;
            break;
        case 7:
            mm->decoded_rc = 371;
            break;
        case 8:
            mm->decoded_rc = 186;
            break;
        case 9:
            mm->decoded_rc = 75;
            break;
        case 10:
            mm->decoded_rc = 25;
            break;
        case 11:
            mm->decoded_rc = 8;
            break;
        default:
            mm->decoded_rc = RC_UNKNOWN;
            break;
        }

        if (mdb.position_valid) {
            mm->decoded_lat = mdb.lat;
            mm->decoded_lon = mdb.lon;
        }

        switch (mdb.altitude_type) {
        case ALT_BARO:
            mm->baro_alt_valid = 1;
            mm->baro_alt_unit = UNIT_FEET;
            mm->baro_alt = mdb.altitude;
            break;
        case ALT_GEO:
            mm->geom_alt_valid = 1;
            mm->geom_alt_unit = UNIT_FEET;
            mm->geom_alt = mdb.altitude;
            break;
        case ALT_INVALID:
            break;
        };

        switch (mdb.vert_rate_source) {
        case ALT_BARO:
            mm->baro_rate_valid = 1;
            mm->baro_rate = mdb.vert_rate;
            break;
        case ALT_GEO:
            mm->geom_rate_valid = 1;
            mm->geom_rate = mdb.vert_rate;
            break;
        case ALT_INVALID:
            mm->geom_rate_valid = 0;
            break;
        }

        switch (mdb.airground_state) {
        case UAT_AG_GROUND:
            mm->airground = AG_GROUND;
            break;
        case UAT_AG_SUBSONIC:
        case UAT_AG_SUPERSONIC:
            mm->airground = AG_AIRBORNE;
            break;
        case UAT_AG_RESERVED:
            mm->airground = AG_INVALID;
        };
        if (mdb.speed_valid) {
            mm->gs_valid = 1;
            mm->gs.v0 = mm->gs.v2 = mm->gs.selected = mdb.speed;
        }

        switch (mdb.track_type) {
        case TT_TRACK:
            mm->heading_valid = 1;
            mm->heading = mdb.track;
            mm->heading_type = HEADING_GROUND_TRACK;
            break;
        case TT_MAG_HEADING:
            mm->heading_valid = 1;
            mm->heading = mdb.track;
            mm->heading_type = HEADING_MAGNETIC;
            break;
        case TT_TRUE_HEADING:
            mm->heading_valid = 1;
            mm->heading = mdb.track;
            mm->heading_type = HEADING_TRUE;
            break;
        case TT_INVALID:
            break;
        }
    }

    if (mdb.has_ms) {
        mm->category_valid = 1;
        if (mdb.emitter_category <= 7) {
            mm->category = 0xA0 | (mdb.emitter_category & 7);
        } else if (mdb.emitter_category <= 15) {
            mm->category = 0xB0 | (mdb.emitter_category & 7);
        } else if (mdb.emitter_category <= 23) {
            mm->category = 0xC0 | (mdb.emitter_category & 7);
        } else if (mdb.emitter_category <= 31) {
            mm->category = 0xD0 | (mdb.emitter_category & 7);
        } else {
            // reserved, map to A0
            mm->category = 0xA0;
        }

        switch (mdb.callsign_type) {
        case CS_CALLSIGN: {
            mm->callsign_valid = 1;
            mm->callsign[0] = mdb.callsign[0];
            mm->callsign[1] = mdb.callsign[1];
            mm->callsign[2] = mdb.callsign[2];
            mm->callsign[3] = mdb.callsign[3];
            mm->callsign[4] = mdb.callsign[4];
            mm->callsign[5] = mdb.callsign[5];
            mm->callsign[6] = mdb.callsign[6];
            mm->callsign[7] = mdb.callsign[7];
            break;
        }
        case CS_SQUAWK:
            mm->squawk_valid = 1;
            mm->squawkDec = strtoul(mdb.callsign, NULL, 10);
            mm->squawkHex = squawkDec2Hex(mm->squawkDec);
            break;
        case CS_INVALID:
            break;
        }

        mm->accuracy.nac_p_valid = 1;
        mm->accuracy.nac_p = mdb.nac_p;
        mm->accuracy.nac_v_valid = 1;
        mm->accuracy.nac_v = mdb.nac_v;
        mm->accuracy.nic_baro_valid = 1;
        mm->accuracy.nic_baro = mdb.nic_baro;
        mm->accuracy.sil_type = (mdb.silsupp == 1 ? SIL_PER_SAMPLE : SIL_PER_HOUR);
        mm->accuracy.sil = mdb.sil;
        mm->accuracy.gva_valid = 1;
        mm->accuracy.gva = mdb.gva;

        mm->opstatus.valid = 1;
        mm->opstatus.version = mdb.uat_version;
        mm->opstatus.cc_1090_in = mdb.es_in;
        mm->opstatus.cc_uat_in = mdb.uat_in;
        mm->opstatus.cc_acas = mdb.has_acas;
        mm->opstatus.om_acas_ra = mdb.acas_ra_active;
        mm->opstatus.om_ident = mdb.ident_active;
        mm->opstatus.om_atc = mdb.atc_services;
        mm->opstatus.om_saf = mdb.single_antenna;
    }

    if (mdb.has_auxsv) {
        switch (mdb.sec_altitude_type) {
        case ALT_BARO:
            mm->baro_alt_valid = 1;
            mm->baro_alt_unit = UNIT_FEET;
            mm->baro_alt = mdb.sec_altitude;
            break;
        case ALT_GEO:
            mm->geom_alt_valid = 1;
            mm->geom_alt_unit = UNIT_FEET;
            mm->geom_alt = mdb.sec_altitude;
            break;
        case ALT_INVALID:
            break;
        };
    }

    return 1;
}

int process_dump978(char *p, char *end, frame_type_t *frametype, uint8_t *frame, float *signal_strength) {
    int len = 0;

    if (*p == '-')
        *frametype = UAT_DOWNLINK;
    else if (*p == '+')
        *frametype = UAT_UPLINK;
    else
        return 0;

    ++p;
    while (p < end) {
        int byte;

        // If we hit a semicolon, the rest of the line is "extra info", like timestmap, RSSI, etc.
        // Try to parse some of it...
        if (p[0] == ';') {
            int done = 0;
            p++;

            while (*p && !done) {
                if (!strncmp(p, "ss=", 3))
                    sscanf(p, "ss=%f;", signal_strength);
                if (!strncmp(p, "rssi=", 5))
                    sscanf(p, "rssi=%f;", signal_strength);
                // if (!strncmp(p, "t=", 2))
                //     sscanf(p, "t=%f;", &timestamp);
                // Advance until next semicolon (or end)...
                while (*p && *p != ';') {
                    p++;
                }

                // Short-circuit and bail if we have SS - remove later if we want to parse other info
                if (!*p || (signal_strength != 0)) {
                    done = 1;
                } else {
                    p++;
                }
            }

            // ignore rest of line

            return 1;
        }

        if (len >= UPLINK_FRAME_DATA_BYTES) {
            return 0; // oversized frame
        }
        byte = hexbyte(p);
        if (byte < 0) {
            return 0; // badly formatted byte
        }
        ++len;
        *frame++ = byte;
        p += 2;
    }

    return 0; // ran off the end without seeing semicolon
}
