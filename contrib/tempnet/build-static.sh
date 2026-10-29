#!/bin/bash
# Release-style build: all libraries (Boost, OpenSSL, libevent, GMP, zlib, Berkeley DB) are built by the
# depends system and linked in, libstdc++/libgcc statically; the binary needs only glibc (>= 2.38 when
# built on Ubuntu 24.04; build on an older distribution for an older floor). This is how the binaries
# on the Releases page are made. Run from the repository root; needs the build dependencies from
# TEMPNET.md plus: sudo apt install autoconf automake libtool bison patch curl
set -euo pipefail
JOBS=${JOBS:-$(nproc)}
( cd depends && make -j"$JOBS" JOBS="$JOBS" HOST=x86_64-linux-gnu NO_QT=1 NO_ZMQ=1 NO_UPNP=1 NO_NATPMP=1 NO_JEMALLOC=1 )
mkdir -p build-static && cd build-static
cmake -GNinja .. -DCMAKE_TOOLCHAIN_FILE=../cmake/platforms/Linux64.cmake -DENABLE_MAN=OFF -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_STATIC_LIBSTDCXX=ON -DENABLE_STATIC_LIBGCC=ON -DENABLE_GLIBC_BACK_COMPAT=ON -DENABLE_REDUCE_EXPORTS=ON \
  -DBUILD_BITCOIN_QT=OFF -DBUILD_BITCOIN_ZMQ=OFF -DBUILD_BITCOIN_SEEDER=OFF -DBUILD_BITCOIN_TX=OFF \
  -DBUILD_LIBBITCOINCONSENSUS=OFF -DENABLE_UPNP=OFF -DENABLE_NATPMP=OFF -DENABLE_TEST=OFF "$@"
ninja -j"$JOBS" bitcoind bitcoin-cli
./src/bitcoind --version | head -1; ldd src/bitcoind
echo "binaries: build-static/src/bitcoind build-static/src/bitcoin-cli"
