#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Core developers
# Copyright (c) 2018-present The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the wallet balance RPC methods."""
from decimal import Decimal

from test_framework.blocktools import get_coinbase_subsidy
from test_framework.cdefs import COINBASE_MATURITY_TICKS
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_not_equal,
    assert_raises_rpc_error,
)

RANDOM_COINBASE_ADDRESS = 'mneYUmWYsuk7kySiURxCi3AGxrAqZxLgPZ'


def create_transactions(node, address, amt, fees):
    # Create and sign raw transactions from node to address for amt.
    # Creates a transaction for each fee and returns an array
    # of the raw transactions.
    utxos = node.listunspent(0)

    # Create transactions
    inputs = []
    ins_total = 0
    for utxo in utxos:
        inputs.append({"txid": utxo["txid"], "vout": utxo["vout"]})
        ins_total += utxo['amount']
        if ins_total > amt:
            break

    txs = []
    for fee in fees:
        outputs = {
            address: amt,
            node.getrawchangeaddress(): ins_total -
            amt -
            fee}
        raw_tx = node.createrawtransaction(inputs, outputs, 0)
        raw_tx = node.signrawtransactionwithwallet(raw_tx)
        txs.append(raw_tx)

    return txs


class WalletTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        # Tests watchonly behavior
        self.nodes[0].importaddress(RANDOM_COINBASE_ADDRESS)

        # Check that nodes don't own any UTXOs
        assert_equal(len(self.nodes[0].listunspent()), 0)
        assert_equal(len(self.nodes[1].listunspent()), 0)

        self.log.info("Mining one block for each node")

        self.generate(self.nodes[0], 1)
        self.sync_all()
        coin0 = get_coinbase_subsidy(self.nodes[0], as_decimal=True)
        self.generate(self.nodes[1], 1)
        self.sync_all()
        coin1 = get_coinbase_subsidy(self.nodes[1], as_decimal=True)

        # Verify listunspent returns immature coinbase if 'include_immature_coinbase' is set
        assert_equal(len(self.nodes[0].listunspent(query_options={'include_immature_coinbase': True})), 1)
        assert_equal(len(self.nodes[0].listunspent(query_options={'include_immature_coinbase': False})), 0)
        assert_equal(len(self.nodes[1].listunspent(query_options={'include_immature_coinbase': True})), 1)
        assert_equal(len(self.nodes[1].listunspent(query_options={'include_immature_coinbase': False})), 0)

        n_watchonly = len(self.mine_for_ticks(self.nodes[1], COINBASE_MATURITY_TICKS, address=RANDOM_COINBASE_ADDRESS))
        self.sync_all()

        # Verify listunspent returns all immature coinbases if 'include_immature_coinbase' is set
        # Wallet will see the coinbases going to the imported 'RANDOM_COINBASE_ADDRESS'
        assert_equal(len(self.nodes[0].listunspent(query_options={'include_immature_coinbase': False})), 1)
        assert_equal(len(self.nodes[0].listunspent(query_options={'include_immature_coinbase': True})),
                     n_watchonly + 1)

        assert_equal(self.nodes[0].getbalance(), coin0)
        assert_equal(self.nodes[1].getbalance(), coin1)

        self.log.info("Test getbalance with different arguments")
        assert_equal(self.nodes[0].getbalance("*"), coin0)
        assert_equal(self.nodes[0].getbalance("*", 1), coin0)
        assert_equal(self.nodes[0].getbalance("*", 1, True), coin0)
        assert_equal(self.nodes[0].getbalance(minconf=1), coin0)

        # Send 4/5 of node 0's coin to node 1, then 2/3 of what node 1 then
        # holds back to node 0, so that both of node 1's utxos are always spent
        # and its change is its whole remaining balance.
        fee = Decimal('0.01')
        amt_0_to_1 = coin0 * 4 / 5
        largest_utxo = max(coin1, amt_0_to_1)
        amt_1_to_0 = (largest_utxo + coin1 + amt_0_to_1) / 2
        assert_not_equal(amt_0_to_1, amt_1_to_0)
        txs = create_transactions(self.nodes[0], self.nodes[1].getnewaddress(), amt_0_to_1, [fee])
        # sending on both nodes is faster than waiting for propagation
        self.nodes[0].sendrawtransaction(txs[0]['hex'])
        self.nodes[1].sendrawtransaction(txs[0]['hex'])
        self.sync_all()

        txs = create_transactions(self.nodes[1], self.nodes[0].getnewaddress(), amt_1_to_0, [fee, Decimal('0.02')])
        # sending on both nodes is faster than waiting for propagation
        self.nodes[0].sendrawtransaction(txs[0]['hex'])
        self.nodes[1].sendrawtransaction(txs[0]['hex'])
        self.sync_all()

        # First argument of getbalance must be set to "*"
        assert_raises_rpc_error(-32, "dummy first argument must be excluded or set to \"*\"",
                                self.nodes[1].getbalance, "")

        self.log.info(
            "Test getbalance and getunconfirmedbalance with unconfirmed inputs")

        change0 = coin0 - amt_0_to_1 - fee
        change1 = coin1 + amt_0_to_1 - amt_1_to_0 - fee
        assert_not_equal(change0, change1)

        # getbalance without any arguments includes unconfirmed transactions,
        # but not untrusted transactions
        assert_equal(self.nodes[0].getbalance(),
                     change0)  # change from node 0's send
        assert_equal(self.nodes[1].getbalance(),
                     change1)  # change from node 1's send
        # Same with minconf=0
        assert_equal(self.nodes[0].getbalance(minconf=0), change0)
        assert_equal(self.nodes[1].getbalance(minconf=0), change1)
        # getbalance with a minconf incorrectly excludes coins that have been spent more recently than the minconf blocks ago
        # TODO: fix getbalance tracking of coin spentness depth
        assert_equal(self.nodes[0].getbalance(minconf=1), Decimal('0'))
        assert_equal(self.nodes[1].getbalance(minconf=1), Decimal('0'))
        # getunconfirmedbalance
        # output of node 1's spend
        assert_equal(self.nodes[0].getunconfirmedbalance(), amt_1_to_0)
        # Doesn't include output of node 0's send since it was spent
        assert_equal(self.nodes[1].getunconfirmedbalance(), Decimal('0'))

        # In the original Core version of this test, Node 1 would've bumped
        # the fee by 0.01 here to resend, but this is BCH, so it has 0.01 BCH
        # left to spend on goods and services
        self.sync_all()

        self.log.info(
            "Test getbalance and getunconfirmedbalance with conflicted unconfirmed inputs")

        # output of node 1's send
        assert_equal(self.nodes[0].getwalletinfo()[
                     "unconfirmed_balance"], amt_1_to_0)
        assert_equal(self.nodes[0].getunconfirmedbalance(), amt_1_to_0)
        # Doesn't include output of node 0's send since it was spent
        assert_equal(self.nodes[1].getwalletinfo()[
                     "unconfirmed_balance"], Decimal('0'))
        assert_equal(self.nodes[1].getunconfirmedbalance(), Decimal('0'))

        self.generatetoaddress(self.nodes[1], 1, RANDOM_COINBASE_ADDRESS)
        self.sync_all()

        # balances are correct after the transactions are confirmed
        # node 1's send plus change from node 0's send
        assert_equal(self.nodes[0].getbalance(), amt_1_to_0 + change0)
        assert_equal(self.nodes[1].getbalance(),
                     change1)  # change from node 0's send

        # Send all but two fees away from node 1, leaving one fee behind as change
        txs = create_transactions(self.nodes[1], self.nodes[0].getnewaddress(),
                                  change1 - 2 * fee, [fee])
        self.nodes[1].sendrawtransaction(txs[0]['hex'])
        self.generatetoaddress(self.nodes[1], 2, RANDOM_COINBASE_ADDRESS)
        self.sync_all()

        # getbalance with a minconf incorrectly excludes coins that have been spent more recently than the minconf blocks ago
        # TODO: fix getbalance tracking of coin spentness depth
        # getbalance with minconf=3 should still show the old balance
        assert_equal(self.nodes[1].getbalance(minconf=3), Decimal('0'))

        # getbalance with minconf=2 will show the new balance.
        assert_equal(self.nodes[1].getbalance(minconf=2), fee)


if __name__ == '__main__':
    WalletTest().main()
