# syntax=docker/dockerfile:1
# check=error=true

ARG DOCKER_IMAGE=alpine:3.24
FROM $DOCKER_IMAGE AS dev

ENV LUAJIT_VERSION=v2.1

RUN apk add --no-cache git build-base cmake curl-dev zlib-dev zstd-dev \
		sqlite-dev postgresql-dev hiredis-dev leveldb-dev \
		gmp-dev jsoncpp-dev ninja

WORKDIR /usr/src/

ADD https://github.com/jupp0r/prometheus-cpp.git?branch=master /usr/src/prometheus-cpp
ADD https://github.com/libspatialindex/libspatialindex.git?branch=main /usr/src/libspatialindex
ADD --keep-git-dir https://luajit.org/git/luajit.git?branch=${LUAJIT_VERSION} /usr/src/luajit

RUN cd prometheus-cpp && \
		cmake -B build \
			-DCMAKE_INSTALL_PREFIX=/usr/local \
			-DCMAKE_BUILD_TYPE=Release \
			-DENABLE_TESTING=0 \
			-GNinja && \
		cmake --build build && \
		cmake --install build && \
		cd /usr/src/ && \
	cd libspatialindex && \
		cmake -B build \
			-DCMAKE_INSTALL_PREFIX=/usr/local && \
		cmake --build build && \
		cmake --install build && \
		cd /usr/src/ && \
	cd luajit && \
		make amalg && make install && \
	cd /usr/src/

FROM dev AS builder

COPY .git /usr/src/axis/.git
COPY CMakeLists.txt /usr/src/axis/CMakeLists.txt
COPY README.md /usr/src/axis/README.md
COPY minetest.conf.example /usr/src/axis/minetest.conf.example
COPY builtin /usr/src/axis/builtin
COPY cmake /usr/src/axis/cmake
COPY doc /usr/src/axis/doc
COPY fonts /usr/src/axis/fonts
COPY lib /usr/src/axis/lib
COPY misc /usr/src/axis/misc
COPY po /usr/src/axis/po
COPY src /usr/src/axis/src
COPY irr /usr/src/axis/irr
COPY textures /usr/src/axis/textures

WORKDIR /usr/src/axis

RUN cmake -B build \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_SERVER=TRUE \
		-DRUN_IN_PLACE=TRUE \
		-DENABLE_PROMETHEUS=TRUE \
		-DBUILD_UNITTESTS=FALSE -DBUILD_BENCHMARKS=FALSE \
		-DBUILD_CLIENT=FALSE \
		-GNinja && \
	cmake --build build

FROM $DOCKER_IMAGE AS runtime

RUN apk add --no-cache curl gmp libstdc++ libgcc libpq jsoncpp zstd-libs \
				sqlite-libs postgresql hiredis leveldb && \
	adduser -D axis --uid 30000 -h /app && \
	chown -R axis:axis /app

WORKDIR /app

COPY --from=builder /usr/src/axis/bin/* /app/bin/axisserver
COPY --from=builder /usr/src/axis/builtin /app/builtin
COPY --from=builder /usr/local/lib /app/libs

ENV LD_LIBRARY_PATH=/app/libs

USER axis:axis

EXPOSE 30000/udp 30000/tcp
VOLUME /app/

ENTRYPOINT ["/app/bin/axisserver"]
CMD ["--config", "/app/minetest.conf"]