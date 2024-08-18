FROM ghcr.io/wiedehopf/readsb-builder:latest AS builder

SHELL ["/bin/bash", "-x", "-o", "pipefail", "-c"]
RUN --mount=type=bind,source=.,target=/app/git \
    cd /app/git && \
    READSB_BUILD_DIR=$(mktemp -d) && \
    cp -r /app/git/* $READSB_BUILD_DIR && \
    cd $READSB_BUILD_DIR && \
    [[ $(uname -m) == x86_64 ]] && MARCH=" -march=nehalem" || MARCH="" && \
    make -j$(nproc) RTLSDR=yes OPTIMIZE="-O2 $MARCH" && \
    mv readsb /usr/local/bin && \
    mv viewadsb /usr/local/bin && \
    chmod +x /usr/local/bin/viewadsb /usr/local/bin/readsb && \
    make clean && \
    make -j$(nproc) PRINT_UUIDS=yes TRACKS_UUID=yes OPTIMIZE="-O2 $MARCH" && \
    mv readsb /usr/local/bin/readsb-uuid && \
    mv viewadsb /usr/local/bin/viewadsb-uuid && \
    chmod +x /usr/local/bin/viewadsb-uuid && \
    chmod +x /usr/local/bin/readsb-uuid && \
    rm -rf $READSB_BUILD_DIR && \
    mkdir -p  /usr/local/share/tar1090 && \
    wget -O /usr/local/share/tar1090/aircraft.csv.gz https://github.com/wiedehopf/tar1090-db/raw/csv/aircraft.csv.gz && \
    true

FROM debian:bookworm-slim
RUN \
    --mount=type=bind,from=builder,source=/,target=/builder/ \
    apt-get update && \
    apt-get -y install --no-install-recommends \
    librtlsdr0 libncurses6 zlib1g libzstd1 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* && \
    mkdir -p /run/readsb && \
    cp /builder/usr/local/bin/readsb* /usr/local/bin/ && \
    cp /builder/usr/local/bin/viewadsb* /usr/local/bin/ && \
    mkdir -p  /usr/local/share/tar1090 && \
    cp /builder/usr/local/share/tar1090/aircraft.csv.gz /usr/local/share/tar1090/aircraft.csv.gz && \
    cp /builder/usr/local/lib/libjemalloc.so.2 /usr/local/lib/libjemalloc.so.2 && \
    true

ENV LD_PRELOAD=/usr/local/lib/libjemalloc.so.2

ENTRYPOINT ["/usr/local/bin/readsb"]
