# JSON output formats

readsb generates several json files with informaton about the receiver itself, currently known aircraft,
and general statistics. These are used by the webmap, but could also be used by other things feeds stats
about readsb operation to collectd for later graphing.

## Reading the json files

There are two ways to obtain the json files:

 * By HTTP from the external webserver that readsb is feeding. The json is served from the data/ path, e.g. http://somehost/readsb/data/aircraft.json
 * As a file in the directory specified by --write-json on readsb command line.

The HTTP versions are always up to date.
The file versions are written periodically; for aircraft, typically once a second, for stats, once a minute.
The file versions are updated to a temporary file, then atomically renamed to the right path, so you should never see partial copies.

Each file contains a single JSON object. The file formats are:

## receiver.json

This file has general metadata about readsb. It does not change often and you probably just want to read it once at startup.
The keys are:

 * version: the version of readsb in use
 * refresh: how often aircraft.json is updated (for the file version), in milliseconds. the webmap uses this to control its refresh interval.
 * history: the current number of valid history files (see below)
 * lat: the latitude of the receiver in decimal degrees. Optional, may not be present.
 * lon: the longitude of the receiver in decimal degrees. Optional, may not be present.

## aircraft.json and --json-port

- --json-port will supply one aircraft object per line, each aircraft object has it's own now as a timestamp
- aircraft.json contains recently seen aircraft. The keys are:

 * now: the time this file was generated, in seconds since Jan 1 1970 00:00:00 GMT (the Unix epoch).
 * messages: the total number of Mode S messages processed since readsb started.
 * aircraft: an array of JSON objects, one per known aircraft. Each aircraft has the following keys. Keys will be omitted if data is not available.
   * hex: the 24-bit ICAO identifier of the aircraft, as 6 hex digits. The identifier may start with '~', this means that the address is a non-ICAO address (e.g. from TIS-B).
   * type: type of underlying messages / best source of current data for this position / aircraft:
            (the following list is in order of which data is preferentially used)
     * adsb_icao: messages from a Mode S or ADS-B transponder, using a 24-bit ICAO address
     * adsb_icao_nt: messages from an ADS-B equipped "non-transponder" emitter e.g. a ground vehicle, using a 24-bit ICAO address
     * adsr_icao: rebroadcast of ADS-B messages originally sent via another data link e.g. UAT, using a 24-bit ICAO address
     * tisb_icao: traffic information about a non-ADS-B target identified by a 24-bit ICAO address, e.g. a Mode S target tracked by secondary radar

     * adsc: ADS-C (received by monitoring satellite downlinks)
     * mlat: MLAT, position calculated arrival time differences using multiple receivers, outliers and varying accuracy is expected.
     * other: miscellaneous data received via Basestation / SBS format, quality / source is unknown.
     * mode_s: ModeS data from the planes transponder (no position transmitted)

     * adsb_other: messages from an ADS-B transponder using a non-ICAO address, e.g. anonymized address
     * adsr_other: rebroadcast of ADS-B messages originally sent via another data link e.g. UAT, using a non-ICAO address
     * tisb_other: traffic information about a non-ADS-B target using a non-ICAO address
     * tisb_trackfile: traffic information about a non-ADS-B target using a track/file identifier, typically from primary or Mode A/C radar
   * flight: callsign, the flight name or aircraft registration as 8 chars (2.2.8.2.6)
   * alt_baro: the aircraft barometric altitude in feet as a number OR "ground" as a string
   * alt_geom: geometric (GNSS / INS) altitude in feet referenced to the WGS84 ellipsoid
   * gs: ground speed in knots
   * ias: indicated air speed in knots
   * tas: true air speed in knots
   * mach: Mach number
   * track: true track over ground in degrees (0-359)
   * track_rate: Rate of change of track, degrees/second
   * roll: Roll, degrees, negative is left roll
   * mag_heading: Heading, degrees clockwise from magnetic north
   * true_heading: Heading, degrees clockwise from true north (usually only transmitted on ground, in the air usually derived from the magnetic heading using magnetic model WMM2020)
   * baro_rate: Rate of change of barometric altitude, feet/minute
   * geom_rate: Rate of change of geometric (GNSS / INS) altitude, feet/minute
   * squawk: Mode A code (Squawk), encoded as 4 octal digits
   * emergency: ADS-B emergency/priority status, a superset of the 7x00 squawks (2.2.3.2.7.8.1.1) (none, general, lifeguard, minfuel, nordo, unlawful, downed, reserved)
   * category: emitter category to identify particular aircraft or vehicle classes (values A0 - D7) (2.2.3.2.5.2)
   * nav_qnh: altimeter setting (QFE or QNH/QNE), hPa
   * nav_altitude_mcp: selected altitude from the Mode Control Panel / Flight Control Unit (MCP/FCU) or equivalent equipment
   * nav_altitude_fms: selected altitude from the Flight Manaagement System (FMS) (2.2.3.2.7.1.3.3)
   * nav_heading: selected heading (True or Magnetic is not defined in DO-260B, mostly Magnetic as that is the de facto standard) (2.2.3.2.7.1.3.7)
   * nav_modes: set of engaged automation modes: 'autopilot', 'vnav', 'althold', 'approach', 'lnav', 'tcas'
   * lat, lon: the aircraft position in decimal degrees
   * nic: Navigation Integrity Category (2.2.3.2.7.2.6)
   * rc: Radius of Containment, meters; a measure of position integrity derived from NIC & supplementary bits. (2.2.3.2.7.2.6, Table 2-69)
   * seen_pos: how long ago (in seconds before "now") the position was last updated
   * track: true track over ground in degrees (0-359)
   * version: ADS-B Version Number 0, 1, 2 (3-7 are reserved) (2.2.3.2.7.5)
   * nic_baro: Navigation Integrity Category for Barometric Altitude (2.2.5.1.35)
   * nac_p: Navigation Accuracy for Position (2.2.5.1.35)
   * nac_v: Navigation Accuracy for Velocity (2.2.5.1.19)
   * sil: Source Integity Level (2.2.5.1.40)
   * sil_type: interpretation of SIL: unknown, perhour, persample
   * gva: Geometric Vertical Accuracy  (2.2.3.2.7.2.8)
   * sda: System Design Assurance (2.2.3.2.7.2.4.6)
   * mlat: list of fields derived from MLAT data
   * tisb: list of fields derived from TIS-B data
   * messages: total number of Mode S messages received from this aircraft
   * seen: how long ago (in seconds before "now") a message was last received from this aircraft
   * rssi: recent average RSSI (signal power), in dbFS; this will always be negative.
   * alert: Flight status alert bit (2.2.3.2.3.2)
   * spi: Flight status special position identification bit (2.2.3.2.3.2)
   * wd, ws: wind direction and wind speed are calculated from ground track, true heading, true airspeed and ground speed
   * oat, tat: outer/static air temperature (C) and total air temperature (C) are calculated from mach number and true airspeed (typically somewhat inaccurate at lower altitudes / mach numbers below 0.5, calculation is inhibited for mach < 0.395)
   * acas_ra: experimental, subject to change, see format here: https://github.com/wiedehopf/readsb/blob/ca5b8257bb6176854eb18ecd96761e107fbb12fa/json_out.c#L249
   * gpsOkBefore: experimental, subject to change: aircraft lost GPS / GPS heavily degraded, it was working well before this timestamp, only displayed for 15 min after GPS is lost / degraded
   * seenByReceiverIds: An array of receiver ids that have provided updates for the aircraft along with their last timestamp. Only present if --aircraft-json-seen-by-list is provided.

