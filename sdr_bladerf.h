// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// sdr_bladerf.h: bladeRF support (header)
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
//
// Copyright (c) 2017 FlightAware LLC
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

#ifndef BLADERF_H
#define BLADERF_H

// Support for the Nuand bladeRF SDR

void bladeRFInitConfig ();
bool bladeRFHandleOption (int argc, char *argv);
bool bladeRFOpen ();
void bladeRFRun ();
void bladeRFClose ();

#endif
