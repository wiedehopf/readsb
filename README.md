# Readsb

This is a detached fork of https://github.com/Mictronics/readsb

It's main purpose is to be the backend for the global map at https://adsbexchange.com/
For that purpose it's used in conjunction with tar1090: https://github.com/wiedehopf/tar1090#0800-destroy-sd-card

It's continually under development, expect bugs, segfaults and all the good stuff :)

## NO WARRANTY

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
see the LICENSE file for details

### Push server support

readsb tries to connect to a listening server, like a VRS push server.

For example feeding VRS at adsbexchange.com use the new parameters:
```
--net-connector feed.adsbexchange.com,30005,beast_out
```

### BeastReduce output

Selectively forwards beast messages if the received data hasn't been forwarded in the last 125 ms (or `--net-beast-reduce-interval`).
Data not related to the physical aircraft state are only forwarded every 500 ms (4 * `--net-beast-reduce-interval`).The messages of
this output are normal beast messages and compatible with every program able to receive beast messages.

## readsb Debian/Raspbian packages

It is designed to build as a Debian package.

## Building under jessie, stretch or buster


### Dependencies - rtlsdr

This is packaged with jessie. "sudo apt-get install librtlsdr-dev"

### Actually building it

Build package with no additional receiver library dependencies: `dpkg-buildpackage -b`.

Build with RTLSDR support: `dpkg-buildpackage -b --build-profiles=rtlsdr`

## Building manually

You can probably just run "make". By default "make" builds with no specific library support. See below.
Binaries are built in the source directory; you will need to arrange to
install them (and a method for starting them) yourself.

"make RTLSDR=yes" will enable rtl-sdr support and add the dependency on
librtlsdr.

## Configuration

After installation, either by manual building or from package, you need to configure readsb service and web application.

Edit `/etc/default/readsb` to set the service options, device type, network ports etc.

The web application is configured by editing `/usr/share/readsb/html/script/readsb/defaults.js` or `src/script/readsb/default.ts`
prior to compilation. Several settings can be modified through web browser. These settings are stored inside browser indexedDB
and are individual to users or browser profiles.

## Note about bias tee support

Bias tee support is available for RTL-SDR.com V3 dongles. If you wish to enable bias tee support,
you must ensure that you are building this package with a version of librtlsdr installed that supports this capability.
You can find suitable source packages [here](https://github.com/librtlsdr/librtlsdr). To enable the necessary
support code when building, be sure to include preprocessor define macro HAVE_BIASTEE, e.g.:

"make HAVE_BIASTEE=yes" will enable biastee support for RTLSDR interfaces.

## Credits

- Michael Wolf (mictronics.de)
- Matthias Wirth aka wiedehopf
- Taner Halicioglu aka tanerH

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
