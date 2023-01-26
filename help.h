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

#include "argp.h"
const char *argp_program_version = VERSION_STRING;
const char *argp_program_bug_address = "Matthias Wirth <matthias.wirth@gmail.com>";
static error_t parse_opt (int key, char *arg, struct argp_state *state);

// preprocessor sillyness, yes both lines are necessary.
#define _stringize(x) #x
#define stringize(x) _stringize(x)
static struct argp_option optionsViewadsb[] = {
    {0,0,0,0, "General options:", 1},
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
    {"filter-DF", OptFilterDF, "<type>", 0, "Only network forward and display decoded ModeS messages on stdout only show this DF type", 1},
    {"receiver-focus", OptReceiverFocus, "<receiverId>", 0, "only process messages from receiverId", 1},
    {"cpr-focus", OptCprFocus, "<hex>", 0, "show CPR details for this hex", 1},
    {"quiet", OptQuiet, 0, 0, "Disable output (default)", 1},
    {"debug", OptDebug, "<flags>", 0, "Debug mode (verbose), n: network, P: CPR, S: speed check", 1},
    {0,0,0,0, "Network options:", 2},
    {"net-connector", OptNetConnector, "<ip,port,protocol>", 0, "Establish connection, can be specified multiple times. (viewadsb default: --net-connector 127.0.0.1,30005,beast_in viewadsb first usage overrides default, second usage adds another input/output) \nProtocols: beast_out, beast_in, raw_out, raw_in, sbs_out, vrs_out, json_out, gpsd_in, uat_in", 2},
    {0,0,0,0, "Help options:", 100},
    { 0 }
};

