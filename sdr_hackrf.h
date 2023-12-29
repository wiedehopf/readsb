// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// sdr_hackrf.c: HackRF support
//
// Copyright (c) 2023 Timothy Mullican <timothy.j.mullican@gmail.com>
//
// This code is based on dump1090_sdrplus.
//
// Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>
// HackRF One support added by Ilker Temir <ilker@ilkertemir.com>
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

#ifndef HACKRF_H
#define HACKRF_H

// Support for the Great Scott Gadgets HackRF One SDR

void hackRFInitConfig ();
bool hackRFHandleOption (int argc, char *argv);
bool hackRFOpen ();
void hackRFRun ();
void hackRFClose ();

#endif
