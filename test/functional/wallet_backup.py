#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017-present The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the wallet backup features.

Test case is:
4 nodes. 1 2 and 3 send transactions between each other,
fourth node is a miner.
1 2 3 each mine a block to start, then
Miner creates enough blocks to mature those coinbases.
Then 5 iterations of 1/2/3 sending coins amongst
themselves to get transactions in the wallets,
and the miner mining one block.

Wallets are backed up using dumpwallet/backupwallet.
Then 5 more iterations of transactions and mining a block.

Miner then mines until every transaction fee paid so far
has matured.

Sanity check:
  Sum(1,2,3,4 balances) == sum of the mature coinbase values

1/2/3 are shutdown, and their wallets erased.
Then restore using wallet.dat backup. And
confirm 1/2/3/4 balances are same as before.

Shutdown again, restore using importwallet,
and confirm again balances are correct.
"""
from decimal import Decimal
import os
from random import randint
import shutil

from test_framework.blocktools import get_coinbase_subsidy
from test_framework.cdefs import COINBASE_MATURITY_TICKS
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    connect_nodes,
)


class WalletBackupTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        # nodes 1, 2,3 are spenders, let's give them a keypool=100
        # whitelist all peers to speed up tx relay / mempool sync
        self.extra_args = [
            ["-keypool=100", "-whitelist=127.0.0.1"],
            ["-keypool=100", "-whitelist=127.0.0.1"],
            ["-keypool=100", "-whitelist=127.0.0.1"],
            ["-whitelist=127.0.0.1"]
        ]
        self.rpc_timeout = 120

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self):
        self.setup_nodes()
        connect_nodes(self.nodes[0], self.nodes[3])
        connect_nodes(self.nodes[1], self.nodes[3])
        connect_nodes(self.nodes[2], self.nodes[3])
        connect_nodes(self.nodes[2], self.nodes[0])
        self.sync_all()

    def one_send(self, from_node, to_address):
        if (randint(1, 2) == 1):
            amount = Decimal(randint(1, 10)) / Decimal(10)
            self.nodes[from_node].sendtoaddress(to_address, amount)

    def do_one_round(self):
        a0 = self.nodes[0].getnewaddress()
        a1 = self.nodes[1].getnewaddress()
        a2 = self.nodes[2].getnewaddress()

        self.one_send(0, a1)
        self.one_send(0, a2)
        self.one_send(1, a0)
        self.one_send(1, a2)
        self.one_send(2, a0)
        self.one_send(2, a1)

        # Have the miner (node3) mine a block.
        # Must sync mempools before mining.
        self.sync_mempools()
        self.generate(self.nodes[3], 1)
        self.sync_blocks()

    # As above, this mirrors the original bash test.
    def start_three(self):
        self.start_node(0)
        self.start_node(1)
        self.start_node(2)
        connect_nodes(self.nodes[0], self.nodes[3])
        connect_nodes(self.nodes[1], self.nodes[3])
        connect_nodes(self.nodes[2], self.nodes[3])
        connect_nodes(self.nodes[2], self.nodes[0])

    def stop_three(self):
        self.stop_node(0)
        self.stop_node(1)
        self.stop_node(2)

    def erase_three(self):
        os.remove(os.path.join(self.nodes[0].datadir, self.chain, 'wallets', 'wallet.dat'))
        os.remove(os.path.join(self.nodes[1].datadir, self.chain, 'wallets', 'wallet.dat'))
        os.remove(os.path.join(self.nodes[2].datadir, self.chain, 'wallets', 'wallet.dat'))

    def run_test(self):
        self.log.info("Generating initial blockchain")
        self.generate(self.nodes[0], 1)
        coin0 = get_coinbase_subsidy(self.nodes[0], as_decimal=True)
        self.sync_blocks()
        self.generate(self.nodes[1], 1)
        coin1 = get_coinbase_subsidy(self.nodes[1], as_decimal=True)
        self.sync_blocks()
        self.generate(self.nodes[2], 1)
        coin2 = get_coinbase_subsidy(self.nodes[2], as_decimal=True)
        self.sync_blocks()
        self.mine_for_ticks(self.nodes[3], COINBASE_MATURITY_TICKS)
        self.sync_blocks()

        assert_equal(self.nodes[0].getbalance(), coin0)
        assert_equal(self.nodes[1].getbalance(), coin1)
        assert_equal(self.nodes[2].getbalance(), coin2)
        assert_equal(self.nodes[3].getbalance(), 0)

        self.log.info("Creating transactions")
        # Five rounds of sending each other transactions.
        for i in range(5):
            self.do_one_round()

        self.log.info("Backing up")
        self.nodes[0].backupwallet(os.path.join(
            self.nodes[0].datadir, 'wallet.bak'))
        self.nodes[0].dumpwallet(os.path.join(
            self.nodes[0].datadir, 'wallet.dump'))
        self.nodes[1].backupwallet(os.path.join(
            self.nodes[1].datadir, 'wallet.bak'))
        self.nodes[1].dumpwallet(os.path.join(
            self.nodes[1].datadir, 'wallet.dump'))
        self.nodes[2].backupwallet(os.path.join(
            self.nodes[2].datadir, 'wallet.bak'))
        self.nodes[2].dumpwallet(os.path.join(
            self.nodes[2].datadir, 'wallet.dump'))

        self.log.info("More transactions")
        for i in range(5):
            self.do_one_round()

        self.generate(self.nodes[3], 1) # claim fees
        youngest_mature_height = self.nodes[3].getblockcount() # capture for totals sum below
        self.mine_for_ticks(self.nodes[3], COINBASE_MATURITY_TICKS) # mature fee payment
        self.sync_all()

        balance0 = self.nodes[0].getbalance()
        balance1 = self.nodes[1].getbalance()
        balance2 = self.nodes[2].getbalance()
        balance3 = self.nodes[3].getbalance()
        total = balance0 + balance1 + balance2 + balance3

        # Sanity check: verify mature coinbase rewards match total wallet balances
        node = self.nodes[3]
        expected_total = sum(get_coinbase_subsidy(node, blockhash_or_height=h, as_decimal=True)
                             for h in range(1, youngest_mature_height + 1))
        assert_equal(total, expected_total)

        ##
        # Test restoring spender wallets from backups
        ##
        self.log.info("Restoring using wallet.dat")
        self.stop_three()
        self.erase_three()

        # Start node2 with no chain
        shutil.rmtree(os.path.join(self.nodes[2].datadir, self.chain, 'blocks'))
        shutil.rmtree(os.path.join(self.nodes[2].datadir, self.chain, 'chainstate'))

        # Restore wallets from backup
        shutil.copyfile(
            os.path.join(self.nodes[0].datadir, 'wallet.bak'),
            os.path.join(self.nodes[0].datadir, self.chain, 'wallets', 'wallet.dat'))
        shutil.copyfile(
            os.path.join(self.nodes[1].datadir, 'wallet.bak'),
            os.path.join(self.nodes[1].datadir, self.chain, 'wallets', 'wallet.dat'))
        shutil.copyfile(
            os.path.join(self.nodes[2].datadir, 'wallet.bak'),
            os.path.join(self.nodes[2].datadir, self.chain, 'wallets', 'wallet.dat'))

        self.log.info("Re-starting nodes")
        self.start_three()
        self.sync_blocks()

        assert_equal(self.nodes[0].getbalance(), balance0)
        assert_equal(self.nodes[1].getbalance(), balance1)
        assert_equal(self.nodes[2].getbalance(), balance2)

        self.log.info("Restoring using dumped wallet")
        self.stop_three()
        self.erase_three()

        # start node2 with no chain
        shutil.rmtree(os.path.join(self.nodes[2].datadir, self.chain, 'blocks'))
        shutil.rmtree(os.path.join(self.nodes[2].datadir, self.chain, 'chainstate'))

        self.start_three()

        assert_equal(self.nodes[0].getbalance(), 0)
        assert_equal(self.nodes[1].getbalance(), 0)
        assert_equal(self.nodes[2].getbalance(), 0)

        self.nodes[0].importwallet(os.path.join(
            self.nodes[0].datadir, 'wallet.dump'))
        self.nodes[1].importwallet(os.path.join(
            self.nodes[1].datadir, 'wallet.dump'))
        self.nodes[2].importwallet(os.path.join(
            self.nodes[2].datadir, 'wallet.dump'))

        self.sync_blocks()

        assert_equal(self.nodes[0].getbalance(), balance0)
        assert_equal(self.nodes[1].getbalance(), balance1)
        assert_equal(self.nodes[2].getbalance(), balance2)

        # Backup to source wallet file must fail
        sourcePaths = [
            os.path.join(self.nodes[0].datadir, self.chain, 'wallets', 'wallet.dat'),
            os.path.join(self.nodes[0].datadir, self.chain, '.', 'wallets', 'wallet.dat'),
            os.path.join(self.nodes[0].datadir, self.chain, 'wallets', ''),
            os.path.join(self.nodes[0].datadir, self.chain, 'wallets')]

        for sourcePath in sourcePaths:
            assert_raises_rpc_error(-4, "backup failed",
                                    self.nodes[0].backupwallet, sourcePath)


if __name__ == '__main__':
    WalletBackupTest().main()
