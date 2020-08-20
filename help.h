// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// help.h: main program help header
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
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

#ifndef HELP_H
#define HELP_H

#include <argp.h>
const char *argp_program_bug_address = "Matthias Wirth <matthias.wirth@gmail.com>";
static error_t parse_opt (int key, char *arg, struct argp_state *state);

static struct argp_option options[] =
{
    {0,0,0,0, "General options:", 1},
#if defined(READSB) || defined(VIEWADSB)
    {"lat", OptLat, "<lat>", 0, "Reference/receiver surface latitude", 1},
    {"lon", OptLon, "<lon>", 0, "Reference/receiver surface longitude", 1},
    {"no-interactive", OptNoInteractive, 0, 0, "Disable interactive mode, print to stdout", 1},
    {"interactive-ttl", OptInteractiveTTL, "<sec>", 0, "Remove from list if idle for <sec> (default: 60)", 1},
    {"modeac", OptModeAc, 0, 0, "Enable decoding of SSR Modes 3/A & 3/C", 1},
    {"max-range", OptMaxRange, "<dist>", 0, "Absolute maximum range for position decoding (in nm, default: 300)", 1},
    {"fix", OptFix, 0, 0, "Enable CRC single-bit error correction (default)", 1},
    {"no-fix", OptNoFix, 0, 0, "Disable CRC single-bit error correction", 1},
    {"metric", OptMetric, 0, 0, "Use metric units", 1},
    {"show-only", OptShowOnly, "<addr>", 0, "Show only messages by given ICAO on stdout", 1},
#ifdef ALLOW_AGGRESSIVE
    {"aggressive", OptAggressive, 0, 0, "Enable two-bit CRC error correction", 1},
#else
    {"aggressive", OptAggressive, 0, OPTION_HIDDEN, "Enable two-bit CRC error correction", 1},
#endif
#endif
#if defined(READSB)
    {"device-type", OptDeviceType, "<type>", 0, "Select SDR type", 1},
    {"gain", OptGain, "<db>", 0, "Set gain (default: max gain. Use -10 for auto-gain)", 1},
    {"freq", OptFreq, "<hz>", 0, "Set frequency (default: 1090 MHz)", 1},
    {"interactive", OptInteractive, 0, 0, "Interactive mode refreshing data on screen. Implies --throttle", 1},
    {"raw", OptRaw, 0, 0, "Show only messages hex values", 1},
    {"no-modeac-auto", OptNoModeAcAuto, 0, 0, "Don't enable Mode A/C if requested by a Beast connection", 1},
    {"forward-mlat", OptForwardMlat, 0, 0, "Allow forwarding of received mlat results to output ports", 1},
    {"mlat", OptMlat, 0, 0, "Display raw messages in Beast ASCII mode", 1},
    {"stats", OptStats, 0, 0, "With --ifile print stats at exit. No other output", 1},
    {"stats-range", OptStatsRange, 0, 0, "Collect/show range histogram", 1},
    {"stats-every", OptStatsEvery, "<sec>", 0, "Show and reset stats every <sec> seconds", 1},
    {"onlyaddr", OptOnlyAddr, 0, 0, "Show only ICAO addresses", 1},
    {"gnss", OptGnss, 0, 0, "Show altitudes as GNSS when available", 1},
    {"snip", OptSnip, "<level>", 0, "Strip IQ file removing samples < level", 1},
    {"debug", OptDebug, "<flags>", 0, "Debug mode (verbose), see flags below", 1},
    {"receiver-focus", OptReceiverFocus, "<receiverId>", 0, "only process messages from receiverId", 1},
    {"cpr-focus", OptCprFocus, "<hex>", 0, "show CPR details for this hex", 1},
    {"quiet", OptQuiet, 0, 0, "Disable output (default)", 1},
    {"dcfilter", OptDcFilter, 0, 0, "Apply a 1Hz DC filter to input data (requires more CPU)", 1},
    {"enable-biastee", OptBiasTee, 0, 0, "Enable bias tee on supporting interfaces (default: disabled)", 1},
    {"write-json", OptJsonDir, "<dir>", 0, "Periodically write json output to <dir>", 1},
    {"write-prom", OptPromFile, "<filepath>", 0, "Periodically write prometheus output to <filepath>", 1},
    {"write-globe-history", OptGlobeHistoryDir, "<dir>", 0, "Extended Globe History", 1},
    {"write-state", OptStateDir, "<dir>", 0, "Write state to disk to have traces after a restart", 1},
    {"heatmap-dir", OptHeatmapDir, "<dir>", 0, "Change the directory where heatmaps are saved (default is in globe history dir)", 1},
    {"heatmap", OptHeatmap, "<interval in seconds>", 0, "Make Heatmap, each aircraft at most every interval seconds (creates historydir/heatmap.bin and exit after that)", 1},
    {"write-json-every", OptJsonTime, "<t>", 0, "Write json output every t seconds (default 1)", 1},
    {"json-location-accuracy", OptJsonLocAcc , "<n>", 0, "Accuracy of receiver location in json metadata: 0=no location, 1=approximate, 2=exact", 1},
    {"write-json-globe-index", OptJsonGlobeIndex, 0, 0, "Write specially indexed globe_xxxx.json files (for tar1090)", 1},
    {"write-receiver-id-json", OptNetReceiverIdJson, 0, 0, "Write receivers.json", 1},
    {"json-trace-interval", OptJsonTraceInt, "<seconds>", 0, "Interval after which a new position will guaranteed to be written to the trace and the json position output (default: 30)", 1},
    {"write-json-gzip", OptJsonGzip, 0, 0, "Write aircraft.json also as aircraft.json.gz", 1},
    {"json-reliable", OptJsonReliable,"<n>", 0, "Minimum position reliability to put it into json (default: 2, home install recommendation: 1, disable speed filter: -1, max: 4)", 1},
#endif
    {0,0,0,0, "Network options:", 2},
#if defined(READSB) || defined(VIEWADSB)
    {"net-bind-address", OptNetBindAddr, "<ip>", 0, "IP address to bind to (default: Any; Use 127.0.0.1 for private)", 2},
    {"net-bo-port", OptNetBoPorts, "<ports>", 0, "TCP Beast output listen ports (default: 0)", 2},
#endif
#if defined(READSB)
    {"net", OptNet, 0, 0, "Enable networking", 2},
    {"net-only", OptNetOnly, 0, 0, "Enable just networking, no RTL device or file used", 2},
    {"net-ri-port", OptNetRiPorts, "<ports>", 0, "TCP raw input listen ports  (default: 0)", 2},
    {"net-ro-port", OptNetRoPorts, "<ports>", 0, "TCP raw output listen ports (default: 0)", 2},
    {"net-sbs-port", OptNetSbsPorts, "<ports>", 0, "TCP BaseStation output listen ports (default: 0)", 2},
    {"net-sbs-in-port", OptNetSbsInPorts, "<ports>", 0, "TCP BaseStation input listen ports (default: 0)", 2},
    {"net-bi-port", OptNetBiPorts, "<ports>", 0, "TCP Beast input listen ports  (default: 0)", 2},
    {"net-vrs-port", OptNetVRSPorts, "<ports>", 0, "TCP VRS json output listen ports (default: 0)", 2},
    {"net-vrs-interval", OptNetVRSInterval, "<seconds>", 0, "TCP VRS json output interval (default: 5)", 2},
    {"net-json-port", OptNetJsonPorts, "<ports>", 0, "TCP json position output listen ports (requires --write-json-globe-index) (default: 0)", 2},
    {"net-api-port", OptNetApiPorts, "<ports>", 0, "TCP API listen port (only for exactly one client, needs an external wrapper program communicating via this port) (work in progress) (default: 0)", 2},
    {"net-beast-reduce-out-port", OptNetBeastReducePorts, "<ports>", 0, "TCP BeastReduce output listen ports (default: 0)", 2},
    {"net-beast-reduce-interval", OptNetBeastReduceInterval, "<seconds>", 0, "BeastReduce position update interval, longer means less data (default: 0.125, valid range: 0.000 - 14.999)", 2},
    {"net-receiver-id", OptNetReceiverId, 0, 0, "forward receiver ID", 2},
    {"net-ingest", OptNetIngest, 0, 0, "primary ingest node", 2},
    {"net-garbage", OptGarbage, "<ports>", 0, "timeout receivers, output messages from timed out receivers as beast on <ports>", 2},
    {"uuid-file", OptUuidFile, "<path>", 0, "path to UUID file", 2},
    {"net-ro-size", OptNetRoSize, "<size>", 0, "TCP output flush size (maximum amount of internally buffered data before writing to network) (default: 1200)", 2},
    {"net-ro-interval", OptNetRoIntervall, "<rate>", 0, "TCP output flush interval in seconds (maximum interval between two network writes of accumulated data)(default: 0.05)", 2},
    {"net-connector", OptNetConnector, "<ip,port,protocol>", 0, "Establish connection, can be specified multiple times (e.g. 127.0.0.1,23004,beast_out) Protocols: beast_out, beast_in, raw_out, raw_in, sbs_out, vrs_out, json_out (one failover ip/address (same port) can be specified: primary-address,port,protocol,failover-address)", 2},
    {"net-connector-delay", OptNetConnectorDelay, "<seconds>", 0, "Outbound re-connection delay (default: 30)", 2},
    {"net-heartbeat", OptNetHeartbeat, "<rate>", 0, "TCP heartbeat rate in seconds (default: 60 sec; 0 to disable)", 2},
    {"net-buffer", OptNetBuffer, "<n>", 0, "TCP buffer size 64Kb * (2^n) (default: n=2, 256Kb)", 2},
    {"net-verbatim", OptNetVerbatim, 0, 0, "Forward messages unchanged", 2},
#ifdef ENABLE_RTLSDR
    {0,0,0,0, "RTL-SDR options:", 3},
    {0,0,0, OPTION_DOC, "use with --device-type rtlsdr", 3},
    {"device", OptDevice, "<index|serial>", 0, "Select device by index or serial number", 3},
    {"enable-agc", OptRtlSdrEnableAgc, 0, 0, "Enable digital AGC (not tuner AGC!)", 3},
    {"ppm", OptRtlSdrPpm, "<correction>", 0, "Set oscillator frequency correction in PPM", 3},
#endif
#ifdef ENABLE_BLADERF
    {0,0,0,0, "BladeRF options:", 4},
    {0,0,0, OPTION_DOC, "use with --device-type bladerf", 4},
    {"device", OptDevice, "<ident>",  0, "Select device by bladeRF 'device identifier'", 4},
    {"bladerf-fpga",            1001, "<path>",   0, "Use alternative FPGA bitstream ('' to disable FPGA load)", 4},
    {"bladerf-decimation",      1002, "<N>",      0, "Assume FPGA decimates by a factor of N", 4},
    {"bladerf-bandwidth",       1003, "<hz>",     0, "Set LPF bandwidth ('bypass' to bypass the LPF)", 4},
#endif
    {0,0,0,0, "Modes-S Beast options:", 5},
    {0,0,0, OPTION_DOC, "use with --device-type modesbeast", 5},
    {0,0,0, OPTION_DOC, "Beast binary protocol and hardware handshake are always enabled.", 5},
    {"beast-serial", OptBeastSerial, "<path>", 0, "Path to Beast serial device (default /dev/ttyUSB0)", 5},
    {"beast-df1117-on", OptBeastDF1117, 0, 0, "Turn ON DF11/17-only filter", 5},
    {"beast-mlat-off", OptBeastMlatTimeOff, 0, 0, "Turn OFF MLAT time stamps", 5},
    {"beast-crc-off", OptBeastCrcOff, 0, 0, "Turn OFF CRC checking", 5},
    {"beast-df045-on", OptBeastDF045, 0, 0, "Turn ON DF0/4/5 filter", 5},
    {"beast-fec-off", OptBeastFecOff, 0, 0, "Turn OFF forward error correction", 5},
    {"beast-modeac", OptBeastModeAc, 0, 0, "Turn ON mode A/C", 5},

