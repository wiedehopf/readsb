FROM debian
RUN mkdir -p /app/git
COPY required_packages /app/git
RUN apt-get update && \
    apt-get install -y pkg-config $(cat /app/git/required_packages) && \
    rm -rf /var/lib/apt/lists/*
COPY . /app/git
RUN cd /app/git && \
    make AIRCRAFT_HASH_BITS=11 RTLSDR=yes OPTIMIZE="-O3 -march=native" && \
    mv readsb /usr/local/bin && \
    mv viewadsb /usr/local/bin && \
    chmod +x /usr/local/bin/readsb && \
    chmod +x /usr/local/bin/viewadsb

ENTRYPOINT ["/usr/local/bin/readsb"]
