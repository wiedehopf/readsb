// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// track.c: aircraft state tracking
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

#include "readsb.h"
#include <inttypes.h>
#include "geomag.h"

uint32_t modeAC_count[4096];
uint32_t modeAC_lastcount[4096];
uint32_t modeAC_match[4096];
uint32_t modeAC_age[4096];

static void cleanupAircraft(struct aircraft *a);
static void globe_stuff(struct aircraft *a, struct modesMessage *mm, double new_lat, double new_lon, uint64_t now);
static void showPositionDebug(struct aircraft *a, struct modesMessage *mm, uint64_t now);
static void position_bad(struct modesMessage *mm, struct aircraft *a);
static void resize_trace(struct aircraft *a, uint64_t now);
static void calc_wind(struct aircraft *a, uint64_t now);
static void calc_temp(struct aircraft *a, uint64_t now);
static inline int declination (struct aircraft *a, double *dec);
static const char *source_string(datasource_t source);

// Should we accept some new data from the given source?
// If so, update the validity and return 1

static int accept_data(data_validity *d, datasource_t source, struct modesMessage *mm, int reduce_often) {
    uint64_t receiveTime = mm->sysTimestampMsg;

    if (source == SOURCE_INVALID)
        return 0;

    if (receiveTime < d->updated)
        return 0;

    if (source < d->source && receiveTime < d->updated + TRACK_STALE)
        return 0;

    // prevent JAERO and other SBS from disrupting
    // other data sources too quickly
    if (source != SOURCE_MODE_S && source <= SOURCE_JAERO && source != d->last_source) {
        if (source != SOURCE_JAERO && receiveTime < d->updated + 60 * 1000)
            return 0;
        if (source == SOURCE_JAERO && receiveTime < d->updated + 600 * 1000)
            return 0;
    }

    if (source == SOURCE_PRIO)
        d->source = SOURCE_ADSB;
    else
        d->source = source;

    d->last_source = d->source;

    d->updated = receiveTime;
    d->stale = 0;

    if (receiveTime > d->next_reduce_forward && !mm->sbs_in) {
        if (mm->msgtype == 17 || reduce_often) {
            d->next_reduce_forward = receiveTime + Modes.net_output_beast_reduce_interval;
        } else {
            d->next_reduce_forward = receiveTime + Modes.net_output_beast_reduce_interval * 4;
        }
        // make sure global CPR stays possible even at high interval:
        if (Modes.net_output_beast_reduce_interval > 7000 && mm->cpr_valid) {
            d->next_reduce_forward = receiveTime + 7000;
        }
        mm->reduce_forward = 1;
    }
    return 1;
}

// Given two datasources, produce a third datasource for data combined from them.

static void combine_validity(data_validity *to, const data_validity *from1, const data_validity *from2, uint64_t now) {
    if (from1->source == SOURCE_INVALID) {
        *to = *from2;
        return;
    }

    if (from2->source == SOURCE_INVALID) {
        *to = *from1;
        return;
    }

    to->source = (from1->source < from2->source) ? from1->source : from2->source; // the worse of the two input sources
    to->last_source = to->source;
    to->updated = (from1->updated > from2->updated) ? from1->updated : from2->updated; // the *later* of the two update times
    to->stale = (now > to->updated + TRACK_STALE);
}

static int compare_validity(const data_validity *lhs, const data_validity *rhs) {
    if (!lhs->stale && lhs->source > rhs->source)
        return 1;
    else if (!rhs->stale && lhs->source < rhs->source)
        return -1;
    else if (lhs->updated > rhs->updated)
        return 1;
    else if (lhs->updated < rhs->updated)
        return -1;
    else
        return 0;
}

//
// CPR position updating
//

// Distance between points on a spherical earth.
// This has up to 0.5% error because the earth isn't actually spherical
// (but we don't use it in situations where that matters)

double greatcircle(double lat0, double lon0, double lat1, double lon1) {
    double dlat, dlon;

    lat0 = lat0 * M_PI / 180.0;
    lon0 = lon0 * M_PI / 180.0;
    lat1 = lat1 * M_PI / 180.0;
    lon1 = lon1 * M_PI / 180.0;

    dlat = fabs(lat1 - lat0);
    dlon = fabs(lon1 - lon0);

    // use haversine for small distances for better numerical stability
    if (dlat < 0.001 && dlon < 0.001) {
        double a = sin(dlat / 2) * sin(dlat / 2) + cos(lat0) * cos(lat1) * sin(dlon / 2) * sin(dlon / 2);
        return 6371e3 * 2 * atan2(sqrt(a), sqrt(1.0 - a));
    }

    // spherical law of cosines
    return 6371e3 * acos(sin(lat0) * sin(lat1) + cos(lat0) * cos(lat1) * cos(dlon));
}

static float bearing(double lat0, double lon0, double lat1, double lon1) {
    lat0 = lat0 * M_PI / 180.0;
    lon0 = lon0 * M_PI / 180.0;
    lat1 = lat1 * M_PI / 180.0;
    lon1 = lon1 * M_PI / 180.0;

    double y = sin(lon1-lon0)*cos(lat1);
    double x = cos(lat0)*sin(lat1) - sin(lat0)*cos(lat1)*cos(lon1-lon0);
    double res = (atan2(y, x) * 180 / M_PI + 360);
    while (res > 360)
        res -= 360;
    return (float) res;
}

static void update_range_histogram(double lat, double lon) {
    double range = 0;
    int valid_latlon = Modes.bUserFlags & MODES_USER_LATLON_VALID;

    if (!valid_latlon)
        return;

    range = greatcircle(Modes.fUserLat, Modes.fUserLon, lat, lon);

    if ((range <= Modes.maxRange || Modes.maxRange == 0)) {
        if (range > Modes.stats_current.distance_max)
            Modes.stats_current.distance_max = range;
        if (range < Modes.stats_current.distance_min)
            Modes.stats_current.distance_min = range;
    }

    if (Modes.stats_range_histo) {
        int bucket = round(range / Modes.maxRange * RANGE_BUCKET_COUNT);

        if (bucket < 0)
            bucket = 0;
        else if (bucket >= RANGE_BUCKET_COUNT)
            bucket = RANGE_BUCKET_COUNT - 1;

        ++Modes.stats_current.range_histogram[bucket];
    }
}

// return true if it's OK for the aircraft to have travelled from its last known position
// to a new position at (lat,lon,surface) at a time of now.

static int speed_check(struct aircraft *a, datasource_t source, double lat, double lon, struct modesMessage *mm) {
    uint64_t elapsed;
    double distance;
    double range;
    double speed;
    double calc_track = 0;
    double track_diff = -1;
    double track_bonus = 0;
    int inrange;
    uint64_t now = a->seen;
    double oldLat = a->lat;
    double oldLon = a->lon;

    MODES_NOTUSED(mm);
    if (bogus_lat_lon(lat, lon) ||
            (mm->cpr_valid && mm->cpr_lat == 0 && mm->cpr_lon == 0)
       ) {
        mm->pos_ignore = 1; // don't decrement pos_reliable
        return 0;
    }

    int surface = trackDataValid(&a->airground_valid)
        && a->airground == AG_GROUND
        && a->pos_surface
        && (!mm->cpr_valid || mm->cpr_type == CPR_SURFACE);

    if (a->pos_reliable_odd < 1 && a->pos_reliable_even < 1)
        return 1;
    if (now > a->position_valid.updated + (120 * 1000))
        return 1; // no reference or older than 120 seconds, assume OK
    if (source > a->position_valid.last_source)
        return 1; // data is better quality, OVERRIDE

    elapsed = trackDataAge(now, &a->position_valid);

    speed = surface ? 150 : 900; // guess

    if (trackDataValid(&a->gs_valid)) {
        // use the larger of the current and earlier speed
        speed = (a->gs_last_pos > a->gs) ? a->gs_last_pos : a->gs;
        // add 2 knots for every second we haven't known the speed
        speed = speed + (3*trackDataAge(now, &a->gs_valid)/1000.0);
    } else if (trackDataValid(&a->tas_valid)) {
        speed = a->tas * 4 / 3;
    } else if (trackDataValid(&a->ias_valid)) {
        speed = a->ias * 2;
    }

    if (source <= SOURCE_MLAT) {
        if (elapsed > 15 * SECONDS)
            return 1;
        speed = speed * 2;
        speed = min(speed, 2400);
    }

    // Work out a reasonable speed to use:
    //  current speed + 1/3
    //  surface speed min 20kt, max 150kt
    //  airborne speed min 200kt, no max
    speed = speed * 1.3;
    if (surface) {
        if (speed < 20)
            speed = 20;
        if (speed > 150)
            speed = 150;
    } else {
        if (speed < 200)
            speed = 200;
    }

    // find actual distance
    distance = greatcircle(oldLat, oldLon, lat, lon);

    if (!surface && distance > 5 && source > SOURCE_MLAT
            && trackDataAge(now, &a->track_valid) < 7 * 1000
            && trackDataAge(now, &a->position_valid) < 7 * 1000
            && (oldLat != lat || oldLon != lon)
            && (a->pos_reliable_odd >= Modes.json_reliable && a->pos_reliable_even >= Modes.json_reliable)
       ) {
        calc_track = bearing(a->lat, a->lon, lat, lon);
        track_diff = fabs(norm_diff(a->track - calc_track, 180));
        track_bonus = speed * (90.0 - track_diff) / 90.0;
        speed += track_bonus * (1.1 - trackDataAge(now, &a->track_valid) / 5000);
        if (track_diff > 170) {
            mm->pos_ignore = 1; // don't decrement pos_reliable
        }
    }

    // 100m (surface) base distance to allow for minor errors, no airborne base distance due to ground track cross check
    // plus distance covered at the given speed for the elapsed time + 1 seconds.
    range = (surface ? 0.1e3 : 0.0e3) + ((elapsed + 1000.0) / 1000.0) * (speed * 1852.0 / 3600.0);

    inrange = (distance <= range);

    if ((source > SOURCE_MLAT && track_diff < 190 && !inrange && (Modes.debug_cpr || Modes.debug_speed_check))
            || (a->addr == Modes.cpr_focus && distance > 5)) {

        //fprintf(stderr, "%3.1f -> %3.1f\n", calc_track, a->track);
        fprintf(stderr, "%06x: %s %s %s %s reliable: %2d tD: %3.0f: %7.2fkm/%7.2fkm in %4.1f s, %4.0fkt/%4.0fkt, %9.5f,%10.5f -> %9.5f,%10.5f\n",
                a->addr,
                source == a->position_valid.last_source ? "SQ" : "LQ",
                mm->cpr_odd ? "O" : "E",
                (inrange ? "  ok" : "fail"),
                (surface ? "S" : "A"),
                min(a->pos_reliable_odd, a->pos_reliable_even),
                track_diff,
                distance / 1000.0,
                range / 1000.0,
                elapsed / 1000.0,
                (distance / elapsed * 1000.0 / 1852.0 * 3600.0),
                speed,
                a->lat, a->lon, lat, lon);
    }

    if (Modes.garbage_ports && !inrange && source >= SOURCE_ADSR
            && distance - range > 1000 && track_diff > 45
            && a->pos_reliable_odd >= Modes.filter_persistence * 3 / 4
            && a->pos_reliable_even >= Modes.filter_persistence * 3 / 4
       ) {
        struct receiver *r = receiverBad(mm->receiverId, a->addr, now);
        if (r && Modes.debug_garbage && r->badCounter > 6) {
            fprintf(stderr, "hex: %06x id: %016"PRIx64" #good: %6d #bad: %3.0f trackDiff: %3.0f: %7.2fkm/%7.2fkm in %4.1f s, max %4.0f kt\n",
                    a->addr, r->id, r->goodCounter, r->badCounter,
                    track_diff,
                    distance / 1000.0,
                    range / 1000.0,
                    elapsed / 1000.0,
                    speed
                   );

        }
    }

    return inrange;
}

