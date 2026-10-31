#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Cash Node developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""fundrawtransaction includeWatching=true must size for a foreign signer.

`includeWatching` is the option a user sets when signing happens elsewhere.

If the wallet's own coins are now sized at 141 B (Schnorr) instead, a
transaction funded this way and signed by any ECDSA signer is 6-7 bytes per
input larger than it paid for, and the network rejects it.

The `includeWatching` option is used to guard against this eventuality
and is interpreted by the wallet as a flag that signals the wallet to
over-estimate the signature sizes for worst-case largest-possible
signatures. This test ensures that the feature does not regress.
"""
from decimal import Decimal

from test_framework.blocktools import (
    get_block_ticks,
    get_chaintip_ticks,
    get_next_block_interval,
)
from test_framework.cdefs import COINBASE_MATURITY_TICKS
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class IncludeWatchingFeeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # node0 funds (Schnorr default); node1 is the external ECDSA signer.
        self.extra_args = [[], ["-signschnorr=0"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node0, node1 = self.nodes
        # Maturity is measured in ticks, not blocks: 101 blocks is 60,600 ticks
        # at 600 but only 6,060 at 60, so a fixed count leaves the wallet with
        # nothing spendable once the interval changes. Mine block 1 first so its
        # own tick can anchor the target (the node is still at genesis on a
        # clean chain), then poll, because the interval changes should 1 min
        # blocks activate.
        self.generate(node0, 1)
        target_ticks = get_block_ticks(node0, 1) + COINBASE_MATURITY_TICKS
        while get_chaintip_ticks(node0) < target_ticks:
            remaining = target_ticks - get_chaintip_ticks(node0)
            interval = get_next_block_interval(node0)
            assert interval in (60, 600)
            self.generate(node0, max(1, remaining // interval))
        self.sync_all()

        # An ordinary coin node0 can spend itself. No watch-only coin needed:
        # includeWatching only says "signing may happen elsewhere".
        addr = node0.getnewaddress()
        privkey = node0.dumpprivkey(addr)
        node0.sendtoaddress(addr, Decimal("1.0"))
        self.generate(node0, 1)
        self.sync_all()

        unsigned = node0.createrawtransaction(
            [], {node0.getnewaddress(): Decimal("0.5")})
        funded = node0.fundrawtransaction(unsigned, {"includeWatching": True})

        # Sign off-box with an ECDSA signer that holds the same key.
        signed = node1.signrawtransactionwithkey(funded["hex"], [privkey])
        assert_equal(signed["complete"], True)

        est = len(funded["hex"]) // 2
        real = len(signed["hex"]) // 2
        fee_sat = int(funded["fee"] * Decimal("1e8"))
        self.log.info("unsigned {} B, signed {} B, fee {} sat, min relay needs {} sat"
                      .format(est, real, fee_sat, real))

        node0.sendrawtransaction(signed["hex"])
        self.log.info("accepted")


if __name__ == '__main__':
    IncludeWatchingFeeTest().main()