static struct argp_option optionsReadsb[] = {
    {0,0,0,0, "General options:", 1},
    {"lat", OptLat, "<lat>", 0, "Reference/receiver surface latitude", 1},
    {"lon", OptLon, "<lon>", 0, "Reference/receiver surface longitude", 1},
    {"no-interactive", OptNoInteractive, 0, 0, "Disable interactive mode, print to stdout", 1},
    {"interactive-ttl", OptInteractiveTTL, "<sec>", 0, "Remove from list if idle for <sec> (default: 60)", 1},
    {"modeac", OptModeAc, 0, 0, "Enable decoding of SSR Modes 3/A & 3/C", 1},
    {"modeac-auto", OptModeAcAuto, 0, 0, "Enable Mode A/C if requested by a Beast connection", 1},
    {"max-range", OptMaxRange, "<dist>", 0, "Absolute maximum range for position decoding (in nm, default: 300)", 1},
    {"fix", OptFix, 0, 0, "Enable CRC single-bit error correction (default)", 1},
    {"no-fix", OptNoFix, 0, 0, "Disable CRC single-bit error correction", 1},
    {"no-fix-df", OptNoFixDf, 0, 0, "Disable CRC single-bit error correction on the DF type to produce more DF17 messages (disabling reduces CPU usage)", 1},
    {"metric", OptMetric, 0, 0, "Use metric units", 1},
    {"show-only", OptShowOnly, "<addr>", 0, "Show only messages by given ICAO on stdout", 1},
    {"filter-DF", OptFilterDF, "<type>", 0, "When displaying decoded ModeS messages on stdout only show this DF type", 1},
    {"aggressive", OptAggressive, 0, OPTION_HIDDEN, "Enable two-bit CRC error correction", 1},
    {"device-type", OptDeviceType, "<type>", 0, "Select SDR type", 1},
    {"gain", OptGain, "<db>", 0, "Set gain (default: max gain. Use -10 for auto-gain)", 1},
    {"freq", OptFreq, "<hz>", 0, "Set frequency (default: 1090 MHz)", 1},
    {"interactive", OptInteractive, 0, 0, "Interactive mode refreshing data on screen. Implies --throttle", 1},
    {"raw", OptRaw, 0, 0, "Show only messages hex values", 1},
    {"preamble-threshold", OptPreambleThreshold, "<"stringize(PREAMBLE_THRESHOLD_MIN)"-"stringize(PREAMBLE_THRESHOLD_MAX)">", 0, "lower threshold --> more CPU usage (default: "stringize(PREAMBLE_THRESHOLD_DEFAULT)", pi zero / pi 1: "stringize(PREAMBLE_THRESHOLD_PIZERO)", hot CPU "stringize(PREAMBLE_THRESHOLD_HOT)")", 1},
    {"forward-mlat", OptForwardMlat, 0, 0, "Allow forwarding of received mlat results to output ports", 1},
    {"mlat", OptMlat, 0, 0, "Display raw messages in Beast ASCII mode", 1},
    {"stats", OptStats, 0, 0, "Print stats at exit. No other output", 1},
    {"stats-range", OptStatsRange, 0, 0, "Collect/show range histogram", 1},
    {"stats-every", OptStatsEvery, "<sec>", 0, "Show and reset stats every <sec> seconds (rounded to the nearest 10 seconds due to implementation, first inteval can be up to 5 seconds shorter)", 1},
    {"range-outline-hours", OptRangeOutlineDuration, "<hours>", 0, "Make the range outline retain data for the last X hours (float, default: 24.0)", 1},
    {"onlyaddr", OptOnlyAddr, 0, 0, "Show only ICAO addresses", 1},
    {"gnss", OptGnss, 0, 0, "Show altitudes as GNSS when available", 1},
    {"snip", OptSnip, "<level>", 0, "Strip IQ file removing samples < level", 1},
    {"debug", OptDebug, "<flags>", 0, "Debug mode (verbose), n: network, P: CPR, S: speed check", 1},
    {"devel", OptDevel, "<mode>", 0, "Development debugging mode, see source for options, can be specified more than once", 1},
    {"receiver-focus", OptReceiverFocus, "<receiverId>", 0, "only process messages from receiverId", 1},
    {"cpr-focus", OptCprFocus, "<hex>", 0, "show CPR details for this hex", 1},
    {"leg-focus", OptLegFocus, "<hex>", 0, "show leg marking details for this hex", 1},
    {"trace-focus", OptTraceFocus, "<hex>", 0, "show traceAdd details for this hex", 1},
    {"quiet", OptQuiet, 0, 0, "Disable output (default)", 1},
    {"dcfilter", OptDcFilter, 0, OPTION_HIDDEN, "Apply a 1Hz DC filter to input data (requires more CPU)", 1},
    {"enable-biastee", OptBiasTee, 0, OPTION_HIDDEN, "Enable bias tee on supporting interfaces (default: disabled)", 1},
    {"write-json", OptJsonDir, "<dir>", 0, "Periodically write json output to <dir>", 1},
    {"aircraft-json-seen-by-list", OptAircraftJsonSeenByList, 0, 0, "Write receiver ids of receivers who have seen an aircraft to aircraft.json", 1},
    {"aircraft-json-seen-by-list-timeout", OptAircraftJsonSeenByListTimeout, "<timeout>", 0, "Remove receiver id from the list if there was no update for more than <timeout> seconds (int, default: 3 s, set 0 to disable timeout)", 1},
    {"write-prom", OptPromFile, "<filepath>", 0, "Periodically write prometheus output to <filepath>", 1},
    {"write-globe-history", OptGlobeHistoryDir, "<dir>", 0, "Extended Globe History", 1},
    {"write-state", OptStateDir, "<dir>", 0, "Write state to disk to have traces after a restart", 1},
    {"write-state-only-on-exit", OptStateOnlyOnExit, 0, 0, "Don't continously update state.", 1},
    {"heatmap-dir", OptHeatmapDir, "<dir>", 0, "Change the directory where heatmaps are saved (default is in globe history dir)", 1},
    {"heatmap", OptHeatmap, "<interval in seconds>", 0, "Make Heatmap, each aircraft at most every interval seconds (creates historydir/heatmap.bin and exit after that)", 1},
    {"dump-beast", OptDumpBeastDir, "<dir>,<interval>", 0, "Dump compressed beast files to this directory, start a new file evey interval seconds", 1},
    {"write-json-every", OptJsonTime, "<sec>", 0, "Write json output and update API json every sec seconds (default 1)", 1},
    {"json-location-accuracy", OptJsonLocAcc , "<n>", 0, "Accuracy of receiver location in json metadata: 0=no location, 1=approximate, 2=exact", 1},
    {"write-json-globe-index", OptJsonGlobeIndex, 0, 0, "Write specially indexed globe_xxxx.json files (for tar1090)", 1},
    {"write-receiver-id-json", OptNetReceiverIdJson, 0, 0, "Write receivers.json", 1},
    {"json-trace-interval", OptJsonTraceInt, "<seconds>", 0, "Interval after which a new position will guaranteed to be written to the trace and the json position output (default: 30)", 1},
    {"json-trace-hist-only", OptJsonTraceHistOnly, "1,2,3,8", 0, "Don't write recent(1), full(2), either(3) traces to /run, only archive via write-globe-history (8: irregularly write limited traces to run, subject to change)", 1},
    {"write-json-gzip", OptJsonGzip, 0, 0, "Write aircraft.json also as aircraft.json.gz", 1},
    {"write-json-binCraft-only", OptJsonOnlyBin, "<n>", 0, "Use only binary binCraft format for globe files (1), for aircraft.json as well (2)", 1},
    {"write-binCraft-old", OptEnableBinGz, 0, 0, "write old gzipped binCraft files\n", 1},
    {"json-reliable", OptJsonReliable,"<n>", 0, "Minimum position reliability to put it into json (default: 1, globe options will default set this to 2, disable speed filter: -1, max: 4)", 1},
    {"position-persistence", OptPositionPersistence,"<n>", 0, "Position persistence against outliers (default: 4), incremented by json-reliable minus 1", 1},
    {"jaero-timeout", OptJaeroTimeout,"<n>", 0, "How long in minutes JAERO positions remain valid and on the map in tar1090 (default:33)", 1},
    {"db-file", OptDbFile, "<file.csv.gz>", 0, "Default: \"none\" (as of writing a compatible file is available here: https://github.com/wiedehopf/tar1090-db/tree/csv)", 1},
    {"db-file-lt", OptDbFileLongtype, 0, 0, "aircraft.json: add long type as field desc, add field ownOp for the owner, add field year", 1},
    {0,0,0,0, "Network options:", 2},
    {"net-connector", OptNetConnector, "<ip,port,protocol>", 0, "Establish connection, can be specified multiple times (e.g. 127.0.0.1,23004,beast_out) Protocols: beast_out, beast_in, raw_out, raw_in, sbs_in, sbs_in_jaero, sbs_out, sbs_out_jaero, vrs_out, json_out, gpsd_in, uat_in (one failover ip/address,port can be specified: primary-address,primary-port,protocol,failover-address,failover-port)", 2},
    {"net", OptNet, 0, 0, "Enable networking", 2},
    {"net-only", OptNetOnly, 0, 0, "Enable just networking, no RTL device or file used", 2},
    {"net-bind-address", OptNetBindAddr, "<ip>", 0, "IP address to bind to (default: Any; Use 127.0.0.1 for private)", 2},
    {"net-bo-port", OptNetBoPorts, "<ports>", 0, "TCP Beast output listen ports (default: 0)", 2},
    {"net-ri-port", OptNetRiPorts, "<ports>", 0, "TCP raw input listen ports  (default: 0)", 2},
    {"net-ro-port", OptNetRoPorts, "<ports>", 0, "TCP raw output listen ports (default: 0)", 2},
    {"net-sbs-port", OptNetSbsPorts, "<ports>", 0, "TCP BaseStation output listen ports (default: 0)", 2},
    {"net-sbs-in-port", OptNetSbsInPorts, "<ports>", 0, "TCP BaseStation input listen ports (default: 0)", 2},
    {"net-sbs-jaero-port", OptNetJaeroPorts, "<ports>", 0, "TCP SBS Jaero output listen ports (default: 0)", 2},
    {"net-sbs-jaero-in-port", OptNetJaeroInPorts, "<ports>", 0, "TCP SBS Jaero input listen ports (default: 0)", 2},
    {"net-bi-port", OptNetBiPorts, "<ports>", 0, "TCP Beast input listen ports  (default: 0)", 2},
    {"net-vrs-port", OptNetVRSPorts, "<ports>", 0, "TCP VRS json output listen ports (default: 0)", 2},
    {"net-vrs-interval", OptNetVRSInterval, "<seconds>", 0, "TCP VRS json output interval (default: 5.0)", 2},
    {"net-json-port", OptNetJsonPorts, "<ports>", 0, "TCP json position output listen ports, sends one line with a json object containing aircraft details for every position received (default: 0)", 2},
    {"net-json-port-interval", OptNetJsonPortInterval, "<seconds>", 0, "Set minimum interval between outputs per aircraft for TCP json output, default: 0.0 (every position)", 2},
    {"net-json-port-include-noposition", OptNetJsonPortNoPos, 0, 0, "TCP json position output: include aircraft without position (state is sent for aircraft for every DF11 with CRC if the aircraft hasn't sent a position in the last 10 seconds and interval allowing)", 2},
    {"net-api-port", OptNetApiPorts, "<port>", 0, "TCP API listen port (in contrast to other listeners, only a single port is allowed) (update frequency controlled by write-json-every parameter) (default: 0)", 2},
    {"api-shutdown-delay", OptApiShutdownDelay, "<seconds>", 0, "Shutdown delay to server remaining API queries, new queries get a 503 response (default: 0)", 2},
    {"tar1090-use-api", OptTar1090UseApi, 0, 0, "when running with globe-index, signal tar1090 use the readsb API to get data, requires webserver mapping of /tar1090/re-api to proxy_pass the requests to the --net-api-port, see nginx-readsb-api.conf in the tar1090 repository for details", 2},
    {"net-beast-reduce-out-port", OptNetBeastReducePorts, "<ports>", 0, "TCP BeastReduce output listen ports (default: 0)", 2},
    {"net-beast-reduce-interval", OptNetBeastReduceInterval, "<seconds>", 0, "BeastReduce data update interval, longer means less data (default: 0.250, valid range: 0.000 - 14.999)", 2},
    {"net-beast-reduce-filter-dist", OptNetBeastReduceFilterDist, "<distance in nmi>", 0, "beast-reduce: remove aircraft which are further than distance from the receiver", 2},
    {"net-beast-reduce-filter-alt", OptNetBeastReduceFilterAlt, "<pressure altitude in ft>", 0, "beast-reduce: remove aircraft which are above altitude", 2},
    {"net-sbs-reduce", OptNetSbsReduce, 0, 0, "Apply beast reduce logic and interval to SBS outputs", 2},
    {"net-receiver-id", OptNetReceiverId, 0, 0, "forward receiver ID", 2},
    {"net-ingest", OptNetIngest, 0, 0, "primary ingest node", 2},
    {"net-garbage", OptGarbage, "<ports>", 0, "timeout receivers, output messages from timed out receivers as beast on <ports>", 2},
    {"decode-threads", OptDecodeThreads, "<n>", 0, "Number of decode threads, either 1 or 2 (default: 1). Only use 2 when you have beast traffic > 200 MBit/s, expect 1.4x speedup for 2x CPU", 2},
    {"uuid-file", OptUuidFile, "<path>", 0, "path to UUID file", 2},
    {"net-ro-size", OptNetRoSize, "<size>", 0, "TCP output flush size (maximum amount of internally buffered data before writing to network) (default: 1200)", 2},
    {"net-ro-interval", OptNetRoIntervall, "<rate>", 0, "TCP output flush interval in seconds (maximum interval between two network writes of accumulated data)(default: 0.05, valid values 0.0 - 1.0)", 2},
    {"net-connector-delay", OptNetConnectorDelay, "<seconds>", 0, "Outbound re-connection delay (default: 30)", 2},
    {"net-heartbeat", OptNetHeartbeat, "<rate>", 0, "TCP heartbeat rate in seconds (default: 60 sec; 0 to disable)", 2},
    {"net-buffer", OptNetBuffer, "<n>", 0, "TCP buffer size 64Kb * (2^n) (default: n=2, 256Kb)", 2},
    {"net-verbatim", OptNetVerbatim, 0, 0, "Forward messages unchanged", 2},
    {"sdr-buffer-size", OptSdrBufSize, "<KiB>", 0, "SDR buffer / USB transfer size in kibibytes (default: 256 which is equivalent to around 54 ms using rtl-sdr, option might be ignored in future versions)", 2},
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
    {0,0,0,0, "Modes-S Beast options, use with --device-type modesbeast:", 5},
    {"beast-serial", OptBeastSerial, "<path>", 0, "Path to Beast serial device (default /dev/ttyUSB0)", 5},
    {"beast-df1117-on", OptBeastDF1117, 0, 0, "Turn ON DF11/17-only filter", 5},
    {"beast-mlat-off", OptBeastMlatTimeOff, 0, 0, "Turn OFF MLAT time stamps", 5},
    {"beast-crc-off", OptBeastCrcOff, 0, 0, "Turn OFF CRC checking", 5},
    {"beast-df045-on", OptBeastDF045, 0, 0, "Turn ON DF0/4/5 filter", 5},
    {"beast-fec-off", OptBeastFecOff, 0, 0, "Turn OFF forward error correction", 5},
    {"beast-modeac", OptBeastModeAc, 0, 0, "Turn ON mode A/C", 5},
    {"beast-baudrate", OptBeastBaudrate, "<baud>", 0, "Override Baudrate (default rate 3000000 baud)", 5},