static int doGlobalCPR(struct aircraft *a, struct modesMessage *mm, double *lat, double *lon, unsigned *nic, unsigned *rc) {
    int result;
    int fflag = mm->cpr_odd;
    int surface = (mm->cpr_type == CPR_SURFACE);
    struct receiver *receiver;
    double reflat, reflon;

    // derive NIC, Rc from the worse of the two position
    // smaller NIC is worse; larger Rc is worse
    *nic = (a->cpr_even_nic < a->cpr_odd_nic ? a->cpr_even_nic : a->cpr_odd_nic);
    *rc = (a->cpr_even_rc > a->cpr_odd_rc ? a->cpr_even_rc : a->cpr_odd_rc);

    if (surface) {
        // surface global CPR
        // find reference location

        if ((receiver = receiverGetReference(mm->receiverId, &reflat, &reflon, a))) {
            //function sets reflat and reflon on success, nothing to do here.
        } else if (trackDataValid(&a->position_valid)) { // Ok to try aircraft relative first
            reflat = a->lat;
            reflon = a->lon;
        } else if (Modes.bUserFlags & MODES_USER_LATLON_VALID) {
            reflat = Modes.fUserLat;
            reflon = Modes.fUserLon;
        } else if (a->seen_pos) {
            reflat = a->lat;
            reflon = a->lon;
        } else {
            // No local reference, give up
            return (-1);
        }

        result = decodeCPRsurface(reflat, reflon,
                a->cpr_even_lat, a->cpr_even_lon,
                a->cpr_odd_lat, a->cpr_odd_lon,
                fflag,
                lat, lon);

        if (Modes.debug_receiver && receiver && a->seen_pos
                && *lat < 89
                && *lat > -89
                && (fabs(a->lat - *lat) > 35 || fabs(a->lon - *lon) > 35 || fabs(reflat - *lat) > 35 || fabs(reflon - *lon) > 35)
                && !bogus_lat_lon(*lat, *lon)
           ) {
            //struct receiver *r = receiver;
            //fprintf(stderr, "id: %016"PRIx64" #pos: %9"PRIu64" lat min:%4.0f max:%4.0f lon min:%4.0f max:%4.0f\n",
            //        r->id, r->positionCounter,
            //        r->latMin, r->latMax,
            //        r->lonMin, r->lonMax);
            int sc = speed_check(a, mm->source, *lat, *lon, mm);
            fprintf(stderr, "%s%06x surface CPR rec. ref.: %4.0f %4.0f sc: %d result: %7.2f %7.2f --> %7.2f %7.2f\n",
                    (a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : " ",
                    a->addr, reflat, reflon, sc, a->lat, a->lon, *lat, *lon);
        }

        if (0 && Modes.debug_receiver && !(a->addr & MODES_NON_ICAO_ADDRESS)) {
            if (receiver && !trackDataValid(&a->position_valid))
                fprintf(stderr, "%06x using receiver reference: %4.0f %4.0f result: %7.2f %7.2f\n", a->addr, reflat, reflon, *lat, *lon);
        }
        if (receiver && a->addr == Modes.cpr_focus)
            fprintf(stderr, "%06x using reference: %4.0f %4.0f result: %7.2f %7.2f\n", a->addr, reflat, reflon, *lat, *lon);
    } else {
        // airborne global CPR
        result = decodeCPRairborne(a->cpr_even_lat, a->cpr_even_lon,
                a->cpr_odd_lat, a->cpr_odd_lon,
                fflag,
                lat, lon);
    }

    if (result < 0) {
        if (a->addr == Modes.cpr_focus || Modes.debug_cpr) {
            fprintf(stderr, "CPR: decode failure for %06x (%d).\n", a->addr, result);
            fprintf(stderr, "  even: %d %d   odd: %d %d  fflag: %s\n",
                    a->cpr_even_lat, a->cpr_even_lon,
                    a->cpr_odd_lat, a->cpr_odd_lon,
                    fflag ? "odd" : "even");
        }
        return result;
    }

    // check max range
    if (Modes.maxRange > 0 && (Modes.bUserFlags & MODES_USER_LATLON_VALID)) {
        double range = greatcircle(Modes.fUserLat, Modes.fUserLon, *lat, *lon);
        if (range > Modes.maxRange) {
            if (a->addr == Modes.cpr_focus || Modes.debug_cpr) {
                fprintf(stderr, "Global range check failed: %06x: %.3f,%.3f, max range %.1fkm, actual %.1fkm\n",
                        a->addr, *lat, *lon, Modes.maxRange / 1000.0, range / 1000.0);
            }

            Modes.stats_current.cpr_global_range_checks++;
            return (-2); // we consider an out-of-range value to be bad data
        }
    }

    // check speed limit
    if (!speed_check(a, mm->source, *lat, *lon, mm)) {
        Modes.stats_current.cpr_global_speed_checks++;
        return -2;
    }

    return result;
}

static int doLocalCPR(struct aircraft *a, struct modesMessage *mm, double *lat, double *lon, unsigned *nic, unsigned *rc) {
    // relative CPR
    // find reference location
    double reflat, reflon;
    double range_limit = 0;
    int result;
    int fflag = mm->cpr_odd;
    int surface = (mm->cpr_type == CPR_SURFACE);
    int relative_to = 0; // aircraft(1) or receiver(2) relative

    if (fflag) {
        *nic = a->cpr_odd_nic;
        *rc = a->cpr_odd_rc;
    } else {
        *nic = a->cpr_even_nic;
        *rc = a->cpr_even_rc;
    }

    if (mm->sysTimestampMsg < a->position_valid.updated + (10*60*1000)) {
        reflat = a->lat;
        reflon = a->lon;

        if (a->pos_nic < *nic)
            *nic = a->pos_nic;
        if (a->pos_rc < *rc)
            *rc = a->pos_rc;

        range_limit = 1852*100; // 100NM
        // 100 NM in the 10 minutes of position validity means 600 knots which
        // is fast but happens even for commercial airliners.
        // It's not a problem if this limitation fails every now and then.
        // A wrong relative position decode would require the aircraft to
        // travel 360-100=260 NM in the 10 minutes of position validity.
        // This is impossible for planes slower than 1560 knots/Mach 2.3 over the ground.
        // Thus this range limit combined with the 10 minutes of position
        // validity should not provide bad positions (1 cell away).

        relative_to = 1;
    } else if (!surface && (Modes.bUserFlags & MODES_USER_LATLON_VALID)) {
        reflat = Modes.fUserLat;
        reflon = Modes.fUserLon;

        // The cell size is at least 360NM, giving a nominal
        // max range of 180NM (half a cell).
        //
        // If the receiver range is more than half a cell
        // then we must limit this range further to avoid
        // ambiguity. (e.g. if we receive a position report
        // at 200NM distance, this may resolve to a position
        // at (200-360) = 160NM in the wrong direction)

        if (Modes.maxRange == 0) {
            return (-1); // Can't do receiver-centered checks at all
        } else if (Modes.maxRange <= 1852 * 180) {
            range_limit = Modes.maxRange;
        } else if (Modes.maxRange < 1852 * 360) {
            range_limit = (1852 * 360) - Modes.maxRange;
        } else {
            return (-1); // Can't do receiver-centered checks at all
        }
        relative_to = 2;
    } else {
        // No local reference, give up
        return (-1);
    }

    result = decodeCPRrelative(reflat, reflon,
            mm->cpr_lat,
            mm->cpr_lon,
            fflag, surface,
            lat, lon);
    if (result < 0) {
        return result;
    }

    // check range limit
    if (range_limit > 0) {
        double range = greatcircle(reflat, reflon, *lat, *lon);
        if (range > range_limit) {
            Modes.stats_current.cpr_local_range_checks++;
            return (-1);
        }
    }

    // check speed limit
    if (!speed_check(a, mm->source, *lat, *lon, mm)) {
        if (a->addr == Modes.cpr_focus || Modes.debug_cpr) {
            fprintf(stderr, "Speed check for %06x with local decoding failed\n", a->addr);
        }
        Modes.stats_current.cpr_local_speed_checks++;
        return -1;
    }

    return relative_to;
}

static uint64_t time_between(uint64_t t1, uint64_t t2) {
    if (t1 >= t2)
        return t1 - t2;
    else
        return t2 - t1;
}


static void setPosition(struct aircraft *a, struct modesMessage *mm, uint64_t now) {
    // Update aircraft state
    a->lat = mm->decoded_lat;
    a->lon = mm->decoded_lon;
    a->pos_nic = mm->decoded_nic;
    a->pos_rc = mm->decoded_rc;

    a->pos_surface = trackDataValid(&a->airground_valid) && a->airground == AG_GROUND;

    if (mm->jsonPos)
        jsonPositionOutput(mm, a);

    if (a->pos_reliable_odd >= 2 && a->pos_reliable_even >= 2 && mm->source == SOURCE_ADSB) {
        update_range_histogram(mm->decoded_lat, mm->decoded_lon);
        if (mm->cpr_type != CPR_SURFACE) {
            receiverPositionReceived(a, mm->receiverId, mm->decoded_lat, mm->decoded_lon, now);
        }
    }

    Modes.stats_current.pos_by_type[mm->addrtype]++;
    Modes.stats_current.pos_all++;

    a->seen_pos = now;

    // update addrtype, we use the type from the accepted position.
    a->addrtype = mm->addrtype;
    a->addrtype_updated = now;
}

static void updatePosition(struct aircraft *a, struct modesMessage *mm, uint64_t now) {
    int location_result = -1;
    uint64_t max_elapsed;
    double new_lat = 0, new_lon = 0;
    unsigned new_nic = 0;
    unsigned new_rc = 0;
    int surface;

    surface = (mm->cpr_type == CPR_SURFACE);
    a->pos_surface = trackDataValid(&a->airground_valid) && a->airground == AG_GROUND;

    if (surface) {
        ++Modes.stats_current.cpr_surface;

        // Surface: 25 seconds if >25kt or speed unknown, 50 seconds otherwise
        if (mm->gs_valid && mm->gs.selected <= 25)
            max_elapsed = 50000;
        else
            max_elapsed = 25000;
    } else {
        ++Modes.stats_current.cpr_airborne;

        // Airborne: 10 seconds
        max_elapsed = 10000;
    }

    // If we have enough recent data, try global CPR
    if (trackDataValid(&a->cpr_odd_valid) && trackDataValid(&a->cpr_even_valid) &&
            a->cpr_odd_valid.source == a->cpr_even_valid.source &&
            a->cpr_odd_type == a->cpr_even_type &&
            time_between(a->cpr_odd_valid.updated, a->cpr_even_valid.updated) <= max_elapsed) {

        location_result = doGlobalCPR(a, mm, &new_lat, &new_lon, &new_nic, &new_rc);

        //if (a->addr == Modes.cpr_focus)
        //    fprintf(stderr, "%06x globalCPR result: %d\n", a->addr, location_result);

        if (location_result == -2) {
            if (a->addr == Modes.cpr_focus || Modes.debug_cpr) {
                fprintf(stderr, "global CPR failure (invalid) for (%06x).\n", a->addr);
            }
            // Global CPR failed because the position produced implausible results.
            // This is bad data.
            // At least one of the CPRs is bad, mark them both invalid.
            // If we are not confident in the position, invalidate it as well.

            mm->pos_bad = 1;

            return;
        } else if (location_result == -1) {
            if (a->addr == Modes.cpr_focus || Modes.debug_cpr) {
                if (mm->source == SOURCE_MLAT) {
                    fprintf(stderr, "CPR skipped from MLAT (%06x).\n", a->addr);
                }
            }
            // No local reference for surface position available, or the two messages crossed a zone.
            // Nonfatal, try again later.
            Modes.stats_current.cpr_global_skipped++;
        } else {
            if (accept_data(&a->position_valid, mm->source, mm, 1)) {
                Modes.stats_current.cpr_global_ok++;

                int persist = Modes.filter_persistence;

                if (a->pos_reliable_odd <= 0 || a->pos_reliable_even <=0) {
                    a->pos_reliable_odd = 1;
                    a->pos_reliable_even = 1;
                } else if (mm->cpr_odd) {
                    a->pos_reliable_odd = min(a->pos_reliable_odd + 1, persist);
                } else {
                    a->pos_reliable_even = min(a->pos_reliable_even + 1, persist);
                }

                if (trackDataValid(&a->gs_valid))
                    a->gs_last_pos = a->gs;

            } else {
                Modes.stats_current.cpr_global_skipped++;
                location_result = -2;
            }
        }
    } else {
        if (a->addr == Modes.cpr_focus)
            fprintf(stderr, "%06x: unable global CPR, current CPR: %s, other CPR age %0.1f sources %d %d %d %d types: %s\n",
                    a->addr,
                    mm->cpr_odd ? " odd" : "even",
                    mm->cpr_odd ? fmin(999, ((double) now - a->cpr_even_valid.updated) / 1000.0) : fmin(999, ((double) now - a->cpr_odd_valid.updated) / 1000.0),
                    a->cpr_odd_valid.source, a->cpr_even_valid.source,
                    a->cpr_odd_valid.last_source, a->cpr_even_valid.last_source,
                    (a->cpr_odd_type == a->cpr_even_type) ? "same" : "diff");
    }

    // Otherwise try relative CPR.
    if (location_result == -1) {
        location_result = doLocalCPR(a, mm, &new_lat, &new_lon, &new_nic, &new_rc);

        if (a->addr == Modes.cpr_focus)
            fprintf(stderr, "%06x: localCPR: %d\n", a->addr, location_result);

        if (location_result >= 0 && accept_data(&a->position_valid, mm->source, mm, 1)) {
            Modes.stats_current.cpr_local_ok++;
            mm->cpr_relative = 1;

            if (trackDataValid(&a->gs_valid))
                a->gs_last_pos = a->gs;

            if (location_result == 1) {
                Modes.stats_current.cpr_local_aircraft_relative++;
            }
            if (location_result == 2) {
                Modes.stats_current.cpr_local_receiver_relative++;
            }
        } else {
            Modes.stats_current.cpr_local_skipped++;
            location_result = -1;
        }
    }

    if (location_result >= 0) {
        // If we sucessfully decoded, back copy the results to mm so that we can print them in list output
        mm->cpr_decoded = 1;
        mm->decoded_lat = new_lat;
        mm->decoded_lon = new_lon;
        mm->decoded_nic = new_nic;
        mm->decoded_rc = new_rc;

        uint64_t now = mm->sysTimestampMsg;
        globe_stuff(a, mm, new_lat, new_lon, now);

        setPosition(a, mm, now);
    }

}

static unsigned compute_nic(unsigned metype, unsigned version, unsigned nic_a, unsigned nic_b, unsigned nic_c) {
    switch (metype) {
        case 5: // surface
        case 9: // airborne
        case 20: // airborne, GNSS altitude
            return 11;

        case 6: // surface
        case 10: // airborne
        case 21: // airborne, GNSS altitude
            return 10;

        case 7: // surface
            if (version == 2) {
                if (nic_a && !nic_c) {
                    return 9;
                } else {
                    return 8;
                }
            } else if (version == 1) {
                if (nic_a) {
                    return 9;
                } else {
                    return 8;
                }
            } else {
                return 8;
            }

        case 8: // surface
            if (version == 2) {
                if (nic_a && nic_c) {
                    return 7;
                } else if (nic_a && !nic_c) {
                    return 6;
                } else if (!nic_a && nic_c) {
                    return 6;
                } else {
                    return 0;
                }
            } else {
                return 0;
            }

        case 11: // airborne
            if (version == 2) {
                if (nic_a && nic_b) {
                    return 9;
                } else {
                    return 8;
                }
            } else if (version == 1) {
                if (nic_a) {
                    return 9;
                } else {
                    return 8;
                }
            } else {
                return 8;
            }

        case 12: // airborne
            return 7;

        case 13: // airborne
            return 6;

        case 14: // airborne
            return 5;

        case 15: // airborne
            return 4;

        case 16: // airborne
            if (nic_a && nic_b) {
                return 3;
            } else {
                return 2;
            }

        case 17: // airborne
            return 1;

        default:
            return 0;
    }
}

static unsigned compute_rc(unsigned metype, unsigned version, unsigned nic_a, unsigned nic_b, unsigned nic_c) {
    switch (metype) {
        case 5: // surface
        case 9: // airborne
        case 20: // airborne, GNSS altitude
            return 8; // 7.5m

        case 6: // surface
        case 10: // airborne
        case 21: // airborne, GNSS altitude
            return 25;

        case 7: // surface
            if (version == 2) {
                if (nic_a && !nic_c) {
                    return 75;
                } else {
                    return 186; // 185.2m, 0.1NM
                }
            } else if (version == 1) {
                if (nic_a) {
                    return 75;
                } else {
                    return 186; // 185.2m, 0.1NM
                }
            } else {
                return 186; // 185.2m, 0.1NM
            }

        case 8: // surface
            if (version == 2) {
                if (nic_a && nic_c) {
                    return 371; // 370.4m, 0.2NM
                } else if (nic_a && !nic_c) {
                    return 556; // 555.6m, 0.3NM
                } else if (!nic_a && nic_c) {
                    return 926; // 926m, 0.5NM
                } else {
                    return RC_UNKNOWN;
                }
            } else {
                return RC_UNKNOWN;
            }

        case 11: // airborne
            if (version == 2) {
                if (nic_a && nic_b) {
                    return 75;
                } else {
                    return 186; // 370.4m, 0.2NM
                }
            } else if (version == 1) {
                if (nic_a) {
                    return 75;
                } else {
                    return 186; // 370.4m, 0.2NM
                }
            } else {
                return 186; // 370.4m, 0.2NM
            }

        case 12: // airborne
            return 371; // 370.4m, 0.2NM

        case 13: // airborne
            if (version == 2) {
                if (!nic_a && nic_b) {
                    return 556; // 555.6m, 0.3NM
                } else if (!nic_a && !nic_b) {
                    return 926; // 926m, 0.5NM
                } else if (nic_a && nic_b) {
                    return 1112; // 1111.2m, 0.6NM
                } else {
                    return RC_UNKNOWN; // bad combination, assume worst Rc
                }
            } else if (version == 1) {
                if (nic_a) {
                    return 1112; // 1111.2m, 0.6NM
                } else {
                    return 926; // 926m, 0.5NM
                }
            } else {
                return 926; // 926m, 0.5NM
            }

        case 14: // airborne
            return 1852; // 1.0NM

        case 15: // airborne
            return 3704; // 2NM

        case 16: // airborne
            if (version == 2) {
                if (nic_a && nic_b) {
                    return 7408; // 4NM
                } else {
                    return 14816; // 8NM
                }
            } else if (version == 1) {
                if (nic_a) {
                    return 7408; // 4NM
                } else {
                    return 14816; // 8NM
                }
            } else {
                return 18520; // 10NM
            }

        case 17: // airborne
            return 37040; // 20NM

        default:
            return RC_UNKNOWN;
    }
}

// Map ADS-B v0 position message type to NACp value
// returned computed NACp, or -1 if not a suitable message type
static int compute_v0_nacp(struct modesMessage *mm)
{
    if (mm->msgtype != 17 && mm->msgtype != 18) {
        return -1;
    }

    // ED-102A Table N-7
    switch (mm->metype) {
    case 0: return 0;
    case 5: return 11;
    case 6: return 10;
    case 7: return 8;
    case 8: return 0;
    case 9: return 11;
    case 10: return 10;
    case 11: return 8;
    case 12: return 7;
    case 13: return 6;
    case 14: return 5;
    case 15: return 4;
    case 16: return 1;
    case 17: return 1;
    case 18: return 0;
    case 20: return 11;
    case 21: return 10;
    case 22: return 0;
    default: return -1;
    }
}

// Map ADS-B v0 position message type to SIL value
// returned computed SIL, or -1 if not a suitable message type
static int compute_v0_sil(struct modesMessage *mm)
{
    if (mm->msgtype != 17 && mm->msgtype != 18) {
        return -1;
    }

    // ED-102A Table N-8
    switch (mm->metype) {
    case 0:
        return 0;

    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        return 2;

    case 18:
        return 0;

    case 20:
    case 21:
        return 2;

    case 22:
        return 0;

    default:
        return -1;
    }
}

static void compute_nic_rc_from_message(struct modesMessage *mm, struct aircraft *a, unsigned *nic, unsigned *rc) {
    int nic_a = (trackDataValid(&a->nic_a_valid) && a->nic_a);
    int nic_b = (mm->accuracy.nic_b_valid && mm->accuracy.nic_b);
    int nic_c = (trackDataValid(&a->nic_c_valid) && a->nic_c);

    *nic = compute_nic(mm->metype, a->adsb_version, nic_a, nic_b, nic_c);
    *rc = compute_rc(mm->metype, a->adsb_version, nic_a, nic_b, nic_c);
}

static int altitude_to_feet(int raw, altitude_unit_t unit) {
    switch (unit) {
        case UNIT_METERS:
            return raw / 0.3048;
        case UNIT_FEET:
            return raw;
        default:
            return 0;
    }
}

//
//=========================================================================
//
// Receive new messages and update tracked aircraft state
//

struct aircraft *trackUpdateFromMessage(struct modesMessage *mm) {
    struct aircraft *a;
    unsigned int cpr_new = 0;

    if (mm->msgtype == 32) {
        // Mode A/C, just count it (we ignore SPI)
        modeAC_count[modeAToIndex(mm->squawk)]++;
        return NULL;
    }

    if (mm->addr == 0) {
        // junk address, don't track it
        return NULL;
    }

    uint64_t now = mm->sysTimestampMsg;

    // Lookup our aircraft or create a new one
    a = aircraftGet(mm->addr);
    if (!a) { // If it's a currently unknown aircraft....
        a = aircraftCreate(mm); // ., create a new record for it,
    }

    if (mm->cpr_valid) {
        memcpy(Modes.scratch, a, sizeof(struct aircraft));
    } else if (mm->garbage) {
        return NULL;
    }

    if (mm->signalLevel > 0) {

        a->signalLevel[a->signalNext] = mm->signalLevel;
        a->signalNext = (a->signalNext + 1) & 7;

        if (a->no_signal_count >= 10) {
            for (int i = 0; i < 8; ++i) {
                a->signalLevel[i] = fmax(1e-5, mm->signalLevel);
            }
        }
        if (a->no_signal_count > 0)
            a->no_signal_count = 0;
    } else {
        // if we haven't received a message with signal level for a bit, set it to zero
        if (a->no_signal_count < 10 && ++a->no_signal_count >= 10) {
            for (int i = 0; i < 8; ++i) {
                a->signalLevel[i] = 1e-5;
            }
        }
    }
    a->seen = mm->sysTimestampMsg;

    // reset to 100000 on overflow ... avoid any low message count checks
    if (a->messages == UINT32_MAX)
        a->messages = 100000;

    a->messages++;

    // update addrtype
    if (a->addrtype_updated > now)
        a->addrtype_updated = now;

    if (
            (mm->addrtype <= a->addrtype && now > 30 * 1000 + a->addrtype_updated)
            || (mm->addrtype > a->addrtype && now > 90 * 1000 + a->addrtype_updated)
       ) {

        a->addrtype = mm->addrtype;
        a->addrtype_updated = now;
    }

    // decide on where to stash the version
    int dummy_version = -1; // used for non-adsb/adsr/tisb messages
    int *message_version;

    switch (mm->source) {
    case SOURCE_ADSB:
        message_version = &a->adsb_version;
        break;
    case SOURCE_TISB:
        message_version = &a->tisb_version;
        break;
    case SOURCE_ADSR:
        message_version = &a->adsr_version;
        break;
    default:
        message_version = &dummy_version;
        break;
    }

    // assume version 0 until we see something else
    if (*message_version < 0) {
        *message_version = 0;
    }

    // category shouldn't change over time, don't bother with metadata
    if (mm->category_valid) {
        a->category = mm->category;
    }

    // operational status message
    // done early to update version / HRD / TAH
    if (mm->opstatus.valid) {
        *message_version = mm->opstatus.version;
        
        if (mm->opstatus.hrd != HEADING_INVALID) {
            a->adsb_hrd = mm->opstatus.hrd;
        }
        if (mm->opstatus.tah != HEADING_INVALID) {
            a->adsb_tah = mm->opstatus.tah;
        }
    }

    // fill in ADS-B v0 NACp, SIL from position message type
    if (*message_version == 0 && !mm->accuracy.nac_p_valid) {
        int computed_nacp = compute_v0_nacp(mm);
        if (computed_nacp != -1) {
            mm->accuracy.nac_p_valid = 1;
            mm->accuracy.nac_p = computed_nacp;
        }
    }

    if (*message_version == 0 && mm->accuracy.sil_type == SIL_INVALID) {
        int computed_sil = compute_v0_sil(mm);
        if (computed_sil != -1) {
            mm->accuracy.sil_type = SIL_UNKNOWN;
            mm->accuracy.sil = computed_sil;
        }
    }

    if (mm->altitude_baro_valid &&
            (mm->source >= a->altitude_baro_valid.source ||
             (trackDataAge(now, &a->altitude_baro_valid) > 10 * 1000
              && a->altitude_baro_valid.source != SOURCE_JAERO
              && a->altitude_baro_valid.source != SOURCE_SBS)
            )
       ) {
        int alt = altitude_to_feet(mm->altitude_baro, mm->altitude_baro_unit);
        if (a->modeC_hit) {
            int new_modeC = (a->altitude_baro + 49) / 100;
            int old_modeC = (alt + 49) / 100;
            if (new_modeC != old_modeC) {
                a->modeC_hit = 0;
            }
        }

        int delta = alt - a->altitude_baro;
        int fpm = 0;

        int max_fpm = 12500;
        int min_fpm = -12500;

        if (abs(delta) >= 300) {
            fpm = delta*60*10/(abs((int)trackDataAge(now, &a->altitude_baro_valid)/100)+10);
            if (trackDataValid(&a->geom_rate_valid) && trackDataAge(now, &a->geom_rate_valid) < trackDataAge(now, &a->baro_rate_valid)) {
                min_fpm = a->geom_rate - 1500 - min(11000, ((int)trackDataAge(now, &a->geom_rate_valid)/2));
                max_fpm = a->geom_rate + 1500 + min(11000, ((int)trackDataAge(now, &a->geom_rate_valid)/2));
            } else if (trackDataValid(&a->baro_rate_valid)) {
                min_fpm = a->baro_rate - 1500 - min(11000, ((int)trackDataAge(now, &a->baro_rate_valid)/2));
                max_fpm = a->baro_rate + 1500 + min(11000, ((int)trackDataAge(now, &a->baro_rate_valid)/2));
            }
            if (trackDataValid(&a->altitude_baro_valid) && trackDataAge(now, &a->altitude_baro_valid) < 30 * SECONDS) {
                a->alt_reliable = min(
                        ALTITUDE_BARO_RELIABLE_MAX - (ALTITUDE_BARO_RELIABLE_MAX*trackDataAge(now, &a->altitude_baro_valid)/(30 * SECONDS)),
                        a->alt_reliable);
            } else {
                a->alt_reliable = 0;
            }
        }
        int good_crc = (mm->crc == 0 && mm->source >= SOURCE_JAERO) ? 4 : 0;

        if (mm->source == SOURCE_SBS || mm->source == SOURCE_MLAT)
            good_crc = ALTITUDE_BARO_RELIABLE_MAX/2 - 1;

        if (a->altitude_baro > 50175 && mm->alt_q_bit && a->alt_reliable > ALTITUDE_BARO_RELIABLE_MAX/4) {
            good_crc = 0;
            //fprintf(stderr, "q_bit == 1 && a->alt > 50175: %06x\n", a->addr);
            goto discard_alt;
        }

        if (a->alt_reliable <= 0  || abs(delta) < 300)
            goto accept_alt;
        if (fpm < max_fpm && fpm > min_fpm)
            goto accept_alt;
        if (good_crc > a->alt_reliable)
            goto accept_alt;
        if (mm->source > a->altitude_baro_valid.source)
            goto accept_alt;
        if (mm->source == SOURCE_JAERO && (a->altitude_baro_valid.source == SOURCE_JAERO || a->altitude_baro_valid.source == SOURCE_INVALID)) {
            good_crc = ALTITUDE_BARO_RELIABLE_MAX;
            goto accept_alt;
        }

        goto discard_alt;
accept_alt:
            if (accept_data(&a->altitude_baro_valid, mm->source, mm, 1)) {
                a->alt_reliable = min(ALTITUDE_BARO_RELIABLE_MAX , a->alt_reliable + (good_crc+1));
                if (0 && a->addr == 0x4b2917 && abs(delta) > -1 && delta != alt) {
                    fprintf(stderr, "Alt check S: %06x: %2d %6d ->%6d, %s->%s, min %.1f kfpm, max %.1f kfpm, actual %.1f kfpm\n",
                            a->addr, a->alt_reliable, a->altitude_baro, alt,
                            source_string(a->altitude_baro_valid.source),
                            source_string(mm->source),
                            min_fpm/1000.0, max_fpm/1000.0, fpm/1000.0);
                }
                a->altitude_baro = alt;
            }
            goto end_alt;
discard_alt:
            a->alt_reliable = a->alt_reliable - (good_crc+1);
            if (0 && a->addr == 0x4b2917)
                fprintf(stderr, "Alt check F: %06x: %2d %6d ->%6d, %s->%s, min %.1f kfpm, max %.1f kfpm, actual %.1f kfpm\n",
                        a->addr, a->alt_reliable, a->altitude_baro, alt,
                        source_string(a->altitude_baro_valid.source),
                        source_string(mm->source),
                        min_fpm/1000.0, max_fpm/1000.0, fpm/1000.0);
            if (a->alt_reliable <= 0) {
                //fprintf(stderr, "Altitude INVALIDATED: %06x\n", a->addr);
                a->alt_reliable = 0;
                if (a->position_valid.source > SOURCE_JAERO)
                    a->altitude_baro_valid.source = SOURCE_INVALID;
            }
            if (Modes.garbage_ports)
                mm->source = SOURCE_INVALID;
end_alt:
            ;
    }

    if (mm->squawk_valid && accept_data(&a->squawk_valid, mm->source, mm, 0)) {
        if (mm->squawk != a->squawk) {
            a->modeA_hit = 0;
        }
        a->squawk = mm->squawk;

#if 0   // Disabled for now as it obscures the origin of the data
        // Handle 7x00 without a corresponding emergency status
        if (!mm->emergency_valid) {
            emergency_t squawk_emergency;
            switch (mm->squawk) {
                case 0x7500:
                    squawk_emergency = EMERGENCY_UNLAWFUL;
                    break;
                case 0x7600:
                    squawk_emergency = EMERGENCY_NORDO;
                    break;
                case 0x7700:
                    squawk_emergency = EMERGENCY_GENERAL;
                    break;
                default:
                    squawk_emergency = EMERGENCY_NONE;
                    break;
            }

            if (squawk_emergency != EMERGENCY_NONE && accept_data(&a->emergency_valid, mm->source, mm, 0)) {
                a->emergency = squawk_emergency;
            }
        }
#endif
    }

    if (mm->emergency_valid && accept_data(&a->emergency_valid, mm->source, mm, 0)) {
        a->emergency = mm->emergency;
    }

    if (mm->altitude_geom_valid && accept_data(&a->altitude_geom_valid, mm->source, mm, 1)) {
        a->altitude_geom = altitude_to_feet(mm->altitude_geom, mm->altitude_geom_unit);
    }

    if (mm->geom_delta_valid && accept_data(&a->geom_delta_valid, mm->source, mm, 1)) {
        a->geom_delta = mm->geom_delta;
    }

    if (mm->heading_valid) {
        heading_type_t htype = mm->heading_type;
        if (htype == HEADING_MAGNETIC_OR_TRUE) {
            htype = a->adsb_hrd;
        } else if (htype == HEADING_TRACK_OR_HEADING) {
            htype = a->adsb_tah;
        }

        if (htype == HEADING_GROUND_TRACK && accept_data(&a->track_valid, mm->source, mm, 1)) {
            a->track = mm->heading;
        } else if (htype == HEADING_MAGNETIC) {
            double dec;
            int err = declination(a, &dec);
            if (accept_data(&a->mag_heading_valid, mm->source, mm, 1)) {
                a->mag_heading = mm->heading;

                // don't accept more than 45 degree crab when deriving the true heading
                if (
                        (!trackDataValid(&a->track_valid) || fabs(norm_diff(mm->heading + dec - a->track, 180)) < 45)
                        && !err && accept_data(&a->true_heading_valid, SOURCE_INDIRECT, mm, 1)
                   ) {
                    a->true_heading = norm_angle(mm->heading + dec, 180);
                    calc_wind(a, now);
                }
            }
        } else if (htype == HEADING_TRUE && accept_data(&a->true_heading_valid, mm->source, mm, 1)) {
            a->true_heading = mm->heading;
        }
    }

    if (mm->track_rate_valid && accept_data(&a->track_rate_valid, mm->source, mm, 1)) {
        a->track_rate = mm->track_rate;
    }

    if (mm->roll_valid && accept_data(&a->roll_valid, mm->source, mm, 1)) {
        a->roll = mm->roll;
    }

    if (mm->gs_valid) {
        mm->gs.selected = (*message_version == 2 ? mm->gs.v2 : mm->gs.v0);
        if (accept_data(&a->gs_valid, mm->source, mm, 1)) {
            a->gs = mm->gs.selected;
        }
    }

    if (mm->ias_valid && accept_data(&a->ias_valid, mm->source, mm, 0)) {
        a->ias = mm->ias;
    }

    if (mm->tas_valid
            && !(trackDataValid(&a->ias_valid) && mm->tas < a->ias)
            && accept_data(&a->tas_valid, mm->source, mm, 0)) {
        a->tas = mm->tas;
        calc_temp(a, now);
        calc_wind(a, now);
    }

    if (mm->mach_valid && accept_data(&a->mach_valid, mm->source, mm, 0)) {
        a->mach = mm->mach;
        calc_temp(a, now);
    }

    if (mm->baro_rate_valid && accept_data(&a->baro_rate_valid, mm->source, mm, 1)) {
        a->baro_rate = mm->baro_rate;
    }

    if (mm->geom_rate_valid && accept_data(&a->geom_rate_valid, mm->source, mm, 1)) {
        a->geom_rate = mm->geom_rate;
    }

    if (mm->airground != AG_INVALID) {
        // If our current state is UNCERTAIN, accept new data as normal
        // If our current state is certain but new data is not, only accept the uncertain state if the certain data has gone stale
        if (mm->airground != AG_UNCERTAIN ||
                (mm->airground == AG_UNCERTAIN && now > a->airground_valid.updated + 60 * 1000)) {
            if (mm->airground != a->airground)
                mm->reduce_forward = 1;
            if (accept_data(&a->airground_valid, mm->source, mm, 0)) {
                a->airground = mm->airground;
            }
        }
    }

    if (mm->callsign_valid && accept_data(&a->callsign_valid, mm->source, mm, 0)) {
        memcpy(a->callsign, mm->callsign, sizeof (a->callsign));
    }

    if (mm->nav.mcp_altitude_valid && accept_data(&a->nav_altitude_mcp_valid, mm->source, mm, 0)) {
        a->nav_altitude_mcp = mm->nav.mcp_altitude;
    }

    if (mm->nav.fms_altitude_valid && accept_data(&a->nav_altitude_fms_valid, mm->source, mm, 0)) {
        a->nav_altitude_fms = mm->nav.fms_altitude;
    }

    if (mm->nav.altitude_source != NAV_ALT_INVALID && accept_data(&a->nav_altitude_src_valid, mm->source, mm, 0)) {
        a->nav_altitude_src = mm->nav.altitude_source;
    }

    if (mm->nav.heading_valid && accept_data(&a->nav_heading_valid, mm->source, mm, 0)) {
        a->nav_heading = mm->nav.heading;
    }

    if (mm->nav.modes_valid && accept_data(&a->nav_modes_valid, mm->source, mm, 0)) {
        a->nav_modes = mm->nav.modes;
    }

    if (mm->nav.qnh_valid && accept_data(&a->nav_qnh_valid, mm->source, mm, 0)) {
        a->nav_qnh = mm->nav.qnh;
    }

    if (mm->alert_valid && accept_data(&a->alert_valid, mm->source, mm, 0)) {
        a->alert = mm->alert;
    }

    if (mm->spi_valid && accept_data(&a->spi_valid, mm->source, mm, 0)) {
        a->spi = mm->spi;
    }

    // CPR, even
    if (mm->cpr_valid && !mm->cpr_odd && accept_data(&a->cpr_even_valid, mm->source, mm, 1)) {
        a->cpr_even_type = mm->cpr_type;
        a->cpr_even_lat = mm->cpr_lat;
        a->cpr_even_lon = mm->cpr_lon;
        compute_nic_rc_from_message(mm, a, &a->cpr_even_nic, &a->cpr_even_rc);
        cpr_new = 1;
    }

    // CPR, odd
    if (mm->cpr_valid && mm->cpr_odd && accept_data(&a->cpr_odd_valid, mm->source, mm, 1)) {
        a->cpr_odd_type = mm->cpr_type;
        a->cpr_odd_lat = mm->cpr_lat;
        a->cpr_odd_lon = mm->cpr_lon;
        compute_nic_rc_from_message(mm, a, &a->cpr_odd_nic, &a->cpr_odd_rc);
        cpr_new = 1;
    }

    if (mm->accuracy.sda_valid && accept_data(&a->sda_valid, mm->source, mm, 0)) {
        a->sda = mm->accuracy.sda;
    }

    if (mm->accuracy.nic_a_valid && accept_data(&a->nic_a_valid, mm->source, mm, 0)) {
        a->nic_a = mm->accuracy.nic_a;
    }

    if (mm->accuracy.nic_c_valid && accept_data(&a->nic_c_valid, mm->source, mm, 0)) {
        a->nic_c = mm->accuracy.nic_c;
    }

    if (mm->accuracy.nic_baro_valid && accept_data(&a->nic_baro_valid, mm->source, mm, 0)) {
        a->nic_baro = mm->accuracy.nic_baro;
    }

    if (mm->accuracy.nac_p_valid && accept_data(&a->nac_p_valid, mm->source, mm, 0)) {
        a->nac_p = mm->accuracy.nac_p;
    }

    if (mm->accuracy.nac_v_valid && accept_data(&a->nac_v_valid, mm->source, mm, 0)) {
        a->nac_v = mm->accuracy.nac_v;
    }

    if (mm->accuracy.sil_type != SIL_INVALID && accept_data(&a->sil_valid, mm->source, mm, 0)) {
        a->sil = mm->accuracy.sil;
        if (a->sil_type == SIL_INVALID || mm->accuracy.sil_type != SIL_UNKNOWN) {
            a->sil_type = mm->accuracy.sil_type;
        }
    }

    if (mm->accuracy.gva_valid && accept_data(&a->gva_valid, mm->source, mm, 0)) {
        a->gva = mm->accuracy.gva;
    }

    if (mm->accuracy.sda_valid && accept_data(&a->sda_valid, mm->source, mm, 0)) {
        a->sda = mm->accuracy.sda;
    }

    // Now handle derived data

    // derive geometric altitude if we have baro + delta
    if (a->alt_reliable >= 3 && compare_validity(&a->altitude_baro_valid, &a->altitude_geom_valid) > 0 &&
            compare_validity(&a->geom_delta_valid, &a->altitude_geom_valid) > 0) {
        // Baro and delta are both more recent than geometric, derive geometric from baro + delta
        a->altitude_geom = a->altitude_baro + a->geom_delta;
        combine_validity(&a->altitude_geom_valid, &a->altitude_baro_valid, &a->geom_delta_valid, now);
    }

    // If we've got a new cpr_odd or cpr_even
    if (cpr_new) {
        updatePosition(a, mm, now);
        if (0 && a->addr == Modes.cpr_focus) {
            fprintf(stderr, "%06x: age: odd %"PRIu64" even %"PRIu64"\n",
                    a->addr,
                    trackDataAge(mm->sysTimestampMsg, &a->cpr_odd_valid),
                    trackDataAge(mm->sysTimestampMsg, &a->cpr_even_valid));
        }
    }

    if (mm->sbs_in && mm->sbs_pos_valid) {
        int old_jaero = 0;
        if (mm->source == SOURCE_JAERO && a->trace_len > 0) {
            for (int i = max(0, a->trace_len - 10); i < a->trace_len; i++) {
                if ( (int32_t) (mm->decoded_lat * 1E6) == a->trace[i].lat
                        && (int32_t) (mm->decoded_lon * 1E6) == a->trace[i].lon )
                    old_jaero = 1;
            }
        }
        // avoid using already received positions
        if (old_jaero || greatcircle(a->lat, a->lon, mm->decoded_lat, mm->decoded_lon) < 1) {
        } else if (
                mm->source != SOURCE_PRIO
                && !speed_check(a, mm->source, mm->decoded_lat, mm->decoded_lon, mm)
           )
        {
            mm->pos_bad = 1;
            // speed check failed, do nothing
        } else if (accept_data(&a->position_valid, mm->source, mm, 0)) {

            int persist = Modes.filter_persistence;
            a->pos_reliable_odd = min(a->pos_reliable_odd + 1, persist);
            a->pos_reliable_even = min(a->pos_reliable_even + 1, persist);

            globe_stuff(a, mm, mm->decoded_lat, mm->decoded_lon, now);

            setPosition(a, mm, now);

            if (a->messages < 2)
                a->messages = 2;
        }
    }


    if (mm->msgtype == 11 && mm->IID == 0 && mm->correctedbits == 0 && now > a->next_reduce_forward_DF11) {

        a->next_reduce_forward_DF11 = now + Modes.net_output_beast_reduce_interval * 4;
        mm->reduce_forward = 1;
    }

    if (mm->cpr_valid && (mm->garbage || mm->pos_bad || mm->duplicate)) {
        memcpy(a, Modes.scratch, sizeof(struct aircraft));
        if (mm->pos_bad)
            position_bad(mm, a);
    }

    if(a->messages == 3 && a->first_message) {
        free(a->first_message);
        a->first_message = NULL;
    }

    return (a);
}

//
// Periodic updates of tracking state
//

// Periodically match up mode A/C results with mode S results

static void trackMatchAC(uint64_t now) {
    // clear match flags
    for (unsigned i = 0; i < 4096; ++i) {
        modeAC_match[i] = 0;
    }

    // scan aircraft list, look for matches
    for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
        for (struct aircraft *a = Modes.aircraft[j]; a; a = a->next) {
            if ((now - a->seen) > 5000) {
                continue;
            }

            // match on Mode A
            if (trackDataValid(&a->squawk_valid)) {
                unsigned i = modeAToIndex(a->squawk);
                if ((modeAC_count[i] - modeAC_lastcount[i]) >= TRACK_MODEAC_MIN_MESSAGES) {
                    a->modeA_hit = 1;
                    modeAC_match[i] = (modeAC_match[i] ? 0xFFFFFFFF : a->addr);
                }
            }

            // match on Mode C (+/- 100ft)
            if (trackDataValid(&a->altitude_baro_valid)) {
                int modeC = (a->altitude_baro + 49) / 100;

                unsigned modeA = modeCToModeA(modeC);
                unsigned i = modeAToIndex(modeA);
                if (modeA && (modeAC_count[i] - modeAC_lastcount[i]) >= TRACK_MODEAC_MIN_MESSAGES) {
                    a->modeC_hit = 1;
                    modeAC_match[i] = (modeAC_match[i] ? 0xFFFFFFFF : a->addr);
                }

                modeA = modeCToModeA(modeC + 1);
                i = modeAToIndex(modeA);
                if (modeA && (modeAC_count[i] - modeAC_lastcount[i]) >= TRACK_MODEAC_MIN_MESSAGES) {
                    a->modeC_hit = 1;
                    modeAC_match[i] = (modeAC_match[i] ? 0xFFFFFFFF : a->addr);
                }

                modeA = modeCToModeA(modeC - 1);
                i = modeAToIndex(modeA);
                if (modeA && (modeAC_count[i] - modeAC_lastcount[i]) >= TRACK_MODEAC_MIN_MESSAGES) {
                    a->modeC_hit = 1;
                    modeAC_match[i] = (modeAC_match[i] ? 0xFFFFFFFF : a->addr);
                }
            }
        }
    }

    // reset counts for next time
    for (unsigned i = 0; i < 4096; ++i) {
        if (!modeAC_count[i])
            continue;

        if ((modeAC_count[i] - modeAC_lastcount[i]) < TRACK_MODEAC_MIN_MESSAGES) {
            if (++modeAC_age[i] > 15) {
                // not heard from for a while, clear it out
                modeAC_lastcount[i] = modeAC_count[i] = modeAC_age[i] = 0;
            }
        } else {
            // this one is live
            // set a high initial age for matches, so they age out rapidly
            // and don't show up on the interactive display when the matching
            // mode S data goes away or changes
            if (modeAC_match[i]) {
                modeAC_age[i] = 10;
            } else {
                modeAC_age[i] = 0;
            }
        }

        modeAC_lastcount[i] = modeAC_count[i];
    }
}

