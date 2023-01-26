# Readsb

This is a detached fork of https://github.com/Mictronics/readsb

It's continually under development, expect bugs, segfaults and all the good stuff :)

## NO WARRANTY

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
see the LICENSE file for details


## how to install / build

I'd recommend this script to automatically install it:
- https://github.com/wiedehopf/adsb-scripts/wiki/Automatic-installation-for-readsb

Or build the package yourself:
```
sudo apt update
sudo apt install --no-install-recommends --no-install-suggests -y \
    git build-essential debhelper libusb-1.0-0-dev \
    librtlsdr-dev librtlsdr0 pkg-config \
    libncurses-dev zlib1g-dev zlib1g libzstd-dev libzstd1
git clone --depth 20 https://github.com/wiedehopf/readsb.git
cd readsb
export DEB_BUILD_OPTIONS=noddebs
dpkg-buildpackage -b -Prtlsdr -ui -uc -us
sudo dpkg -i ../readsb_*.deb
```

Or check here for more build instructions and other useful stuff:
- https://github.com/wiedehopf/adsb-wiki/wiki/Building-readsb-from-source
- https://github.com/wiedehopf/adsb-wiki/wiki/Raspbian-Lite:-ADS-B-receiver

### aircraft.json format:

[json file Readme](README-json.md)

### Push server support

readsb connects to a listening server.

Sending beast data (beast_out):
```
--net-connector 192.168.2.22,30004,beast_out
```
Receiving beast data (beast_in);
```
--net-connector 192.168.2.28,30005,beast_in
```

### BeastReduce output

Selectively forwards beast messages if the received data hasn't been forwarded in the last 125 ms (or `--net-beast-reduce-interval`).
Data not related to the physical aircraft state are only forwarded every 500 ms (4 * `--net-beast-reduce-interval`).The messages of
this output are normal beast messages and compatible with every program able to receive beast messages.

### Debian package

- Build package with no additional receiver library dependencies: `dpkg-buildpackage -b`.
- Build with RTLSDR support: `dpkg-buildpackage -b --build-profiles=rtlsdr`

## Building manually

You can probably just run "make". By default "make" builds with no specific library support. See below.
Binaries are built in the source directory; you will need to arrange to
install them (and a method for starting them) yourself.

"make RTLSDR=yes" will enable rtl-sdr support and add the dependency on
librtlsdr.

On Raspbian 32 bit, mostly rpi2 and older you might want to use this to compile if you're running into CPU issues:
```
make AIRCRAFT_HASH_BITS=11 RTLSDR=yes OPTIMIZE="-Ofast -mcpu=arm1176jzf-s -mfpu=vfp"
```

In general if you want to save on CPU cycles, you can try building with these options:
```
make AIRCRAFT_HASH_BITS=11 RTLSDR=yes OPTIMIZE="-O3 -march=native"
```
Or even more aggressive but could cause unexpected behaviour:
```
make AIRCRAFT_HASH_BITS=11 RTLSDR=yes OPTIMIZE="-Ofast -march=native"
```

The difference of using -Ofast or -O3 over the default of -O2 is likely very minimal.
-march=native also usually makes little difference but it might, so it's worth a try.

## Configuration

If required, edit `/etc/default/readsb` to set the service options, device type, network ports etc.

## rtl-sdr bias tee

Use this utility independen of readsb:
https://github.com/wiedehopf/adsb-wiki/wiki/RTL-Bias-Tee

## Global map of aircraft

One of this forks main uses is to be the backend for the global map at https://adsbexchange.com/
For that purpose it's used in conjunction with tar1090 with some extra options to cope with the number of aircraft and also record a history of flight paths: https://github.com/wiedehopf/tar1090#0800-destroy-sd-card

## --debug=S: speed check debugging output

For current reference please see the speed_check function.

hex

SQ means same quality (ADS-B vs MLAT and stuff)
LQ means lower quality

fail / ok
ok means speed check passed (displayed only with cpr-focus)

A means airborne and S means surface.

reliable is my reliable counter
every good position increases each aircrafts position reliability
if it gets to zero, speed check is no longer applied and it's allowed to "JUMP"
"JUMP" is also allowed if we haven't had a position for 2 minutes

tD is the trackDifference
170 or 180 means the new position goes in the opposite direction of the ground track broadcast by the aircraft.

then we have actual distance / allowed distance.
the allowed distance i tweak depending on the trackDifference
high trackDifference makes the allowed distance go slightly negative
as i don't want aircraft to jump backwards.

elapsed time

actual / allowed speed (allowed speed based on allowed distance)

old --> new
lat, lon -> lat, lon

oh if you want that display:
--debug=S
you'll have to update, just disabled the MLAT speed check from displayign stuff ... because usually it's not interesting


## readsb --help

might be out of date, check the command on a freshly compiled version