    {0, 0, 0, 0, "GNS HULC options, use with --device-type gnshulc:", 6},
    {0, 0, 0, OPTION_DOC, "Beast binary and HULC protocol input with hardware handshake enabled.", 6},
    {"beast-serial", OptBeastSerial, "<path>", 0, "Path to GNS HULC serial device (default /dev/ttyUSB0)", 6},

    {0,0,0,0, "ifile-specific options, use with --ifile:", 7},
    {"ifile", OptIfileName, "<path>", 0, "Read samples from given file ('-' for stdin)", 7},
    {"iformat", OptIfileFormat, "<type>", 0, "Set sample format (UC8, SC16, SC16Q11)", 7},
    {"throttle", OptIfileThrottle, 0, 0, "Process samples at the original capture speed", 7},
#ifdef ENABLE_PLUTOSDR
    {0,0,0,0, "ADALM-Pluto SDR options:", 8},
    {0,0,0, OPTION_DOC, "use with --device-type plutosdr", 8},
    {"pluto-uri", OptPlutoUri, "<USB uri>", 0, "Create USB context from this URI.(eg. usb:1.2.5)", 8},
    {"pluto-network", OptPlutoNetwork, "<hostname or IP>", 0, "Hostname or IP to create networks context. (default pluto.local)", 8},
#endif
    {0,0,0,0, "Help options:", 100},
    { 0 }
};
#undef stringize
#undef _stringize

#endif /* HELP_H */
