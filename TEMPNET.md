# Bitcoin Cash Node for the 2026 tempnet (CHIP-2025-03 Faster Blocks)

An unofficial build of [Bitcoin Cash Node](https://bitcoincashnode.org) (BCHN) that follows
**tempnet**, the temporary test network that Jason Dreyzehner (bitjson) started on 2026-10-15 to test
[CHIP-2025-03 Faster Blocks](https://gitlab.com/0353F40E/fablous) (1-minute block target) ahead of its
activation on chipnet. Announcement and discussion:
[Tempnet: 2027 Chipnet Upgrade & Pre-Upgrade Testing](https://bitcoincashresearch.org/t/tempnet-2027-chipnet-upgrade-pre-upgrade-testing/2173).
Explorer: <https://tempnet.chaingraph.cash>.

## What tempnet is

- A fork of **chipnet**: same genesis block, same network magic (`e2b7daaf`), same default P2P port
  (48333) and RPC port (48332), same address format (`bchtest:`).
- The May 2027 upgrade rules (CHIP-2025-03) are activated at MTP **1792065600**
  (2026-10-15 12:00:00 UTC). Last common block with chipnet: **327988**
  (`00000000000002bcc82b979f71d827d7788c84b981975101cb9e415ace23c46c`); first tempnet block:
  **327989** (`00000000e18af2c66d44a1bbfe165acc4b87a625d211a09e33d5ee61d35dcb66`).
- Planned lifetime: until **2026-11-15**, when chipnet activates the same rules.

## What this repository is

- Branch `tempnet` (the default) = BCHN master (development version 29.2.1) **plus** Calin Culianu's work-in-progress
  implementation of CHIP-2025-03, branch `blocktime/wip_preview` of
  [gitlab.com/cculianu/bitcoin-cash-node](https://gitlab.com/cculianu/bitcoin-cash-node) at commit
  `6df24810` (2026-11-10): the draft merge request
  [!2184 "Implement CHIP-2025-03 Faster Blocks for Bitcoin Cash"](https://gitlab.com/bitcoin-cash-node/bitcoin-cash-node/-/merge_requests/2184)
  on the official BCHN repository (branch `blocktime/proposed1`) merged with BCHN master, plus upstream's
  node-expiry bump to the May 2028 upgrade (Calin Culianu publishes gitian builds of exactly that commit
  at <https://www.c3-soft.com/downloads/BitcoinCash/testing/wip_preview/>; they have no `-tempnet`),
  **plus one commit** that adds the `tempnet` chain: the `-tempnet` option, chain
  id `temp`, data directory `tempnet/`, the activation time, DNS seeds and a checkpoint at the fork
  block, and a second commit giving tempnet its own software expiry (2026-11-15 12:00 UTC, the
  chipnet activation, when tempnet ends) instead of the one it would inherit from chipnet
  (2027-11-15, the next tentative upgrade). About 57 lines in six files, no consensus code
  (`src/chainparams.{h,cpp}`, `src/chainparamsbase.{h,cpp}`, `src/util/system.cpp`, `src/init.cpp`);
  see `git log` or the scripts in `contrib/tempnet/` that produced the commits.
- [Releases](../../releases): `bitcoind` and `bitcoin-cli` for Linux x86-64, with every library linked
  in (`contrib/tempnet/build-static.sh`); the only requirement is glibc 2.38 or newer.
- This is a **reconstruction**: bitjson's own build (`/Bitcoin Cash Node:29.2.1(tempnet-20261015)/`) had
  not been published when this was made. It was verified on 2026-10-18 by syncing the whole chain: the
  tip and the fork-point block hashes were identical to the explorer's. The upstream branch is work in
  progress; this repository is a snapshot of it, not a maintained fork. **`tempnet` is rebased on
  each resync** (history rewrite); every snapshot stays reachable through its release tag (GitHub:
  branch switcher, "Tags"; `git checkout v29.2.1-tempnet-<date>`).

## Snapshots

- **2026-11-10** (`tempnet`; tag and release `v29.2.1-tempnet-20261110`): upstream `6df24810`, branch
  `blocktime/wip_preview` = MR !2184 (`blocktime/proposed1`) merged with BCHN master as of 2026-11-10, plus
  upstream's "Bump node expiry to May 2028". Made after Calin Culianu published gitian builds of that commit
  (a preview of BCHN with the complete implementation that does not switch itself off at the chipnet
  activation). **Consensus changes since the 2026-11-05 snapshot**, from the latest CHIP revisions, both
  loosenings: after the upgrade a coinbase transaction is always final (any nLockTime and nSequence), and
  the forbidden coinbase shape (version 1, locktime 0, outputs of at least 25 BCH) now also requires the
  input sequence 0xffffffff (mainnet additionally defers that rule until height 3,708,179). A node on the
  2026-11-05 snapshot rejects a post-upgrade coinbase with a non-zero locktime and a non-final sequence that
  this snapshot accepts: upgrade. Also: double-spend-proof orphan rescue is rate-limited, an ABLA lookahead
  shortcut for fixed-size chains, gitian and CI fixes. Tempnet's own expiry stays 2026-11-15 (the stock
  expiry of this base would be 2027-11-15 on chipnet).
- **2026-11-05** (tag and release `v29.2.1-tempnet-20261105`; superseded): upstream `344220cf`, the head of
  BCHN merge request [!2184](https://gitlab.com/bitcoin-cash-node/bitcoin-cash-node/-/merge_requests/2184)
  (draft: the complete implementation, all tests passing, under review). Resynced at cculianu's request
  ([thread post](https://bitcoincashresearch.org/t/chip-2025-03-faster-blocks-for-bitcoin-cash/1513/246)).
  Node-code changes since the 2026-10-29 snapshot are small (the ASERT/pow code now rejects a forged chain
  that activates 1-minute blocks before the ASERT anchor, an ABLA lookahead rounding fix, an unused mempool
  field removed); the rest is the functional-test suite ported to 1-minute blocks. No consensus change for
  tempnet; the previous snapshot stays in consensus with this one.
- **2026-10-29** (tag and release `v29.2.1-tempnet-20261029`; superseded): upstream `1ea14c74`. Resynced
  at cculianu's request: the first snapshot had p2p bugs that upstream fixed since 2026-10-15 (header
  download across the activation boundary, relay to limited nodes, broadcast rates) besides many review
  fixes. bitcoincashautist's identical rebase of these patches:
  [gitlab.com/0353F40E/bitcoin-cash-node, branch `tempnet`](https://gitlab.com/0353F40E/bitcoin-cash-node/-/tree/tempnet).
- **2026-10-18** (tag and release `v29.2.1-tempnet-20261018`; **superseded, do not run it any more**):
  upstream `77dec2d2`.

## Running the binary

Requirements: x86-64 Linux with glibc 2.38 or newer (Ubuntu 24.04, Debian 13, Fedora 39, or newer). No
other packages: Boost, OpenSSL, libevent, GMP, zlib and Berkeley DB are linked into the binary.

Download the tarball from the Releases page, check it against `SHA256SUMS.txt`, unpack it.

Configuration (`~/.bitcoin/bitcoin.conf`, or any file given with `-conf=`):

```
tempnet=1
server=1
# Optional: identify your node to its peers.
uacomment=my-tempnet-node

[temp]
# The DNS seeds are chipnet's and return chipnet nodes, which this node rejects at the fork
# ("bad-diffbits") and drops. These are the known tempnet peers as of 2026-10-18: bitjson's explorer
# node and the two seed nodes behind tempnet.bitjson.com.
addnode=15.204.253.47:48335
addnode=15.204.228.134:48333
addnode=135.148.26.244:48333
```

As with every BCHN network, the options `addnode`, `connect`, `port`, `bind`, `rpcport`, `rpcbind` and
`wallet` only apply to tempnet when they are in the `[temp]` section.

Start and check:

```
bitcoind -tempnet -daemon
bitcoin-cli -tempnet getblockchaininfo
```

The data goes to `<datadir>/tempnet/` (about 3.5 GB, initial sync takes well under an hour on a
normal machine). A correct node reports `"chain": "temp"`, and after the sync `"target_time": 60`,
an `upgrade_status` named `May 2027 Upgrade (tempnet)` with `mempool_activated: true` and the two
fork-point hashes above, and a `bestblockhash` equal to the explorer's best block.

If a chipnet node runs on the same machine, give one of them other ports (`port=` and `rpcport=`
in its network section): both default to 48333 and 48332.

## Building from source

Ubuntu 24.04 build dependencies:

```
sudo apt install build-essential cmake ninja-build git pkg-config libboost-dev libboost-chrono-dev \
  libboost-filesystem-dev libboost-test-dev libboost-thread-dev libevent-dev libssl-dev libgmp-dev \
  libdb++-dev help2man python3
```

Then either `contrib/tempnet/build.sh` or by hand:

```
mkdir build && cd build
cmake -GNinja .. -DCMAKE_BUILD_TYPE=Release -DBUILD_BITCOIN_QT=OFF -DBUILD_BITCOIN_ZMQ=OFF \
  -DBUILD_BITCOIN_SEEDER=OFF -DENABLE_UPNP=OFF -DENABLE_NATPMP=OFF -DENABLE_TEST=OFF
ninja bitcoind bitcoin-cli
```

Add `-DBUILD_BITCOIN_WALLET=OFF` to skip the wallet and the Berkeley DB dependency. A few minutes on
four cores. This gives a binary that uses the system's shared libraries. For a self-contained binary like
the released one (all libraries linked in, only glibc needed), use `contrib/tempnet/build-static.sh`,
which first builds the libraries with BCHN's `depends` system (about 10 more minutes).

## Caveats

- Pre-release test software on a test network: the node says so itself in its warnings. Never use it
  on mainnet or for anything of value.
- The consensus code is an upstream work in progress. If the implementation changes before the
  chipnet activation, nodes running this snapshot will fork off; follow the thread linked above.
- The node treats itself as expiring on 2026-11-15 12:00 UTC, when tempnet ends: from 30 days before
  it warns ("may fall out of network consensus in N days"), and at that time it disables its RPC
  interface, as BCHN does on every network. `expire=0` overrides that if you need to keep it running.
- The fixed `addnode` peers may disappear; if your node finds no peers, look for current ones in the
  thread or on the explorer's peers page.

## Credits and license

Bitcoin Cash Node and its contributors (MIT license, see [COPYING](COPYING)); Calin Culianu for the
faster-blocks implementation; Jason Dreyzehner for the tempnet and its explorer; bitcoincashautist
for CHIP-2025-03. The tempnet chain commit in this repository is likewise MIT.
