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

uint32_t modeAC_count[4096];
uint32_t modeAC_lastcount[4096];
uint32_t modeAC_match[4096];
uint32_t modeAC_age[4096];

static void showPositionDebug(struct aircraft *a, struct modesMessage *mm, int64_t now, double bad_lat, double bad_lon);
static void position_bad(struct modesMessage *mm, struct aircraft *a);
static void calc_wind(struct aircraft *a, int64_t now);
static void calc_temp(struct aircraft *a, int64_t now);
static inline int declination(struct aircraft *a, double *dec, int64_t now);
static const char *source_string(datasource_t source);
static void incrementReliable(struct aircraft *a, struct modesMessage *mm, int64_t now, int odd);

static uint16_t simpleHash(uint64_t receiverId) {
    uint16_t simpleHash = receiverId;
    simpleHash ^= (uint16_t) (receiverId >> 16);
    simpleHash ^= (uint16_t) (receiverId >> 32);
    simpleHash ^= (uint16_t) (receiverId >> 48);
    if (simpleHash == 0)
        return 1;

    return simpleHash;
}

static float knots_to_meterpersecond = (1852.0 / 3600.0);

static void calculateMessageRate(struct aircraft *a, int64_t now) {
    float sum = 0.0f;
    float mult = REMOVE_STALE_INTERVAL / 1000.0f;
    float multSum = 0.0f;
    for (int k = 0; k < MESSAGE_RATE_CALC_POINTS; k++) {
        sum += a->messageRateAcc[k] * mult;
        multSum += mult;
        mult *= 0.7f;
    }

    a->messageRate = sum / multSum * Modes.messageRateMult;
    a->nextMessageRateCalc = now + REMOVE_STALE_INTERVAL;

    memmove(&a->messageRateAcc[1], &a->messageRateAcc[0], sizeof(uint16_t) * (MESSAGE_RATE_CALC_POINTS - 1));
    a->messageRateAcc[0] = 0;
}


// Should we accept some new data from the given source?
// If so, update the validity and return 1

static int32_t currentReduceInterval(int64_t now) {
    return Modes.net_output_beast_reduce_interval * (1 + (Modes.doubleBeastReduceIntervalUntil > now));
}

static inline int will_accept_data(data_validity *d, datasource_t source, struct modesMessage *mm, struct aircraft *a) {
    int64_t now = mm->sysTimestamp;
    if (source == SOURCE_INVALID) {
        return 0;
    }

    if (now < d->updated) {
        return 0;
    }

    if (source < d->source && now < d->updated + TRACK_STALE) {
        return 0;
    }

    // this is a position and will be wholly reverted if it's not accepted
    //int is_pos = (mm->sbs_pos_valid || mm->cpr_valid || d == &a->pos_reliable_valid || d == &a->position_valid || d == &a->cpr_odd_valid || d == &a->cpr_even_valid || d == &a->mlat_pos_valid);
    int is_pos = (mm->sbs_pos_valid || mm->cpr_valid);

    // if we have a jaero position, don't allow non-position data to have a lesser source
    if (!is_pos && a->pos_reliable_valid.source == SOURCE_JAERO && source < SOURCE_JAERO) {
        return 0;
    }

    // prevent JAERO from disrupting other data sources too quickly
    if (source == SOURCE_JAERO && a->pos_reliable_valid.last_source != SOURCE_JAERO && now < d->updated + 7 * MINUTES) {
        return 0;
    }

    // if we have recent data and a recent position, only accept data from the last couple receivers that contributed a position
    // this hopefully reduces data jitter introduced by differing receiver latencies
    // it's important to excluded data coming in alongside positions, that extra data if accepted is discarded via the scratch mechanism

    if (Modes.netReceiverId && !is_pos && a->pos_reliable_valid.source >= SOURCE_TISB && now - d->updated < 5 * SECONDS && now - a->seenPosReliable < 2200 * MS) {
        uint16_t hash = simpleHash(mm->receiverId);
        uint32_t found = 0;
        for (int i = 0; i < RECEIVERIDBUFFER; i++) {
            found += (a->receiverIds[i] == hash);
        }
        if (!found) {
            return 0;
        }
    }

    if (0 && is_pos && a->addr == Modes.cpr_focus) {
        //fprintf(stderr, "%d %p %p\n", mm->duplicate, d, &a->pos_reliable_valid);
        fprintf(stderr, "%d %s", mm->duplicate, source_string(mm->source));
    }

    return 1;
}

static int accept_data(data_validity *d, datasource_t source, struct modesMessage *mm, struct aircraft *a, int reduce_often) {

    if (!will_accept_data(d, source, mm, a)) {
        return 0;
    }

    int64_t now = mm->sysTimestamp;

    d->source = source;
    if (unlikely(source == SOURCE_PRIO)) {
        d->source = SOURCE_ADSB;
    }

    d->last_source = d->source;

    d->updated = now;
    d->stale = 0;

    if (now > d->next_reduce_forward || mm->reduce_forward) {
        int32_t reduceInterval = currentReduceInterval(now);

        if (reduce_often == REDUCE_OFTEN) {
            reduceInterval = reduceInterval * 3 / 4;
        } else if (reduce_often == REDUCE_RARE) {
            reduceInterval = reduceInterval * 4;
        }

        if (mm->cpr_valid && reduceInterval > 7000) {
            // make sure global CPR stays possible even at high interval:
            reduceInterval = 7000;
        }

        d->next_reduce_forward = now + reduceInterval;

        mm->reduce_forward = 1;
    }
    return 1;
}


// Given two datasources, produce a third datasource for data combined from them.

static void combine_validity(data_validity *to, const data_validity *from1, const data_validity *from2, int64_t now) {
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
    else if (lhs->updated >= rhs->updated)
        return 1;
    else if (lhs->updated < rhs->updated)
        return -1;
    else
        return 0;
}

static void update_range_histogram(struct aircraft *a, int64_t now) {

    double lat = a->lat;
    double lon = a->lon;

    double range = a->receiver_distance;

    int rangeDirDirection = ((int) nearbyint(a->receiver_direction)) % RANGEDIRS_BUCKETS;

    int rangeDirIval = (now * (RANGEDIRS_IVALS - 1) / Modes.range_outline_duration) % RANGEDIRS_IVALS;
    if (rangeDirIval != Modes.lastRangeDirHour) {
        //log_with_timestamp("rangeDirIval: %d", rangeDirIval);
        // when the current interval changes, reset the data for it
        memset(Modes.rangeDirs[rangeDirIval], 0, sizeof(Modes.rangeDirs[rangeDirIval]));
        Modes.lastRangeDirHour = rangeDirIval;
    }

    struct distCoords *current = &(Modes.rangeDirs[rangeDirIval][rangeDirDirection]);


    // if the position isn't proper reliable, only allow it if the range in that direction is increased by less than 25 nmi compared to the maximum of the last 24h
    if (range > current->distance && (a->pos_reliable_odd < 2 || a->pos_reliable_even < 2)) {
        float directionMax = 0;
        for (int i = 0; i < RANGEDIRS_IVALS; i++) {
            if (Modes.rangeDirs[i][rangeDirDirection].distance > directionMax)
                directionMax = Modes.rangeDirs[i][rangeDirDirection].distance;
        }
        directionMax += 50.0f * 1852.0f; // allow 50 nmi more than recorded for that direction in the last 24h
        if (range > directionMax && !Modes.debug_bogus && Modes.json_reliable > 0) {
            return;
        }
        //fprintf(stderr, "actual %.1f max %.1f\n", range / 1852.0f, (directionMax / 1852.0f));
    }

    if (range > current->distance) {
        current->distance = range;
        current->lat = lat;
        current->lon = lon;
        if (trackDataValid(&a->baro_alt_valid) || !trackDataValid(&a->geom_alt_valid)) {
            current->alt = a->baro_alt;
        } else {
            current->alt = a->geom_alt;
        }
    }

    if (range > Modes.stats_current.distance_max)
        Modes.stats_current.distance_max = range;
    if (range < Modes.stats_current.distance_min)
        Modes.stats_current.distance_min = range;

    int bucket = round(range / Modes.maxRange * RANGE_BUCKET_COUNT);

    if (bucket < 0)
        bucket = 0;
    else if (bucket >= RANGE_BUCKET_COUNT)
        bucket = RANGE_BUCKET_COUNT - 1;

    ++Modes.stats_current.range_histogram[bucket];
}

static int cpr_duplicate_check(int64_t now, struct aircraft *a, struct modesMessage *mm) {

    struct cpr_cache *cpr;
    uint32_t inCache = 0;
    uint32_t cpr_lat = mm->cpr_lat;
    uint32_t cpr_lon = mm->cpr_lon;
    uint64_t receiverId = mm->receiverId;
    for (int i = 0; i < CPR_CACHE; i++) {
        cpr = &a->cpr_cache[i];
        if (
                (
                 now - cpr->ts < 2 * SECONDS
                 && cpr->cpr_lat == cpr_lat
                 && cpr->cpr_lon == cpr_lon
                 && cpr->receiverId != receiverId
                )
           ) {
            inCache += 1;
        }
    }
    if (inCache > 0) {
        mm->duplicate = 1;
        return 1;
    } else {
        // CPR not yet known to cpr cache

        a->cpr_cache_index = (a->cpr_cache_index + 1) % CPR_CACHE;

        cpr = &a->cpr_cache[a->cpr_cache_index];
        cpr->ts = now;
        cpr->cpr_lat = cpr_lat;
        cpr->cpr_lon = cpr_lon;
        cpr->receiverId = receiverId;

        return 0;
    }
}

static int duplicate_check(int64_t now, struct aircraft *a, double new_lat, double new_lon, struct modesMessage *mm) {
    if (mm->duplicate_checked || mm->duplicate) {
        // already checked
        return mm->duplicate;
    }
    mm->duplicate_checked = 1;

    // if the last position is older than 2 seconds we don't consider it a duplicate
    if (now > a->seen_pos + 2 * SECONDS) {
        return 0;
    }
    // duplicate
    if (a->lat == new_lat && a->lon == new_lon) {
        mm->duplicate = 1;
        return 1;
    }

    // if the previous position is older than 2 seconds we don't consider it a duplicate
    if (now > a->prev_pos_time + 2 * SECONDS) {
        return 0;
    }
    // duplicate (this happens either with some transponder or delayed data arrival due to odd / even CPR, not certain)
    if (a->prev_lat == new_lat && a->prev_lon == new_lon) {
        mm->duplicate = 1;
        return 1;
    }

    return 0;
}

static int uat2esnt_duplicate(int64_t now, struct aircraft *a, struct modesMessage *mm) {
    return (
            mm->cpr_valid && mm->cpr_odd && mm->msgtype == 18
            && (mm->timestamp == MAGIC_UAT_TIMESTAMP || mm->timestamp == 0)
            && now - a->seenPosReliable < 2500
           );
}

static int inDiscCache(int64_t now, struct aircraft *a, struct modesMessage *mm) {
    struct cpr_cache *disc;
    uint32_t inCache = 0;
    uint32_t cpr_lat = mm->cpr_lat;
    uint32_t cpr_lon = mm->cpr_lon;
    uint64_t receiverId = mm->receiverId;
    for (int i = 0; i < DISCARD_CACHE; i++) {
        disc = &a->disc_cache[i];
        // don't decrement pos_reliable if we already got the same bad position within the last second
        // rate limit reliable decrement per receiver
        if (
                (
                 now - disc->ts < 4 * SECONDS
                 && disc->cpr_lat == cpr_lat
                 && disc->cpr_lon == cpr_lon
                )
                ||
                (
                 now - disc->ts < 300
                 && disc->receiverId == receiverId
                )
           ) {
            inCache += 1;
        }
    }
    if (inCache > 0) {
        return 1;
    } else {
        return 0;
    }
}

// return true if it's OK for the aircraft to have travelled from its last known position
// to a new position at (lat,lon,surface) at a time of now.

