FROM debian:trixie-slim AS builder

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates wget \
        build-essential \
        libcjson-dev libsqlite3-dev libmicrohttpd-dev && \
    rm -rf /var/lib/apt/lists/*

ARG MOSQ_VERSION=2.1.2
WORKDIR /tmp
RUN wget -qO mosquitto.tar.gz \
        "https://github.com/eclipse-mosquitto/mosquitto/archive/refs/tags/v${MOSQ_VERSION}.tar.gz" && \
    tar -xzf mosquitto.tar.gz

WORKDIR /tmp/mosquitto-${MOSQ_VERSION}

ENV MAKE_OPTS="WITH_TLS=no WITH_CJSON=yes WITH_BRIDGE=no WITH_PERSISTENCE=yes WITH_DOCS=no WITH_SHARED_LIBRARIES=yes WITH_STATIC_LIBRARIES=no"

RUN make -j$(nproc) -C libcommon $MAKE_OPTS && \
    make -j$(nproc) -C lib       $MAKE_OPTS && \
    make -j$(nproc) -C src       $MAKE_OPTS && \
    make           -C libcommon install $MAKE_OPTS && \
    make           -C lib       install $MAKE_OPTS && \
    make           -C src       install $MAKE_OPTS

FROM debian:trixie-slim
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        libcjson1 libsqlite3-0 libmicrohttpd12 && \
    rm -rf /var/lib/apt/lists/* && \
    mkdir -p /mosquitto/plugins /mosquitto/config

COPY --from=builder /usr/local/sbin/mosquitto /usr/local/sbin/
COPY --from=builder /usr/local/lib/ /usr/local/lib/
RUN ldconfig

EXPOSE 1883
CMD ["mosquitto", "-c", "/mosquitto/config/mosquitto.conf"]