(Section references (2.2.xyz) refer to DO-260B.)


   If used with --db-file using a aircraft.csv.gz from the tar1090-db repository (csv branch), these additional fields will be available if the aircraft is in the database:
   * r: aircraft registration pulled from database
   * t: aircraft type pulled from database
   * (optional with --db-file-lt: desc: long type name)
   * dbFlags: bitfield for certain database flags, below & must be a bitwise and ... check the documentation for your programming language:

   ```
      military = dbFlags & 1;
      interesting = dbFlags & 2;
      PIA = dbFlags & 4;
      LADD = dbFlags & 8;
   ```

   * lastPosition: {lat, lon, nic, rc, seen_pos} when the regular lat and lon are older than 60 seconds they are no longer considered valid, this will provide the last position and show the age for the last position. aircraft will only be in the aircraft json if a position has been received in the last 60 seconds or if any message has been received in the last 30 seconds.

  * rr_lat, rr_lon: If no ADS-B or MLAT position available, a rough estimated position for the aircraft based on the receiver’s estimated coordinates. (If used with multiple receivers / as an aggregation server with --net-ingest --net-receiver-id)

## --net-api-port query formats

  * opens a builtin webserver that can handle a couple query formats
  * for more info on nginx proxy_pass and technical details, see README-api.md
  * now: the time the api data was cached, in seconds since Jan 1 1970 00:00:00 GMT (the Unix epoch).
  * api caching frequency is controlled by --write-json-every
  * resultCount: number of aircraft in the aircraft array
  * ptime: time in milliseconds it took to parse the request and create the json output

  ```
  --net-api-port 8042
  curl -sS 'http://localhost:8042/?hexlist=3CD6E3'  | jq
  /?circle=<lat>,<lon>,<radius in nmi>
  /?closest=<lat>,<lon>,<radius in nmi>
  /?box=<lat south>,<lat north>,<lon west>,<lon east>
  /?all_with_pos
  /?all
  /?find_hex=<hex1>,<hex2>,....
  /?find_callsign=<callsign1>,<callsign2>,.....
  /?find_reg=<reg1>,<reg2>,.....
  /?find_type=<type1>,<type2>,.....
  ```
  * circle returns all aircraft within radius nautical miles of lat, lon
  * closest is the same as circle but only returning the closest aircraft
  * box is will give you all aircraft within a rectangle delimited by 2 latitudes and longitudes
  * closest and circle will supply an extra field named "dst" which will have the distance in nautical miles from the supplied location
  * all_with_pos will return all aircraft for which we have received a position in the last minute or last 40 minutes for ADS-C
  * all will return all aircraft returned by all_with_pos and all aircraft with ModeS messages received in the last 30 seconds
  * find_hex (alias: hexList) will return all aircraft with an exact match on one of the given hex / ICAO ids (limited to 1000)
  * find_callsign will return all aircraft with an exact match on one of the given callsigns (limited to 1000 or 8000 characters for the request)
  * find_reg will return all aircraft with an exact match on one of the given registrations (limited to 1000 or 8000 characters for the request)
  * find_type will return all aircraft that have one of the specified icao type codes (A321, B738, .....)


  For circle and closest the following two fields are added to each aircraft object:
  * dst: distance from supplied center point in nmi
  * dir: true direction of the aircraft from the supplied center point (degrees)

  To the above base queries you can add these filteroptions
  ```
  &filter_callsign_exact=<callsign>
  &filter_callsign_prefix=<prefix>
  &filter_squawk=<squawk>
  &filter_with_pos
  &filter_type=<type1>,<type2>,.....
  &below_alt_baro=<altitude in feet>
  &above_alt_baro=<altitude in feet>
  ```
  filter any of the base queries for:
  * an exact callsign match (multiple exact matches possible)
  * all callsigns that start with <prefix>
  * a specific squawk code
  * only return aircraft that have a valid position
  * only return aircraft that match one of the icao type codes
  * only above and / or below a certain altitude in ft (uncorrected barometric altitude, ground treated as 0 ft for simplicity / versatility)

  ```
  &filter_mil
  &filter_pia
  &filter_ladd
  ```
  filter any of the base queries for these database flags:
  * filter_mil will return military aircraft
  * filter_pia using a PIA hex code
  * filter_ladd will return aircraft on the LADD list
  these three filter options can be combined in any combination and will be connected by an OR
  in contrast, when combining other filters they restrict an already filtered result


  ```
  &jv2
  ```
  * Change json syntax to be compatible with adsbexchange v2 API output
  * now: same as normal BUT in milliseconds
  * total: number of aircraft in the aircraft array
  * ptime: time in milliseconds it took to parse the request and create the json output

  ```
  ?status
  ```
  * Status code 200 during normal operation