static int speed_check(struct aircraft *a, datasource_t source, double lat, double lon, struct modesMessage *mm, cpr_local_t cpr_local) {
    int64_t now = mm->sysTimestamp;
    int64_t elapsed = trackDataAge(now, &a->position_valid);
    int receiverRangeExceeded = 0;

    if (0 && mm->sbs_in && a->addr == Modes.cpr_focus) {
        fprintf(stderr, ".");
    }

    if (duplicate_check(now, a, lat, lon, mm)) {
        // don't use duplicate positions
        mm->pos_ignore = 1;
        // but count it as a received position towards receiver heuristics
        if (!Modes.userLocationValid) {
            receiverPositionReceived(a, mm, lat, lon, now);
        }
        if (elapsed > 200 && a->receiverId == mm->receiverId && (Modes.debug_cpr || Modes.debug_speed_check || a->addr == Modes.cpr_focus)) {
            // let speed_check continue for displaying this duplicate (at least for non-aggregated receivers)
        } else {
            // omit rest of speed check to save on cycles
            return 1;
        }
    }
    if (0 && (a->prev_lat == lat && a->prev_lon == lon) && (Modes.debug_cpr || Modes.debug_speed_check || a->addr == Modes.cpr_focus)) {
        fprintf(stderr, "%06x now - seen_pos %6.3f now - prev_pos_time %6.3f\n",
                a->addr,
                (now - a->seen_pos) / 1000.0,
                (now - a->prev_pos_time) / 1000.0
               );
    }

    if (mm->cpr_valid && inDiscCache(now, a, mm)) {
        mm->in_disc_cache = 1;
    }

    float distance = -1;
    float range = -1;
    float speed = -1;
    float transmitted_speed = -1;
    float calc_track = -1;
    int inrange;
    int override = 0;
    double oldLat = a->lat;
    double oldLon = a->lon;


    int surface = trackDataValid(&a->airground_valid)
        && a->airground == AG_GROUND
        && a->pos_surface
        && (!mm->cpr_valid || mm->cpr_type == CPR_SURFACE);


    // json_reliable == -1 disables the speed check
    if (Modes.json_reliable == -1 || mm->source == SOURCE_PRIO) {
        override = 1;
    } else if (bogus_lat_lon(lat, lon) ||
            (mm->cpr_valid && mm->cpr_lat == 0 && mm->cpr_lon == 0)
            || (
                mm->cpr_valid && (mm->cpr_lat == 0 || mm->cpr_lon == 0)
                && (a->position_valid.source < SOURCE_TISB || !posReliable(a))
               )
            ) {
        mm->pos_ignore = 1; // don't decrement pos_reliable
    } else if (a->pos_reliable_odd < 0.2 || a->pos_reliable_even < 0.2) {
        override = 1;
    } else if (now - a->position_valid.updated > POS_RELIABLE_TIMEOUT) {
        override = 1; // no reference or older than 60 minutes, assume OK
    } else if (source > a->position_valid.source && source > a->position_valid.last_source && source > a->pos_reliable_valid.source) {
        override = 1; // data is better quality, OVERRIDE
    } else if (source > a->position_valid.source && a->position_valid.source == SOURCE_INDIRECT) {
        override = 1; // data is better quality, OVERRIDE
    } else if (source <= SOURCE_MLAT && elapsed > 45 * SECONDS) {
        override = 1;
    } else if (a->addr == 0xa19b53) {
        // Virgin SS2
        override = 1;
    }

    if (mm->in_disc_cache) {
        override = 0; // don't override if in discard cache
    }

    if (trackDataValid(&a->gs_valid)) {
        // use the larger of the current and earlier speed
        speed = (a->gs_last_pos > a->gs) ? a->gs_last_pos : a->gs;
        // add 3 knots for every second we haven't known the speed and the position
        speed = speed + (3 * trackDataAge(now, &a->gs_valid)/1000.0f) + (3 * trackDataAge(now, &a->position_valid)/1000.0f);
    } else if (trackDataValid(&a->tas_valid)) {
        speed = a->tas * 4 / 3;
    } else if (trackDataValid(&a->ias_valid)) {
        speed = a->ias * 2;
    }
    transmitted_speed = speed;

    // find actual distance
    distance = greatcircle(oldLat, oldLon, lat, lon, 0);
    mm->distance_traveled = distance;

    float track_diff = -1;
    float track_bonus = 0;
    int64_t track_max_age = 5 * SECONDS;
    int64_t track_age = -1;
    float track = -1;
    if (trackDataAge(now, &a->track_valid) < track_max_age) {
        track = a->track;
        track_age = trackDataAge(now, &a->track_valid);
    } else if (trackDataAge(now, &a->true_heading_valid) < track_max_age) {
        track = a->true_heading;
        track_age = trackDataAge(now, &a->true_heading_valid);
    }

    if (distance > 2.5f) {
        calc_track = bearing(oldLat, oldLon, lat, lon);
        mm->calculated_track = calc_track;
        if (source != SOURCE_MLAT
                && track > -1
                && trackDataAge(now, &a->position_valid) < 7 * SECONDS
           ) {
            track_diff = fabs(norm_diff(track - calc_track, 180));
        }
    }

    if (track_diff > 70.0f) {
        mm->trackUnreliable = +1;
    } else if (track_diff > -1) {
        mm->trackUnreliable = -1;
    }

    if (!posReliable(a)) {
        // don't use track_diff for further calculations unless position is already reliable
        track_diff = -1;
    }

    if (speed < 0 || a->speedUnreliable > 8) {
        speed = surface ? 120 : 900; // guess
    }

    if (speed > 1 && track_diff > -1 && a->trackUnreliable < 8) {
        track_bonus = speed * (90.0f - track_diff) / 90.0f;
        track_bonus *= (surface ? 0.9f : 1.0f) * (1.0f - track_age / track_max_age);
        if (a->gs < 10) {
            // don't allow negative "bonus" below 10 knots speed
            track_bonus = fmaxf(0.0f, track_bonus);
            speed += 2;
        }
        speed += track_bonus;
        if (track_diff > 160) {
            mm->pos_old = 1; // don't decrement pos_reliable
        }
        // allow relatively big forward jumps
        if (speed > 40 && track_diff < 10) {
            range += 2e3;
        }
    } else {
        // Work out a reasonable speed to use:
        //  current speed + 1/3
        speed = speed * 1.3f;
    }
    if (surface) {
        range += 10;
    } else {
        range += 30;
    }

    // same TCP packet (2 ms), two positions from same receiver id, allow plenty of extra range
    if (elapsed < 2 && a->receiverId == mm->receiverId && source > SOURCE_MLAT) {
        range += 500; // 500 m extra in this case
    }

    // cap speed at 2000 knots ..
    speed = fmin(speed, 2000);

    if (source == SOURCE_MLAT) {
        speed *= 1.4;
        speed += 50;
        range += 250;
    }

    if (distance > 2.5f && (track_diff < 70 || track_diff == -1)) {
        if (distance <= range + (((float) elapsed + 50.0f) * (1.0f / 1000.0f)) * (transmitted_speed * knots_to_meterpersecond)) {
            mm->speedUnreliable = -1;
        } else if (distance > range + (((float) elapsed + 400.0f) * (1.0f / 1000.0f)) * (transmitted_speed * knots_to_meterpersecond)) {
            mm->speedUnreliable = +1;
        }
    }

    // plus distance covered at the given speed for the elapsed time + 0.2 seconds.
    range += (((float) elapsed + 200.0f) * (1.0f / 1000.0f)) * (speed * (1852.0f / 3600.0f));
    inrange = (distance <= range);

    // don't allow going backwards in the air contrary to good track information
    // when only a short time has elapsed
    // when the receiverId differs
    // this mostly happens due to static range allowed
    if (!surface && a->gs > 10 && track_diff > 135 && elapsed < 2 * SECONDS && trackDataAge(now, &a->track_valid) < 2 * SECONDS && a->receiverId != mm->receiverId) {
        inrange = 0;
    }

    float backInTimeSeconds = 0;
    if (!inrange && a->gs > 10 && track_diff > 135 && trackDataAge(now, &a->gs_valid) < 10 * SECONDS) {
        backInTimeSeconds = distance / (a->gs * (1852.0f / 3600.0f));
    }

    if (!Modes.userLocationValid && (inrange || override)) {
        if (receiverPositionReceived(a, mm, lat, lon, now) == RECEIVER_RANGE_BAD) {
            // far outside receiver area
            receiverRangeExceeded = 1;
        }
    }

    if (!Modes.userLocationValid && !override && mm->source == SOURCE_ADSB) {
        if (!receiverRangeExceeded && !inrange
                && (distance - range > 800 || backInTimeSeconds > 3) && track_diff > 45
                && a->pos_reliable_odd >= Modes.position_persistence * 3 / 4
                && a->pos_reliable_even >= Modes.position_persistence * 3 / 4
                && a->trackUnreliable < 3
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

    }

    if (
            (
             ((!inrange && track_diff < 160) || (!surface && (a->speedUnreliable > 8 || a->trackUnreliable > 8)))
             && source == a->position_valid.source && source > SOURCE_MLAT && (Modes.debug_cpr || Modes.debug_speed_check)
            )
            || (a->addr == Modes.cpr_focus && source >= a->position_valid.source)
            || (Modes.debug_maxRange && track_diff > 90)
            || (receiverRangeExceeded && Modes.debug_receiverRangeLimit)
       ) {
        if (uat2esnt_duplicate(now, a, mm) || (!inrange && !override && mm->in_disc_cache) || mm->garbage) {
            // don't show debug
        } else {
            char *failMessage;
            if (inrange) {
                failMessage = "pass";
            } else if (override) {
                failMessage = "ovrd";
            } else if (receiverRangeExceeded) {
                failMessage = "RcvR";
            } else {
                failMessage = "FAIL";
            }
            char uuid[32]; // needs 18 chars and null byte
            sprint_uuid1(mm->receiverId, uuid);
            fprintTime(stderr, now);
            fprintf(stderr, " %06x R%3.1f|%3.1f %s %s %s %s %4.0f%%%2ds%2dt %3.0f/%3.0f td %3.0f %8.3fkm in%4.1fs, %4.0fkt %11.6f,%11.6f->%11.6f,%11.6f biT %4.1f s %s rId %s\n",
                    a->addr,
                    a->pos_reliable_odd, a->pos_reliable_even,
                    mm->cpr_odd ? "O" : "E",
                    cpr_local == CPR_LOCAL ? "L" : (cpr_local == CPR_GLOBAL ? "G" : "S"),
                    (surface ? "S" : "A"),
                    failMessage,
                    fmin(9001.0, 100.0 * distance / range),
                    a->speedUnreliable,
                    a->trackUnreliable,
                    track,
                    calc_track,
                    track_diff,
                    fmin(9001.0, distance / 1000.0),
                    elapsed / 1000.0,
                    fmin(9001.0, distance / elapsed * 1000.0 / 1852.0 * 3600.0),
                    oldLat, oldLon, lat, lon,
                    backInTimeSeconds, source_string(mm->source), uuid);
        }
    }

    // override, this allows for printing stuff instead of returning
    if (override) {
        if (!inrange) {
            a->lastOverrideTs = now;
        }
        inrange = override;
    }

    if (receiverRangeExceeded && Modes.garbage_ports) {
        mm->pos_receiver_range_exceeded = 1;
        inrange = 0; // far outside receiver area
        mm->pos_ignore = 1;
        mm->pos_bad = 1;
    }


    return inrange;
}

/* debug code for surface CPR decoding ... might be useful and reduce typing at some point
   if (Modes.debug_receiver && Modes.debug_speed_check && receiver && a->seen_pos
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
int sc = speed_check(a, mm->source, *lat, *lon, mm, CPR_GLOBAL);
fprintf(stderr, "%s%06x surface CPR rec. ref.: %4.0f %4.0f sc: %d result: %7.2f %7.2f --> %7.2f %7.2f\n",
(a->addr & MODES_NON_ICAO_ADDRESS) ? "~" : " ",
a->addr, reflat, reflon, sc, a->lat, a->lon, *lat, *lon);
}

if (Modes.debug_receiver && receiver && a->addr == Modes.cpr_focus)
fprintf(stderr, "%06x using reference: %4.0f %4.0f result: %7.2f %7.2f\n", a->addr, reflat, reflon, *lat, *lon);
*/

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

        int ref = 0;
        if (Modes.userLocationValid) {
            reflat = Modes.fUserLat;
            reflon = Modes.fUserLon;
            ref = 1;
        } else if ((receiver = receiverGetReference(mm->receiverId, &reflat, &reflon, a, 0))) {
            //function sets reflat and reflon on success, nothing to do here.
            ref = 2;
        } else if (a->seen_pos && a->surfaceCPR_allow_ac_rel) {
            reflat = a->latReliable;
            reflon = a->lonReliable;
            ref = 3;
        } else {
            // No local reference, give up
            return (-1);
        }

        result = decodeCPRsurface(reflat, reflon,
                a->cpr_even_lat, a->cpr_even_lon,
                a->cpr_odd_lat, a->cpr_odd_lon,
                fflag,
                lat, lon);
        double refDistance = greatcircle(reflat, reflon, *lat, *lon, 0);
        if (refDistance > 450e3) {
            if (0 && (a->addr == Modes.cpr_focus || Modes.debug_cpr)) {
                fprintf(stderr, "%06x CPRsurface ref %d refDistance: %4.0f km (%4.0f, %4.0f) allow_ac_rel %d\n", a->addr, ref, refDistance / 1000.0, reflat, reflon, a->surfaceCPR_allow_ac_rel);
            }
            // change to failure which doesn't decrement reliable
            result = -1;
            return result;
        }
    } else {
        // airborne global CPR
        result = decodeCPRairborne(a->cpr_even_lat, a->cpr_even_lon,
                a->cpr_odd_lat, a->cpr_odd_lon,
                fflag,
                lat, lon);
    }

    if (result < 0) {
        if (!mm->duplicate && (a->addr == Modes.cpr_focus || Modes.debug_cpr) && !inDiscCache(mm->sysTimestamp, a, mm)) {
            fprintf(stderr, "CPR: decode failure for %06x (%d): even: %d %d   odd: %d %d  fflag: %s\n",
                    a->addr, result,
                    a->cpr_even_lat, a->cpr_even_lon,
                    a->cpr_odd_lat, a->cpr_odd_lon,
                    fflag ? "odd" : "even");
        }
        return result;
    }

    // check max range
    if (Modes.maxRange > 0 && Modes.userLocationValid) {
        mm->receiver_distance = greatcircle(Modes.fUserLat, Modes.fUserLon, *lat, *lon, 0);
        if (mm->receiver_distance > Modes.maxRange) {
            if (a->addr == Modes.cpr_focus || Modes.debug_bogus) {
                fprintf(stdout, "%5llu %5.1f Global range check failed: %06x %.3f,%.3f, max range %.1fkm, actual %.1fkm\n",
                        (long long) mm->timestamp % 65536,
                        10 * log10(mm->signalLevel),
                        a->addr, *lat, *lon, Modes.maxRange / 1000.0, mm->receiver_distance / 1000.0);
            }

            if (mm->source != SOURCE_MLAT) {
                Modes.stats_current.cpr_global_range_checks++;
                if (Modes.debug_maxRange) {
                    showPositionDebug(a, mm, mm->sysTimestamp, *lat, *lon);
                }
            }
            return (-2); // we consider an out-of-range value to be bad data
        }
    }

    // check speed limit
    if (!speed_check(a, mm->source, *lat, *lon, mm, CPR_GLOBAL)) {
        if (mm->source != SOURCE_MLAT)
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

    int64_t now = mm->sysTimestamp;
    if (now < a->seenPosGlobal + 10 * MINUTES && a->localCPR_allow_ac_rel) {
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
    } else if (!surface && Modes.userLocationValid) {
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
        double range = greatcircle(reflat, reflon, *lat, *lon, 0);
        if (range > range_limit) {
            if (mm->source != SOURCE_MLAT)
                Modes.stats_current.cpr_local_range_checks++;
            return (-1);
        }
    }

    // check max range
    if (Modes.maxRange > 0 && Modes.userLocationValid) {
        mm->receiver_distance = greatcircle(Modes.fUserLat, Modes.fUserLon, *lat, *lon, 0);
        if (mm->receiver_distance > Modes.maxRange) {
            if (a->addr == Modes.cpr_focus || Modes.debug_bogus) {
                fprintf(stdout, "%5llu %5.1f Global range check failed: %06x %.3f,%.3f, max range %.1fkm, actual %.1fkm\n",
                        (long long) mm->timestamp % 65536,
                        10 * log10(mm->signalLevel),
                        a->addr, *lat, *lon, Modes.maxRange / 1000.0, mm->receiver_distance / 1000.0);
            }

            if (mm->source != SOURCE_MLAT) {
                Modes.stats_current.cpr_local_range_checks++;
                if (Modes.debug_maxRange) {
                    showPositionDebug(a, mm, mm->sysTimestamp, *lat, *lon);
                }
            }
            return (-2); // we consider an out-of-range value to be bad data
        }
    }


    // check speed limit
    if (!speed_check(a, mm->source, *lat, *lon, mm, CPR_LOCAL)) {
        if (mm->source != SOURCE_MLAT)
            Modes.stats_current.cpr_local_speed_checks++;
        return -2;
    }

    return relative_to;
}