/*
static void updateAircraft() {
    for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
        for (struct aircraft *a = Modes.aircraft[j]; a; a = a->next) {
        }
    }
}
*/

//
//=========================================================================
//
// If we don't receive new nessages within TRACK_AIRCRAFT_TTL
// we remove the aircraft from the list.
//

static void trackRemoveStaleAircraft(struct aircraft **freeList, uint64_t now) {

    if (now > Modes.next_stats_update)
        statsReset();

    if (Modes.api)
        apiClear();

    int full_write = checkNewDay(); // this function does more than the return value!!!!

    /*
    int stride = AIRCRAFT_BUCKETS / 32;
    int start = stride * blob;
    int end = start + stride;
    */

    for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
        struct aircraft *prev = NULL;
        struct aircraft *a = Modes.aircraft[j];
        while (a) {
            if (
                    (!a->seen_pos && (now > a->seen + TRACK_AIRCRAFT_NO_POS_TTL))
                    || ((a->addr & MODES_NON_ICAO_ADDRESS) && (now > a->seen + TRACK_AIRCRAFT_NON_ICAO_TTL))
                    || (a->seen_pos && (
                            (Modes.state_dir && now > a->seen_pos + TRACK_AIRCRAFT_TTL) ||
                            (!Modes.state_dir && now > a->seen_pos + TRACK_AIRCRAFT_NO_STATE_TTL)
                            ))
               ) {
                // Count aircraft where we saw only one message before reaping them.
                // These are likely to be due to messages with bad addresses.
                if (a->messages == 1)
                    Modes.stats_current.single_message_aircraft++;

                if (a->addr == Modes.cpr_focus)
                    fprintf(stderr, "del: %06x seen: %"PRIu64" seen_pos %"PRIu64"\n", a->addr, now - a->seen, now - a->seen_pos);

                // remove from the globeList
                set_globe_index(a, -5);

                // Remove the element from the linked list, with care
                // if we are removing the first element
                struct aircraft *del = a;
                if (!prev) {
                    Modes.aircraft[j] = a->next;
                } else {
                    prev->next = a->next;
                }
                a = a->next;

                Modes.aircraftCount--;

                del->next = *freeList;
                *freeList = del;
            } else {
                if (now < a->seen + TRACK_EXPIRE_JAERO + 1 * MINUTES)
                    updateValidities(a, now);
                if (now > Modes.next_stats_update
                    && (now < a->seen + 30 * SECONDS && a->messages >= 2))
                        statsCount(a, now);

                if (Modes.api)
                    apiAdd(a, now);

                if (Modes.keep_traces && a->trace_alloc) {

                    if (Modes.json_globe_index) {
                        if (now > a->trace_next_fw) {
                            resize_trace(a, now);
                            a->trace_write = 1;
                        }

                        if (full_write) {
                            a->trace_next_fw = now + random() % (2 * MINUTES); // spread over 2 mins
                            a->trace_full_write = 0xc0ffee;
                        }
                    } else {
                        if (now > a->trace_next_fw) {
                            resize_trace(a, now);
                            a->trace_next_fw = now + 2 * HOURS + random() % (30 * MINUTES);
                        }
                    }

                    if (a->trace_len + GLOBE_STEP / 2 >= a->trace_alloc) {
                        resize_trace(a, now);
                        //fprintf(stderr, "%06x: new trace_alloc: %d).\n", a->addr, a->trace_alloc);
                    }
                }

                prev = a;
                a = a->next;
            }
        }
    }
}


