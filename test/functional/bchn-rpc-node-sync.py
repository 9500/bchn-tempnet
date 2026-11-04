#!/usr/bin/env python3
# Copyright (c) 2020-present The Bitcoin Cash Node developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test sync_all."""

from test_framework.cdefs import COINBASE_MATURITY_TICKS
from test_framework.test_framework import BitcoinTestFramework


class NodeSetupAndSyncTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        assert self.nodes[0].getmempoolinfo()["loaded"]
        assert self.nodes[1].getmempoolinfo()["loaded"]

        # Mature some coins for easy spending, have a tx in the mempool
        # (use mine_for_ticks, since maturity is tick-based).
        self.nodes[0].generate(1)
        self.mine_for_ticks(self.nodes[0], COINBASE_MATURITY_TICKS)
        assert self.nodes[0].listunspent()
        self.sync_all()  # to prevent bad-txns-nonfinal on the following sendtoaddress
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), "1")
        self.sync_all()
        # Check that the coin appeared in node's 1 wallet, as an "unsafe", 0-conf coin.
        assert self.nodes[1].listunspent(0, 9999999, None, True)


if __name__ == '__main__':
    NodeSetupAndSyncTest().main()