static int64_t time_between(int64_t t1, int64_t t2) {
    if (t1 >= t2)
        return t1 - t2;
    else
        return t2 - t1;
}

static void setPosition(struct aircraft *a, struct modesMessage *mm, int64_t now) {
    if (0 && a->addr == Modes.cpr_focus) {
        showPositionDebug(a, mm, now, 0, 0);
    }

    // if we get the same position again but from an inferior source, assume it's delayed and treat as duplicate
    if (now < a->seen_pos + 10 * MINUTES && mm->source < a->position_valid.last_source && mm->distance_traveled < 20) {
        if (a->addr == Modes.cpr_focus) {
            fprintf(stderr, "%06x less than 20 m\n", a->addr);
        }
        mm->duplicate = 1;
        mm->pos_ignore = 1;
    }
    if (duplicate_check(now, a, mm->decoded_lat, mm->decoded_lon, mm)) {
        // don't use duplicate positions
        mm->pos_ignore = 1;
    }

    if (bogus_lat_lon(mm->decoded_lat, mm->decoded_lon)) {
        if (0 && (fabs(mm->decoded_lat) >= 90.0 || fabs(mm->decoded_lon) >= 180.0)) {
            fprintf(stderr, "%06x lat,lon out of bounds: %.2f,%.2f source: %s\n", a->addr, mm->decoded_lat, mm->decoded_lon, source_enum_string(mm->source));
        }
        return;
    }

    // for UAT messages converted by uat2esnt each position becomes a odd / even message pair
    // only update the position for the odd message if we've recently seen a reliable position
    if (uat2esnt_duplicate(now, a, mm)) {
        return;
    }

    Modes.stats_current.pos_by_type[mm->addrtype]++;
    Modes.stats_current.pos_all++;

    // mm->pos_bad should never arrive here, handle it just in case
    if (mm->cpr_valid && (mm->garbage || mm->pos_bad)) {
        Modes.stats_current.pos_garbage++;
        return;
    }

    if (mm->client) {
        mm->client->positionCounter++;
    }

    if (mm->duplicate) {
        Modes.stats_current.pos_duplicate++;
        return;
    }

    a->receiverId = mm->receiverId;

    if (mm->source != SOURCE_JAERO && mm->distance_traveled >= 100) {
        if (mm->calculated_track != -1)
            a->calc_track = mm->calculated_track;
        else
            a->calc_track = bearing(a->lat, a->lon, mm->decoded_lat, mm->decoded_lon);
    }

    if (mm->source == SOURCE_JAERO && a->seenPosReliable) {
        if ((a->position_valid.last_source == SOURCE_JAERO || now > a->seenPosReliable + 10 * MINUTES)
                && mm->distance_traveled > 2e3) {
            if (accept_data(&a->track_valid, SOURCE_JAERO, mm, a, REDUCE_OFTEN)) {
                a->calc_track = a->track = bearing(a->latReliable, a->lonReliable, mm->decoded_lat, mm->decoded_lon);
                //fprintf(stderr, "%06x track %0.1f\n", a->addr, a->track);
            }
        } else if (trackDataAge(now, &a->track_valid) < 10 * MINUTES) {
            accept_data(&a->track_valid, SOURCE_JAERO, mm, a, REDUCE_OFTEN);
        }
    }

    // Update aircraft state
    a->prev_lat = a->lat;
    a->prev_lon = a->lon;
    a->prev_pos_time = a->seen_pos;

    a->lat = mm->decoded_lat;
    a->lon = mm->decoded_lon;
    a->pos_nic = mm->decoded_nic;
    a->pos_rc = mm->decoded_rc;
    a->seen_pos = now;


    a->pos_surface = trackDataValid(&a->airground_valid) && a->airground == AG_GROUND;

    // due to accept_data / beast_reduce forwarding we can't put receivers
    // into the receiver list which aren't the first ones to send us the position
    if (!Modes.netReceiverId) {
        a->receiverCount = 1;
    } else {
        a->receiverIdsNext = (a->receiverIdsNext + 1) % RECEIVERIDBUFFER;
        a->receiverIds[a->receiverIdsNext] = simpleHash(mm->receiverId);
        if (0 && a->addr == Modes.cpr_focus) {
            fprintf(stderr, "%u\n", simpleHash(mm->receiverId));
        }

        if (mm->source == SOURCE_MLAT && mm->receiverCountMlat) {
            a->receiverCount = mm->receiverCountMlat;
        } else if (a->position_valid.source < SOURCE_TISB) {
            a->receiverCount = 1;
        } else {
            uint16_t *set1 = a->receiverIds;
            uint16_t set2[RECEIVERIDBUFFER] = { 0 };
            int div = 0;
            for (int k = 0; k < RECEIVERIDBUFFER; k++) {
                int unequal = 0;
                for (int j = 0; j < div; j++) {
                    unequal += (set1[k] != set2[j]);
                }
                if (unequal == div && set1[k]) {
                    set2[div++] = set1[k];
                }
            }
            a->receiverCount = div;
        }
    }

    if (Modes.netReceiverId && posReliable(a)) {

        int64_t valid_elapsed = now - a->pos_reliable_valid.updated;
        int64_t override_elapsed = now - a->lastOverrideTs;
        int64_t status_elapsed = now - a->lastStatusTs;

        if (
                (valid_elapsed > 10 * MINUTES || override_elapsed < 10 * MINUTES)
                && (mm->msgtype == 17 || (mm->addrtype == ADDR_ADSB_ICAO_NT && mm->cpr_type != CPR_SURFACE
                        && !(a->dbFlags & (1 << 7)) && ((a->addr >= 0xa00000 && a->addr <= 0xafffff) || (a->dbFlags & (1 << 0))) ))
                && mm->cpr_valid
                && status_elapsed > 5 * MINUTES
           ) {
            double dist = greatcircle(a->latReliable, a->lonReliable, mm->decoded_lat, mm->decoded_lon, 0);
            double estimate = a->gs_reliable * knots_to_meterpersecond * (valid_elapsed * 1e-3);
            if (dist > 200e3 && fabs(estimate - dist) > 100e3) {
                a->lastStatusDiscarded++;
                if (((Modes.debug_lastStatus & 1) && a->lastStatusDiscarded > 8) || (Modes.debug_lastStatus & 4)) {
                    char uuid[32]; // needs 18 chars and null byte
                    sprint_uuid1(mm->receiverId, uuid);
                    fprintf(stderr, "%06x dist: %4d status_e: %4d valid_e: %4d over_e: %4d rCount: %d uuid: %s dbFlags: %u DF: %d\n",
                            a->addr,
                            (int) imin(9999, (dist / 1000)),
                            (int) imin(9999, (status_elapsed / 1000)),
                            (int) imin(9999, (valid_elapsed / 1000)),
                            (int) imin(9999, (override_elapsed / 1000)),
                            a->receiverCount,
                            uuid,
                            a->dbFlags,
                            mm->msgtype);
                }
                if (!(Modes.debug_lastStatus & 2)) {
                    return;
                }
            }
        }
    }

    if (posReliable(a) && accept_data(&a->pos_reliable_valid, mm->source, mm, a, REDUCE_OFTEN)) {

        a->lastStatusDiscarded = 0;

        // when accepting crappy positions, invalidate the data indicating position accuracy
        if (mm->source < SOURCE_TISB) {
            // a->nic_baro_valid.source = SOURCE_INVALID;
            a->nac_p_valid.source = SOURCE_INVALID;
            a->nac_v_valid.source = SOURCE_INVALID;
            a->sil_valid.source = SOURCE_INVALID;
            a->gva_valid.source = SOURCE_INVALID;
            a->sda_valid.source = SOURCE_INVALID;
            a->sil_type = SIL_INVALID;
        }

        set_globe_index(a, globe_index(a->lat, a->lon));

        if (0 && a->addr == Modes.trace_focus) {
            fprintf(stderr, "%5.1fs traceAdd: %06x\n", (now % (600 * SECONDS)) / 1000.0, a->addr);
        }

        a->lastPosReceiverId = mm->receiverId;

        // update addrtype, we use the type from the accepted position.
        a->addrtype = mm->addrtype;
        a->addrtype_updated = now;

        if (now < a->seenPosReliable) {
            fprintf(stderr, "%06x now < seenPosReliable ??? mstime: %.3f now: %.3f seenPosReliabe: %.3f\n", a->addr, mstime() / 1000.0, now / 1000.0, a->seenPosReliable / 1000.0);
        }
        int stale = (now > a->seenPosReliable + TRACE_STALE);
        a->seenPosReliable = now;

        a->latReliable = mm->decoded_lat;
        a->lonReliable = mm->decoded_lon;
        a->pos_nic_reliable = mm->decoded_nic;
        a->pos_rc_reliable = mm->decoded_rc;
        a->surfaceCPR_allow_ac_rel = 1; // allow ac relative CPR for ground positions


        if (trackDataValid(&a->gs_valid)) {
            a->gs_reliable = a->gs;
        }
        if (trackDataValid(&a->track_valid)) {
            a->track_reliable = a->track;
        }

        traceAdd(a, mm, now, stale);

        if (
                mm->source == SOURCE_ADSB
                && trackDataValid(&a->nac_p_valid) && a->nac_p >= 4 // 1 nmi
           ) {
            a->seenAdsbReliable = now;
            a->seenAdsbLat = mm->decoded_lat;
            a->seenAdsbLon = mm->decoded_lon;
        }

        if (Modes.userLocationValid) {

            if (mm->receiver_distance == 0) {
                mm->receiver_distance = greatcircle(Modes.fUserLat, Modes.fUserLon, a->lat, a->lon, 0);
            }

            a->receiver_distance = mm->receiver_distance;
            a->receiver_direction = bearing(Modes.fUserLat, Modes.fUserLon, a->lat, a->lon);

            if (mm->source == SOURCE_ADSB || mm->source == SOURCE_ADSR) {
                update_range_histogram(a, now);
            }

        }
        if (now > a->nextJsonPortOutput) {
            a->nextJsonPortOutput = now + Modes.net_output_json_interval;
            mm->jsonPositionOutputEmit = 1;
        }
    }

    if (0 && a->addr == Modes.cpr_focus) {
        fprintf(stderr, "%06x: reliability odd: %3.1f even: %3.1f status: %d\n", a->addr, a->pos_reliable_odd, a->pos_reliable_even, posReliable(a));
    }
}

