FROM debian:bookworm AS builder
RUN mkdir -p /app/git
COPY required_packages /app/git
RUN apt-get update && \
    apt-get install -y git wget gdb pkg-config autoconf $(cat /app/git/required_packages)

# install jemalloc
RUN JEMALLOC=$(mktemp -d) && \
    git clone https://github.com/jemalloc/jemalloc $JEMALLOC && \
    cd $JEMALLOC && \
    ./autogen.sh && \
    ./configure && \
    make -j$(nproc) && \
    make install
# install readsb
COPY . /app/git
RUN cd /app/git && \
    READSB_BUILD_DIR=$(mktemp -d) && \
    cp -r /app/git/* $READSB_BUILD_DIR && \
    cd $READSB_BUILD_DIR && \
    make -j$(nproc) OPTIMIZE="-O2" && \
    mv readsb /usr/local/bin && \
    mv viewadsb /usr/local/bin && \
    chmod +x /usr/local/bin/viewadsb /usr/local/bin/readsb && \
    READSB_UUID_BUILD_DIR=$(mktemp -d) && \
    cp -r /app/git/* $READSB_UUID_BUILD_DIR && \
    cd $READSB_UUID_BUILD_DIR && \
    make -j$(nproc) PRINT_UUIDS=yes OPTIMIZE="-O2" && \
    mv readsb /usr/local/bin/readsb-uuid && \
    mv viewadsb /usr/local/bin/viewadsb-uuid && \
    chmod +x /usr/local/bin/viewadsb-uuid && \
    chmod +x /usr/local/bin/readsb-uuid && \
    rm -rf $READSB_BUILD_DIR $READSB_UUID_BUILD_DIR && \
    mkdir -p  /usr/local/share/tar1090 && \
    wget -O /usr/local/share/tar1090/aircraft.csv.gz https://github.com/wiedehopf/tar1090-db/raw/csv/aircraft.csv.gz

FROM debian:bookworm-slim
RUN apt-get update && \
    apt-get -y install --no-install-recommends \
    rsync openssh-client openssh-server wget \
    libusb-1.0-0-dev librtlsdr-dev librtlsdr0 libncurses-dev zlib1g-dev zlib1g libzstd-dev libzstd1 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* && \
    mkdir -p /run/readsb
ENV LD_PRELOAD=/usr/local/lib/libjemalloc.so.2
COPY --from=builder /usr/local/bin/readsb* /usr/local/bin/
COPY --from=builder /usr/local/bin/viewadsb* /usr/local/bin/
COPY --from=builder /usr/local/share/tar1090/aircraft.csv.gz /usr/local/share/tar1090/aircraft.csv.gz
COPY --from=builder /usr/local/lib/libjemalloc.so.2 /usr/local/lib/libjemalloc.so.2
ENTRYPOINT ["/usr/local/bin/readsb"]
