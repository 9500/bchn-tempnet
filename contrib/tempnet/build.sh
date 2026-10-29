#!/bin/bash
# Build bitcoind and bitcoin-cli for the tempnet (Release, no GUI/ZMQ/seeder/UPnP/tests).
# Run from the repository root. Ubuntu/Debian build dependencies:
#   sudo apt install build-essential cmake ninja-build git pkg-config libboost-dev libboost-chrono-dev \
#     libboost-filesystem-dev libboost-test-dev libboost-thread-dev libevent-dev libssl-dev libgmp-dev \
#     libdb++-dev help2man python3
# Pass -DBUILD_BITCOIN_WALLET=OFF as an argument to skip the wallet (and the libdb dependency).
set -euo pipefail
mkdir -p build && cd build
cmake -GNinja .. -DCMAKE_BUILD_TYPE=Release -DBUILD_BITCOIN_QT=OFF -DBUILD_BITCOIN_ZMQ=OFF \
  -DBUILD_BITCOIN_SEEDER=OFF -DBUILD_BITCOIN_TX=OFF -DBUILD_LIBBITCOINCONSENSUS=OFF \
  -DENABLE_UPNP=OFF -DENABLE_NATPMP=OFF -DENABLE_TEST=OFF "$@"
ninja bitcoind bitcoin-cli
./src/bitcoind --version | head -1
echo "binaries: build/src/bitcoind build/src/bitcoin-cli"