static void lockThreads() {
    for (int i = 0; i < TRACE_THREADS; i++) {
        pthread_mutex_lock(&Modes.jsonTraceThreadMutex[i]);
    }
    pthread_mutex_lock(&Modes.jsonThreadMutex);
    pthread_mutex_lock(&Modes.jsonGlobeThreadMutex);
    pthread_mutex_lock(&Modes.decodeThreadMutex);
}
static void unlockThreads() {
    pthread_mutex_unlock(&Modes.decodeThreadMutex);
    pthread_mutex_unlock(&Modes.jsonThreadMutex);
    pthread_mutex_unlock(&Modes.jsonGlobeThreadMutex);
    for (int i = 0; i < TRACE_THREADS; i++) {
        pthread_mutex_unlock(&Modes.jsonTraceThreadMutex[i]);
    }
}

//
// Entry point for periodic updates
//

void trackPeriodicUpdate() {
    // Only do updates once per second
    static uint32_t blob; // current blob
    static uint32_t counter;
    counter++; // free running counter
    int writeStats = 0;

    struct aircraft *freeList = NULL;

    // stop all threads so we can remove aircraft from the list.
    // also serves as memory barrier so json threads get new aircraft in the list
    // adding aircraft does not need to be done with locking:
    // the worst case is that the newly added aircraft is skipped as it's not yet
    // in the cache used by the json threads.
    lockThreads();

    uint64_t now = mstime();

    struct timespec start_time;
    start_cpu_timing(&start_time);

    trackRemoveStaleAircraft(&freeList, now);

    if (Modes.mode_ac)
        trackMatchAC(now);

    int nParts = 256;
    receiverTimeout((counter % nParts), nParts);

    if (now > Modes.next_stats_update)
        writeStats = statsUpdate(now); // needs to happen under lock

    int64_t elapsed = end_cpu_timing(&start_time, &Modes.stats_current.remove_stale_cpu);

    unlockThreads();

    if (elapsed > 80) {
        fprintf(stderr, "<3>High load: removeStale took %"PRIu64" ms!\n", elapsed);
    }
    //fprintf(stderr, "removeStale took %"PRIu64" ms!\n", after - before);

    start_cpu_timing(&start_time);

    cleanupAircraft(freeList);

    if (Modes.api)
        apiSort();

    if (counter % (3000 / STATE_BLOBS) == 0) {
        save_blob(blob++ % STATE_BLOBS);
    }

    if (Modes.heatmap)
        handleHeatmap(); // only does sth every 30 min

    if (writeStats) {
        statsCalc(); // calculate statistics stuff

        if (Modes.json_dir)
            writeJsonToFile(Modes.json_dir, "stats.json", generateStatsJson());

        if (Modes.prom_file)
            writeJsonToFile(NULL, Modes.prom_file, generatePromFile());
    }

    end_cpu_timing(&start_time, &Modes.stats_current.heatmap_and_state_cpu);
}

