#ifndef GEOMAG_H
#define GEOMAG_H

// this library is NOT thread safe.
// UNSAFE with threading
//
//
// functions with type int:
// return value 0: SUCCESS
// return value -1: ERROR
// on ERROR an error message is printed to stderr

// call once to initialize the library
int geomag_init();
// call on program exit if you care about freeing malloced memory
int geomag_destroy();

// alt: altitude above WGS84 ellipsoid in km
// glat: latitude in degrees
// glon: longitude in degrees
// time: decimal year
//
// pass variables by reference, results will be written to the variables
// (see example.c)
// dec: https://en.wikipedia.org/wiki/Magnetic_declination
// dip: https://en.wikipedia.org/wiki/Magnetic_dip
// ti: Total intensity in nano Tesla nT
// from geomag.c:
//       COMPUTE DECLINATION (DEC), INCLINATION (DIP) AND
//       TOTAL INTENSITY (TI)
// from geomag.c
// gv: Grid variation
//       COMPUTE MAGNETIC GRID VARIATION IF THE CURRENT
//       GEODETIC POSITION IS IN THE ARCTIC OR ANTARCTIC
//       (I.E. GLAT > +55 DEGREES OR GLAT < -55 DEGREES)
//
//       OTHERWISE, SET MAGNETIC GRID VARIATION TO -999.0
//
//
int geomag_calc(double alt, double glat, double glon, double time, double *dec, double *dip, double *ti, double *gv);

// more or less the original program this library was adapted from, interactive console input/output of data
void geomag_interactive();

#endif
