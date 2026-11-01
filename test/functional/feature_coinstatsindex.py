#!/usr/bin/env python3
# Copyright (c) 2020-2022 The Bitcoin Core developers
# Copyright (c) 2024-present The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test coinstatsindex across nodes.

Test that the values returned by gettxoutsetinfo are consistent
between a node running the coinstatsindex and a node without
the index.

Supports runs with the 10-minute -> 1-minute block time change active; heights
and subsidies are read from the chain rather than assumed, so both the 600-tick
and 60-tick regimes are covered.
"""

from decimal import Decimal

from test_framework.blocktools import (
    create_block,
    create_coinbase,
    get_coinbase_subsidy,
    get_next_block_interval,
)

from test_framework.cdefs import COINBASE_MATURITY_TICKS
from test_framework.messages import (
    COIN,
    CTransaction,
    CTxOut,
    FromHex,
)
from test_framework.script import (
    CScript,
    OP_FALSE
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_not_equal,
    assert_raises_rpc_error,
    connect_nodes,
    wait_until,
)


class CoinStatsIndexTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.supports_cli = False
        self.extra_args = [
            [],
            ["-coinstatsindex"]
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self._test_coin_stats_index()
        self._test_use_index_option()
        self._test_reorg_index()
        self._test_index_rejects_hash_serialized()
        self._test_init_index_after_reorg()

    @staticmethod
    def block_sanity_check(block_info, subsidy):
        # The block's own subsidy, not a constant: 1/10th as large once 1-min blocks is active.
        assert_equal(
            block_info['prevout_spent'] + subsidy,
            block_info['new_outputs_ex_coinbase'] + block_info['coinbase'] + block_info['unspendable']
        )

    def sync_index_node(self):
        wait_until(lambda: self.nodes[1].getindexinfo()['coinstatsindex']['synced'] is True)

    def _test_coin_stats_index(self):
        node = self.nodes[0]
        index_node = self.nodes[1]
        # Both none and muhash_testing options allow the usage of the index
        index_hash_options = ['none', 'muhash_testing', 'ecmh']

        def pop_hash_from_result(res):
            # Pop hash-related keys
            keys = ['muhash_testing', 'ecmh', 'ecmh_pubkey', 'hash_serialized_3']
            for to_pop in keys:
                res.pop(to_pop, None)

        def del_index_result_keys(res):
            # The fields 'block_info' and 'total_unspendable_amount' only exist on the index
            del res['block_info'], res['total_unspendable_amount']

        # Mine until the height-1 coinbase is spendable in the next block, plus
        # one more so the three spends below get a block of their own. At a
        # genesis tip the interval is 600 either way, so this is 101 blocks
        # under the 10-mins block regime and 1001 under the 1-min block regime.
        self.mine_for_ticks(node, COINBASE_MATURITY_TICKS + self.block_interval)
        assert get_next_block_interval(node) in (60, 600)

        # Height of the block that will carry the three spends below, captured
        # so no later query has to assume 10-minute block regime.
        self.spend_height = node.getblockcount() + 1

        # get_coinbase_subsidy() reports satoshis. The subsidy is 50 BCH or 5
        # BCH depending on whether 10-min or 1-min blocks are active.
        cb_sats = get_coinbase_subsidy(node)
        subsidy = Decimal(cb_sats) / COIN

        # Generate 3 txns that spend the same coin(s)
        addrs = [node.getnewaddress(), node.getnewaddress(), node.getnewaddress()]
        txns = []
        fees = []
        spends = []
        outs = []
        for addr in addrs:
            txid = node.sendtoaddress(addr, 1.0, "", "", False, 1, True)
            txns.append(txid)
            tx = node.getrawtransaction(txid, 2)
            fees.append(Decimal(tx['fee']))
            spends.append(sum(Decimal(inp['value']) for inp in tx['vin']))
            outs.append(sum(Decimal(outp['value']) for outp in tx['vout']))

        self.generate(node, 1)
        self.sync_blocks()
        # Guard that the three spends really landed in self.spend_height; the
        # by-height queries below would otherwise read the wrong block.
        assert_equal(node.getblockcount(), self.spend_height)

        self.log.info("Test that gettxoutsetinfo() output is consistent with or without coinstatsindex option")
        res0 = node.gettxoutsetinfo('none')

        # The fields 'disk_size' and 'transactions' do not exist on the index
        del res0['disk_size'], res0['transactions']

        for hash_option in index_hash_options:
            res1 = index_node.gettxoutsetinfo(hash_option)
            del_index_result_keys(res1)
            pop_hash_from_result(res1)
            # Everything left should be the same
            assert_equal(res1, res0)

        self.log.info("Test that gettxoutsetinfo() can get fetch data on specific heights with index")

        # Generate a new tip
        self.generate(node, 5)
        self.sync_blocks()

        for hash_option in index_hash_options:
            # Fetch old stats by height
            res2 = index_node.gettxoutsetinfo(hash_option, self.spend_height)
            del_index_result_keys(res2)
            pop_hash_from_result(res2)
            assert_equal(res0, res2)

            # Fetch old stats by hash
            res3 = index_node.gettxoutsetinfo(hash_option, res0['bestblock'])
            del_index_result_keys(res3)
            pop_hash_from_result(res3)
            assert_equal(res0, res3)

            # It does not work without coinstatsindex
            assert_raises_rpc_error(-8, "Querying specific block heights requires coinstatsindex",
                                    node.gettxoutsetinfo, hash_option, self.spend_height)

        self.log.info("Test gettxoutsetinfo() with index and verbose flag")

        for hash_option in index_hash_options:
            # Genesis block is unspendable. Genesis is a 600-tick block under
            # either schedule, so its subsidy is 50 BCH at any interval.
            res4 = index_node.gettxoutsetinfo(hash_option, 0)
            assert_equal(res4['total_unspendable_amount'], 50)
            assert_equal(res4['block_info'], {
                'unspendable': 50,
                'prevout_spent': 0,
                'new_outputs_ex_coinbase': 0,
                'coinbase': 0,
                'unspendables': {
                    'genesis_block': 50,
                    'bip30': 0,
                    'scripts': 0,
                    'unclaimed_rewards': 0
                }
            })
            self.block_sanity_check(res4['block_info'], 50)

            # Test an older block height that included 3 normal txns
            res5 = index_node.gettxoutsetinfo(hash_option, self.spend_height)
            assert_equal(res5['total_unspendable_amount'], 50)
            assert_equal(res5['block_info'], {
                'unspendable': 0,
                'prevout_spent': sum(spends),
                'new_outputs_ex_coinbase': sum(outs) ,
                'coinbase': subsidy + sum(fees),
                'unspendables': {
                    'genesis_block': 0,
                    'bip30': 0,
                    'scripts': 0,
                    'unclaimed_rewards': 0,
                }
            })
            self.block_sanity_check(res5['block_info'], subsidy)


        # Generate and send a normal tx with two outputs
        addr1 = node.validateaddress(node.get_deterministic_priv_key().address)['address']
        txid1 = node.sendtoaddress(addr1, Decimal('21.00000200'), "", "", False, 1, True)
        tx1 = node.getrawtransaction(txid1, 2)
        self.log.info(tx1)
        # Find the right position of the 21 BCH output
        out_n = None
        val_out_n = None
        fees = [Decimal(tx1['fee'])]
        spends = []
        outs = []
        for i, outp in enumerate(tx1['vout']):
            outs.append(Decimal(outp['value']))
            if outp['scriptPubKey']['addresses'][0] == addr1:
                out_n = i
                val_out_n = Decimal(outp['value'])
                assert val_out_n == Decimal('21.00000200')
        if out_n is None or val_out_n is None:
            raise RuntimeError("Could not find output N")
        for inp in tx1['vin']:
            spends.append(Decimal(inp['value']))

        # Generate and send another tx with a 21 BCH OP_RETURN output (which is unspendable)
        tx2 = FromHex(CTransaction(), node.createrawtransaction([{"txid": txid1, "vout": out_n}], [{"data": "ff"}]))
        tx2.vout[0].nValue = int(val_out_n * COIN - 200)  # Modify OP_RETURN to lock the 21 BCH amount
        fees.append(Decimal(200) / COIN)
        spends.append(val_out_n)
        res = node.signrawtransactionwithkey(tx2.serialize().hex(), [node.get_deterministic_priv_key().key])
        assert_equal(res['complete'], True)
        tx2 = FromHex(CTransaction(), res['hex'])
        tx2.rehash()
        tx2_val = Decimal(tx2.vout[0].nValue) / COIN
        assert_equal(tx2_val, 21)
        tx2_id = node.sendrawtransaction(tx2.serialize().hex())
        assert_equal(tx2_id, tx2.hash)

        # Include both txs in a block
        self.generate(self.nodes[0], 1)
        self.sync_blocks()
        self.opreturn_height = node.getblockcount()

        for hash_option in index_hash_options:
            # Check all amounts were registered correctly
            res6 = index_node.gettxoutsetinfo(hash_option, self.opreturn_height)
            assert_equal(res6['total_unspendable_amount'], 71)
            assert_equal(res6['block_info'], {
                'unspendable': tx2_val,
                'prevout_spent': sum(spends),
                'new_outputs_ex_coinbase': sum(outs),
                'coinbase': subsidy + sum(fees),
                'unspendables': {
                    'genesis_block': 0,
                    'bip30': 0,
                    'scripts': tx2_val,
                    'unclaimed_rewards': 0,
                }
            })
            self.block_sanity_check(res6['block_info'], subsidy)

        # Create a coinbase that does not claim full subsidy and has two
        # outputs: claim 70% of the subsidy in vout[0] and lock a further 10%
        # behind OP_FALSE, leaving 20% unclaimed. That is 35/5/10 BCH at 600
        # ticks and 3.5/0.5/1 BCH with upgrade2027 active.
        claimed_sats = cb_sats * 7 // 10
        locked_sats = cb_sats // 10
        unclaimed = Decimal(cb_sats - claimed_sats - locked_sats) / COIN
        # Set vout[0] directly: create_coinbase(nValue=) takes a pre-halving
        # base and shifts it by height // 150 -- a no-op at the legacy height
        # 109, but 6 halvings at the ~1009 this block lands on with upgrade2027.
        cb = create_coinbase(self.opreturn_height + 1)
        cb.vout[0].nValue = claimed_sats
        cb.vout.append(CTxOut(locked_sats, CScript([OP_FALSE])))
        cb.rehash()

        # Generate a block that includes previous coinbase
        tip = self.nodes[0].getbestblockhash()
        block_time = self.nodes[0].getblockheader(tip)['time'] + 1
        block = create_block(int(tip, 16), cb, block_time)
        block.solve()
        self.nodes[0].submitblock(block.serialize().hex())
        self.sync_all()

        for hash_option in index_hash_options:
            res7 = index_node.gettxoutsetinfo(hash_option, self.opreturn_height + 1)
            assert_equal(res7['total_unspendable_amount'], 71 + unclaimed)
            assert_equal(res7['block_info'], {
                'unspendable': unclaimed,
                'prevout_spent': 0,
                'new_outputs_ex_coinbase': 0,
                'coinbase': Decimal(claimed_sats + locked_sats) / COIN,
                'unspendables': {
                    'genesis_block': 0,
                    'bip30': 0,
                    'scripts': 0,
                    'unclaimed_rewards': unclaimed
                }
            })
            self.block_sanity_check(res7['block_info'], subsidy)

        for hash_type in ('muhash_testing', 'ecmh'):
            self.log.info(f"Test that the index is robust across restarts for hash_type: {hash_type}")

            res8 = index_node.gettxoutsetinfo(hash_type)
            # A bare restart_node() reads node.extra_args and re-applies the
            # harness's -upgrade2027activationtime injection. The explicit
            # extra_args= form drops it (which would leave this node mining
            # 50 BCH coinbases that node 0 rejects as bad-cb-amount).
            index_node.extra_args = self.extra_args[1]
            self.restart_node(1)
            assert_equal(get_next_block_interval(index_node), self.block_interval)
            res9 = index_node.gettxoutsetinfo(hash_type)
            assert_equal(res8, res9)

            self.generate(index_node, 1)
            res10 = index_node.gettxoutsetinfo(hash_type)
            assert res8['txouts'] < res10['txouts']

            self.log.info("Test that the index works with -reindex")

            index_node.extra_args = self.extra_args[1] + ["-reindex"]
            self.restart_node(1)
            self.sync_index_node()
            # Checked after the reindex, not right after the restart, because the
            # tip is still genesis while the block index is being rebuilt.
            assert_equal(get_next_block_interval(index_node), self.block_interval)
            res11 = index_node.gettxoutsetinfo(hash_type)
            assert_equal(res11, res10)

            self.log.info("Test that the index works with -reindex-chainstate")

            index_node.extra_args = self.extra_args[1] + ["-reindex-chainstate"]
            self.restart_node(1)
            self.sync_index_node()
            assert_equal(get_next_block_interval(index_node), self.block_interval)
            res12 = index_node.gettxoutsetinfo(hash_type)
            assert_equal(res12, res10)

        self.log.info("Test obtaining info for a non-existent block hash")
        assert_raises_rpc_error(-5, "Block not found", index_node.gettxoutsetinfo, hash_type="none",
                                hash_or_height="ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
                                use_index=True)

    def _test_use_index_option(self):
        self.log.info("Test use_index option for nodes running the index")

        connect_nodes(self.nodes[0], self.nodes[1])
        # tip is self.opreturn_height + 3: the extra coinbase block plus one
        # block mined per hash_type in the loop above.
        self.nodes[0].waitforblockheight(self.opreturn_height + 3)
        for hash_type in ('muhash_testing', 'ecmh'):
            res = self.nodes[0].gettxoutsetinfo(hash_type)
            option_res = self.nodes[1].gettxoutsetinfo(hash_type=hash_type, hash_or_height=None, use_index=False)
            del res['disk_size'], option_res['disk_size']
            assert_equal(res, option_res)

    def _test_reorg_index(self):
        self.log.info("Test that index can handle reorgs")

        # Generate two block, let the index catch up, then invalidate the blocks
        index_node = self.nodes[1]
        non_wallet_addr = index_node.get_deterministic_priv_key().address
        reorg_blocks = self.generatetoaddress(index_node, 2, non_wallet_addr)
        reorg_block = reorg_blocks[1]
        self.sync_index_node()
        res_invalid = index_node.gettxoutsetinfo('muhash_testing')
        index_node.invalidateblock(reorg_blocks[0])
        assert_equal(index_node.gettxoutsetinfo('muhash_testing')['height'], self.opreturn_height + 3)

        # Add two new blocks
        block = self.generate(index_node, 2)[1]
        res = index_node.gettxoutsetinfo(hash_type='muhash_testing', hash_or_height=None, use_index=False)
        assert_equal(res['height'], self.opreturn_height + 5)

        # Test that the result of the reorged block is not returned for its old block height
        res2 = index_node.gettxoutsetinfo(hash_type='muhash_testing',
                                          hash_or_height=self.opreturn_height + 5)
        assert_equal(res["bestblock"], block)
        assert_equal(res["muhash_testing"], res2["muhash_testing"])
        assert_not_equal(res["muhash_testing"], res_invalid["muhash_testing"])

        # Test that requesting reorged out block by hash is still returning correct results
        res_invalid2 = index_node.gettxoutsetinfo(hash_type='muhash_testing', hash_or_height=reorg_block)
        assert_equal(res_invalid2["muhash_testing"], res_invalid["muhash_testing"])
        assert_not_equal(res["muhash_testing"], res_invalid2["muhash_testing"])

        # Add another block, so we don't depend on reconsiderblock remembering which
        # blocks were touched by invalidateblock
        self.generate(index_node, 1)

        # Ensure that removing and re-adding blocks yields consistent results.
        # self.spend_height - 3 rather than a literal 99: with upgrade2027
        # active that literal is ~900 blocks deep, and the unwind should stay
        # shallow.
        block = index_node.getblockhash(self.spend_height - 3)
        index_node.invalidateblock(block)
        index_node.reconsiderblock(block)
        res3 = index_node.gettxoutsetinfo(hash_type='muhash_testing',
                                          hash_or_height=self.opreturn_height + 5)
        assert_equal(res2, res3)

    def _test_index_rejects_hash_serialized(self):
        self.log.info("Test that the rpc raises if the legacy hash is passed with the index")

        index_node = self.nodes[1]
        tip_height = index_node.getblockheader(index_node.getbestblockhash())['height']
        assert_greater_than(tip_height, 99)

        msg = "hash_serialized_3 hash type cannot be queried for a specific block"

        for hash_type in ('hash_serialized_3',):
            assert_raises_rpc_error(-8, msg, index_node.gettxoutsetinfo, hash_type=hash_type,
                                    hash_or_height=tip_height - 2)

            for use_index in (True, False, None):
                assert_raises_rpc_error(-8, msg, index_node.gettxoutsetinfo, hash_type=hash_type,
                                        hash_or_height=tip_height - 2, use_index=use_index)

    def _test_init_index_after_reorg(self):
        self.log.info("Test a reorg while the index is deactivated")
        index_node = self.nodes[1]
        block = self.nodes[0].getbestblockhash()
        self.generate(index_node, 2)
        self.sync_index_node()

        # Restart without index: drop -coinstatsindex only. The upgrade2027
        # activation flag (if present) must still be injected, or this node
        # rejects node 0's 60-tick blocks.
        index_node.extra_args = []
        self.restart_node(1)
        assert_equal(get_next_block_interval(index_node), self.block_interval)
        assert_equal(index_node.getindexinfo("coinstatsindex"), {})
        connect_nodes(self.nodes[0], self.nodes[1])
        index_node.invalidateblock(block)
        non_wallet_addr = index_node.get_deterministic_priv_key().address
        self.generatetoaddress(index_node, 5, non_wallet_addr)
        res = index_node.gettxoutsetinfo(hash_type='muhash_testing', hash_or_height=None, use_index=False)
        res_ec = index_node.gettxoutsetinfo(hash_type='ecmh', hash_or_height=None, use_index=False)

        # Restart with index that still has its best block on the old chain
        index_node.extra_args = self.extra_args[1]
        self.restart_node(1)
        assert_equal(get_next_block_interval(index_node), self.block_interval)
        self.sync_index_node()
        res1 = index_node.gettxoutsetinfo(hash_type='muhash_testing', hash_or_height=None, use_index=True)
        res1_ec = index_node.gettxoutsetinfo(hash_type='ecmh', hash_or_height=None, use_index=True)
        assert_equal(res["muhash_testing"], res1["muhash_testing"])
        assert_equal(res_ec["ecmh"], res1_ec["ecmh"])


if __name__ == '__main__':
    CoinStatsIndexTest().main()