static void cleanupAircraft(struct aircraft *a) {

    struct aircraft *iter = a;
    while (iter) {
        a = iter;
        iter = iter->next;

        char filename[1024];

        unlink_trace(a);

        if (Modes.state_dir) {
            snprintf(filename, 1024, "%s/%02x/%06x", Modes.state_dir, a->addr % 256, a->addr);
            if (unlink(filename)) {
                //perror("unlink internal_state");
                //fprintf(stderr, "unlink %06x: %s\n", a->addr, filename);
            }
        }

        freeAircraft(a);
    }
}

static void globe_stuff(struct aircraft *a, struct modesMessage *mm, double new_lat, double new_lon, uint64_t now) {

    if (0 && a->addr == Modes.cpr_focus) {
        showPositionDebug(a, mm, now);
    }

    if (now < a->seen_pos + 3 * SECONDS && a->lat == new_lat && a->lon == new_lon) {
        // don't use duplicate positions for beastReduce
        mm->reduce_forward = 0;
        mm->duplicate = 1;
        mm->pos_ignore = 1;
    }

    if (mm->cpr_valid && (mm->garbage || mm->pos_bad || mm->duplicate))
        return;

    a->lastPosReceiverId = mm->receiverId;

    if (trackDataAge(now, &a->track_valid) >= 10000 && a->seen_pos) {
        double distance = greatcircle(a->lat, a->lon, new_lat, new_lon);
        if (distance > 100)
            a->calc_track = bearing(a->lat, a->lon, new_lat, new_lon);
    }


    if (Modes.keep_traces) {

        set_globe_index(a, globe_index(new_lat, new_lon));

        if (mm->source > SOURCE_JAERO &&
                (a->pos_reliable_odd < Modes.json_reliable || a->pos_reliable_even < Modes.json_reliable))
            goto no_save_state;

        if (!a->trace) {
            //pthread_mutex_lock(&a->trace_mutex);

            a->trace_alloc = GLOBE_STEP;
            a->trace = malloc(a->trace_alloc * sizeof(struct state));
            a->trace_all = malloc((1 + a->trace_alloc / 4) * sizeof(struct state_all));
            a->trace->timestamp = now;
            a->trace_full_write = 9999; // rewrite full history file

            //fprintf(stderr, "%06x: new trace\n", a->addr);

            //pthread_mutex_unlock(&a->trace_mutex);
        } else if (a->trace_len > 5) {
            for (int i = a->trace_len - 1; i >= a->trace_len - 5; i--)
                if ( (int32_t) (new_lat * 1E6) == a->trace[i].lat
                        && (int32_t) (new_lon * 1E6) == a->trace[i].lon )
                    return;
        }
        if (a->trace_len + 1 >= a->trace_alloc) {
            fprintf(stderr, "%06x: trace_len + 1 >= a->trace_alloc (%d).\n", a->addr, a->trace_len);
            goto no_save_state;
        }

        struct state *trace = a->trace;

        struct state *new = &(trace[a->trace_len]);
        int on_ground = 0;
        int was_ground = 0;
        float turn_density = 5;
        float track = a->track;
        int track_valid = trackVState(now, &a->track_valid, &a->position_valid);
        struct state *last = NULL;

        if (trackDataValid(&a->airground_valid) && a->airground_valid.source >= SOURCE_MODE_S_CHECKED && a->airground == AG_GROUND) {
            on_ground = 1;

            if (trackVState(now, &a->true_heading_valid, &a->position_valid)) {
                track = a->true_heading;
                track_valid = 1;
            } else {
                track_valid = 0;
            }
        }
        if (a->trace_len == 0 )
            goto save_state;



        last = &(trace[a->trace_len-1]);
        float track_diff = fabs(track - last->track / 10.0);
        uint64_t elapsed = now - last->timestamp;
        if (now < last->timestamp)
            elapsed = 0;

        int32_t last_alt = last->altitude * 25;

        was_ground = last->flags.on_ground;

        if (elapsed < 11 * SECONDS && mm->source <= SOURCE_JAERO
                && (a->pos_reliable_odd < Modes.json_reliable || a->pos_reliable_even < Modes.json_reliable))
            goto no_save_state;

        if (on_ground != was_ground) {
            goto save_state;
        }

        double distance = greatcircle(a->trace_llat, a->trace_llon, new_lat, new_lon);

        // record non moving targets every 10 minutes
        if (elapsed > 20 * Modes.json_trace_interval)
            goto save_state;
        if (distance < 40)
            goto no_save_state;

        if (elapsed > Modes.json_trace_interval) // default 30000 ms
            goto save_state;

        if (elapsed < 2000)
            goto no_save_state;

        // save a point if reception is spotty so we can mark track as spotty on display
        if (now > a->seen_pos + 20 * 1000)
            goto save_state;

        if (on_ground) {
            if (distance * track_diff > 200)
                goto save_state;

            if (distance > 400)
                goto save_state;
        }

        if (trackVState(now, &a->altitude_baro_valid, &a->position_valid)
                && a->alt_reliable >= ALTITUDE_BARO_RELIABLE_MAX / 5) {
            if (!last->flags.altitude_valid) {
                goto save_state;
            }
            if (last->flags.altitude_valid) {

                if (a->altitude_baro > 8000 && abs((a->altitude_baro + 250)/500 - (last_alt + 250)/500) >= 1) {
                    //fprintf(stderr, "1");
                    goto save_state;
                }

                {
                    int offset = 125;
                    int div = 250;
                    int alt_add = (a->altitude_baro >= 0) ? offset : (-1 * offset);
                    int last_alt_add = (last_alt >= 0) ? offset : (-1 * offset);
                    if (a->altitude_baro <= 8000 && a->altitude_baro > 4000
                            && abs((a->altitude_baro + alt_add)/div - (last_alt + last_alt_add)/div) >= 1) {
                        //fprintf(stderr, "2");
                        goto save_state;
                    }
                }

                {
                    int offset = 62;
                    int div = 125;
                    int alt_add = (a->altitude_baro >= 0) ? offset : (-1 * offset);
                    int last_alt_add = (last_alt >= 0) ? offset : (-1 * offset);
                    if (a->altitude_baro <= 4000
                            && abs((a->altitude_baro + alt_add)/div - (last_alt + last_alt_add)/div) >= 1) {
                        //fprintf(stderr, "3");
                        goto save_state;
                    }
                }

                if (abs(a->altitude_baro - last_alt) >= 100 && now > last->timestamp + ((1000 * 12000)  / abs(a->altitude_baro - last_alt))) {
                    //fprintf(stderr, "4");
                    //fprintf(stderr, "%06x %d %d\n", a->addr, abs(a->altitude_baro - last_alt), ((1000 * 12000)  / abs(a->altitude_baro - last_alt)));
                    goto save_state;
                }
            }
        }

        if (last->flags.track_valid && track_valid) {
            if (track_diff > 0.5
                    && (elapsed > (uint64_t) (100.0 * 1000.0 / turn_density / track_diff))
               ) {
                //fprintf(stderr, "t");
                goto save_state;
            }
        }

        if (trackDataValid(&a->gs_valid) && last->flags.gs_valid && fabs(last->gs / 10.0 - a->gs) > 8) {
            //fprintf(stderr, "s\n");
            //fprintf(stderr, "%06x %0.1f %0.1f\n", a->addr, fabs(last->gs / 10.0 - a->gs), a->gs);
            goto save_state;
        }

        goto no_save_state;
save_state:

        mm->jsonPos = 1;

        memset(new, 0, sizeof(struct state));

        new->lat = (int32_t) nearbyint(new_lat * 1E6);
        new->lon = (int32_t) nearbyint(new_lon * 1E6);
        new->timestamp = now;


        /*
           unsigned on_ground:1;
           unsigned stale:1;
           unsigned leg_marker:1;
           unsigned altitude_valid:1;
           unsigned gs_valid:1;
           unsigned track_valid:1;
           unsigned rate_valid:1;
           unsigned rate_geom:1;
        */



        if (now > a->seen_pos + 15 * 1000 || (last && now > last->timestamp + 400 * 1000))
            new->flags.stale = 1;

        if (on_ground)
            new->flags.on_ground = 1;

        if (trackVState(now, &a->altitude_baro_valid, &a->position_valid)
                && a->alt_reliable >= ALTITUDE_BARO_RELIABLE_MAX / 5) {
            new->flags.altitude_valid = 1;
            new->altitude = (int16_t) nearbyint(a->altitude_baro / 25.0);
        } else if (trackVState(now, &a->altitude_geom_valid, &a->position_valid)) {
            new->flags.altitude_valid = 1;
            new->flags.altitude_geom = 1;
            new->altitude = (int16_t) nearbyint(a->altitude_geom / 25.0);
        }
        if (trackVState(now, &a->gs_valid, &a->position_valid)) {
            new->flags.gs_valid = 1;
            new->gs = (int16_t) nearbyint(10 * a->gs);
        }

        if (trackVState(now, &a->geom_rate_valid, &a->position_valid)) {
            new->flags.rate_valid = 1;
            new->flags.rate_geom = 1;
            new->rate = (int16_t) nearbyint(a->geom_rate / 32.0);
        } else if (trackVState(now, &a->baro_rate_valid, &a->position_valid)) {
            new->flags.rate_valid = 1;
            new->flags.rate_geom = 0;
            new->rate = (int16_t) nearbyint(a->baro_rate / 32.0);
        } else {
            new->rate = 0;
            new->flags.rate_valid = 0;
        }

        if (track_valid) {
            new->track = (int16_t) nearbyint(10 * track);
            new->flags.track_valid = 1;
        }
        // trace_all stuff:

        if (a->trace_len % 4 == 0) {
            struct state_all *new_all = &(a->trace_all[a->trace_len/4]);
            memset(new_all, 0, sizeof(struct state_all));

            to_state_all(a, new_all, now);
        }

        // bookkeeping:
        a->trace_llat = new_lat;
        a->trace_llon = new_lon;

        //pthread_mutex_lock(&a->trace_mutex);
        (a->trace_len)++;
        a->trace_write = 1;
        a->trace_full_write++;
        //pthread_mutex_unlock(&a->trace_mutex);

        //fprintf(stderr, "Added to trace for %06x (%d).\n", a->addr, a->trace_len);

no_save_state:
        ;
    }

}