## history_0.json, history_1.json, ..., history_119.json

These files are historical copies of aircraft.json at (by default) 30 second intervals. They follow exactly the
same format as aircraft.json. To know how many are valid, see receiver.json ("history" value). They are written in
a cycle, with history_0 being overwritten after history_119 is generated, so history_0.json is not necessarily the
oldest history entry. To load history, you should:

 * read "history" from receiver.json.
 * load that many history_N.json files
 * sort the resulting files by their "now" values
 * process the files in order

## trace jsons

 * overall structure
```
{
    icao: "0123ac", // hex id of the aircraft
    timestamp: 1609275898.495, // unix timestamp in seconds since epoch (1970)
    trace: [
        [ seconds after timestamp,
            lat,
            lon,
            altitude in ft or "ground" or null,
            ground speed in knots or null,
            track in degrees or null, (if altitude == "ground", this will be true heading instead of track)
            flags as a bitfield: (use bitwise and to extract data)
                (flags & 1 > 0): position is stale (no position received for 20 seconds before this one)
                (flags & 2 > 0): start of a new leg (tries to detect a separation point between landing and takeoff that separates fligths)
                (flags & 4 > 0): vertical rate is geometric and not barometric
                (flags & 8 > 0): altitude is geometric and not barometric
             ,
            vertical rate in fpm or null,
            aircraft object with extra details or null (see aircraft.json documentation, note that not all fields are present as lat and lon for example arlready in the values above),
            // the following fields only in files generated 2022 and later:
            type / source of this position or null,
            geometric altitude or null,
            geometric vertical rate or null,
            indicated airspeed or null,
            roll angle or null
        ],
        [next entry like the one before],
        [next entry like the one before],
    ]
}
```