static void updatePosition(struct aircraft *a, struct modesMessage *mm, int64_t now) {
    int location_result = -1;
    int globalCPR = 0;
    int64_t max_elapsed;
    double new_lat = 0, new_lon = 0;
    unsigned new_nic = 0;
    unsigned new_rc = 0;
    int surface;

    surface = (mm->cpr_type == CPR_SURFACE);
    a->pos_surface = trackDataValid(&a->airground_valid) && a->airground == AG_GROUND;

    if (surface) {
        if (mm->source != SOURCE_MLAT)
            Modes.stats_current.cpr_surface++;

        // Surface: 25 seconds if >25kt or speed unknown, 50 seconds otherwise
        if (mm->gs_valid && mm->gs.selected <= 25)
            max_elapsed = 50000;
        else
            max_elapsed = 25000;
    } else {
        if (mm->source != SOURCE_MLAT)
            Modes.stats_current.cpr_airborne++;

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
            // Global CPR failed because the position produced implausible results.
            // This is bad data.
            if (mm->source != SOURCE_MLAT)
                Modes.stats_current.cpr_global_bad++;

            mm->pos_bad = 1;

            // still note which position we decoded
            mm->decoded_lat = new_lat;
            mm->decoded_lon = new_lon;
        } else if (location_result == -1) {
            if (a->addr == Modes.cpr_focus || Modes.debug_cpr) {
                if (mm->source == SOURCE_MLAT) {
                    fprintf(stderr, "CPR skipped from MLAT (%06x).\n", a->addr);
                }
            }
            // No local reference for surface position available, or the two messages crossed a zone.
            // Nonfatal, try again later.
            if (mm->source != SOURCE_MLAT)
                Modes.stats_current.cpr_global_skipped++;
        } else {
            if (accept_data(&a->position_valid, mm->source, mm, a, REDUCE_OFTEN)) {
                if (mm->source != SOURCE_MLAT)
                    Modes.stats_current.cpr_global_ok++;

                globalCPR = 1;
            } else {
                if (mm->source != SOURCE_MLAT)
                    Modes.stats_current.cpr_global_skipped++;
                location_result = -2;
            }
        }
    }

    // Otherwise try relative CPR.
    if (location_result == -1) {
        location_result = doLocalCPR(a, mm, &new_lat, &new_lon, &new_nic, &new_rc);
        //if (a->addr == Modes.cpr_focus)
        //    fprintf(stderr, "%06x: localCPR: %d\n", a->addr, location_result);


        if (location_result == -2) {
            // Local CPR failed because the position produced implausible results.
            // This is bad data.

            mm->pos_bad = 1;

            // still note which position we decoded
            mm->decoded_lat = new_lat;
            mm->decoded_lon = new_lon;
        } else if (location_result >= 0 && accept_data(&a->position_valid, mm->source, mm, a, REDUCE_OFTEN)) {
            if (mm->source != SOURCE_MLAT)
                Modes.stats_current.cpr_local_ok++;
            mm->cpr_relative = 1;

            if (location_result == 1) {
                if (mm->source != SOURCE_MLAT)
                    Modes.stats_current.cpr_local_aircraft_relative++;
            }
            if (location_result == 2) {
                if (mm->source != SOURCE_MLAT)
                    Modes.stats_current.cpr_local_receiver_relative++;
            }
        } else {
            if (mm->source != SOURCE_MLAT)
                Modes.stats_current.cpr_local_skipped++;
            location_result = -1;
        }
    }

    if (location_result >= 0) {
        // If we sucessfully decoded, back copy the results to mm
        mm->cpr_decoded = 1;
        mm->decoded_lat = new_lat;
        mm->decoded_lon = new_lon;
        mm->decoded_nic = new_nic;
        mm->decoded_rc = new_rc;

        if (trackDataValid(&a->gs_valid))
            a->gs_last_pos = a->gs;

        if (globalCPR)
            incrementReliable(a, mm, now, mm->cpr_odd);

        setPosition(a, mm, now);
    } else if (location_result == -1 && a->addr == Modes.cpr_focus && !mm->duplicate) {
        fprintf(stderr, "%5.1fs %d: mm->cpr: (%d) (%d) %s %s, %s age: %0.1f sources o: %s %s e: %s %s lpos src: %s \n",
                (now % (600 * SECONDS)) / 1000.0,
                location_result,
                mm->cpr_lat, mm->cpr_lon,
                mm->cpr_odd ? " odd" : "even", cpr_type_string(mm->cpr_type), mm->cpr_odd ? "even" : " odd",
                mm->cpr_odd ? fmin(999, ((double) now - a->cpr_even_valid.updated) / 1000.0) : fmin(999, ((double) now - a->cpr_odd_valid.updated) / 1000.0),
                source_enum_string(a->cpr_odd_valid.source), cpr_type_string(a->cpr_odd_type),
                source_enum_string(a->cpr_even_valid.source), cpr_type_string(a->cpr_even_type),
                source_enum_string(a->position_valid.last_source));
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

// check if we trust that this message is actually from the aircraft with this address
// similar reasoning to icaoFilterAdd in mode_s.c
static int addressReliable(struct modesMessage *mm) {
    if (mm->msgtype == 17 || mm->msgtype == 18 || (mm->msgtype == 11 && mm->IID == 0) || mm->sbs_in) {
        return 1;
    }
    return 0;
}

static inline void focusGroundstateChange(struct aircraft *a, struct modesMessage *mm, int arg, int64_t now, airground_t airground) {
    //if (a->airground != mm->airground) {
    if (a->addr == Modes.cpr_focus && a->airground != airground) {
        fprintf(stderr, "%02d:%04.1f %06x Ground state change %d: Source: %s, %s -> %s\n",
            (int) (now / (60 * SECONDS) % 60),
            (now % (60 * SECONDS)) / 1000.0,
                a->addr,
                arg,
                source_enum_string(mm->source),
                airground_to_string(a->airground),
                airground_to_string(airground));
        displayModesMessage(mm);
    }
}
static void updateAltitude(int64_t now, struct aircraft *a, struct modesMessage *mm) {

    int alt = altitude_to_feet(mm->baro_alt, mm->baro_alt_unit);
    if (a->modeC_hit) {
        int new_modeC = (a->baro_alt + 49) / 100;
        int old_modeC = (alt + 49) / 100;
        if (new_modeC != old_modeC) {
            a->modeC_hit = 0;
        }
    }

    int delta = alt - a->baro_alt;
    int fpm = 0;

    int max_fpm = 12500;
    int min_fpm = -12500;

    if (abs(delta) >= 300) {
        fpm = delta*60*10/(abs((int)trackDataAge(now, &a->baro_alt_valid)/100)+10);
        if (trackDataValid(&a->geom_rate_valid) && trackDataAge(now, &a->geom_rate_valid) < trackDataAge(now, &a->baro_rate_valid)) {
            min_fpm = a->geom_rate - 1500 - imin(11000, ((int)trackDataAge(now, &a->geom_rate_valid)/2));
            max_fpm = a->geom_rate + 1500 + imin(11000, ((int)trackDataAge(now, &a->geom_rate_valid)/2));
        } else if (trackDataValid(&a->baro_rate_valid)) {
            min_fpm = a->baro_rate - 1500 - imin(11000, ((int)trackDataAge(now, &a->baro_rate_valid)/2));
            max_fpm = a->baro_rate + 1500 + imin(11000, ((int)trackDataAge(now, &a->baro_rate_valid)/2));
        }
        if (trackDataValid(&a->baro_alt_valid) && trackDataAge(now, &a->baro_alt_valid) < 30 * SECONDS) {
            a->alt_reliable = imin(
                    ALTITUDE_BARO_RELIABLE_MAX - (ALTITUDE_BARO_RELIABLE_MAX*trackDataAge(now, &a->baro_alt_valid)/(30 * SECONDS)),
                    a->alt_reliable);
        } else {
            a->alt_reliable = 0;
        }
    }

    int good_crc = 0;

    // just trust messages with this source implicitely and rate the altitude as max reliable
    // if we get the occasional altitude excursion that's acceptable and preferable to not capturing implausible altitude changes for example before a crash
    if (mm->crc == 0 && (mm->source >= SOURCE_JAERO || mm->source == SOURCE_SBS)) {
        good_crc = ALTITUDE_BARO_RELIABLE_MAX;
    }
    if (mm->source == SOURCE_MLAT) {
        good_crc = ALTITUDE_BARO_RELIABLE_MAX/2 - 1;
    }

    if (a->baro_alt > 50175 && mm->alt_q_bit && a->alt_reliable > ALTITUDE_BARO_RELIABLE_MAX/4) {
        good_crc = 0;
        //fprintf(stderr, "q_bit == 1 && a->alt > 50175: %06x\n", a->addr);
        goto discard_alt;
    }

    // accept the message if the good_crc score is better than the current alt reliable score
    if (good_crc >= a->alt_reliable)
        goto accept_alt;
    // accept the altitude if the source is better than the current one
    if (mm->source > a->baro_alt_valid.source)
        goto accept_alt;

    if (a->alt_reliable <= 0  || abs(delta) < 300)
        goto accept_alt;
    if (fpm < max_fpm && fpm > min_fpm)
        goto accept_alt;

    goto discard_alt;
    int score_add;
accept_alt:
    if (Modes.netReceiverId && mm->source == SOURCE_MODE_S && now - a->baro_alt_valid.updated > 10 * SECONDS) {
        score_add = 0;
    } else {
        score_add = good_crc + 1;
    }
    if (mm->source == SOURCE_MODE_S && a->position_valid.source == SOURCE_MLAT
            && trackDataAge(now, &a->baro_alt_valid) > 5 * SECONDS && trackDataAge(now, &a->position_valid) < 15 * SECONDS && a->receiverCount > 1) {
        // when running with certain mlat-server versions, set altitude to inreliable when we get them too infrequently
        score_add = -1;
    }

    if (accept_data(&a->baro_alt_valid, mm->source, mm, a, REDUCE_OFTEN)) {
        a->alt_reliable = imin(ALTITUDE_BARO_RELIABLE_MAX , a->alt_reliable + score_add);
        if (a->alt_reliable < 0) {
            a->alt_reliable = 0;
        }
        if (0 && a->addr == Modes.trace_focus && abs(delta) > -1) {
            fprintf(stdout, "Alt check S: %06x: %2d %6d ->%6d, %s->%s, min %.1f kfpm, max %.1f kfpm, actual %.1f kfpm\n",
                    a->addr, a->alt_reliable, a->baro_alt, alt,
                    source_string(a->baro_alt_valid.source),
                    source_string(mm->source),
                    min_fpm/1000.0, max_fpm/1000.0, fpm/1000.0);
        }
        a->baro_alt = alt;
    }
    return;
discard_alt:
    a->alt_reliable = a->alt_reliable - (good_crc+1);
    if (Modes.debug_bogus
            && trackDataAge(now, &a->baro_rate_valid) < 20 * SECONDS
            && trackDataAge(now, &a->baro_alt_valid) < 20 * SECONDS
       ) {
        fprintf(stdout, "%6llx %5.1f Alt check F: %06x %2d %6d ->%6d, %s->%s, min %.1f kfpm, max %.1f kfpm, actual %.1f kfpm\n",
                (long long) mm->timestamp % 0x1000000,
                10 * log10(mm->signalLevel),
                a->addr, a->alt_reliable, a->baro_alt, alt,
                source_string(a->baro_alt_valid.source),
                source_string(mm->source),
                min_fpm/1000.0, max_fpm/1000.0, fpm/1000.0);
    }
    if (a->alt_reliable <= 0) {
        //fprintf(stdout, "Altitude INVALIDATED: %06x\n", a->addr);
        a->alt_reliable = 0;
        if (a->pos_reliable_valid.source != SOURCE_JAERO)
            a->baro_alt_valid.source = SOURCE_INVALID;
    }
    if (Modes.garbage_ports)
        mm->source = SOURCE_INVALID;
    return;
}

//
//=========================================================================
//
// Receive new messages and update tracked aircraft state
//

static void processReceiverId(struct modesMessage *mm, struct aircraft *a) {
    // Append new seen by receiver id entry or update an existing entry
    if (mm->client) {
        struct seenByReceiverIdLlEntry *current = NULL;
        // This is the first entry
        if (a->seenByReceiverIds == NULL) {
            current = calloc(1, sizeof(struct seenByReceiverIdLlEntry));
            a->seenByReceiverIds = current;
        } else {
            // Find an existing entry for the same receiver id or append a new entry
            current = a->seenByReceiverIds;
            bool match = false;
            while (current) {
                if (current->receiverId == mm->client->receiverId && current->receiverId2 == mm->client->receiverId2) {
                    match = true;
                    break;
                } else if (!current->next) {
                    break;
                }

                current = current->next;
            }

            if (!match) {
                current->next = calloc(1, sizeof(struct seenByReceiverIdLlEntry));
                current = current->next;
            }
        }

        // Set receiver id and timestamp
        current->receiverId = mm->client->receiverId;
        current->receiverId2 = mm->client->receiverId2;
        current->lastTimestamp = mm->sysTimestamp;
    }
}

struct aircraft *trackUpdateFromMessage(struct modesMessage *mm) {
    ++Modes.stats_current.messages_total;
    if (mm->msgtype == DFTYPE_MODEAC) {
        // Mode A/C, just count it (we ignore SPI)
        modeAC_count[modeAToIndex(mm->squawk)]++;
        return NULL;
    }
    if (mm->decodeResult < 0)
        return NULL;

    int64_t now = mm->sysTimestamp;

    struct aircraft *a;
    unsigned int cpr_new = 0;
    mm->calculated_track = -1;

    if (CHECK_APPROXIMATIONS) {
        // great circle random testing stuff ...
        for (int i = 0; i < 100; i++) {
            double la1 = 2 * random() / (double) INT_MAX - 1;
            double la2 = 2 * random() / (double) INT_MAX - 1;
            double lo1 = 2 * random() / (double) INT_MAX - 1;
            double lo2 = 2 * random() / (double) INT_MAX - 1;
            la1 *= 90;
            lo1 *= 180;
            la2 = la1 + 90 * la2;
            lo2 = lo1 + 90 * lo2;
            if (greatcircle(la1, lo1, la2, lo2, 0)) {
            }
            if (bearing(la1, lo1, la2, lo2)) {
            }
        }
    }

    int address_reliable = addressReliable(mm);

    // Lookup our aircraft or create a new one
    a = aircraftGet(mm->addr);
    if (!a) { // If it's a currently unknown aircraft....
        if (address_reliable) {
            a = aircraftCreate(mm->addr); // ., create a new record for it,
        } else {
            //fprintf(stderr, "%06x: !a && !addressReliable(mm)\n", mm->addr);
            return NULL;
        }
    }

    struct aircraft scratch;
    bool haveScratch = false;
    if (mm->cpr_valid || mm->sbs_pos_valid) {
        memcpy(&scratch, a, offsetof(struct aircraft, traceCache));
        haveScratch = true;
        // messages from receivers classified garbage with position get processed to see if they still send garbage
    } else if (mm->garbage) {
        return NULL;
    }

    // only count the aircraft as "seen" for reliable messages with CRC
    if (address_reliable) {
        a->seen = now;
    }

    // don't use messages with unreliable CRC too long after receiving a reliable address from an aircraft
    if (now - a->seen > 45 * SECONDS) {
        return NULL;
    }

    if (Modes.json_dir && Modes.aircraft_json_seen_by_list)
        processReceiverId(mm, a);

    a->messageRateAcc[0]++;
    if (now > a->nextMessageRateCalc) {
        calculateMessageRate(a, now);
    }

    if (mm->signalLevel > 0) {

        a->signalLevel[a->signalNext % 8] = mm->signalLevel;
        a->signalNext++;
        //fprintf(stderr, "%0.4f\n",mm->signalLevel);

        a->lastSignalTimestamp = now;
    } else {
        //fprintf(stderr, "signal zero: %06x; %s\n", a->addr, source_string(mm->source));
        // if we haven't received a message with signal level for a bit, set it to zero
        if (now - a->lastSignalTimestamp > 15 * SECONDS) {
            a->signalNext = 0;
            //fprintf(stderr, "no_sig_thresh: %06x; %d; %d\n", a->addr, (int) a->no_signal_count, (int) a->signalNext);
        }
    }

    // reset to 100000 on overflow ... avoid any low message count checks
    if (a->messages == UINT32_MAX)
        a->messages = UINT16_MAX + 1;

    a->messages++;

    if (mm->client) {
        if (!mm->garbage) {
            mm->client->messageCounter++;
        }
        mm->client->recentMessages++;
    }

    // update addrtype
    float newType = mm->addrtype == ADDR_MODE_S ? 4.5 : mm->addrtype;
    float oldType = a->addrtype == ADDR_MODE_S ? 4.5 : a->addrtype;
    // change type ranking without messing with enum :/
    if (
            (newType <= oldType && now - a->addrtype_updated > TRACK_EXPIRE * 3 / 4)
            || (newType > oldType && now - a->addrtype_updated > TRACK_EXPIRE * 3 / 2)
       ) {

        if (mm->addrtype == ADDR_ADSB_ICAO && a->position_valid.source != SOURCE_ADSB) {
            // don't set to ADS-B without a position
        } else {
            a->addrtype = mm->addrtype;
            a->addrtype_updated = now;
        }

        if (a->addrtype > ADDR_ADSB_ICAO_NT) {
            a->adsb_version = -1; // reset ADS-B version if a non ADS-B message type is received
        }
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

    if (mm->category_valid) {
        a->category = mm->category;
        a->category_updated = now;
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

    if (mm->baro_alt_valid &&
            (mm->source >= a->baro_alt_valid.source
             || Modes.debug_bogus
             || (trackDataAge(now, &a->baro_alt_valid) > 10 * SECONDS
                 && a->baro_alt_valid.source != SOURCE_JAERO
                 && a->baro_alt_valid.source != SOURCE_SBS)
             || trackDataAge(now, &a->baro_alt_valid) > 30 * SECONDS
            )
       ) {
        updateAltitude(now, a, mm);
    }

    if (mm->squawk_valid) {
        uint32_t oldsquawk = a->squawk;

        int changeTentative = 0;
        if (a->squawkTentative != mm->squawk && now - a->seen < 15 * SECONDS && will_accept_data(&a->squawk_valid, mm->source, mm, a)) {
            a->squawk_valid.next_reduce_forward = now + currentReduceInterval(now);
            mm->reduce_forward = 1;
            changeTentative = 1;
        }
        if (a->squawkTentative == mm->squawk && now - a->squawkTentativeChanged > 750 && accept_data(&a->squawk_valid, mm->source, mm, a, REDUCE_RARE)) {
            if (mm->squawk != a->squawk) {
                a->modeA_hit = 0;
            }
            a->squawk = mm->squawk;
        }
        if (changeTentative) {
            a->squawkTentative = mm->squawk;
            a->squawkTentativeChanged = now;
        }

        if (Modes.debug_squawk
                && (a->squawk == 0x7500 || a->squawk == 0x7600 || a->squawk == 0x7700
                    || a->squawkTentative == 0x7500 || a->squawkTentative == 0x7600 || a->squawkTentative == 0x7700
                    || oldsquawk == 0x7500 || oldsquawk == 0x7600 || oldsquawk == 0x7700)
           ) {
            char uuid[32]; // needs 18 chars and null byte
            sprint_uuid1(mm->receiverId, uuid);
            if (changeTentative) {
                if (1) {
                    fprintf(stderr, "%06x DF: %02d a->squawk: %04x tentative %04x (receiverId: %s)\n",
                            a->addr,
                            mm->msgtype,
                            a->squawk,
                            mm->squawk,
                            uuid);
                }
            } else {
                static int64_t antiSpam;
                if (now > antiSpam + 15 * SECONDS || oldsquawk != a->squawk) {
                    antiSpam = now;
                    fprintf(stderr, "%06x DF: %02d a->squawk: %04x   --->    %04x (receiverId: %s)\n",
                            a->addr,
                            mm->msgtype,
                            oldsquawk,
                            a->squawk,
                            uuid);
                }
            }
        }
    }

    if (mm->emergency_valid && accept_data(&a->emergency_valid, mm->source, mm, a, REDUCE_RARE)) {
        if (a->emergency != mm->emergency) {
            mm->reduce_forward = 1;
        }
        a->emergency = mm->emergency;
    }

    if (mm->geom_alt_valid && accept_data(&a->geom_alt_valid, mm->source, mm, a, REDUCE_OFTEN)) {
        a->geom_alt = altitude_to_feet(mm->geom_alt, mm->geom_alt_unit);
    }

    if (mm->geom_delta_valid && accept_data(&a->geom_delta_valid, mm->source, mm, a, REDUCE_OFTEN)) {
        a->geom_delta = mm->geom_delta;
    }

    if (mm->heading_valid) {
        heading_type_t htype = mm->heading_type;
        if (htype == HEADING_MAGNETIC_OR_TRUE) {
            htype = a->adsb_hrd;
        } else if (htype == HEADING_TRACK_OR_HEADING) {
            htype = a->adsb_tah;
        }

        if (htype == HEADING_GROUND_TRACK && accept_data(&a->track_valid, mm->source, mm, a, REDUCE_OFTEN)) {
            a->track = mm->heading;
        } else if (htype == HEADING_MAGNETIC) {
            double dec;
            int err = declination(a, &dec, now);
            if (accept_data(&a->mag_heading_valid, mm->source, mm, a, REDUCE_OFTEN)) {
                a->mag_heading = mm->heading;

                // don't accept more than 45 degree crab when deriving the true heading
                if (
                        (!trackDataValid(&a->track_valid) || fabs(norm_diff(mm->heading + dec - a->track, 180)) < 45)
                        && !err && accept_data(&a->true_heading_valid, SOURCE_INDIRECT, mm, a, REDUCE_OFTEN)
                   ) {
                    a->true_heading = norm_angle(mm->heading + dec, 180);
                    calc_wind(a, now);
                }
            }
        } else if (htype == HEADING_TRUE && accept_data(&a->true_heading_valid, mm->source, mm, a, REDUCE_OFTEN)) {
            a->true_heading = mm->heading;
        }
    }

    if (mm->track_rate_valid && accept_data(&a->track_rate_valid, mm->source, mm, a, REDUCE_OFTEN)) {
        a->track_rate = mm->track_rate;
    }

    if (mm->roll_valid && accept_data(&a->roll_valid, mm->source, mm, a, REDUCE_OFTEN)) {
        a->roll = mm->roll;
    }

    if (mm->gs_valid) {
        mm->gs.selected = (*message_version == 2 ? mm->gs.v2 : mm->gs.v0);
        if (accept_data(&a->gs_valid, mm->source, mm, a, REDUCE_OFTEN)) {
            a->gs = mm->gs.selected;
        }
    }

    if (mm->ias_valid && accept_data(&a->ias_valid, mm->source, mm, a, REDUCE_OFTEN)) {
        a->ias = mm->ias;
    }

    if (mm->tas_valid
            && !(trackDataValid(&a->ias_valid) && mm->tas < a->ias)
            && accept_data(&a->tas_valid, mm->source, mm, a, REDUCE_OFTEN)) {
        a->tas = mm->tas;
        calc_temp(a, now);
        calc_wind(a, now);
    }

    if (mm->mach_valid && accept_data(&a->mach_valid, mm->source, mm, a, REDUCE_OFTEN)) {
        a->mach = mm->mach;
        calc_temp(a, now);
    }

    if (mm->baro_rate_valid && accept_data(&a->baro_rate_valid, mm->source, mm, a, REDUCE_OFTEN)) {
        a->baro_rate = mm->baro_rate;
    }

    if (mm->geom_rate_valid && accept_data(&a->geom_rate_valid, mm->source, mm, a, REDUCE_OFTEN)) {
        a->geom_rate = mm->geom_rate;
    }

    if (
            mm->airground != AG_INVALID
            // only consider changing ground state if the message has information about ground state
            && (mm->source >= a->airground_valid.source || mm->source >= SOURCE_MODE_S_CHECKED || trackDataAge(now, &a->airground_valid) > 2 * TRACK_EXPIRE)
            // don't accept lower quality data until our state is 2 minutes old
            // usually lower quality data is allowed after 15 seconds by accept_data, this doesn't make sense for ground state
       ) {
        // If our current state is UNCERTAIN or INVALID, accept new data
        // If our current state is certain but new data is not, don't accept the new data
        if (a->airground == AG_INVALID || a->airground == AG_UNCERTAIN || mm->airground != AG_UNCERTAIN
                // also accept ground to air transitions for an uncertain ground state message if the plane is moving fast enough
                // and the baro altitude is in a reliable state
                || (mm->airground == AG_UNCERTAIN
                    && a->airground == AG_GROUND
                    && altBaroReliable(a)
                    && trackDataAge(now, &a->gs_valid) < 3 * SECONDS
                    && a->gs > 80
                   )
           ) {
            if (
                    (a->airground == AG_AIRBORNE || a->airground == AG_GROUND)
                    && (a->last_cpr_type == CPR_SURFACE || a->last_cpr_type == CPR_AIRBORNE)
                    && trackDataAge(now, &a->cpr_odd_valid) < 20 * SECONDS
                    && trackDataAge(now, &a->cpr_even_valid) < 20 * SECONDS
                    && now < a->seenPosReliable + 20 * SECONDS
                    && trackDataAge(now, &a->airground_valid) < 20 * SECONDS
               ) {
                // if we have very recent CPR / position data ...
                // those are more reliable in an aggregation situation,
                // ignore other airground status indication
            } else if (accept_data(&a->airground_valid, mm->source, mm, a, REDUCE_RARE)) {
                focusGroundstateChange(a, mm, 1, now, mm->airground);
                if (mm->airground != a->airground)
                    mm->reduce_forward = 1;
                a->airground = mm->airground;
            }
        }
    }

    if (mm->callsign_valid && accept_data(&a->callsign_valid, mm->source, mm, a, REDUCE_RARE)) {
        memcpy(a->callsign, mm->callsign, sizeof (a->callsign));
    }

    if (mm->nav.mcp_altitude_valid && accept_data(&a->nav_altitude_mcp_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->nav_altitude_mcp = mm->nav.mcp_altitude;
    }

    if (mm->nav.fms_altitude_valid && accept_data(&a->nav_altitude_fms_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->nav_altitude_fms = mm->nav.fms_altitude;
    }

    if (mm->nav.altitude_source != NAV_ALT_INVALID && accept_data(&a->nav_altitude_src_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->nav_altitude_src = mm->nav.altitude_source;
    }

    if (mm->nav.heading_valid && accept_data(&a->nav_heading_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->nav_heading = mm->nav.heading;
    }

    if (mm->nav.modes_valid && accept_data(&a->nav_modes_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->nav_modes = mm->nav.modes;
    }

    if (mm->nav.qnh_valid && accept_data(&a->nav_qnh_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->nav_qnh = mm->nav.qnh;
    }

    if (mm->alert_valid && accept_data(&a->alert_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->alert = mm->alert;
    }

    if (mm->spi_valid && accept_data(&a->spi_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->spi = mm->spi;
    }

    // CPR, even
    if (mm->cpr_valid && !mm->cpr_odd && accept_data(&a->cpr_even_valid, mm->source, mm, a, REDUCE_OFTEN)) {
        a->cpr_even_type = mm->cpr_type;
        a->cpr_even_lat = mm->cpr_lat;
        a->cpr_even_lon = mm->cpr_lon;
        compute_nic_rc_from_message(mm, a, &a->cpr_even_nic, &a->cpr_even_rc);
        cpr_new = 1;
        if (0 && a->addr == Modes.cpr_focus)
            fprintf(stderr, "E \n");
    }

    // CPR, odd
    if (mm->cpr_valid && mm->cpr_odd && accept_data(&a->cpr_odd_valid, mm->source, mm, a, REDUCE_OFTEN)) {
        a->cpr_odd_type = mm->cpr_type;
        a->cpr_odd_lat = mm->cpr_lat;
        a->cpr_odd_lon = mm->cpr_lon;
        compute_nic_rc_from_message(mm, a, &a->cpr_odd_nic, &a->cpr_odd_rc);
        cpr_new = 1;
        if (0 && a->addr == Modes.cpr_focus)
            fprintf(stderr, "O \n");
    }

    if (mm->cpr_valid) {
        cpr_duplicate_check(now, a, mm);
    }

    if (!mm->duplicate && !mm->garbage) {
        int setLastStatus = 0;
        if (mm->msgtype == 17) {
            if ((mm->metype >= 1 && mm->metype <= 4) || mm->metype >= 23) {
                setLastStatus = 1;
            }
        }
        if (mm->msgtype == 11) {
            setLastStatus = 1;
        }
        if (!mm->squawk_valid) {
            if ((mm->msgtype == 0 || mm->msgtype == 4 || mm->msgtype == 20) && a->airground == AG_GROUND) {
                setLastStatus = 1;
            }
        }

        if (setLastStatus) {
            a->lastStatusTs = now;
            if (now > a->next_reduce_forward_status) {
                a->next_reduce_forward_status = now + 4 * currentReduceInterval(now);
                mm->reduce_forward = 1;
            }
        }
    }



    if (mm->acas_ra_valid) {

        unsigned char *bytes = NULL;
        if (mm->msgtype == 16) {
            bytes = mm->MV;
        } else if (mm->metype == 28 && mm->mesub == 2) {
            bytes = mm->ME;
        } else {
            bytes = mm->MB;
        }

        if (bytes && (checkAcasRaValid(bytes, mm, 0))) {
            if (accept_data(&a->acas_ra_valid, mm->source, mm, a, REDUCE_RARE)) {
                mm->reduce_forward = 1;
                memcpy(a->acas_ra, bytes, sizeof(a->acas_ra));
                logACASInfoShort(mm->addr, bytes, a, mm, mm->sysTimestamp);
            }
        } else if (bytes && Modes.debug_ACAS
                && checkAcasRaValid(bytes, mm, 1)
                && (getbit(bytes, 9) || getbit(bytes, 27) || getbit(bytes, 28))) {
            // getbit checks for ARA/RAT/MTE, at least one must be set
            logACASInfoShort(mm->addr, bytes, a, mm, mm->sysTimestamp);
        }
    }

    if (mm->accuracy.sda_valid && accept_data(&a->sda_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->sda = mm->accuracy.sda;
    }

    if (mm->accuracy.nic_a_valid && accept_data(&a->nic_a_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->nic_a = mm->accuracy.nic_a;
    }

    if (mm->accuracy.nic_c_valid && accept_data(&a->nic_c_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->nic_c = mm->accuracy.nic_c;
    }

    if (mm->accuracy.nic_baro_valid && accept_data(&a->nic_baro_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->nic_baro = mm->accuracy.nic_baro;
    }

    if (mm->accuracy.nac_p_valid && accept_data(&a->nac_p_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->nac_p = mm->accuracy.nac_p;
    }

    if (mm->accuracy.nac_v_valid && accept_data(&a->nac_v_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->nac_v = mm->accuracy.nac_v;
    }

    if (mm->accuracy.sil_type != SIL_INVALID && accept_data(&a->sil_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->sil = mm->accuracy.sil;
        if (a->sil_type == SIL_INVALID || mm->accuracy.sil_type != SIL_UNKNOWN) {
            a->sil_type = mm->accuracy.sil_type;
        }
    }

    if (mm->accuracy.gva_valid && accept_data(&a->gva_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->gva = mm->accuracy.gva;
    }

    if (mm->accuracy.sda_valid && accept_data(&a->sda_valid, mm->source, mm, a, REDUCE_RARE)) {
        a->sda = mm->accuracy.sda;
    }

    // Now handle derived data

    // derive geometric altitude if we have baro + delta
    if (
            (a->baro_alt_valid.updated > a->geom_alt_valid.updated || a->geom_delta_valid.updated > a->geom_alt_valid.updated)
            && altBaroReliable(a)
            && compare_validity(&a->baro_alt_valid, &a->geom_alt_valid) > 0
            && trackDataValid(&a->geom_delta_valid)
            && a->geom_delta_valid.source >= a->geom_alt_valid.source) {
        // Baro is more recent than geometric, derive geometric from baro + delta
        mm->geom_alt = a->baro_alt + a->geom_delta;
        mm->geom_alt_unit = UNIT_FEET;
        mm->geom_alt_derived = 1;
        a->geom_alt = mm->geom_alt;
        combine_validity(&a->geom_alt_valid, &a->baro_alt_valid, &a->geom_delta_valid, now);
    }

    // to keep the barometric altitude consistent with geometric altitude, save a derived geom_delta if all data are current
    if (mm->geom_alt_valid
            && a->geom_alt_valid.updated > a->geom_delta_valid.updated
            && altBaroReliable(a)
            && trackDataAge(now, &a->baro_alt_valid) < 1 * SECONDS
            && accept_data(&a->geom_delta_valid, mm->source, mm, a, REDUCE_OFTEN)
       ) {
        combine_validity(&a->geom_delta_valid, &a->baro_alt_valid, &a->geom_alt_valid, now);
        a->geom_delta = a->geom_alt - a->baro_alt;
    }

    // If we've got a new cpr_odd or cpr_even
    if (cpr_new) {
        // this is in addition to the normal air / ground handling
        // making especially sure we catch surface -> airborne transitions
        if (mm->addrtype >= a->addrtype) {
            //if (a->last_cpr_type == CPR_SURFACE && mm->cpr_type == CPR_AIRBORNE
            if (mm->cpr_type == CPR_AIRBORNE
                    && (a->last_cpr_type == CPR_SURFACE || mm->airground == AG_AIRBORNE)
                    && accept_data(&a->airground_valid, mm->source, mm, a, REDUCE_RARE)) {
                focusGroundstateChange(a, mm, 2, now, AG_AIRBORNE);
                if (mm->airground != a->airground)
                    mm->reduce_forward = 1;
                a->airground = AG_AIRBORNE;
            }
            // old or shitty transponders can continue sending CPR_AIRBORNE while on the ground
            // but the risk to wrongly show good transponders on the ground is too damn great
            // thus set AG_UNCERTAIN if we get CPR_AIRBORNE and are currently AG_GROUND
            if (mm->cpr_type == CPR_AIRBORNE
                    && mm->airground == AG_UNCERTAIN
                    && a->airground == AG_GROUND
                    && accept_data(&a->airground_valid, mm->source, mm, a, REDUCE_RARE)) {
                focusGroundstateChange(a, mm, 2, now, AG_UNCERTAIN);
                if (mm->airground != a->airground)
                    mm->reduce_forward = 1;
                a->airground = AG_UNCERTAIN;
            }
            if (mm->cpr_type == CPR_SURFACE
                    && accept_data(&a->airground_valid, mm->source, mm, a, REDUCE_RARE)) {
                focusGroundstateChange(a, mm, 2, now, AG_GROUND);
                if (mm->airground != a->airground)
                    mm->reduce_forward = 1;
                a->airground = AG_GROUND;
            }
        }

        updatePosition(a, mm, now);
        if (0 && a->addr == Modes.cpr_focus) {
            fprintf(stderr, "%06x: age: odd %"PRIu64" even %"PRIu64"\n",
                    a->addr,
                    trackDataAge(mm->sysTimestamp, &a->cpr_odd_valid),
                    trackDataAge(mm->sysTimestamp, &a->cpr_even_valid));
        }
    }

    if (mm->sbs_in && mm->sbs_pos_valid) {
        int old_jaero = 0;
        if (mm->source == SOURCE_JAERO && a->trace_len > 0) {
            for (int i = imax(0, a->trace_current_len - 10); i < a->trace_current_len; i++) {
                if ( (int32_t) (mm->decoded_lat * 1E6) == getState(a->trace_current, i)->lat
                        && (int32_t) (mm->decoded_lon * 1E6) == getState(a->trace_current, i)->lon )
                    old_jaero = 1;
            }
        }
        if (old_jaero) {
            // avoid using already received positions for JAERO input
        } else if (mm->source == SOURCE_MLAT && mm->mlatEPU > 2 * a->mlatEPU
                && imin((int)(3000.0f * logf((float)mm->mlatEPU / (float)a->mlatEPU)), TRACE_STALE * 3 / 4) > (int64_t) trackDataAge(mm->sysTimestamp, &a->pos_reliable_valid)
                ) {
            // don't use less accurate MLAT positions unless some time has elapsed
            // only works with SBS input MLAT data coming from some versions of mlat-server
        } else {
            if (mm->source == SOURCE_MLAT && accept_data(&a->mlat_pos_valid, mm->source, mm, a, REDUCE_OFTEN)) {
                if (0 && greatcircle(a->mlat_lat, a->mlat_lon, mm->decoded_lat, mm->decoded_lon, 1) > 5000) {
                    a->mlat_pos_valid.source = SOURCE_INVALID;
                }
                a->mlat_lat = mm->decoded_lat;
                a->mlat_lon = mm->decoded_lon;
                if (mm->mlatEPU) {
                    a->mlatEPU += 0.5 * mm->mlatEPU - a->mlatEPU;
                }
                if (0 && a->pos_reliable_valid.source > SOURCE_MLAT) {
                    fprintf(stderr, "%06x: %d\n", a->addr, mm->reduce_forward);
                }
            }
            int usePosition = 0;
            if (mm->source == SOURCE_MLAT && now - a->seenPosReliable > TRACK_STALE) {
                usePosition = 1;

                // pretend it's been very long since we updated the position to guarantee accept_data to update position_valid
                a->position_valid.updated = 0;
                // force accept_data
                accept_data(&a->position_valid, mm->source, mm, a, REDUCE_OFTEN);
            } else if (!speed_check(a, mm->source, mm->decoded_lat, mm->decoded_lon, mm, CPR_NONE)) {
                mm->pos_bad = 1;
                // speed check failed, do nothing
            } else if (accept_data(&a->position_valid, mm->source, mm, a, REDUCE_OFTEN)) {
                usePosition = 1;
            }
            if (usePosition) {

                incrementReliable(a, mm, now, 2);

                setPosition(a, mm, now);

                if (a->messages < 2)
                    a->messages = 2;
            }
        }
    }

    if (mm->msgtype == 11 && mm->IID == 0 && mm->correctedbits == 0) {
        double reflat;
        double reflon;
        struct receiver *r = receiverGetReference(mm->receiverId, &reflat, &reflon, a, 1);
        if (r) {
            if (now - a->rr_seen < 600 * SECONDS && fabs(a->lon - reflon) < 5 && fabs(a->lon - reflon) < 5) {
                a->rr_lat = 0.1 * reflat + 0.9 * a->rr_lat;
                a->rr_lon = 0.1 * reflon + 0.9 * a->rr_lon;
            } else {
                a->rr_lat = reflat;
                a->rr_lon = reflon;
            }
            a->rr_seen = now;
            if (Modes.debug_rough_receiver_location) {
                if (
                        (a->position_valid.last_source == SOURCE_INDIRECT && trackDataAge(now, &a->position_valid) > TRACK_EXPIRE_ROUGH - 30 * SECONDS)
                        || (a->position_valid.last_source != SOURCE_INDIRECT && a->position_valid.source == SOURCE_INVALID)
                   ) {
                    if (accept_data(&a->position_valid, SOURCE_INDIRECT, mm, a, REDUCE_OFTEN)) {
                        a->addrtype_updated = now;
                        a->addrtype = ADDR_MODE_S;
                        if (0 && a->position_valid.last_source > SOURCE_INDIRECT && a->position_valid.source == SOURCE_INVALID) {
                            mm->decoded_lat = a->lat;
                            mm->decoded_lon = a->lon;
                        } else {
                            mm->decoded_lat = a->rr_lat;
                            mm->decoded_lon = a->rr_lon;
                        }
                        set_globe_index(a, globe_index(mm->decoded_lat, mm->decoded_lon));
                        setPosition(a, mm, now);
                    }
                }
            }
        }
    }

    if (mm->msgtype == 17) {
        int oldNogpsCounter = a->nogpsCounter;
        if (
                // no position info or uncertainty >= 4 nmi
                (mm->metype == 0 || (mm->accuracy.nac_p_valid && mm->accuracy.nac_p <= 2) )
                && a->gs > 50
                && a->airground != AG_GROUND
                && now < a->seenAdsbReliable + NOGPS_DWELL
                && now > a->seenAdsbReliable + 10 * SECONDS
                && a->nogpsCounter < NOGPS_MAX
           ) {
            a->nogpsCounter++;
        }
        if (a->nogpsCounter > 0) {
            if (now > a->seenAdsbReliable + NOGPS_DWELL) {
                a->nogpsCounter = 0;
            }

            // position info during last 10 seconds and uncertainty <= 1 nmi
            if (mm->source == SOURCE_ADSB && mm->accuracy.nac_p_valid && mm->accuracy.nac_p >= 4
                    && now < a->seenAdsbReliable + 10 * SECONDS) {
                a->nogpsCounter--;
            }
        }
        if (Modes.debug_nogps && oldNogpsCounter != a->nogpsCounter) {
            fprintf(stderr, "%06x nogps %d -> %d\n", a->addr, oldNogpsCounter, a->nogpsCounter);
        }
    }

    if (mm->msgtype == 11 && mm->IID == 0 && mm->correctedbits == 0) {
        if (Modes.net_output_json_include_nopos && now > a->nextJsonPortOutput
                && now - a->seenPosReliable > 10 * SECONDS && now - a->seenPosReliable > 2 * Modes.net_output_json_interval) {
            a->nextJsonPortOutput = now + Modes.net_output_json_interval;
            mm->jsonPositionOutputEmit = 1;
        }
        // forward DF0/DF11 every 2 * beast_reduce_interval for beast_reduce
        if (now > a->next_reduce_forward_DF11) {
            a->next_reduce_forward_DF11 = now + 4 * currentReduceInterval(now);
            mm->reduce_forward = 1;
        }
    }

    if (cpr_new) {
        a->last_cpr_type = mm->cpr_type;
    }

    if (haveScratch && (mm->garbage || mm->pos_bad || mm->duplicate)) {
        memcpy(a, &scratch, offsetof(struct aircraft, traceCache));
    }

    if (!(mm->source < a->position_valid.source || mm->in_disc_cache || mm->garbage || mm->pos_ignore || mm->pos_receiver_range_exceeded)) {
        if (mm->pos_bad) {
            position_bad(mm, a);
        }
        a->speedUnreliable += mm->speedUnreliable;
        a->trackUnreliable += mm->trackUnreliable;
        a->speedUnreliable = imax(0, imin(16, a->speedUnreliable));
        a->trackUnreliable = imax(0, imin(16, a->trackUnreliable));
    }

    if (!a->onActiveList && includeAircraftJson(now, a)) {
        updateValidities(a, now);
        ca_add(&Modes.aircraftActive, a);
        a->onActiveList = 1;
        //fprintf(stderr, "active len %d\n", Modes.aircraftActive.len);
    }

    if (mm->reduce_forward) {
        // don't reduce forward duplicate / garbage / bad positions when garbage ports is active
        if ((mm->duplicate || mm->garbage || mm->pos_bad) && Modes.garbage_ports) {
            mm->reduce_forward = 0;
        }
        if (Modes.beast_reduce_filter_distance != -1
                && now < a->seenPosReliable + 1 * MINUTES
                && Modes.userLocationValid
                && greatcircle(Modes.fUserLat, Modes.fUserLon, a->latReliable, a->lonReliable, 0) > Modes.beast_reduce_filter_distance) {
            mm->reduce_forward = 0;
            //fprintf(stderr, "%.0f %0.f\n", greatcircle(Modes.fUserLat, Modes.fUserLon, a->latReliable, a->lonReliable, 0) / 1852.0, Modes.beast_reduce_filter_distance / 1852.0);
        }
        if (Modes.beast_reduce_filter_altitude != -1
                && altBaroReliable(a)
                && a->airground != AG_GROUND
                && a->baro_alt > Modes.beast_reduce_filter_altitude) {
            mm->reduce_forward = 0;
            //fprintf(stderr, "%.0f %.0f\n", (double) a->baro_alt, Modes.beast_reduce_filter_altitude);
        }
    }

    // forward all CPRs to the apex for faster garbage detection and such
    // even the duplicates and the garbage
    if (Modes.netIngest && mm->cpr_valid) {
        mm->reduce_forward = 1;
    }

    mm->aircraft = a;
    return (a);
}

//
// Periodic updates of tracking state
//

// Periodically match up mode A/C results with mode S results

void trackMatchAC(int64_t now) {
    // clear match flags
    for (unsigned i = 0; i < 4096; ++i) {
        modeAC_match[i] = 0;
    }

    // scan aircraft list, look for matches
    struct craftArray *ca = &Modes.aircraftActive;
    struct aircraft *a;

    ca_lock_read(ca);
    for (int i = 0; i < ca->len; i++) {
        a = ca->list[i];
        if (a == NULL) continue;
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
        if (trackDataValid(&a->baro_alt_valid)) {
            int modeC = (a->baro_alt + 49) / 100;

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
    ca_unlock_read(ca);

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

static void removeStaleRange(void *arg, threadpool_threadbuffers_t * buffer_group) {
    task_info_t *info = (task_info_t *) arg;

    int64_t now = info->now;
    //fprintf(stderr, "%9d %9d %9d\n", info->from, info->to, AIRCRAFT_BUCKETS);

    // timeout for aircraft with position
    int64_t posTimeout = now - 1 * HOURS;

    // timeout for non-ICAO aircraft with position
    int64_t nonIcaoPosTimeout = now - 30 * MINUTES;

    if (Modes.json_globe_index) {
        posTimeout = now - 26 * HOURS;
        nonIcaoPosTimeout = now - 26 * HOURS;
    }
    if (Modes.state_dir && !Modes.userLocationValid) {
        posTimeout = now - 6 * 28 * 24 * HOURS; // 6 months
        nonIcaoPosTimeout = now - 26 * HOURS;
    }
    if (Modes.debug_rough_receiver_location) {
        posTimeout = now - 2 * 24 * HOURS;
        nonIcaoPosTimeout = now - 26 * HOURS;
    }

    // timeout for aircraft without position
    int64_t noposTimeout = now - 5 * MINUTES;

    for (int j = info->from; j < info->to; j++) {
        struct aircraft **nextPointer = &(Modes.aircraft[j]);
        while (*nextPointer) {
            struct aircraft *a = *nextPointer;

            if (!includeAircraftJson(info->now, a))
                clearAircraftSeenByList(a);

            if (
                    (!a->seenPosReliable && a->seen < noposTimeout)
                    || (
                        a->seenPosReliable &&
                        (a->seenPosReliable < posTimeout || ((a->addr & MODES_NON_ICAO_ADDRESS) && a->seenPosReliable < nonIcaoPosTimeout))
                       )
               ) {
                // Count aircraft where we saw only one message before reaping them.
                // These are likely to be due to messages with bad addresses.
                if (a->messages == 1)
                    Modes.stats_current.single_message_aircraft++;

                if (a->addr == Modes.cpr_focus)
                    fprintf(stderr, "del: %06x seen: %.1f seen_pos: %.1f\n", a->addr, (now - a->seen) / 1000.0, (now - a->seen_pos) / 1000.0);

                // Remove the element from the linked list
                *nextPointer = a->next;

                freeAircraft(a);

            } else {
                traceMaintenance(a, now, &buffer_group->buffers[0]);

                nextPointer = &(a->next);
            }
        }
    }
}

static void activeUpdateRange(void *arg, threadpool_threadbuffers_t * buffer_group) {
    task_info_t *info = (task_info_t *) arg;
    int64_t now = info->now;
    struct craftArray *ca = &Modes.aircraftActive;

    for (int i = info->from; i < info->to; i++) {
        struct aircraft *a = ca->list[i];
        if (!a) {
            continue;
        }
        updateValidities(a, now);
        traceMaintenance(a, now, &buffer_group->buffers[0]);
    }
    //fprintf(stderr, "%9d %9d %9d\n", info->from, info->to, ca->len);
}

// update active Aircraft
static void activeUpdate(int64_t now) {
    struct craftArray *ca = &Modes.aircraftActive;
    pthread_mutex_lock(&ca->change_mutex);
    for (int i = 0; i < ca->len; i++) {

        struct aircraft *a = ca->list[i];
        if (!a) {
            continue;
        }

        if (!includeAircraftJson(now, a) && now - a->seen > TRACK_EXPIRE_LONG + 1 * MINUTES) {
            a->onActiveList = 0;

            if (a->globe_index >= 0) {
                set_globe_index(a, -5);
            }

            // we have the lock and are already scannign the array, remove without ca_remove()
            // also keep this array compact

            if (i == ca->len - 1) {
                ca->list[i] = NULL;
                ca->len--;
            } else if (ca->list[ca->len - 1]) {
                ca->list[i] = ca->list[ca->len - 1];
                ca->list[ca->len - 1] = NULL;
                ca->len--;
                i--; // take a step back
                continue;
            }
        }
    }
    quickInit();
    pthread_mutex_unlock(&ca->change_mutex);
}

// run activeUpdate and remove stale aircraft for a fraction of the entire hashtable
void trackRemoveStale(int64_t now) {
    // update the active aircraft list
    //fprintf(stderr, "activeUpdate\n");
    activeUpdate(now);

    int taskCount;
    threadpool_task_t *tasks;
    task_info_t *infos;

    taskCount = imin(Modes.allPoolSize, Modes.allTasks->task_count);
    tasks = Modes.allTasks->tasks;
    infos = Modes.allTasks->infos;

    // tasks to maintain validities / traces in active list
    struct craftArray *ca = &Modes.aircraftActive;
    // we must not lock ca here as aircraft freeing locks it

    int section_len = ca->len / taskCount;
    int extra = ca->len % taskCount;
    // assign tasks
    for (int i = 0; i < taskCount; i++) {
        threadpool_task_t *task = &tasks[i];
        task_info_t *range = &infos[i];

        range->now = now;
        range->from = i * section_len + imin(extra, i);
        range->to = range->from + section_len + (i < extra ? 1 : 0);

        if (range->to > ca->len || (i == taskCount - 1 && range->to != ca->len)) {
            range->to = ca->len;
            fprintf(stderr, "check trackRemoveStale distribution\n");
        }

        task->function = activeUpdateRange;
        task->argument = range;

        //fprintf(stderr, "%d %d\n", range->from, range->to);
    }

    ca_lock_read(ca);
    threadpool_run(Modes.allPool, tasks, taskCount);
    ca_unlock_read(ca);


    // don't mix tasks above and below
    // don't mix tasks above and below
    // don't mix tasks above and below


    // tasks to maintain aircraft in list of all aircraft

    static int part = 0;
    int n_parts = 32 * taskCount;

    section_len = AIRCRAFT_BUCKETS / n_parts;
    extra = AIRCRAFT_BUCKETS % n_parts;


    //fprintf(stderr, "part %d\n", part);

    // assign tasks
    for (int i = 0; i < taskCount; i++) {
        threadpool_task_t *task = &tasks[i];
        task_info_t *range = &infos[i];

        range->now = now;

        range->from = part * section_len + imin(extra, part);
        range->to = range->from + section_len + (part < extra ? 1 : 0);

        if (range->to > AIRCRAFT_BUCKETS || (part == n_parts - 1 && range->to != AIRCRAFT_BUCKETS)) {
            range->to = AIRCRAFT_BUCKETS;
            fprintf(stderr, "check trackRemoveStale distribution\n");
        }

        task->function = removeStaleRange;
        task->argument = range;

        //fprintf(stderr, "%d %d\n", range->from, range->to);

        if (++part >= n_parts) {
            part = 0;
        }
    }
    //fprintf(stderr, "removeStaleRange start\n");

    // run tasks
    threadpool_run(Modes.allPool, tasks, taskCount);

    //fprintf(stderr, "removeStaleRange done\n");
}

/*
static void adjustExpire(struct aircraft *a, int64_t timeout) {
#define F(f,s,e) do { a->f##_valid.stale_interval = (s) * 1000; a->f##_valid.expire_interval = (e) * 1000; } while (0)
    F(callsign, 60,  timeout); // ADS-B or Comm-B
    F(baro_alt, 15,  timeout); // ADS-B or Mode S
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

static void calc_wind(struct aircraft *a, int64_t now) {
    uint32_t focus = 0xc0ffeeba;

    if (a->addr == focus)
        fprintf(stderr, "%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64"\n", trackDataAge(now, &a->tas_valid), trackDataAge(now, &a->true_heading_valid),
                trackDataAge(now, &a->gs_valid), trackDataAge(now, &a->track_valid));

    if (!trackDataValid(&a->position_valid) || a->airground == AG_GROUND)
        return;

    if (now < a->wind_updated + 1 * SECONDS) {
        // don't do wind calculation more often than necessary, precision isn't THAT good anyhow
        return;
    }

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
        if (last->track_valid && track_diff > 0.5)
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
    a->wind_altitude = a->baro_alt;
}
static void calc_temp(struct aircraft *a, int64_t now) {
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

static inline int declination(struct aircraft *a, double *dec, int64_t now) {
    // only update delination every 30 seconds (per plane)
    // it doesn't change that much assuming the plane doesn't move huge distances in that time
    if (now < a->updatedDeclination + 5 * SECONDS) {
        *dec = a->magneticDeclination;
        return 0;
    }

    double year;
    time_t now_t = now/1000;

    struct tm utc;
    gmtime_r(&now_t, &utc);

    year = 1900.0 + utc.tm_year + utc.tm_yday / 365.0;

    double dip;
    double ti;
    double gv;

    int res = geomag_calc(a->baro_alt * 0.0003048, a->lat, a->lon, year, dec, &dip, &ti, &gv);
    if (res) {
        *dec = 0.0;
    } else {
        a->updatedDeclination = now;
        a->magneticDeclination = *dec;
    }
    return res;
}

/*
{
  int64_t timestamp:48;
  // 16 bits of flags

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
  int padding:3;
#if defined(TRACKS_UUID)
  uint64_t receiverId;
  */
void to_state(struct aircraft *a, struct state *new, int64_t now, int on_ground, float track, int stale) {
    memset(new, 0, sizeof(struct state));

    new->timestamp = now;

    new->lat = (int32_t) nearbyint(a->latReliable * 1E6);
    new->lon = (int32_t) nearbyint(a->lonReliable * 1E6);

    new->stale = stale;

    if (on_ground)
        new->on_ground = 1;

    if (trackVState(now, &a->gs_valid, &a->pos_reliable_valid)) {
        new->gs_valid = 1;
        new->gs = (uint16_t) nearbyint(a->gs * _gs_factor);
    }
    if (track > -1) {
        new->track = (uint16_t) nearbyint(track * _track_factor);
        new->track_valid = 1;
    }
    if (altBaroReliableTrace(now, a)) {
        new->baro_alt_valid = 1;
        new->baro_alt = (int16_t) nearbyint(a->baro_alt * _alt_factor);
    }

    if (trackVState(now, &a->baro_rate_valid, &a->pos_reliable_valid)) {
        new->baro_rate_valid = 1;
        new->baro_rate = (int16_t) nearbyint(a->baro_rate * _rate_factor);
    }

    if (trackVState(now, &a->geom_alt_valid, &a->pos_reliable_valid)) {
        new->geom_alt_valid = 1;
        new->geom_alt = (int16_t) nearbyint(a->geom_alt * _alt_factor);
    }
    if (trackVState(now, &a->geom_rate_valid, &a->pos_reliable_valid)) {
        new->geom_rate_valid = 1;
        new->geom_rate = (int16_t) nearbyint(a->geom_rate * _rate_factor);
    }

    /*
    unsigned ias:12;
    int roll:12;
    addrtype_t addrtype:5;
    */

    if (trackVState(now, &a->ias_valid, &a->pos_reliable_valid)) {
        new->ias = a->ias;
        new->ias_valid = 1;
    }
    if (trackVState(now, &a->roll_valid, &a->pos_reliable_valid)) {
        new->roll = (int16_t) nearbyint(a->roll * _roll_factor);
        new->roll_valid = 1;
    }

    new->addrtype = a->addrtype;

#if defined(TRACKS_UUID)
    new->receiverId = (uint32_t) (a->lastPosReceiverId >> 32);
#endif
}

void to_state_all(struct aircraft *a, struct state_all *new, int64_t now) {
    memset(new, 0, sizeof(struct state_all));
    for (int i = 0; i < 8; i++)
        new->callsign[i] = a->callsign[i];

    new->pos_nic = a->pos_nic_reliable;
    new->pos_rc = a->pos_rc_reliable;

    new->tas = a->tas;

    new->squawk = a->squawk;
    new->category = a->category; // Aircraft category A0 - D7 encoded as a single hex byte. 00 = unset
    new->nav_altitude_mcp = (int16_t) nearbyint(a->nav_altitude_mcp / 4.0f);
    new->nav_altitude_fms = (int16_t) nearbyint(a->nav_altitude_fms / 4.0f);

    new->nav_qnh = (int16_t) nearbyint(a->nav_qnh * 10.0f);
    new->mach = (int16_t) nearbyint(a->mach * 1000.0f);

    new->track_rate = (int16_t) nearbyint(a->track_rate * 100.0f);

    new->mag_heading = (uint16_t) nearbyint(a->mag_heading * _track_factor);
    new->true_heading = (uint16_t) nearbyint(a->true_heading * _track_factor);
    new->nav_heading = (uint16_t) nearbyint(a->nav_heading * _track_factor);

    new->emergency = a->emergency;
    new->airground = a->airground;
    new->nav_modes = a->nav_modes;
    new->nav_altitude_src = a->nav_altitude_src;
    new->sil_type = a->sil_type;

    if (now < a->wind_updated + TRACK_EXPIRE && abs(a->wind_altitude - a->baro_alt) < 500) {
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

    new->position_valid = trackDataValid(&a->pos_reliable_valid);

#define F(f) do { new->f = trackVState(now, &a->f, &a->pos_reliable_valid); } while (0)
    F(callsign_valid);
    F(tas_valid);
    F(mach_valid);
    F(track_rate_valid);
    F(mag_heading_valid);
    F(true_heading_valid);
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
    F(alert_valid);
    F(spi_valid);
#undef F
}

void from_state_all(struct state_all *in, struct state *in2, struct aircraft *a , int64_t ts) {
    for (int i = 0; i < 8; i++)
        a->callsign[i] = in->callsign[i];
    a->callsign[8] = '\0';

    a->pos_nic = in->pos_nic;
    a->pos_rc = in->pos_rc;

    a->pos_nic_reliable = in->pos_nic;
    a->pos_rc_reliable = in->pos_rc;

    a->tas = in->tas;

    a->squawk = in->squawk;
    a->category =  in->category; // Aircraft category A0 - D7 encoded as a single hex byte. 00 = unset
    a->nav_altitude_mcp = in->nav_altitude_mcp * 4.0f;
    a->nav_altitude_fms = in->nav_altitude_fms * 4.0f;

    a->nav_qnh = in->nav_qnh / 10.0f;
    a->mach = in->mach / 1000.0f;

    a->track_rate = in->track_rate / 100.0f;
    a->mag_heading = in->mag_heading / _track_factor;
    a->true_heading = in->true_heading / _track_factor;
    a->nav_heading = in->nav_heading / _track_factor;

    a->emergency = in->emergency;
    a->airground = in->airground;
    a->nav_modes = in->nav_modes;
    a->nav_altitude_src = in->nav_altitude_src;
    a->sil_type = in->sil_type;

    a->geom_alt = in2->geom_alt / _alt_factor;
    a->baro_alt = in2->baro_alt / _alt_factor;

    if (in->wind_valid) {
        a->wind_direction = in->wind_direction;
        a->wind_speed = in->wind_speed;
        a->wind_updated = ts - 5000;
        a->wind_altitude = a->baro_alt;
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

    a->addrtype = in2->addrtype;
    a->ias = in2->ias;

    a->baro_rate = in2->baro_rate / _rate_factor;
    a->geom_rate = in2->geom_rate / _rate_factor;

    a->gs = in2->gs / _gs_factor;
    a->roll = in2->roll / _roll_factor;
    a->track = in2->track / _track_factor;

    a->baro_alt_valid.source = (in2->baro_alt_valid ? SOURCE_INDIRECT : SOURCE_INVALID);
    a->baro_alt_valid.updated = ts - 5000;
    a->geom_alt_valid.source = (in2->geom_alt_valid ? SOURCE_INDIRECT : SOURCE_INVALID);
    a->geom_alt_valid.updated = ts - 5000;

#define F(f) do { a->f.source = (in2->f ? SOURCE_INDIRECT : SOURCE_INVALID); a->f.updated = ts - 5000; } while (0)
    F(baro_rate_valid);
    F(geom_rate_valid);
    F(ias_valid);
    F(roll_valid);
    F(gs_valid);
    F(track_valid);
#undef F

    a->pos_reliable_valid.source = a->position_valid.source = in->position_valid ? SOURCE_INDIRECT : SOURCE_INVALID;
    a->pos_reliable_valid.updated = a->position_valid.updated = ts - 5000;

    // giving this a timestamp is kinda hacky, do it anyway
    // we want to be able to reuse the sprintAircraft routine for printing aircraft details
#define F(f) do { a->f.source = (in->f ? SOURCE_INDIRECT : SOURCE_INVALID); a->f.updated = ts - 5000; } while (0)
    F(callsign_valid);
    F(tas_valid);
    F(mach_valid);
    F(track_rate_valid);
    F(mag_heading_valid);
    F(true_heading_valid);
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
    F(alert_valid);
    F(spi_valid);
#undef F
}

/*
static const char *cpr_string(cpr_type_t type) {
    switch (type) {
        case CPR_INVALID:  return "INVALID ";
        case CPR_SURFACE:  return "SURFACE ";
        case CPR_AIRBORNE: return "AIRBORNE";
        case CPR_COARSE:   return "COARSE  ";
        default:           return "ERR     ";
    }
}
*/
static const char *source_string(datasource_t source) {
    switch (source) {
        case SOURCE_INVALID:
            return "INVALID ";
        case SOURCE_INDIRECT:
            return "INDIRECT";
        case SOURCE_MODE_AC:
            return "MODE_AC ";
        case SOURCE_SBS:
            return "SBS     ";
        case SOURCE_MLAT:
            return "MLAT    ";
        case SOURCE_MODE_S:
            return "MODE_S  ";
        case SOURCE_JAERO:
            return "JAERO   ";
        case SOURCE_MODE_S_CHECKED:
            return "MODE_CH ";
        case SOURCE_TISB:
            return "TISB    ";
        case SOURCE_ADSR:
            return "ADSR    ";
        case SOURCE_ADSB:
            return "ADSB    ";
        case SOURCE_PRIO:
            return "PRIO    ";
        default:
            return "ERROR   ";
    }
}

void updateValidities(struct aircraft *a, int64_t now) {

    int64_t elapsed_seen_global = now - a->seenPosGlobal;

    if (Modes.json_globe_index && elapsed_seen_global < 45 * MINUTES) {
        a->receiverIdsNext = (a->receiverIdsNext + 1) % RECEIVERIDBUFFER;
        a->receiverIds[a->receiverIdsNext] = 0;
    }

    if (a->globe_index >= 0 && now > a->seen_pos + Modes.trackExpireMax) {
        set_globe_index(a, -5);
    }

    if (a->category != 0 && now > a->category_updated + Modes.trackExpireMax)
        a->category = 0;

    // reset position reliability when no position was received for 60 minutes
    if (a->pos_reliable_odd != 0 && a->pos_reliable_even != 0 && elapsed_seen_global > POS_RELIABLE_TIMEOUT) {
        a->pos_reliable_odd = 0;
        a->pos_reliable_even = 0;
    }
    if (a->tracePosBuffered && now > a->seenPosReliable + TRACE_STALE) {
        traceUsePosBuffered(a);
    }

    updateValidity(&a->baro_alt_valid, now, TRACK_EXPIRE);

    if (a->alt_reliable != 0 && a->baro_alt_valid.source == SOURCE_INVALID) {
        a->alt_reliable = 0;
    }

    updateValidity(&a->callsign_valid, now, TRACK_EXPIRE_LONG);
    updateValidity(&a->geom_alt_valid, now, TRACK_EXPIRE);
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
    updateValidity(&a->nic_a_valid, now, TRACK_EXPIRE);

    updateValidity(&a->nic_c_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nic_baro_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nac_p_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nac_v_valid, now, TRACK_EXPIRE);
    updateValidity(&a->sil_valid, now, TRACK_EXPIRE);
    updateValidity(&a->gva_valid, now, TRACK_EXPIRE);
    updateValidity(&a->sda_valid, now, TRACK_EXPIRE);
    updateValidity(&a->squawk_valid, now, TRACK_EXPIRE);

    updateValidity(&a->emergency_valid, now, TRACK_EXPIRE);
    updateValidity(&a->airground_valid, now, TRACK_EXPIRE_LONG);
    updateValidity(&a->nav_qnh_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nav_altitude_mcp_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nav_altitude_fms_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nav_altitude_src_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nav_heading_valid, now, TRACK_EXPIRE);
    updateValidity(&a->nav_modes_valid, now, TRACK_EXPIRE);

    updateValidity(&a->cpr_odd_valid, now, TRACK_EXPIRE);
    updateValidity(&a->cpr_even_valid, now, TRACK_EXPIRE);
    updateValidity(&a->position_valid, now, TRACK_EXPIRE);
    updateValidity(&a->alert_valid, now, TRACK_EXPIRE);
    updateValidity(&a->spi_valid, now, TRACK_EXPIRE);

    updateValidity(&a->acas_ra_valid, now, TRACK_EXPIRE);
    updateValidity(&a->mlat_pos_valid, now, TRACK_EXPIRE);
    updateValidity(&a->pos_reliable_valid, now, TRACK_EXPIRE);


    if (now > a->nextMessageRateCalc) {
        calculateMessageRate(a, now);
    }
}

static void showPositionDebug(struct aircraft *a, struct modesMessage *mm, int64_t now, double bad_lat, double bad_lon) {

    fprintf(stderr, "%06x ", a->addr);
    fprintf(stderr, "elapsed %4.1f ", (now - a->seen_pos) / 1000.0);
    if (mm->receiver_distance > 0) {
        fprintf(stderr, "receiver_distance: %7.1f nmi ", mm->receiver_distance / 1852.0);
    }

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
        fprintf(stderr,"lat: %11.6f (%u),"
                " lon: %11.6f (%u),"
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
        fprintf(stderr,"lat: %11.6f (%u),"
                " lon: %11.6f (%u),"
                " CPR decoding: failed",
                bad_lat,
                mm->cpr_lat,
                bad_lon,
                mm->cpr_lon);
    }
    fprintf(stderr, "\n");
}

static void incrementReliable(struct aircraft *a, struct modesMessage *mm, int64_t now, int odd) {
    a->seenPosGlobal = now;
    a->localCPR_allow_ac_rel = 1;

    float threshold = Modes.json_reliable;
    if (a->seenPosReliable && now - a->seenPosReliable > POS_RELIABLE_TIMEOUT && mm->source > SOURCE_JAERO
            && a->pos_reliable_odd < threshold && a->pos_reliable_even < threshold
            ) {
        double distance = greatcircle(a->latReliable, a->lonReliable, mm->decoded_lat, mm->decoded_lon, 0);
        // if aircraft is within 50 km of last reliable position, treat new position as reliable immediately.
        // pos_reliable is mostly to filter out bad decodes which usually show a much larger offset than 50km
        // it's very unlikely to get a bad decode that's in a range of 50 km of the last known position
        // at this point the position has already passed the speed check
        if (distance < 50e3) {
            a->pos_reliable_odd = fmaxf(1, threshold);
            a->pos_reliable_even = fmaxf(1, threshold);
            if (a->addr == Modes.cpr_focus)
                fprintf(stderr, "%06x: fast track json_reliable\n", a->addr);
        }
    }

    float increment = 1.0f;
    if (mm->pos_receiver_range_exceeded) {
        increment = 0.25f;
    }
    if (mm->source == SOURCE_SBS) {
        increment = 0.5f;
    }

    if (odd)
        a->pos_reliable_odd = fminf(a->pos_reliable_odd + increment, Modes.position_persistence);

    if (!odd || odd == 2)
        a->pos_reliable_even = fminf(a->pos_reliable_even + increment, Modes.position_persistence);

    if (a->pos_reliable_odd < increment) {
        a->pos_reliable_odd = increment;
    }
    if (a->pos_reliable_even < increment) {
        a->pos_reliable_even = increment;
    }
}

static void position_bad(struct modesMessage *mm, struct aircraft *a) {

    int64_t now = mm->sysTimestamp;

    if (mm->cpr_valid) {
        struct cpr_cache *disc;
        a->disc_cache_index = (a->disc_cache_index + 1) % DISCARD_CACHE;

        // most recent discarded position which led to decrementing reliability and timestamp (speed_check)
        disc = &a->disc_cache[a->disc_cache_index];
        disc->ts = now;
        disc->cpr_lat = mm->cpr_lat;
        disc->cpr_lon = mm->cpr_lon;
        disc->receiverId = mm->receiverId;
    }

    a->pos_reliable_odd -= 0.26f;
    a->pos_reliable_odd = fmax(0, a->pos_reliable_odd);
    a->pos_reliable_even -= 0.26f;
    a->pos_reliable_even = fmax(0, a->pos_reliable_even);


    if (0 && a->addr == Modes.cpr_focus)
        fprintf(stderr, "%06x: position_bad %.1f %.1f %u %u\n", a->addr, a->pos_reliable_odd, a->pos_reliable_even, mm->cpr_lat, mm->cpr_lon);
}

int nogps(int64_t now, struct aircraft *a) {
    return (a->nogpsCounter >= NOGPS_SHOW && now < a->seenAdsbReliable + NOGPS_DWELL && now > a->seenAdsbReliable + 15 * SECONDS);
}