/*
static void adjustExpire(struct aircraft *a, uint64_t timeout) {
#define F(f,s,e) do { a->f##_valid.stale_interval = (s) * 1000; a->f##_valid.expire_interval = (e) * 1000; } while (0)
    F(callsign, 60,  timeout); // ADS-B or Comm-B
    F(altitude_baro, 15,  timeout); // ADS-B or Mode S
    F(altitude_geom, 30, timeout); // ADS-B only
    F(geom_delta, 30, timeout); // ADS-B only
    F(gs, 30,  timeout); // ADS-B or Comm-B
    F(ias, 30, timeout); // ADS-B (rare) or Comm-B
    F(tas, 30, timeout); // ADS-B (rare) or Comm-B
    F(mach, 30, timeout); // Comm-B only
    F(track, 30,  timeout); // ADS-B or Comm-B
    F(track_rate, 30, timeout); // Comm-B only
    F(roll, 30, timeout); // Comm-B only
    F(mag_heading, 30, timeout); // ADS-B (rare) or Comm-B
    F(true_heading, 30, timeout); // ADS-B only (rare)
    F(baro_rate, 30, timeout); // ADS-B or Comm-B
    F(geom_rate, 30, timeout); // ADS-B or Comm-B
    F(squawk, 15, timeout); // ADS-B or Mode S
    F(airground, 15, timeout); // ADS-B or Mode S
    F(nav_qnh, 30, timeout); // Comm-B only
    F(nav_altitude_mcp, 30, timeout);  // ADS-B or Comm-B
    F(nav_altitude_fms, 30, timeout);  // ADS-B or Comm-B
    F(nav_altitude_src, 30, timeout); // ADS-B or Comm-B
    F(nav_heading, 30, timeout); // ADS-B or Comm-B
    F(nav_modes, 30, timeout); // ADS-B or Comm-B
    F(cpr_odd, 10, timeout); // ADS-B only
    F(cpr_even, 10, timeout); // ADS-B only
    F(position, 10,  timeout); // ADS-B only
    F(nic_a, 30, timeout); // ADS-B only
    F(nic_c, 30, timeout); // ADS-B only
    F(nic_baro, 30, timeout); // ADS-B only
    F(nac_p, 30, timeout); // ADS-B only
    F(nac_v, 30, timeout); // ADS-B only
    F(sil, 30, timeout); // ADS-B only
    F(gva, 30, timeout); // ADS-B only
    F(sda, 30, timeout); // ADS-B only
#undef F
}
*/