Example :
```json
{
    "icao":"3c66b0",
    "r":"D-AIUP",
    "t":"A320",
    "dbFlags":0,
    "desc":"AIRBUS A-320",
    "timestamp": 1663259853.016,
    "trace":[
        [7016.59,49.263300,10.614239,25125,446.5,309.0,0,-2176,
            {"type":"adsb_icao","flight":"DLH7YA  ","alt_geom":25875,"ias":335,"tas":484,"mach":0.796,"wd":297,"ws":40,"oat":-30,"tat":1,"track":309.00,"track_rate":-0.53,"roll":-10.72,"mag_heading":304.28,"true_heading":308.02,"baro_rate":-2176,"geom_rate":-2208,"squawk":"1000","category":"A3","nav_qnh":1012.8,"nav_altitude_mcp":14016,"nic":8,"rc":186,"version":2,"nic_baro":1,"nac_p":8,"nac_v":0,"sil":3,"sil_type":"perhour","gva":2,"sda":2,"alert":0,"spi":0},
            "adsb_icao",25875,-2208,335,-10.7],
        [7024.85,49.273589,10.593278,24825,446.0,306.6,0,-2176,null,"adsb_icao",25550,-2144,337,-1.6],
        [7035.67,49.286865,10.565890,24425,446.8,306.5,0,-2176,null,"adsb_icao",25150,-2144,339,0.3],
        [7046.71,49.300403,10.537985,24025,446.8,306.5,0,-2176,null,"adsb_icao",24775,-2176,341,0.3],
        [7057.80,49.314042,10.509941,23625,445.2,306.7,0,-2176,
            {"type":"adsb_icao","flight":"DLH7YA  ","alt_geom":24325,"ias":339,"tas":482,"mach":0.784,"wd":296,"ws":37,"oat":-24,"tat":6,"track":306.69,"track_rate":0.00,"roll":0.18,"mag_heading":302.17,"true_heading":305.89,"baro_rate":-2176,"geom_rate":-2176,"squawk":"1000","category":"A3","nav_qnh":1012.8,"nav_altitude_mcp":14016,"nic":8,"rc":186,"version":2,"nic_baro":1,"nac_p":8,"nac_v":0,"sil":3,"sil_type":"perhour","gva":2,"sda":2,"alert":0,"spi":0},
            "adsb_icao",24325,-2176,339,0.2],
        [7068.82,49.327469,10.482225,23250,443.2,306.6,0,-2112,null,"adsb_icao",23925,-2144,340,0.2],
        [7080.53,49.341694,10.452841,22875,441.2,306.4,0,-1728,null,"adsb_icao",23550,-1728,341,-0.2]
}
```

## stats.json

This file contains statistics about readsb operations.

There are 5 top level keys: "latest", "last1min", "last5min", "last15min", "total". Each key has statistics for a different period, defined by the "start" and "end" subkeys:

 * "total" covers the entire period from when readsb was started up to the current time
 * "last1min" covers a recent 1-minute period. This may be up to 1 minute out of date (i.e. "end" may be up to 1 minute old).
 * "last5min" covers a recent 5-minute period. As above, this may be up to 1 minute out of date.
 * "last15min" covers a recent 15-minute period. As above, this may be up to 1 minute out of date.
 * "latest" covers the time between the end of the "last1min" period and the current time.

