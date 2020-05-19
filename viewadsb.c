// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// view1090, a messages viewer for readsb backend.
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
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
// Copyright (c) 2014 by Malcolm Robb <Support@ATTAvionics.com>
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
//

#define VIEWADSB
#include "readsb.h"
#include "help.h"

#define _stringize(x) x
#define verstring(x) _stringize(x)

static error_t parse_opt(int key, char *arg, struct argp_state *state);
const char *argp_program_version = verstring(MODES_READSB_VARIANT " " MODES_READSB_VERSION);
const char doc[] = "readsb Mode-S/ADSB/TIS viewer - "
        verstring(MODES_READSB_VARIANT " " MODES_READSB_VERSION);
#undef _stringize
#undef verstring

const char args_doc[] = "";
static struct argp argp = {options, parse_opt, args_doc, doc, NULL, NULL, NULL};

char *bo_connect_ipaddr = "127.0.0.1";
char *bo_connect_port = "30005";

//
// ============================= Utility functions ==========================
//

void sigintHandler(int dummy) {
    MODES_NOTUSED(dummy);
    signal(SIGINT, SIG_DFL); // reset signal handler - bit extra safety
    Modes.exit = 1; // Signal to threads that we are done
}

void receiverPositionChanged(float lat, float lon, float alt) {
    /* nothing */
    (void) lat;
    (void) lon;
    (void) alt;
}

//
// =============================== Initialization ===========================
//

static void view1090InitConfig(void) {
    // Default everything to zero/NULL
    memset(&Modes, 0, sizeof (Modes));
    srand(get_seed());

    // Now initialise things that should not be 0/NULL to their defaults
    Modes.check_crc = 1;
    Modes.interactive_display_ttl = MODES_INTERACTIVE_DISPLAY_TTL;
    Modes.interactive = 1;
    Modes.maxRange = 1852 * 300; // 300NM default max range
    Modes.net_connector_delay = 1 * 1000;
}
//
//=========================================================================
//

static void view1090Init(void) {

    pthread_mutex_init(&Modes.data_mutex, NULL);
    pthread_cond_init(&Modes.data_cond, NULL);

#ifdef _WIN32
    if ((!Modes.wsaData.wVersion)
            && (!Modes.wsaData.wHighVersion)) {
        // Try to start the windows socket support
        if (WSAStartup(MAKEWORD(2, 1), &Modes.wsaData) != 0) {
            fprintf(stderr, "WSAStartup returned Error\n");
        }
    }
#endif

    // Validate the users Lat/Lon home location inputs
    if ((Modes.fUserLat > 90.0) // Latitude must be -90 to +90
            || (Modes.fUserLat < -90.0) // and
            || (Modes.fUserLon > 360.0) // Longitude must be -180 to +360
            || (Modes.fUserLon < -180.0)) {
        Modes.fUserLat = Modes.fUserLon = 0.0;
    } else if (Modes.fUserLon > 180.0) { // If Longitude is +180 to +360, make it -180 to 0
        Modes.fUserLon -= 360.0;
    }
    // If both Lat and Lon are 0.0 then the users location is either invalid/not-set, or (s)he's in the
    // Atlantic ocean off the west coast of Africa. This is unlikely to be correct.
    // Set the user LatLon valid flag only if either Lat or Lon are non zero. Note the Greenwich meridian
    // is at 0.0 Lon,so we must check for either fLat or fLon being non zero not both.
    // Testing the flag at runtime will be much quicker than ((fLon != 0.0) || (fLat != 0.0))
    Modes.bUserFlags &= ~MODES_USER_LATLON_VALID;
    if ((Modes.fUserLat != 0.0) || (Modes.fUserLon != 0.0)) {
        Modes.bUserFlags |= MODES_USER_LATLON_VALID;
    }

    // Prepare error correction tables
    modesChecksumInit(Modes.nfix_crc);
    icaoFilterInit();
    modeACInit();
    interactiveInit();
}

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    switch (key) {
        case OptFix:
            Modes.nfix_crc = 1;
            break;
        case OptNoFix:
            Modes.nfix_crc = 0;
            break;
        case OptNoCrcCheck:
            Modes.check_crc = 0;
            break;
        case OptModeAc:
            Modes.mode_ac = 1;
            Modes.mode_ac_auto = 0;
            break;
        case OptShowOnly:
            Modes.show_only = (uint32_t) strtoul(arg, NULL, 16);
            Modes.interactive = 0;
            break;
        case OptMetric:
            Modes.metric = 1;
            break;
        case OptAggressive:
            Modes.nfix_crc = MODES_MAX_BITERRORS;
            break;
        case OptNoInteractive:
            Modes.interactive = 0;
            break;
        case OptInteractiveTTL:
            Modes.interactive_display_ttl = (uint64_t) (1000 * atof(arg));
            break;
        case OptLat:
            Modes.fUserLat = atof(arg);
            break;
        case OptLon:
            Modes.fUserLon = atof(arg);
            break;
        case OptMaxRange:
            Modes.maxRange = atof(arg) * 1852.0; // convert to metres
            break;
        case OptNetBoPorts:
            bo_connect_port = arg;
            break;
        case OptNetBindAddr:
            bo_connect_ipaddr = arg;
            break;
        case ARGP_KEY_END:
            if (state->arg_num > 0)
                /* We use only options but no arguments */
                argp_usage(state);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}