static void position_bad(struct modesMessage *mm, struct aircraft *a) {
    if (mm->garbage)
        return;
    if (mm->pos_ignore)
        return;
    if (mm->source < a->position_valid.source)
        return;


    Modes.stats_current.cpr_global_bad++;


    if (a->addr == Modes.cpr_focus)
        fprintf(stderr, "%06x: position_bad\n", a->addr);

    a->pos_reliable_odd--;
    a->pos_reliable_even--;

    if (a->pos_reliable_odd <= 0 || a->pos_reliable_even <=0) {
        a->position_valid.source = SOURCE_INVALID;
        a->pos_reliable_odd = 0;
        a->pos_reliable_even = 0;
        a->cpr_odd_valid.source = SOURCE_INVALID;
        a->cpr_even_valid.source = SOURCE_INVALID;
    }
}

static void resize_trace(struct aircraft *a, uint64_t now) {

    if (a->trace_alloc == 0) {
        return;
    }

    if (a->trace_len == 0) {
        //pthread_mutex_lock(&a->trace_mutex);

        free(a->trace);
        free(a->trace_all);

        a->trace_alloc = 0;
        a->trace = NULL;
        a->trace_all = NULL;

        unlink_trace(a);
        // if the trace length is zero, the trace is deleted from run
        // it is not written again until a new position is received

        //pthread_mutex_unlock(&a->trace_mutex);
        return;
    }
    if (now < Modes.keep_traces)
        fprintf(stderr, "now < Modes.keep_traces: %"PRIu64" %"PRIu32"\n", now, Modes.keep_traces);

    uint64_t keep_after = now - Modes.keep_traces;

    if (a->addr & MODES_NON_ICAO_ADDRESS)
        keep_after = now - TRACK_AIRCRAFT_NON_ICAO_TTL;

    if (a->trace_len == GLOBE_TRACE_SIZE || a->trace->timestamp < keep_after - 20 * MINUTES ) {
        int new_start = a->trace_len;

        if (a->trace_len + GLOBE_STEP / 2 >= GLOBE_TRACE_SIZE) {
            new_start = GLOBE_TRACE_SIZE / 64;
        } else {
            int found = 0;
            for (int i = 0; i < a->trace_len; i++) {
                struct state *state = &a->trace[i];
                if (state->timestamp > keep_after) {
                    new_start = i;
                    found = 1;
                    break;
                }
            }
            if (!found)
                new_start = a->trace_len;
        }

        if (new_start != a->trace_len) {
            new_start -= (new_start % 4);

            if (new_start % 4 != 0)
                fprintf(stderr, "not divisible by 4: %d %d\n", new_start, a->trace_len);
        }

        //pthread_mutex_lock(&a->trace_mutex);

        a->trace_len -= new_start;

        memmove(a->trace, a->trace + new_start, a->trace_len * sizeof(struct state));
        memmove(a->trace_all, a->trace_all + new_start / 4, a->trace_len / 4 * sizeof(struct state_all));

        //a->trace_write = 1;
        //a->trace_full_write = 9999; // rewrite full history file

        //pthread_mutex_unlock(&a->trace_mutex);
    }

    if (a->trace_len && a->trace_len + GLOBE_STEP / 2 >= a->trace_alloc) {
        //pthread_mutex_lock(&a->trace_mutex);
        a->trace_alloc = a->trace_alloc * 5 / 4;
        if (a->trace_alloc > GLOBE_TRACE_SIZE)
            a->trace_alloc = GLOBE_TRACE_SIZE;
        a->trace = realloc(a->trace, a->trace_alloc * sizeof(struct state));
        a->trace_all = realloc(a->trace_all, (1 + a->trace_alloc / 4) * sizeof(struct state_all));
        //pthread_mutex_unlock(&a->trace_mutex);

        if (a->trace_len >= GLOBE_TRACE_SIZE / 2)
            fprintf(stderr, "Quite a long trace: %06x (%d).\n", a->addr, a->trace_len);

        if (a->trace_alloc > GLOBE_TRACE_SIZE)
            fprintf(stderr, "GLOBE_TRACE_SIZE EXCEEDED!: %06x (%d).\n", a->addr, a->trace_len);
    }

    if (a->trace_len < (a->trace_alloc * 7 / 10) && a->trace_alloc >= 2 * GLOBE_STEP) {
        //pthread_mutex_lock(&a->trace_mutex);
        a->trace_alloc = a->trace_alloc * 4 / 5;
        a->trace = realloc(a->trace, a->trace_alloc * sizeof(struct state));
        a->trace_all = realloc(a->trace_all, (1 + a->trace_alloc / 4) * sizeof(struct state_all));
        //pthread_mutex_unlock(&a->trace_mutex);
    }
}

void to_state_all(struct aircraft *a, struct state_all *new, uint64_t now) {
            for (int i = 0; i < 8; i++)
                new->callsign[i] = a->callsign[i];

            new->pos_nic = a->pos_nic;
            new->pos_rc = a->pos_rc;

            new->altitude_geom = (int16_t) nearbyint(a->altitude_geom / 25.0);
            new->baro_rate = (int16_t) nearbyint(a->baro_rate / 8.0);
            new->geom_rate = (int16_t) nearbyint(a->geom_rate / 8.0);
            new->ias = a->ias;
            new->tas = a->tas;

            new->squawk = a->squawk;
            new->category = a->category; // Aircraft category A0 - D7 encoded as a single hex byte. 00 = unset
            new->nav_altitude_mcp = (uint16_t) nearbyint(a->nav_altitude_mcp / 4.0);
            new->nav_altitude_fms = (uint16_t) nearbyint(a->nav_altitude_fms / 4.0);

            new->nav_qnh = (int16_t) nearbyint(a->nav_qnh * 10.0);
            new->gs = (int16_t) nearbyint(a->gs * 10.0);
            new->mach = (int16_t) nearbyint(a->mach * 1000.0);

            new->track_rate = (int16_t) nearbyint(a->track_rate * 100.0);
            new->roll = (int16_t) nearbyint(a->roll * 100.0);

            new->track = (int16_t) nearbyint(a->track * 90.0);
            new->mag_heading = (int16_t) nearbyint(a->mag_heading * 90.0);
            new->true_heading = (int16_t) nearbyint(a->true_heading * 90.0);
            new->nav_heading = (int16_t) nearbyint(a->nav_heading * 90.0);

            new->emergency = a->emergency;
            new->airground = a->airground;
            new->addrtype = a->addrtype;
            new->nav_modes = a->nav_modes;
            new->nav_altitude_src = a->nav_altitude_src;
            new->sil_type = a->sil_type;

            if (now < a->wind_updated + TRACK_EXPIRE && abs(a->wind_altitude - a->altitude_baro) < 500) {
                new->wind_direction = (int) nearbyint(a->wind_direction);
                new->wind_speed = (int) nearbyint(a->wind_speed);
                new->wind_valid = 1;
            }
            if (now < a->oat_updated + TRACK_EXPIRE) {
                new->oat = (int) nearbyint(a->oat);
                new->tat = (int) nearbyint(a->tat);
                new->temp_valid = 1;
            }

            if (a->adsb_version < 0)
                new->adsb_version = 15;
            else
                new->adsb_version = a->adsb_version;

            if (a->adsr_version < 0)
                new->adsr_version = 15;
            else
                new->adsr_version = a->adsr_version;

            if (a->tisb_version < 0)
                new->tisb_version = 15;
            else
                new->tisb_version = a->tisb_version;

            new->nic_a = a->nic_a;
            new->nic_c = a->nic_c;
            new->nic_baro = a->nic_baro;
            new->nac_p = a->nac_p;
            new->nac_v = a->nac_v;
            new->sil = a->sil;
            new->gva = a->gva;
            new->sda = a->sda;
            new->alert = a->alert;
            new->spi = a->spi;

#define F(f) do { new->f = trackVState(now, &a->f, &a->position_valid); } while (0)
           F(callsign_valid);
           F(altitude_baro_valid);
           F(altitude_geom_valid);
           F(geom_delta_valid);
           F(gs_valid);
           F(ias_valid);
           F(tas_valid);
           F(mach_valid);
           F(track_valid);
           F(track_rate_valid);
           F(roll_valid);
           F(mag_heading_valid);
           F(true_heading_valid);
           F(baro_rate_valid);
           F(geom_rate_valid);
           F(nic_a_valid);
           F(nic_c_valid);
           F(nic_baro_valid);
           F(nac_p_valid);
           F(nac_v_valid);
           F(sil_valid);
           F(gva_valid);
           F(sda_valid);
           F(squawk_valid);
           F(emergency_valid);
           F(airground_valid);
           F(nav_qnh_valid);
           F(nav_altitude_mcp_valid);
           F(nav_altitude_fms_valid);
           F(nav_altitude_src_valid);
           F(nav_heading_valid);
           F(nav_modes_valid);
           F(position_valid);
           F(alert_valid);
           F(spi_valid);
#undef F
}
static void calc_wind(struct aircraft *a, uint64_t now) {
    uint32_t focus = 0xc0ffeeba;

    if (a->addr == focus)
        fprintf(stderr, "%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64"\n", trackDataAge(now, &a->tas_valid), trackDataAge(now, &a->true_heading_valid),
                trackDataAge(now, &a->gs_valid), trackDataAge(now, &a->track_valid));

    if (!trackDataValid(&a->position_valid) || a->airground == AG_GROUND)
        return;

    if (trackDataAge(now, &a->tas_valid) > TRACK_WT_TIMEOUT
            || trackDataAge(now, &a->gs_valid) > TRACK_WT_TIMEOUT
            || trackDataAge(now, &a->track_valid) > TRACK_WT_TIMEOUT / 2
            || trackDataAge(now, &a->true_heading_valid) > TRACK_WT_TIMEOUT / 2
       ) {
        return;
    }

    // don't use this code for now
    /*
    if (a->trace && a->trace_len >= 2) {
        struct state *last = &(a->trace[a->trace_len-1]);
        if (now + 1500 < last->timestamp)
            last = &(a->trace[a->trace_len-2]);
        float track_diff = fabs(a->track - last->track / 10.0);
        if (last->flags.track_valid && track_diff > 0.5)
            return;
    }
    */

    double trk = (M_PI / 180) * a->track;
    double hdg = (M_PI / 180) * a->true_heading;
    double tas = a->tas;
    double gs = a->gs;
    double crab = norm_diff(hdg - trk, M_PI);

    double hw = tas - cos(crab) * gs;
    double cw = sin(crab) * gs;
    double ws = sqrt(hw * hw + cw * cw);
    double wd = hdg + atan2(cw, hw);

    wd = norm_angle(wd, M_PI);

    wd *= (180 / M_PI);
    crab *= (180 / M_PI);

    //if (a->addr == focus)
    //fprintf(stderr, "%06x: %.1f %.1f %.1f %.1f %.1f\n", a->addr, ws, wd, gs, tas, crab);
    if (ws > 250) {
        // Filter out wildly unrealistic wind speeds
        return;
    }
    a->wind_speed = ws;
    a->wind_direction = wd;
    a->wind_updated = now;
    a->wind_altitude = a->altitude_baro;
}
static void calc_temp(struct aircraft *a, uint64_t now) {
    if (a->airground == AG_GROUND)
        return;
    if (trackDataAge(now, &a->tas_valid) > TRACK_WT_TIMEOUT || trackDataAge(now, &a->mach_valid) > TRACK_WT_TIMEOUT)
        return;

    if (a->mach < 0.395)
        return;

    double fraction = a->tas / 661.47 / a->mach;
    double oat = (fraction * fraction * 288.15) - 273.15;
    double tat = -273.15 + ((oat + 273.15) * (1 + 0.2 * a->mach * a->mach));

    a->oat = oat;
    a->tat = tat;
    a->oat_updated = now;
}

