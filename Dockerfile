FROM debian:bookworm-20240311 AS builder
RUN apt-get update && \
    apt-get install -y git wget pkg-config autoconf gcc make libusb-1.0-0-dev librtlsdr-dev librtlsdr0 libncurses-dev zlib1g-dev zlib1g libzstd-dev libzstd1

# install jemalloc
RUN JEMALLOC_BDIR=$(mktemp -d) && \
    git clone --depth 1 https://github.com/jemalloc/jemalloc $JEMALLOC_BDIR && \
    cd $JEMALLOC_BDIR && \
    ./autogen.sh && \
    ./configure --with-lg-page=14 && \
    make -j$(nproc) && \
    make install && \
    rm -rf $JEMALLOC_BDIR

# install readsb
RUN mkdir -p /app/git
COPY . /app/git
RUN cd /app/git && \
    READSB_BUILD_DIR=$(mktemp -d) && \
    cp -r /app/git/* $READSB_BUILD_DIR && \
    cd $READSB_BUILD_DIR && \
    make -j$(nproc) RTLSDR=yes OPTIMIZE="-O2" && \
    mv readsb /usr/local/bin && \
    mv viewadsb /usr/local/bin && \
    chmod +x /usr/local/bin/viewadsb /usr/local/bin/readsb && \
    make clean && \
    make -j$(nproc) PRINT_UUIDS=yes TRACKS_UUID=yes OPTIMIZE="-O2" && \
    mv readsb /usr/local/bin/readsb-uuid && \
    mv viewadsb /usr/local/bin/viewadsb-uuid && \
    chmod +x /usr/local/bin/viewadsb-uuid && \
    chmod +x /usr/local/bin/readsb-uuid && \
    rm -rf $READSB_BUILD_DIR && \
    mkdir -p  /usr/local/share/tar1090 && \
    wget -O /usr/local/share/tar1090/aircraft.csv.gz https://github.com/wiedehopf/tar1090-db/raw/csv/aircraft.csv.gz && \
    true

FROM debian:bookworm-slim
RUN apt-get update && \
    apt-get -y install --no-install-recommends \
    wget \
    librtlsdr0 libncurses6 zlib1g libzstd1 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* && \
    mkdir -p /run/readsb

ENV LD_PRELOAD=/usr/local/lib/libjemalloc.so.2
COPY --from=builder /usr/local/bin/readsb* /usr/local/bin/
COPY --from=builder /usr/local/bin/viewadsb* /usr/local/bin/
COPY --from=builder /usr/local/share/tar1090/aircraft.csv.gz /usr/local/share/tar1090/aircraft.csv.gz
COPY --from=builder /usr/local/lib/libjemalloc.so.2 /usr/local/lib/libjemalloc.so.2

ENTRYPOINT ["/usr/local/bin/readsb"]