//
//=========================================================================
//

int main(int argc, char **argv) {
    struct net_service *s;
    struct net_connector *con = calloc(1, sizeof(struct net_connector));

    // Set sane defaults

    view1090InitConfig();
    signal(SIGINT, sigintHandler); // Define Ctrl/C handler (exit program)

    // Parse the command line options
    if (argp_parse(&argp, argc, argv, 0, 0, 0)) {
        goto exit;
    }

#ifdef _WIN32
    // Try to comply with the Copyright license conditions for binary distribution
    if (!Modes.quiet) {
        showCopyright();
    }
#define MSG_DONTWAIT 0
#endif

    // We need only one service here created below, no need to call modesInitNet
    Modes.services = NULL;

    // Try to connect to the selected ip address and port. We only support *ONE* input connection which we initiate.here.
    s = makeBeastInputService();
    con->address = bo_connect_ipaddr;
    con->port = bo_connect_port;
    con->service = s;

    if (pthread_mutex_init(&con->mutex, NULL)) {
        fprintf(stderr, "Unable to initialize connector mutex!\n");
        exit(1);
    }
    pthread_mutex_lock(&con->mutex);

    serviceConnect(con);
    uint64_t timeout = mstime() + 10 * 1000;
    int counter = 0;
    while (!con->connected && timeout > mstime() && counter < 8) {
        struct timespec slp = {0, 100 * 1000 * 1000};
        //slp.tv_nsec = 100 * 1000 * 1000;
        nanosleep(&slp, NULL);
        if (con->connecting) {
            // Check to see...
            checkServiceConnected(con);
        } else {
            if (con->next_reconnect <= mstime()) {
                counter++;
                serviceConnect(con);
            }
        }
    }

    if (!con->connected) {
        fprintf(stderr, "Failed to connect to %s:%s: timed out or maximum tries reached!\n", bo_connect_ipaddr, bo_connect_port);
        exit(1);
    }

    sendBeastSettings(con->fd, "Cd"); // Beast binary format, no filters
    sendBeastSettings(con->fd, Modes.mode_ac ? "J" : "j"); // Mode A/C on or off
    sendBeastSettings(con->fd, Modes.check_crc ? "f" : "F"); // CRC checks on or off

    // Initialization
    view1090Init();
    
    // Keep going till the user does something that stops us
    while (!Modes.exit) {
        struct timespec r = { 0, 100 * 1000 * 1000};
        icaoFilterExpire();
        trackPeriodicUpdate();
        modesNetPeriodicWork();

        if (Modes.interactive)
            interactiveShowData();

        if (s->connections == 0) {
            // lost input connection, try to reconnect
            sleep(1);
            serviceConnect(con);
            continue;
        }

        nanosleep(&r, NULL);
    }

    /* Go through tracked aircraft chain and free up any used memory */
    for (int j = 0; j < AIRCRAFT_BUCKETS; j++) {
        struct aircraft *a = Modes.aircraft[j], *n;
        while (a) {
            n = a->next;
            if (a) free(a);
            a = n;
        }
    }
    // Free local service and client
    if (s) free(s);
    freeaddrinfo(con->addr_info);
    pthread_mutex_unlock(&con->mutex);
    pthread_mutex_destroy(&con->mutex);
    free(con);

exit:
    interactiveCleanup();
    return (0);
}
//
//=========================================================================
//