static inline int declination (struct aircraft *a, double *dec) {
    double year;
    time_t now_t = a->seen/1000;

    struct tm utc;
    gmtime_r(&now_t, &utc);

    year = 1900.0 + utc.tm_year + utc.tm_yday / 365.0;

    double dip;
    double ti;
    double gv;

    int res = geomag_calc(a->altitude_baro * 0.0003048, a->lat, a->lon, year, dec, &dip, &ti, &gv);
    if (res)
        *dec = 0.0;
    return res;
}

void from_state_all(struct state_all *in, struct aircraft *a , uint64_t ts) {
            for (int i = 0; i < 8; i++)
                a->callsign[i] = in->callsign[i];
            a->callsign[8] = '\0';

            a->pos_nic = in->pos_nic;
            a->pos_rc = in->pos_rc;

            a->altitude_geom = in->altitude_geom * 25;
            a->baro_rate = in->baro_rate * 8;
            a->geom_rate = in->geom_rate * 8;
            a->ias = in->ias;
            a->tas = in->tas;

            a->squawk = in->squawk;
            a->category =  in->category; // Aircraft category A0 - D7 encoded as a single hex byte. 00 = unset
            a->nav_altitude_mcp = in->nav_altitude_mcp * 4;
            a->nav_altitude_fms = in->nav_altitude_fms * 4;

            a->nav_qnh = in->nav_qnh / 10.0;
            a->gs = in->gs / 10.0;
            a->mach = in->mach / 1000.0;

            a->track_rate = in->track_rate / 100.0;
            a->roll = in->roll / 100.0;

            a->track = in->track / 90.0;
            a->mag_heading = in->mag_heading / 90.0;
            a->true_heading = in->true_heading / 90.0;
            a->nav_heading = in->nav_heading / 90.0;

            a->emergency = in->emergency;
            a->airground = in->airground;
            a->addrtype = in->addrtype;
            a->nav_modes = in->nav_modes;
            a->nav_altitude_src = in->nav_altitude_src;
            a->sil_type = in->sil_type;

            if (in->wind_valid) {
                a->wind_direction = in->wind_direction;
                a->wind_speed = in->wind_speed;
                a->wind_updated = ts - 5000;
                a->wind_altitude = a->altitude_baro;
            }
            if (in->temp_valid) {
                a->oat = in->oat;
                a->tat = in->tat;
                a->oat_updated = ts - 5000;
            }

            if (in->adsb_version == 15)
                a->adsb_version = -1;
            else
                a->adsb_version = in->adsb_version;

            if (in->adsr_version == 15)
                a->adsr_version = -1;
            else
                a->adsr_version = in->adsr_version;

            if (in->tisb_version == 15)
                a->tisb_version = -1;
            else
                a->tisb_version = in->tisb_version;

            a->nic_a = in->nic_a;
            a->nic_c = in->nic_c;
            a->nic_baro = in->nic_baro;
            a->nac_p = in->nac_p;
            a->nac_v = in->nac_v;
            a->sil = in->sil;
            a->gva = in->gva;
            a->sda = in->sda;
            a->alert = in->alert;
            a->spi = in->spi;


            // giving this a timestamp is kinda hacky, do it anyway
            // we want to be able to reuse the sprintAircraft routine for printing aircraft details
#define F(f) do { a->f.source = (in->f ? SOURCE_INDIRECT : SOURCE_INVALID); a->f.updated = ts - 5000; } while (0)
           F(callsign_valid);
           F(altitude_baro_valid);
           F(altitude_geom_valid);
           F(geom_delta_valid);
           F(gs_valid);
           F(ias_valid);
           F(tas_valid);
           F(mach_valid);
           F(track_valid);
           F(track_rate_valid);
           F(roll_valid);
           F(mag_heading_valid);
           F(true_heading_valid);
           F(baro_rate_valid);
           F(geom_rate_valid);
           F(nic_a_valid);
           F(nic_c_valid);
           F(nic_baro_valid);
           F(nac_p_valid);
           F(nac_v_valid);
           F(sil_valid);
           F(gva_valid);
           F(sda_valid);
           F(squawk_valid);
           F(emergency_valid);
           F(airground_valid);
           F(nav_qnh_valid);
           F(nav_altitude_mcp_valid);
           F(nav_altitude_fms_valid);
           F(nav_altitude_src_valid);
           F(nav_heading_valid);
           F(nav_modes_valid);
           F(position_valid);
           F(alert_valid);
           F(spi_valid);
#undef F
}

static const char *source_string(datasource_t source) {
    switch (source) {
        case SOURCE_INVALID:
            return "INVALID";
        case SOURCE_INDIRECT:
            return "INDIRECT";
        case SOURCE_MODE_AC:
            return "MODE_AC";
        case SOURCE_SBS:
            return "SBS";
        case SOURCE_MLAT:
            return "MLAT";
        case SOURCE_MODE_S:
            return "MODE_S";
        case SOURCE_JAERO:
            return "JAERO";
        case SOURCE_MODE_S_CHECKED:
            return "MODE_CH";
        case SOURCE_TISB:
            return "TISB";
        case SOURCE_ADSR:
            return "ADSR";
        case SOURCE_ADSB:
            return "ADSB";
        case SOURCE_PRIO:
            return "PRIO";
        default:
            return "UNKN";
    }
}

void freeAircraft(struct aircraft *a) {
        pthread_mutex_unlock(&a->trace_mutex);
        pthread_mutex_destroy(&a->trace_mutex);

        if (a->first_message)
            free(a->first_message);
        if (a->trace) {
            free(a->trace);
            free(a->trace_all);
        }
        free(a);
}
void updateValidities(struct aircraft *a, uint64_t now) {
    if (a->globe_index >= 0 && now > a->seen_pos + 30 * MINUTES) {
        set_globe_index(a, -5);
    }

    updateValidity(&a->callsign_valid, now, TRACK_EXPIRE_LONG);
    updateValidity(&a->altitude_baro_valid, now, TRACK_EXPIRE);
    updateValidity(&a->altitude_geom_valid, now, TRACK_EXPIRE);
    updateValidity(&a->geom_delta_valid, now, TRACK_EXPIRE);
    updateValidity(&a->gs_valid, now, TRACK_EXPIRE);
    updateValidity(&a->ias_valid, now, TRACK_EXPIRE);
    updateValidity(&a->tas_valid, now, TRACK_EXPIRE);
    updateValidity(&a->mach_valid, now, TRACK_EXPIRE);
    updateValidity(&a->track_valid, now, TRACK_EXPIRE);
    updateValidity(&a->track_rate_valid, now, TRACK_EXPIRE);
    updateValidity(&a->roll_valid, now, TRACK_EXPIRE);
    updateValidity(&a->mag_heading_valid, now, TRACK_EXPIRE);
    updateValidity(&a->true_heading_valid, now, TRACK_EXPIRE);
    updateValidity(&a->baro_rate_valid, now, TRACK_EXPIRE);
    updateValidity(&a->geom_rate_valid, now, TRACK_EXPIRE);
    updateValidity(&a->squawk_valid, now, TRACK_EXPIRE_LONG);
    updateValidity(&a->airground_valid, now, TRACK_EXPIRE + 30 * SECONDS);
    updateValidity(&a->nav_qnh_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nav_altitude_mcp_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nav_altitude_fms_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nav_altitude_src_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nav_heading_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nav_modes_valid, now, TRACK_EXPIRE);
    updateValidity(&a->cpr_odd_valid, now, TRACK_EXPIRE + 30 * SECONDS);
    updateValidity(&a->cpr_even_valid, now, TRACK_EXPIRE + 30 * SECONDS);
    updateValidity(&a->position_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nic_a_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nic_c_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nic_baro_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nac_p_valid, now, TRACK_EXPIRE);
    updateValidity(&a->sil_valid, now, TRACK_EXPIRE);
    updateValidity(&a->gva_valid, now, TRACK_EXPIRE);
    updateValidity(&a->sda_valid, now, TRACK_EXPIRE);

    // reset position reliability when no position was received for 120 seconds
    if (trackDataAge(now, &a->position_valid) > 120 * 1000) {
        a->pos_reliable_odd = 0;
        a->pos_reliable_even = 0;
    }

    if (a->altitude_baro_valid.source == SOURCE_INVALID)
        a->alt_reliable = 0;
}

static void showPositionDebug(struct aircraft *a, struct modesMessage *mm, uint64_t now) {

    fprintf(stderr, "%06x: ", a->addr);
    fprintf(stderr, "elapsed: %0.1f ", (now - a->seen_pos) / 1000.0);

    if (mm->sbs_in) {
        fprintf(stderr, "SBS, ");
        if (mm->source == SOURCE_JAERO)
            fprintf(stderr, "JAERO, ");
        if (mm->source == SOURCE_MLAT)
            fprintf(stderr, "MLAT, ");
    } else {
        fprintf(stderr, "%s%s",
                (mm->cpr_type == CPR_SURFACE) ? "surf, " : "air,  ",
                mm->cpr_odd ? "odd,  " : "even, ");
    }

    if (mm->sbs_in) {
        fprintf(stderr,
                "lat: %.6f,"
                "lon: %.6f",
                mm->decoded_lat,
                mm->decoded_lon);
    } else if (mm->cpr_decoded) {
        fprintf(stderr,"lat: %.6f (%u),"
                " lon: %.6f (%u),"
                " relative: %d,"
                " NIC: %u,"
                " Rc: %.3f km",
                mm->decoded_lat,
                mm->cpr_lat,
                mm->decoded_lon,
                mm->cpr_lon,
                mm->cpr_relative,
                mm->decoded_nic,
                mm->decoded_rc / 1000.0);
    } else {
        fprintf(stderr,"lat: (%u),"
                " lon: (%u),"
                " CPR decoding: none",
                mm->cpr_lat,
                mm->cpr_lon);
    }
    fprintf(stderr, "\n");
}