```
Usage: readsb [OPTION...] 
readsb Mode-S/ADSB/TIS Receiver   
Build options: 

 General options:
      --cpr-focus=<hex>      show CPR details for this hex
      --db-file=<file.csv.gz>   Default: "none" (as of writing a compatible
                             file is available here:
                             https://github.com/wiedehopf/tar1090-db/tree/csv)
      --db-file-lt           Write long type to aircraft.json as field desc
      --debug=<flags>        Debug mode (verbose), n: network, P: CPR, S: speed
                             check
      --device-type=<type>   Select SDR type
      --filter-DF=<type>     When displaying decoded ModeS messages on stdout
                             only show this DF type
      --fix                  Enable CRC single-bit error correction (default)
      --forward-mlat         Allow forwarding of received mlat results to
                             output ports
      --freq=<hz>            Set frequency (default: 1090 MHz)
      --gain=<db>            Set gain (default: max gain. Use -10 for
                             auto-gain)
      --gnss                 Show altitudes as GNSS when available
      --heatmap=<interval in seconds>
                             Make Heatmap, each aircraft at most every interval
                             seconds (creates historydir/heatmap.bin and exit
                             after that)
      --heatmap-dir=<dir>    Change the directory where heatmaps are saved
                             (default is in globe history dir)
      --interactive          Interactive mode refreshing data on screen.
                             Implies --throttle
      --interactive-ttl=<sec>   Remove from list if idle for <sec> (default:
                             60)
      --jaero-timeout=<n>    How long in minutes JAERO positions remain valid
                             and on the map in tar1090 (default:33)
      --json-location-accuracy=<n>
                             Accuracy of receiver location in json metadata:
                             0=no location, 1=approximate, 2=exact
      --json-reliable=<n>    Minimum position reliability to put it into json
                             (default: 1, globe options will default set this
                             to 2, disable speed filter: -1, max: 4)
                                   --json-trace-hist-only=1,2,3,8
Don't write recent(1), full(2), either(3) traces
                             to /run, only archive via write-globe-history (8:
                             irregularly write limited traces to run, subject
                             to change)
      --json-trace-interval=<seconds>
                             Interval after which a new position will
                             guaranteed to be written to the trace and the json
                             position output (default: 30)
      --lat=<lat>            Reference/receiver surface latitude
      --leg-focus=<hex>      show leg marking details for this hex
      --lon=<lon>            Reference/receiver surface longitude
      --max-range=<dist>     Absolute maximum range for position decoding (in
                             nm, default: 300)
      --metric               Use metric units
      --mlat                 Display raw messages in Beast ASCII mode
      --modeac               Enable decoding of SSR Modes 3/A & 3/C
      --modeac-auto          Enable Mode A/C if requested by a Beast
                             connection
      --no-fix               Disable CRC single-bit error correction
      --no-fix-df            Disable CRC single-bit error correction on the DF
                             type to produce more DF17 messages (disabling
                             reduces CPU usage)
      --no-interactive       Disable interactive mode, print to stdout
      --onlyaddr             Show only ICAO addresses
      --position-persistence=<n>   Position persistence against outliers
                             (default: 4), incremented by json-reliable minus
                             1
      --preamble-threshold=<40-400>
                             lower threshold --> more CPU usage (default: 58,
                             pi zero / pi 1: 75, hot CPU 42)
      --quiet                Disable output (default)
      --range-outline-hours=<hours>
                             Make the range outline retain data for the last X
                             hours (float, default: 24.0)
      --raw                  Show only messages hex values
      --receiver-focus=<receiverId>
                             only process messages from receiverId
      --show-only=<addr>     Show only messages by given ICAO on stdout
      --snip=<level>         Strip IQ file removing samples < level
      --stats                With --ifile print stats at exit. No other output
      --stats-every=<sec>    Show and reset stats every <sec> seconds
      --stats-range          Collect/show range histogram
      --trace-focus=<hex>    show traceAdd details for this hex
      --write-globe-history=<dir>
                             Extended Globe History
      --write-json=<dir>     Periodically write json output to <dir>
      --aircraft-json-seen-by-list
                             Write receiver ids of receivers who have seen
                             an aircraft to aircraft.json
      --aircraft-json-seen-by-list-timeout=<timeout>
                             Remove receiver id from the list if there was no update
                             for more than <timeout> seconds
                             (int, default: 3 s, set 0 to disable timeout)
      --write-json-binCraft-only=<n>
                             Use only binary binCraft format for globe files
                             (1), for aircraft.json as well (2)
      --write-json-every=<sec>   Write json output and update API json every
                             sec seconds (default 1)
      --write-json-globe-index   Write specially indexed globe_xxxx.json files
                             (for tar1090)
      --write-json-gzip      Write aircraft.json also as aircraft.json.gz
      --write-prom=<filepath>   Periodically write prometheus output to
                             <filepath>
      --write-receiver-id-json   Write receivers.json
      --write-state=<dir>    Write state to disk to have traces after a restart
                            
      --write-state-only-on-exit   Don't continously update state.

 Network options:
      --net                  Enable networking
      --net-api-port=<port>  TCP API listen port (in contrast to other
                             listeners, only a single port is allowed) (update
                             frequency controlled by write-json-every
                             parameter) (default: 0)
      --net-beast-reduce-filter-alt=<pressure altitude in ft>
                             beast-reduce: remove aircraft which are above
                             altitude
      --net-beast-reduce-filter-dist=<distance in nmi>
                             beast-reduce: remove aircraft which are further
                             than distance from the receiver
      --net-beast-reduce-interval=<seconds>
                             BeastReduce position update interval, longer means
                             less data (default: 0.125, valid range: 0.000 -
                             14.999)
      --net-beast-reduce-out-port=<ports>
                             TCP BeastReduce output listen ports (default: 0)
      --net-bi-port=<ports>  TCP Beast input listen ports  (default: 0)
      --net-bind-address=<ip>   IP address to bind to (default: Any; Use
                             127.0.0.1 for private)
      --net-bo-port=<ports>  TCP Beast output listen ports (default: 0)
      --net-buffer=<n>       TCP buffer size 64Kb * (2^n) (default: n=2, 256Kb)
                            
      --net-connector=<ip,port,protocol>
                             Establish connection, can be specified multiple
                             times (e.g. 127.0.0.1,23004,beast_out) Protocols:
                             beast_out, beast_in, raw_out, raw_in, sbs_in,
                             sbs_in_jaero, sbs_out, sbs_out_jaero, vrs_out,
                             json_out, gpsd_in (one failover ip/address,port
                             can be specified:
                             primary-address,primary-port,protocol,failover-address,failover-port)
      --net-connector-delay=<seconds>
                             Outbound re-connection delay (default: 30)
      --net-garbage=<ports>  timeout receivers, output messages from timed out
                             receivers as beast on <ports>
      --net-heartbeat=<rate> TCP heartbeat rate in seconds (default: 60 sec; 0
                             to disable)
                                   --net-ingest           primary ingest node
      --net-json-port=<ports>   TCP json position output listen ports, sends
                             one line with a json object containing aircraft
                             details for every position received (default: 0)
      --net-json-port-include-noposition
TCP json position output: include aircraft without
                             position (state is sent for aircraft for every
                             DF11 with CRC if the aircraft hasn't sent a
                             position in the last 10 seconds and interval
                             allowing)
      --net-json-port-interval=<seconds>
                             Set minimum interval between outputs per aircraft
                             for TCP json output, default: 0.0 (every
                             position)
      --net-only             Enable just networking, no RTL device or file
                             used
      --net-receiver-id      forward receiver ID
      --net-ri-port=<ports>  TCP raw input listen ports  (default: 0)
      --net-ro-interval=<rate>   TCP output flush interval in seconds (maximum
                             interval between two network writes of accumulated
                             data)(default: 0.05, valid values 0.005 - 1.0)
      --net-ro-port=<ports>  TCP raw output listen ports (default: 0)
      --net-ro-size=<size>   TCP output flush size (maximum amount of
                             internally buffered data before writing to
                             network) (default: 1200)
      --net-sbs-in-port=<ports>   TCP BaseStation input listen ports (default:
                             0)
      --net-sbs-jaero-in-port=<ports>
                             TCP SBS Jaero input listen ports (default: 0)
      --net-sbs-jaero-port=<ports>
                             TCP SBS Jaero output listen ports (default: 0)
      --net-sbs-port=<ports> TCP BaseStation output listen ports (default: 0)
      --net-sbs-reduce       Apply beast reduce logic and interval to SBS
                             outputs
      --net-verbatim         Forward messages unchanged
      --net-vrs-interval=<seconds>
                             TCP VRS json output interval (default: 5.0)
      --net-vrs-port=<ports> TCP VRS json output listen ports (default: 0)
      --uuid-file=<path>     path to UUID file

 Modes-S Beast options, use with --device-type modesbeast:
      --beast-baudrate=<baud>   Override Baudrate (default rate 3000000 baud)
      --beast-crc-off        Turn OFF CRC checking
      --beast-df045-on       Turn ON DF0/4/5 filter
      --beast-df1117-on      Turn ON DF11/17-only filter
      --beast-fec-off        Turn OFF forward error correction
      --beast-mlat-off       Turn OFF MLAT time stamps
      --beast-modeac         Turn ON mode A/C
      --beast-serial=<path>  Path to Beast serial device (default
                             /dev/ttyUSB0)

 GNS HULC options, use with --device-type gnshulc:
      --beast-serial=<path>  Path to GNS HULC serial device (default
                             /dev/ttyUSB0)

 ifile-specific options, use with --ifile:
      --ifile=<path>         Read samples from given file ('-' for stdin)
      --iformat=<type>       Set sample format (UC8, SC16, SC16Q11)
      --throttle             Process samples at the original capture speed

 Help options:

  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version
```
