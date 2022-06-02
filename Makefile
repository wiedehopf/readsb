PROGNAME=readsb
READSB_VERSION := "$(shell echo -n 'wiedehopf git: '; { git describe --abbrev --dirty --always && git show -s --format=format:"(committed: %cd)" | tr -cd '[a-z],[A-Z],[0-9],:, ,\-,_,(,)';} || cat READSB_VERSION)"

RTLSDR ?= no
BLADERF ?= no
PLUTOSDR ?= no
AGGRESSIVE ?= no
HAVE_BIASTEE ?= no
TRACKS_UUID ?= no

CPPFLAGS += -DMODES_READSB_VERSION=\"$(READSB_VERSION)\"
CPPFLAGS += -D_GNU_SOURCE
CPPFLAGS += -D_FORTIFY_SOURCE=2 -fstack-protector-strong -Wformat -Werror=format-security

#OPTIMIZE ?= -march=native

DIALECT = -std=c11

CFLAGS := $(DIALECT) -g -W -D_DEFAULT_SOURCE -Wall -Werror -fno-common -O2 $(CFLAGS) $(OPTIMIZE)
LIBS = -pthread -lpthread -lm -lrt

ifeq ($(ZLIB_STATIC), yes)
	LIBS += ../zlib/libz.a
else
	LIBS += -lz
endif

ifeq ($(shell $(CC) -c feature_test.c -o feature_test.o -Wno-format-truncation -Werror >/dev/null 2>&1 && echo 1 || echo 0), 1)
	CFLAGS += -Wno-format-truncation
endif

ifeq ($(HISTORY), yes)
  CPPFLAGS += -DALL_JSON=1
endif

ifneq ($(PREAMBLE_THRESHOLD_DEFAULT),)
  CPPFLAGS += -DPREAMBLE_THRESHOLD_DEFAULT=$(PREAMBLE_THRESHOLD_DEFAULT)
endif

ifneq ($(GLOBE_PERM_IVAL),)
  CPPFLAGS += -DGLOBE_PERM_IVAL=$(GLOBE_PERM_IVAL)
endif

ifneq ($(TRACE_THREADS),)
  CPPFLAGS += -DTRACE_THREADS=$(TRACE_THREADS)
endif

ifneq ($(TRACE_RECENT_POINTS),)
  CPPFLAGS += -DTRACE_RECENT_POINTS=$(TRACE_RECENT_POINTS)
endif

ifneq ($(AIRCRAFT_HASH_BITS),)
  CPPFLAGS += -DAIRCRAFT_HASH_BITS=$(AIRCRAFT_HASH_BITS)
endif

ifeq ($(STATS_PHASE),yes)
  CPPFLAGS += -DSTATS_PHASE
endif

ifeq ($(TRACKS_UUID), yes)
	CPPFLAGS += -DTRACKS_UUID
endif
ifeq ($(RTLSDR), yes)
  SDR_OBJ += sdr_rtlsdr.o
  CPPFLAGS += -DENABLE_RTLSDR

  ifeq ($(HAVE_BIASTEE), yes)
    CPPFLAGS += -DENABLE_RTLSDR_BIASTEE
  endif

  ifdef RTLSDR_PREFIX
    CPPFLAGS += -I$(RTLSDR_PREFIX)/include
    LDFLAGS += -L$(RTLSDR_PREFIX)/lib
  else
    CFLAGS += $(shell pkg-config --cflags librtlsdr)
    LDFLAGS += $(shell pkg-config --libs-only-L librtlsdr)
  endif

  ifeq ($(STATIC), yes)
    LIBS_SDR += -Wl,-Bstatic -lrtlsdr -Wl,-Bdynamic -lusb-1.0
  else
    LIBS_SDR += -lrtlsdr -lusb-1.0
  endif
endif

ifeq ($(BLADERF), yes)
  SDR_OBJ += sdr_bladerf.o sdr_ubladerf.o
  CPPFLAGS += -DENABLE_BLADERF
  CFLAGS += $(shell pkg-config --cflags libbladeRF)
  LIBS_SDR += $(shell pkg-config --libs libbladeRF)
endif

ifeq ($(PLUTOSDR), yes)
    SDR_OBJ += sdr_plutosdr.o
    CPPFLAGS += -DENABLE_PLUTOSDR
    CFLAGS += $(shell pkg-config --cflags libiio libad9361)
    LIBS_SDR += $(shell pkg-config --libs libiio libad9361)
endif

all: readsb viewadsb

ifneq ($(shell cat .version 2>/dev/null),prefix $(READSB_VERSION))
.PHONY: .version
.version:
	@(echo 'prefix $(READSB_VERSION)' >.version &)
endif

readsb.o: readsb.c *.h .version
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

%.o: %.c *.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

minilzo.o: minilzo/minilzo.c minilzo/minilzo.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

readsb: readsb.o argp.o anet.o interactive.o mode_ac.o mode_s.o comm_b.o json_out.o net_io.o crc.o demod_2400.o \
	stats.o cpr.o icao_filter.o track.o util.o fasthash.o convert.o sdr_ifile.o sdr_beast.o sdr.o ais_charset.o \
	globe_index.o geomag.o receiver.o aircraft.o api.o minilzo.o threadpool.o \
	$(SDR_OBJ) $(COMPAT)
	$(CC) -g -o $@ $^ $(LDFLAGS) $(LIBS) $(LIBS_SDR) -lncurses $(OPTIMIZE)

viewadsb: readsb
	cp --remove-destination readsb viewadsb

clean:
	rm -f *.o compat/clock_gettime/*.o compat/clock_nanosleep/*.o readsb viewadsb cprtests crctests convert_benchmark

cprtest: cprtests
	./cprtests

cprtests: cpr.o cprtests.o
	$(CC) $(CPPFLAGS) $(CFLAGS) -g -o $@ $^ -lm

crctests: crc.c crc.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -g -DCRCDEBUG -o $@ $<

benchmarks: convert_benchmark
	./convert_benchmark

oneoff/convert_benchmark: oneoff/convert_benchmark.o convert.o util.o
	$(CC) $(CPPFLAGS) $(CFLAGS) -g -o $@ $^ -lm

oneoff/decode_comm_b: oneoff/decode_comm_b.o comm_b.o ais_charset.o
	$(CC) $(CPPFLAGS) $(CFLAGS) -g -o $@ $^ -lm
