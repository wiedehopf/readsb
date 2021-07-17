# Readsb

This is a detached fork of https://github.com/Mictronics/readsb

It's continually under development, expect bugs, segfaults and all the good stuff :)

## NO WARRANTY

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
see the LICENSE file for details


## how to install / build

I'd recommend this script to automatically install it:
- https://github.com/wiedehopf/adsb-scripts/wiki/Automatic-installation-for-readsb

How to build the package yourself:
- https://github.com/wiedehopf/adsb-wiki/wiki/Building-readsb-from-source

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
