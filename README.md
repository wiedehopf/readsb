# Readsb

This is a detached fork of https://github.com/Mictronics/readsb

It's continually under development, expect bugs, segfaults and all the good stuff :)

## NO WARRANTY

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
see the LICENSE file for details


## how to install / build

I'd recommend this script to automatically install it:
- https://github.com/wiedehopf/adsb-scripts/wiki/Automatic-installation-for-readsb

The package is also [available on Homebrew](https://formulae.brew.sh/formula/readsb) for MacOS and Linux.
Run `brew install readsb` to get started.

Or build the package yourself:
```
sudo apt update
sudo apt install --no-install-recommends --no-install-suggests -y \
    git build-essential debhelper libusb-1.0-0-dev \
    librtlsdr-dev librtlsdr0 pkg-config fakeroot \
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

For macOS / OS X build and general info, check further down.

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

This is used by some aggregators to aggregate ADS-B data, an example net connector would be:
```
--net-connector=feed.airplanes.live,30004,beast_reduce_plus_out,uuid=0033062d-e17e-4389-91a9-79ebb967fb4c
```
The uuid is optional, if none is given, the uuid from --uuid-file is used, if that isn't present no uuid is sent.
The beast_reduce_out net-connector will never send an uuid.
The aggregator enables --net-receiver-id and --net-ingest on their readsb server, it's made to work with beast_reduce_plus_out.

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

## Autogain

For rtl-sdr devices a software gain algorithm is the default, it's optimized for ADS-B.
On the command line it's activated using `--gain=auto` an is silent by default.
`--gain=auto-verbose` can be used to enable log messages for gain changes.
To tweak the internals, more parameters can be passed:
```
--gain=auto-verbose,<lowestGain>,<noiseLowThreshold>,<noiseHighThreshold>,<loudThreshold>
```
The defaults are:
```
--gain=auto-verbose,0,25,31,243
```
The thresholds are numbers 0 to 256, tweaking them requires some understanding of how it works.
One option would be to change the noise thresholds up or down and then observe the log.
There should be no need to tweak these parameters.

## rtl-sdr bias tee

Use this utility independent of readsb:
https://github.com/wiedehopf/adsb-wiki/wiki/RTL-Bias-Tee

## Global map of aircraft

One of this forks main uses is to be the backend of a global map.
For that purpose it's used in conjunction with tar1090 with some extra options to cope with the number of aircraft and also record a history of flight paths: https://github.com/wiedehopf/tar1090#0800-destroy-sd-card

Websites using this software:

- https://adsb.lol/
- https://globe.airplanes.live/
- https://globe.adsb.fi/
- https://globe.adsbexchange.com/

Projects that use or have used data generated by this software:

- https://gpsjam.org/
- https://adsb.exposed/
- https://tech.marksblogg.com/global-flight-tracking-adsb.html

## Saving traces

History function as used by several online aggregators using tar1090.

Warning: the following will generate several thousand files a day and can use significant amounts of
disk space depending on your data source.

The following command line options need to be added to for example the decoder options in `/etc/default/readsb`
```
--write-globe-history /var/globe_history --heatmap 30
```
To increase time resolution to maximum, you can add `--json-trace-interval=0.1` which will add every
position received to traces. The heatmap interval can also be reduced from the default of 30
seconds, i wouldn't recommend less than 5 seconds for that though.

Aggregators will generally use `--write-json-globe-index` as well but that's not necessary if you don't have more than 500 concurrent planes.

/var/globe_history needs to be a directory writeable by the user readsb.
`sudo mkdir /var/globe_history` and `sudo chown readsb /var/globe_history` are useful for that.

You should also download
```
wget -O /usr/local/share/tar1090/aircraft.csv.gz https://github.com/wiedehopf/tar1090-db/raw/csv/aircraft.csv.gz
```

and add this command line option (for exaple via /etc/default/readsb):
```
--db-file /usr/local/share/tar1090/aircraft.csv.gz
```

This will obviously write data to the hard drive, be aware of that.
The data format is subject to change, don't expect this to be stable.
Be aware of that when upgrading either tar1090 or readsb to a new commit.

If you're using --write-json-globe-index, it's also recommended to use --tar1090-use-api
It will use the readsb API to get data, it's less requests and usually more efficient,
for details see the file nginx-readsb-api.conf
(this needs adding to your existing nginx tar1090 configuration, this is only for people who really know their stuff anyway)

If configuring this stuff seems complicated, consider just using the sdr-enthusiasts ultrafeeder
container. Just don't configure feeds for aggregated data or if you don't want to feed data from
there.

## non-SDR data source

If you don't want readsb to read data from the SDR, you'll also need to change the receiver options line to something like this:
```
RECEIVER_OPTIONS="--net-only --net-connector 192.168.2.7,30005,beast_in"
```
If you have another dump1090/readsb running on the same machine, you'll also need to change all the ports to avoid conflicts.


## --debug=S: speed check debugging output

For current reference please see the speed_check function.

hex

SQ means same quality (ADS-B vs MLAT and stuff)
LQ means lower quality

fail / ok
ok means speed check passed (displayed only with cpr-focus)

A means airborne and S means surface.

reliable is my reliability counter
every good position increases each aircrafts position reliability,
if it gets to zero, speed check is no longer applied and it's allowed to "JUMP",
"JUMP" is also allowed if we haven't had a position for 2 minutes.

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


## Apple / macOS / OS X

Thank you to https://github.com/ind006/readsb_macos/ for all the shims needed to make this work.

readsb should be (largely) compatible with macOS, on both Intel and ARM architectures.
You may need to modify the Makefile to point at where on your mac you have the standard libs and includes installed.
This in turn depends on whether you're using macports or homebrew for those libs and includes. The Makefile has paths
pre-set for homebrew.

These packages are needed, possibly more:
```
brew install git librtlsdr libusb ncurses
```

Build using:
```
make -j4 RTLSDR=yes
```

You can run it from the command line (try `screen -S readsb` and run it in there, press ctrl-A to detach the terminal)
Example command line:
```sh
./readsb --quiet --net --device-type rtlsdr --gain auto
# add console table of planes for a quick test:
--interactive
# optionally add coordinates:
--lat -33.874 --lon 151.206
# add --interactive for testing, it will show a list of planes in the terminal
# optional output of json and other regularly updated files:
--write-json-every 0.5 --write-json=~/tar1090/html/data/
# optional database info
--db-file ~/tar1090/aircraft.csv.gz
# optional listen ports:
--net-ri-port 30001 --net-ro-port 30002 --net-sbs-port 30003 --net-bi-port 30004,30104 --net-bo-port 30005
# optional sending of data to an aggregator:
--net-connector feed.flyrealtraffic.com,30004,beast_reduce_plus_out,7817bd08-f226-11ef-ba9e-072eee452592
```

For a graphical interface, the tar1090 webinterface is recommended: https://github.com/wiedehopf/tar1090
The install script won't work so i'd recommend the following basic webserver configuration:
- serve the html directory as /tar1090
- serve the write-json directory as /tar1090/data

```
git clone --depth 1 https://github.com/wiedehopf/tar1090 ~/tar1090
wget -O ~/tar1090/aircraft.csv.gz https://github.com/wiedehopf/tar1090-db/raw/csv/aircraft.csv.gz
```

Simple http server using python reachable using http://localhost:8081
```
cd ~/tar1090/html
python3 -m http.server 8081
```


Using nginx this would look something like this (replace USER appropriately):
```
location /tar1090/data/ {
    alias /home/USER/tar1090/data/;
    add_header Cache-Control "no-cache";
    location /tar1090/data/traces/ {
        gzip off;
        add_header Content-Encoding "gzip";
        add_header Cache-Control "no-cache";
    }
}
location /tar1090/globe_history/ {
    alias /home/USER/readsb_history/;
    gzip off;
    add_header Content-Encoding "gzip";
    add_header Cache-Control "no-cache";
}
location /tar1090 {
    alias /home/USER/tar1090/html/;
    add_header Cache-Control "no-cache";
}
```

An easy way to add some traces when selecting a plane (with nginx):
Add `--write-globe-history=/home/USER/readsb_history` to the readsb command line.
You can also serve this folder as /tar1090/globe_history but that's only required for the history going back further
than 24h.

The classical tar1090 uses traces created via a shell script and served at /tar1090/chunks but running that shell
script is probably a hassle, so just use the above.


## readsb --help

might be out of date, check the command on a freshly compiled version

```
Usage: readsb [OPTIONS...] 
readsb Mode-S/ADSB/TIS Receiver   
Build options: ENABLE_RTLSDR 


General options:
  --lat=<lat>                                                    Reference/receiver surface latitude
  --lon=<lon>                                                    Reference/receiver surface longitude
  --no-interactive                                               Disable interactive mode, print to stdout
  --interactive-ttl=<sec>                                        Remove from list if idle for <sec> (default: 60)
  --modeac                                                       Enable decoding of SSR Modes 3/A & 3/C
  --modeac-auto                                                  Enable Mode A/C if requested by a Beast connection
  --max-range=<dist>                                             Absolute maximum range for position decoding (in nm, default: 300)
  --fix                                                          Enable CRC single-bit error correction (default)
  --no-fix                                                       Disable CRC single-bit error correction
  --no-fix-df                                                    Disable CRC single-bit error correction on the DF type to produce more DF17 messages (disabling reduces CPU usage)
  --metric                                                       Use metric units
  --show-only=<addr>                                             Show only messages by given ICAO on stdout
  --process-only=<addr>                                          Process only messages by given ICAO
  --filter-DF=<type>                                             When displaying decoded ModeS messages on stdout only show this DF type
  --device-type=<type>                                           Select SDR type (this needs to be placed on the command line before any SDR type specific options)
  --gain=<db>                                                    Set gain (default: auto gain, possible values for rtl-sdr devices: auto auto-verbose 0.0 0.9 1.4 2.7 3.7 7.7 8.7 12.5 14.4 15.7 16.6 19.7 20.7 22.9 25.4 28.0 29.7 32.8 33.8 36.4 37.2 38.6 40.2 42.1 43.4 43.9 44.5 48.0 49.6 58)
  --freq=<hz>                                                    Set frequency (default: 1090 MHz)
  --interactive                                                  Interactive mode refreshing data on screen. Implies --throttle
  --raw                                                          Show only messages hex values
  --preamble-threshold=<40-400>                                  lower threshold --> more CPU usage (default: 58, pi zero / pi 1: 75, hot CPU 42)
  --forward-mlat                                                 Forward received beast mlat results to beast output ports
  --forward-mlat-sbs                                             Forward received mlat results to sbs output ports
  --stats                                                        Print stats at exit. No other output
  --stats-range                                                  Collect/show range histogram
  --stats-every=<sec>                                            Show and reset stats every <sec> seconds (rounded to the nearest 10 seconds due to implementation, first inteval can be up to 5 seconds shorter)
  --auto-exit=<sec>                                              Run for X seconds, then exit (default: run indefinitely)
  --range-outline-hours=<hours>                                  Make the range outline retain data for the last X hours (float, default: 24.0)
  --onlyaddr                                                     Show only ICAO addresses
  --gnss                                                         Show altitudes as GNSS when available
  --snip=<level>                                                 Strip IQ file removing samples < level
  --debug=<flags>                                                Debug mode (verbose), n: network, P: CPR, S: speed check
  --devel=<mode>                                                 Development debugging mode, see source for options, can be specified more than once
  --receiver-focus=<receiverId>                                  only process messages from receiverId
  --cpr-focus=<hex>                                              show CPR details for this hex
  --leg-focus=<hex>                                              show leg marking details for this hex
  --trace-focus=<hex>                                            show traceAdd details for this hex
  --quiet                                                        Disable output (default)
  --write-json=<dir>                                             Periodically write json output to <dir>
  --write-prom=<file>                                            Periodically write prometheus output to <file>
  --write-globe-history=<dir>                                    Write traces to this directory, 1 gz compressed json per day and airframe
  --write-state=<dir>                                            Write state to disk to have traces after a restart
  --write-state-every=<seconds>                                  Continuously write state to disk every X seconds (default: 3600)
  --write-state-only-on-exit                                     Don't continously update state.
  --heatmap-dir=<dir>                                            Change the directory where heatmaps are saved (default is in globe history dir)
  --heatmap=<interval in seconds>                                Make Heatmap, each aircraft at most every interval seconds (creates historydir/heatmap.bin and exit after that)
  --dump-beast=<dir>,<interval>,<compressionLevel>               Dump compressed beast files to this directory, start a new file evey interval seconds
  --write-json-every=<sec>                                       Write json output and update API json every sec seconds (default 1)
  --json-location-accuracy=<n>                                   Accuracy of receiver location in json metadata: 0=no location, 1=approximate, 2=exact
  --ac-hash-bits=<n>                                             Main hash map size: 2^n entries (default: AIRCRAFT_HASH_BITS)
  --write-json-globe-index                                       Write specially indexed globe_xxxx.json files (for tar1090)
  --write-receiver-id-json                                       Write receivers.json
  --json-trace-interval=<seconds>                                Interval after which a new position will guaranteed to be written to the trace and the json position output (default: 30)
  --json-trace-hist-only=1,2,3,8                                 Don't write recent(1), full(2), either(3) traces to /run, only archive via write-globe-history (8: irregularly write limited traces to run, subject to change)
  --write-json-gzip                                              Write aircraft.json also as aircraft.json.gz
  --write-json-binCraft-only=<n>                                 Use only binary binCraft format for globe files (1), for aircraft.json as well (2)
  --write-binCraft-old                                           write old gzipped binCraft files

  --json-reliable=<n>                                            Minimum position reliability to put it into json (default: 1, globe options will default set this to 2, disable speed filter: -1, max: 4)
  --position-persistence=<n>                                     Position persistence against outliers (default: 4), incremented by json-reliable minus 1
  --jaero-timeout=<n>                                            How long in minutes JAERO positions remain valid and on the map in tar1090 (default:33)
  --db-file=<file.csv.gz>                                        Default: "none" (as of writing a compatible file is available here: https://github.com/wiedehopf/tar1090-db/tree/csv)
  --db-file-lt                                                   aircraft.json: add long type as field desc, add field ownOp for the owner, add field year

Network options:
  --net-connector=<ip,port,protocol>                             Establish connection, can be specified multiple times (e.g. 127.0.0.1,23004,beast_out) Protocols: beast_out, beast_in, raw_out, raw_in, sbs_in, sbs_in_jaero, sbs_out, sbs_out_jaero, vrs_out, json_out, gpsd_in, uat_in, uat_replay_out, planefinder_in, asterix_in, asterix_out (one failover ip/address,port can be specified: primary-address,primary-port,protocol,failover-address,failover-port) (any position in the comma separated list can also be either silent_fail or uuid=<uuid>)
  --net                                                          Enable networking
  --net-only                                                     Legacy Option, Enable networking, use --net instead
  --net-bind-address=<ip>                                        IP address to bind to (default: Any; Use 127.0.0.1 for private)
  --net-bo-port=<ports>                                          TCP Beast output listen ports (default: 0)
  --net-bi-port=<ports>                                          TCP Beast input listen ports  (default: 0)
  --net-ro-port=<ports>                                          TCP raw output listen ports (default: 0)
  --net-ri-port=<ports>                                          TCP raw input listen ports  (default: 0)
  --net-uat-replay-port=<ports>                                  UAT replay output listen ports (default: 0)
  --net-uat-in-port=<ports>                                      UAT input listen ports (default: 0)
  --net-sbs-port=<ports>                                         TCP BaseStation output listen ports (default: 0)
  --net-sbs-in-port=<ports>                                      TCP BaseStation input listen ports (default: 0)
  --net-sbs-jaero-port=<ports>                                   TCP SBS Jaero output listen ports (default: 0)
  --net-sbs-jaero-in-port=<ports>                                TCP SBS Jaero input listen ports (default: 0)
  --net-asterix-out-port=<ports>                                 TCP Asterix output listen ports (default: 0)
  --net-asterix-in-port=<ports>                                  TCP Asterix input listen ports (default: 0)
  --net-asterix-reduce                                           Apply beast reduce logic and interval to ASTERIX outputs
  --net-vrs-port=<ports>                                         TCP VRS json output listen ports (default: 0)
  --net-vrs-interval=<seconds>                                   TCP VRS json output interval (default: 5.0)
  --net-json-port=<ports>                                        TCP json position output listen ports, sends one line with a json object containing aircraft details for every position received (default: 0) (consider raising --net-ro-size to 8192 for less fragmentation if this is a concern)
  --net-json-port-interval=<seconds>                             Set minimum interval between outputs per aircraft for TCP json output, default: 0.0 (every position)
  --net-json-port-include-noposition                             TCP json position output: include aircraft without position (state is sent for aircraft for every DF11 with CRC if the aircraft hasn't sent a position in the last 10 seconds and interval allowing)
  --net-api-port=<port>                                          TCP API listen port (in contrast to other listeners, only a single port is allowed) (update frequency controlled by write-json-every parameter) (default: 0)
  --api-shutdown-delay=<seconds>                                 Shutdown delay to server remaining API queries, new queries get a 503 response (default: 0)
  --tar1090-use-api                                              when running with globe-index, signal tar1090 use the readsb API to get data, requires webserver mapping of /tar1090/re-api to proxy_pass the requests to the --net-api-port, see nginx-readsb-api.conf in the tar1090 repository for details
  --net-beast-reduce-out-port=<ports>                            TCP BeastReduce output listen ports (default: 0)
  --net-beast-reduce-interval=<seconds>                          BeastReduce data update interval, longer means less data (default: 0.250, valid range: 0.000 - 14.999)
  --net-beast-reduce-optimize-for-mlat                           BeastReduce output: keep all messages relevant to mlat-client
  --net-beast-reduce-filter-dist=<distance in nmi>               beast-reduce: remove aircraft which are further than distance from the receiver
  --net-beast-reduce-filter-alt=<pressure altitude in ft>        beast-reduce: remove aircraft which are above altitude
  --net-sbs-reduce                                               Apply beast reduce logic and interval to SBS outputs
  --net-receiver-id                                              forward receiver ID
  --net-ingest                                                   primary ingest node
  --net-garbage=<ports>                                          timeout receivers, output messages from timed out receivers as beast on <ports>
  --decode-threads=<n>                                           Number of decode threads, either 1 or 2 (default: 1). Only use 2 when you have beast traffic > 200 MBit/s, expect 1.4x speedup for 2x CPU
  --uuid-file=<path>                                             path to UUID file
  --net-ro-size=<size>                                           TCP output flush size (maximum amount of internally buffered data before writing to network) (default: 1280)
  --net-ro-interval=<seconds>                                    TCP output flush interval in seconds (maximum delay between placing data in the output buffer and sending)(default: 0.05, valid values 0.0 - 1.0)
  --net-ro-interval-beast-reduce=<seconds>                       TCP output flush interval in seconds for beast-reduce outputs (default: value from --net-ro-interval, valid values 0.0 - 1.0)
  --net-connector-delay=<seconds>                                Outbound re-connection delay (default: 15)
  --net-heartbeat=<rate>                                         TCP heartbeat rate in seconds (default: 60 sec; 0 to disable)
  --net-buffer=<n>                                               control some buffer sizes: 8KB * (2^n) (default: n=1, 16KB)
  --net-verbatim                                                 Forward messages unchanged
  --sdr-buffer-size=<KiB>                                        SDR buffer / USB transfer size in kibibytes (default: 256 which is equivalent to around 54 ms using rtl-sdr, option might be ignored in future versions)

RTL-SDR options:

use with --device-type rtlsdr
  --device=<index|serial>                                        Select device by index or serial number
  --enable-agc                                                   Enable digital AGC (not tuner AGC!)
  --ppm=<correction>                                             Set oscillator frequency correction in PPM

Modes-S Beast options, use with --device-type modesbeast:
  --beast-serial=<path>                                          Path to Beast serial device (default /dev/ttyUSB0)
  --beast-df1117-on                                              Turn ON DF11/17-only filter
  --beast-mlat-off                                               Turn OFF MLAT time stamps
  --beast-crc-off                                                Turn OFF CRC checking
  --beast-df045-on                                               Turn ON DF0/4/5 filter
  --beast-fec-off                                                Turn OFF forward error correction
  --beast-modeac                                                 Turn ON mode A/C
  --beast-baudrate=<baud>                                        Override Baudrate (default rate 3000000 baud)

GNS HULC options, use with --device-type gnshulc:

Beast binary and HULC protocol input with hardware handshake enabled.
  --beast-serial=<path>                                          Path to GNS HULC serial device (default /dev/ttyUSB0)

ifile-specific options, use with --device-type ifile:
  --ifile=<path>                                                 Read samples from given file ('-' for stdin)
  --iformat=<type>                                               Set sample format (UC8, SC16, SC16Q11)
  --throttle                                                     Process samples at the original capture speed

Help options:
  --help                                                         Give this help list
  --usage                                                        Give a short usage message

Report bugs to Matthias Wirth <matthias.wirth@gmail.com>
```
