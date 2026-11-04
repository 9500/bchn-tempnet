#!/usr/bin/env python3
# Copyright (c) 2021-present The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Construct a transaction using ANYONECANPAY hash type."""

from decimal import Decimal

from test_framework.blocktools import get_coinbase_subsidy, get_next_block_interval
from test_framework.cdefs import COINBASE_MATURITY_TICKS
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, find_output


class AnyoneCanPayTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def test_anyonecanpay(self):
        node0, node1 = self.nodes[0], self.nodes[1]
        # Mature exactly block 1: its interval is the legacy 600 ticks even if
        # 1-min blocks is activated, so one interval past the maturity depth
        # lands inside its coinbase maturity window and short of block 2's
        # (irrespective of block 2's blocktime interval).
        self.mine_for_ticks(node0, COINBASE_MATURITY_TICKS + get_next_block_interval(node0))

        # Block 1 keeps the legacy 600-tick subsidy even when the upgrade is
        # active, while every later coinbase is scaled by the new interval.
        # Read the coin's own value rather than assuming 50 BCH; a balance
        # equal to that single coin also pins "exactly one matured".
        coin_amount = node0.getblock(node0.getblockhash(1), 2)['tx'][0]['vout'][0]['value']
        assert_equal(coin_amount, node0.getbalance())
        assert_equal(Decimal('0'), node1.getbalance())

        # Send most of block 1 to node 1
        tx0_id = node0.getblock(node0.getblockhash(1))['tx'][0]
        vout0 = find_output(node0, tx0_id, coin_amount)
        addr1 = node1.getnewaddress()
        pubkey1 = node1.getaddressinfo(addr1)['pubkey']
        amount1 = coin_amount - Decimal('0.01')
        inputs1 = [
            {'txid': tx0_id,
             'vout': vout0, 'amount': amount1,
             'scriptPubKey': pubkey1}
        ]
        outputs1 = {addr1: amount1}
        rawtx1 = node0.createrawtransaction(inputs1, outputs1)
        # We could also use SINGLE or ALL combined with FORKID|ANYONECANPAY .
        # For no particular reason we first use NONE (no outputs covered by signature).
        signed1 = node0.signrawtransactionwithwallet(rawtx1,
                                                    None,
                                                    "NONE|FORKID|ANYONECANPAY")
        assert 'complete' in signed1
        assert_equal(signed1['complete'], True)
        assert 'errors' not in signed1

        tx1_id = node0.sendrawtransaction(signed1['hex'])
        self.generate(node0, 1)
        self.sync_all()
        subsidy = get_coinbase_subsidy(node0, as_decimal=True)
        assert_equal(subsidy, node0.getbalance())
        assert_equal(amount1, node1.getbalance())

        # Send back to node 0, also with SIGHASH_ANYONECANPAY.
        addr2 = node0.getnewaddress()
        pubkey2 = node0.getaddressinfo(addr2)['pubkey']
        amount2 = amount1 - Decimal('0.01')
        vout1 = find_output(node0, tx1_id, amount1)
        inputs2 = [
            {'txid': tx1_id,
             'vout': vout1, 'amount': amount2,
             'scriptPubKey': pubkey2}
        ]
        outputs2 = {addr2: amount2}
        rawtx2 = node1.createrawtransaction(inputs2, outputs2)
        signed2 = node1.signrawtransactionwithwallet(rawtx2,
                                                     None,
                                                     "SINGLE|FORKID|ANYONECANPAY")
        assert 'complete' in signed2
        assert_equal(signed2['complete'], True)
        assert 'errors' not in signed2
        node1.sendrawtransaction(signed2['hex'])
        self.generate(node1, 1)
        self.sync_all()

        # Check balances. Node 0 got the returned amount plus a second full
        # coinbase maturing on top of it.
        assert_equal(2 * subsidy + amount2, node0.getbalance())
        # Nothing left on node 1.
        assert_equal(Decimal('0'), node1.getbalance())

    def run_test(self):
        self.test_anyonecanpay()


if __name__ == '__main__':
    AnyoneCanPayTest().main()
