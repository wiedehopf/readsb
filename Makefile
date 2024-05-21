PROGNAME=readsb
READSB_VERSION := "$(shell echo -n `cat version`; { git show -s --format=format: && echo -n ' wiedehopf git: ' && git describe --abbrev --dirty --always && git show -s --format=format:"(committed: %cd)" | tr -cd '[a-z],[A-Z],[0-9],:, ,\-,_,(,)';} || echo -n ' compiled on '`date +%y%m%d` )"

RTLSDR ?= no
BLADERF ?= no
HACKRF ?= no
PLUTOSDR ?= no
SOAPYSDR ?= no
AGGRESSIVE ?= no
HAVE_BIASTEE ?= no
TRACKS_UUID ?= no
PRINT_UUIDS ?= no

DIALECT = -std=c11
CFLAGS = $(DIALECT) -W -D_GNU_SOURCE -D_DEFAULT_SOURCE -Wall -Werror -fno-common -O2
CFLAGS += -DMODES_READSB_VERSION=\"$(READSB_VERSION)\"
CFLAGS += -Wdate-time -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 -fstack-protector-strong -Wformat -Werror=format-security

LIBS = -pthread -lpthread -lm -lrt -lzstd

ifeq ($(ZLIB_STATIC), yes)
	LIBS += -l:libz.a
else
	LIBS += -lz
endif

ifeq ($(shell $(CC) -c feature_test.c -o feature_test.o -Wno-format-truncation -Werror >/dev/null 2>&1 && echo 1 || echo 0), 1)
	CFLAGS += -Wno-format-truncation
endif

ifeq ($(shell uname -m | grep -qs -e arm -e aarch64 >/dev/null 2>&1 && echo 1 || echo 0), 1)
  CFLAGS += -DSC16Q11_TABLE_BITS=8
endif

ifeq ($(DISABLE_INTERACTIVE), yes)
  CFLAGS += -DDISABLE_INTERACTIVE
else
  LIBS += $(shell pkg-config --libs ncurses)
endif

# only disable workaround if zerocopy is disabled in librtlsdr, otherwise expect significantly increased CPU use
ifeq ($(DISABLE_RTLSDR_ZEROCOPY_WORKAROUND), yes)
  CFLAGS += -DDISABLE_RTLSDR_ZEROCOPY_WORKAROUND
endif

ifeq ($(HISTORY), yes)
  CFLAGS += -DALL_JSON=1
endif

ifneq ($(PREAMBLE_THRESHOLD_DEFAULT),)
  CFLAGS += -DPREAMBLE_THRESHOLD_DEFAULT=$(PREAMBLE_THRESHOLD_DEFAULT)
endif

ifneq ($(GLOBE_PERM_IVAL),)
  CFLAGS += -DGLOBE_PERM_IVAL=$(GLOBE_PERM_IVAL)
endif

ifneq ($(TRACE_THREADS),)
  CFLAGS += -DTRACE_THREADS=$(TRACE_THREADS)
endif

ifneq ($(TRACE_RECENT_POINTS),)
  CFLAGS += -DTRACE_RECENT_POINTS=$(TRACE_RECENT_POINTS)
endif

ifneq ($(AIRCRAFT_HASH_BITS),)
  CFLAGS += -DAIRCRAFT_HASH_BITS=$(AIRCRAFT_HASH_BITS)
endif

ifeq ($(STATS_PHASE),yes)
  CFLAGS += -DSTATS_PHASE
endif

ifeq ($(TRACKS_UUID), yes)
  CFLAGS += -DTRACKS_UUID
endif

ifeq ($(PRINT_UUIDS), yes)
  CFLAGS += -DPRINT_UUIDS
endif

ifneq ($(RECENT_RECEIVER_IDS),)
  CFLAGS += -DRECENT_RECEIVER_IDS=$(RECENT_RECEIVER_IDS)
endif

ifeq ($(RTLSDR), yes)
  SDR_OBJ += sdr_rtlsdr.o
  CFLAGS += -DENABLE_RTLSDR

  ifeq ($(HAVE_BIASTEE), yes)
    CFLAGS += -DENABLE_RTLSDR_BIASTEE
  endif

  ifdef RTLSDR_PREFIX
    CFLAGS += -I$(RTLSDR_PREFIX)/include
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
  CFLAGS += $(shell pkg-config --cflags libbladeRF) -DENABLE_BLADERF
  LIBS_SDR += $(shell pkg-config --libs libbladeRF)
endif

ifeq ($(HACKRF), yes)
  SDR_OBJ += sdr_hackrf.o
  CFLAGS += $(shell pkg-config --cflags libhackrf) -DENABLE_HACKRF
  LIBS_SDR += $(shell pkg-config --libs libhackrf)
endif

ifeq ($(PLUTOSDR), yes)
    SDR_OBJ += sdr_plutosdr.o
    CFLAGS += $(shell pkg-config --cflags libiio libad9361) -DENABLE_PLUTOSDR
    LIBS_SDR += $(shell pkg-config --libs libiio libad9361)
endif

ifeq ($(SOAPYSDR), yes)
  SDR_OBJ += sdr_soapy.o
  CFLAGS += $(shell pkg-config --cflags SoapySDR) -DENABLE_SOAPYSDR
  LIBS_SDR += $(shell pkg-config --libs SoapySDR)
endif

# add custom overrides if user defines them
CFLAGS += -g $(OPTIMIZE)

all: readsb viewadsb

ifneq ($(shell cat .version 2>/dev/null),prefix $(READSB_VERSION))
.PHONY: .version
.version:
	@(echo 'prefix $(READSB_VERSION)' >.version &)
endif

readsb.o: readsb.c *.h .version
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c *.h
	$(CC) $(CFLAGS) -c $< -o $@

minilzo.o: minilzo/minilzo.c minilzo/minilzo.h
	$(CC) $(CFLAGS) -c $< -o $@

readsb: readsb.o argp.o anet.o interactive.o mode_ac.o mode_s.o comm_b.o json_out.o net_io.o crc.o demod_2400.o \
	uat2esnt/uat2esnt.o uat2esnt/uat_decode.o \
	stats.o cpr.o icao_filter.o track.o util.o fasthash.o convert.o sdr_ifile.o sdr_beast.o sdr.o ais_charset.o \
	globe_index.o geomag.o receiver.o aircraft.o api.o minilzo.o threadpool.o \
	$(SDR_OBJ) $(COMPAT)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS) $(LIBS_SDR) $(OPTIMIZE)

viewadsb: readsb
	rm -f viewadsb
	cp readsb viewadsb

clean:
	rm -f *.o uat2esnt/*.o compat/clock_gettime/*.o compat/clock_nanosleep/*.o readsb viewadsb cprtests crctests convert_benchmark

cprtest: cprtests
	./cprtests

cprtests: cpr.o cprtests.o
	$(CC) $(CFLAGS) -o $@ $^ -lm

crctests: crc.c crc.h
	$(CC) $(CFLAGS) -DCRCDEBUG -o $@ $<

benchmarks: convert_benchmark
	./convert_benchmark

oneoff/convert_benchmark: oneoff/convert_benchmark.o convert.o util.o
	$(CC) $(CFLAGS) -o $@ $^ -lm

oneoff/decode_comm_b: oneoff/decode_comm_b.o comm_b.o ais_charset.o
	$(CC) $(CFLAGS) -o $@ $^ -lm