    {0,0,0,0, "GNS5894 options:", 6},
    {0,0,0, OPTION_DOC, "use with --device-type gns5894", 6},
    {0,0,0, OPTION_DOC, "Expects ASCII HEX protocal input.", 6},
    {"beast-serial", OptBeastSerial, "<path>", 0, "Path to GNS5894 serial device (default /dev/ttyAMA0)", 6},

    {0,0,0,0, "ifile-specific options:", 7},
    {0,0,0, OPTION_DOC, "use with --ifile", 7},
    {"ifile", OptIfileName, "<path>", 0, "Read samples from given file ('-' for stdin)", 7},
    {"iformat", OptIfileFormat, "<type>", 0, "Set sample format (UC8, SC16, SC16Q11)", 7},
    {"throttle", OptIfileThrottle, 0, 0, "Process samples at the original capture speed", 7},
#ifdef ENABLE_PLUTOSDR
    {0,0,0,0, "ADALM-Pluto SDR options:", 8},
    {0,0,0, OPTION_DOC, "use with --device-type plutosdr", 8},
    {"pluto-uri", OptPlutoUri, "<USB uri>", 0, "Create USB context from this URI.(eg. usb:1.2.5)", 8},
    {"pluto-network", OptPlutoNetwork, "<hostname or IP>", 0, "Hostname or IP to create networks context. (default pluto.local)", 8},
#endif
#endif
    {0,0,0,0, "Help options:", 100},
    { 0 }
};

#endif /* HELP_H */