Internally, live stats are collected into "latest". Once a minute, "latest" is copied to "last1min" and "latest" is reset. Then "last5min" and "last15min" are recalculated from a history of the last 5 or 15 1-minute periods.

Each period has the following subkeys:

 * start: the start time (in seconds-since-1-Jan-1970) of this statistics collection period.
 * end: the end time (in seconds-since-1-Jan-1970) of this statistics collection period.
 * local: statistics about messages received from a local SDR dongle. Not present in --net-only mode. Has subkeys:
   * blocks_processed: number of sample blocks processed
   * blocks_dropped: number of sample blocks dropped before processing. A nonzero value means CPU overload.
   * modeac: number of Mode A / C messages decoded
   * modes: number of Mode S preambles received. This is *not* the number of valid messages!
   * bad: number of Mode S preambles that didn't result in a valid message
   * unknown_icao: number of Mode S preambles which looked like they might be valid but we didn't recognize the ICAO address and it was one of the message types where we can't be sure it's valid in this case.
   * accepted: array. Index N has the number of valid Mode S messages accepted with N-bit errors corrected.
   * signal: mean signal power of successfully received messages, in dbFS; always negative.
   * peak_signal: peak signal power of a successfully received message, in dbFS; always negative.
   * strong_signals: number of messages received that had a signal power above -3dBFS.
 * remote: statistics about messages received from remote clients. Only present in --net or --net-only mode. Has subkeys:
   * modeac: number of Mode A / C messages received.
   * modes: number of Mode S messages received.
   * bad: number of Mode S messages that had bad CRC or were otherwise invalid.
   * unknown_icao: number of Mode S messages which looked like they might be valid but we didn't recognize the ICAO address and it was one of the message types where we can't be sure it's valid in this case.
   * accepted: array. Index N has the number of valid Mode S messages accepted with N-bit errors corrected.
   * http_requests: number of HTTP requests handled.
 * cpu: statistics about CPU use. Has subkeys:
   * demod: milliseconds spent doing demodulation and decoding in response to data from a SDR dongle
   * reader: milliseconds spent reading sample data over USB from a SDR dongle
   * background: milliseconds spent doing network I/O, processing received network messages, and periodic tasks.
 * cpr: statistics about Compact Position Report message decoding. Has subkeys:
   * surface: total number of surface CPR messages received
   * airborne: total number of airborne CPR messages received
   * global_ok: global positions successfuly derived
   * global_bad: global positions that were rejected because they were inconsistent
     * global_range: global positions that were rejected because they exceeded the receiver max range
     * global_speed: global positions that were rejected because they failed the inter-position speed check
   * global_skipped: global position attempts skipped because we did not have the right data (e.g. even/odd messages crossed a zone boundary)
   * local_ok: local (relative) positions successfully found
     * local_aircraft_relative: local positions found relative to a previous aircraft position
     * local_receiver_relative: local positions found relative to the receiver position
   * local_skipped: local (relative) positions not used because we did not have the right data
     * local_range: local positions not used because they exceeded the receiver max range or fell into the ambiguous part of the receiver range
     * local_speed: local positions not used because they failed the inter-position speed check
   * filtered: number of CPR messages ignored because they matched one of the heuristics for faulty transponder output
 * tracks: statistics on aircraft tracks. Each track represents a unique aircraft and persists for up to 5 minutes after the last message
   from the aircraft is heard. If messages from the same aircraft are subsequently heard after the 5 minute period, this will be counted
   as a new track.
   * all: total tracks created
   * single_message: tracks consisting of only a single message. These are usually due to message decoding errors that produce a bad aircraft address.
 * messages: total number of messages accepted by readsb from any source

## minimal example on how to use python to process aircraft.json:

```
from contextlib import closing
from urllib.request import urlopen, URLError
import json


url="http://192.168.2.14/tar1090/data/aircraft.json"
with closing(urlopen(url, None, 5.0)) as aircraft_file:
    aircraft_data = json.load(aircraft_file)

for a in aircraft_data['aircraft']:
   hex = a.get('hex')
   lat = a.get('lat')
   lon = a.get('lon')
   if lat and lon:
      print("Icao 24 bit id: {hex} Latitude: {lat:.4f} Longitude: {lon:.4f}".format(hex=hex, lat=lat, lon=lon))
```
