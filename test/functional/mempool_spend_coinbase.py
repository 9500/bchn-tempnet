#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017-present The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test spending coinbase transactions.

Coinbase maturity is a tick gate: an output created in block c is spendable in
block b iff T(b) - T(c) >= 60000 ticks, where T(x) is the cumulative tick count
through block x inclusive of x's own interval. That is 100 blocks on a legacy
chain and 1000 on a 1-minute chain.

`confirmations_ticks` includes the coin's own interval, so it equals that span
only when the coin's block and the spending block have the same target time:

    confirmations_ticks(coin) - target_time(coin) + target_time(next) >= 60000

Across the 600 -> 60 seam the flat `confirmations_ticks >= 60000` is short by
540 ticks in the unsafe direction: the seam coin can report exactly 60000 and
still be refused.

Three phases:

  1. uniform legacy interval, boundary derived from the cache tip;
  2. a coinbase whose maturity window spans the seam, where the flat predicate
     is unsafe;
  3. uniform fast interval, where the flat predicate is exact again but the
     span covers 1000 blocks instead of 100.

This test makes sure coinbase spends that will be mature in the next block are
accepted into the memory pool, but less mature coinbase spends are NOT.
"""

from decimal import Decimal

from test_framework.blocktools import (
    create_raw_transaction,
    get_block_confirmations_ticks,
    get_block_interval,
    get_chaintip_ticks,
    get_next_block_interval,
    get_next_block_tick_depth,
)
from test_framework.cdefs import COINBASE_MATURITY_TICKS
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

# Framework cache chain: 200 blocks, each mined at the legacy interval.
CACHE_HEIGHT = 200
LEGACY_INTERVAL = 600
FAST_INTERVAL = 60
CACHE_TIP_TICKS = CACHE_HEIGHT * LEGACY_INTERVAL

# The cache chain predates the activation time and MTP is the median of 11
# timestamps, so the seam lands 6 blocks after the cache tip.
SEAM_HEIGHT = CACHE_HEIGHT + 6


class MempoolSpendCoinbaseTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        node0_address = node.getnewaddress()

        def uniform_boundary(tip_height, i):
            """Highest height whose coinbase is mature in the next block, on a
            chain mined at a single interval `i` up to `tip_height`.

            T(tip+1) - T(h) == 60000  <=>  tip + 1 - h == 60000 / i
            """
            assert_equal(COINBASE_MATURITY_TICKS % i, 0)
            return tip_height + 1 - COINBASE_MATURITY_TICKS // i

        def make_spend(height):
            blockhash = node.getblockhash(height)
            txid = node.getblock(blockhash)["tx"][0]
            # Derived from the actual output: regtest halves every 150 blocks,
            # so cache heights >= 150 pay 25 BCH, not 50.
            value = node.getrawtransaction(txid, True, blockhash)["vout"][0]["value"]
            return create_raw_transaction(node, txid, node0_address,
                                          value - Decimal("0.01"))

        def check_uniform_boundary(label, tip_height, i):
            """Boundary checks for a stretch mined at a single interval, where
            `confirmations_ticks` and the consensus span are the same number."""
            h_ok = uniform_boundary(tip_height, i)
            h_bad = h_ok + 1
            assert_equal(get_block_interval(node, h_ok), i)
            assert_equal(get_block_interval(node, h_bad), i)

            span_ok = get_next_block_tick_depth(node, h_ok)
            span_bad = get_next_block_tick_depth(node, h_bad)
            assert_equal(span_ok, COINBASE_MATURITY_TICKS)
            assert span_bad < COINBASE_MATURITY_TICKS
            assert_equal(span_ok - span_bad, i)

            # Uniform: the RPC field equals the span, so `>= 60000` is both
            # necessary and sufficient here.
            assert_equal(get_block_confirmations_ticks(node, h_ok), COINBASE_MATURITY_TICKS)
            assert_equal(get_block_confirmations_ticks(node, h_ok), span_ok)
            assert_equal(get_block_confirmations_ticks(node, h_bad), span_bad)

            self.log.info("%s boundary: heights %d/%d, span %d/%d, conf_ticks %d/%d, %d blocks per span at %ds",
                          label, h_ok, h_bad, span_ok, span_bad, get_block_confirmations_ticks(node, h_ok),
                          get_block_confirmations_ticks(node, h_bad), COINBASE_MATURITY_TICKS // i, i)

            spend_ok = make_spend(h_ok)
            spend_bad = make_spend(h_bad)
            spend_ok_id = node.sendrawtransaction(spend_ok)

            # the next coinbase up is too immature to spend
            assert_raises_rpc_error(-26, "bad-txns-premature-spend-of-coinbase", node.sendrawtransaction, spend_bad)
            assert_equal(node.getrawmempool(), [spend_ok_id])

            # A uniform stretch matures the next coinbase in exactly one block.
            mined = 0
            while get_next_block_tick_depth(node, h_bad) < COINBASE_MATURITY_TICKS:
                self.generate(node, 1)
                mined += 1
            assert_equal(mined, 1)
            assert_equal(get_next_block_tick_depth(node, h_bad), COINBASE_MATURITY_TICKS)
            # Now exactly at the threshold: the predicate is tight, not merely satisfied.
            assert_equal(get_block_confirmations_ticks(node, h_bad), COINBASE_MATURITY_TICKS)
            assert_equal(set(node.getrawmempool()), set())

            # ... and now h_bad can be spent:
            spend_bad_id = node.sendrawtransaction(spend_bad)
            assert_equal(node.getrawmempool(), [spend_bad_id])
            self.generate(node, 1)
            assert_equal(set(node.getrawmempool()), set())

        # ------------------------------------------------------------------
        # The harness hands every test the same cache chain. Assert it, so a
        # framework change fails here rather than as a confusing span error.
        # ------------------------------------------------------------------
        assert_equal(node.getblockcount(), CACHE_HEIGHT)
        assert_equal(get_block_interval(node, 1), LEGACY_INTERVAL)
        assert_equal(get_block_interval(node, CACHE_HEIGHT), LEGACY_INTERVAL)
        assert_equal(get_chaintip_ticks(node), CACHE_TIP_TICKS)
        self.log.info("cache chain: %d blocks, %d ticks, %ds interval", CACHE_HEIGHT, CACHE_TIP_TICKS, LEGACY_INTERVAL)

        # ------------------------------------------------------------------
        # Phase 1: the original scenario, on the uniform cache chain.
        # ------------------------------------------------------------------
        check_uniform_boundary("phase 1", CACHE_HEIGHT, LEGACY_INTERVAL)

        # ------------------------------------------------------------------
        # Phases 2 and 3 need the seam.
        # ------------------------------------------------------------------
        to_seam = SEAM_HEIGHT - node.getblockcount()
        if to_seam > 0:
            self.generate(node, to_seam)
        act = node.getblockcount()
        assert_equal(act, SEAM_HEIGHT)
        assert_equal(get_block_interval(node, act), LEGACY_INTERVAL)
        # At this point either upgrade2027 is activated or not activated. If it is activated, it is either because this
        # test is being run after May 15, 2027, or because the user specified --with-upgrade2027activation. If that
        # is not the case, and we are still on 10-min block times, just return early.
        if get_next_block_interval(node) != FAST_INTERVAL:
            # It did not activate naturally, so check that upgrade2027activation is NOT set
            assert not self.options.upgrade2027activation
            self.log.info("phases 2 and 3 skipped: need --with-upgrade2027activation")
            return

        # If we get here we definitely are at the upgrade boundary and the next block is 1-min
        assert_equal(get_next_block_interval(node), FAST_INTERVAL)

        # ------------------------------------------------------------------
        # Phase 2: a coinbase whose maturity window spans the 600 -> 60 seam.
        # ------------------------------------------------------------------
        tau_seam = get_block_interval(node, act)
        tau_next = get_next_block_interval(node)
        gap = tau_seam - tau_next
        naive_depth = COINBASE_MATURITY_TICKS - gap
        self.log.info("phase 2: seam coin at height %d (%ds), tip at %ds, gap %d ticks, naive boundary at depth %d",
                      act, tau_seam, tau_next, gap, naive_depth)

        # Mine until a flat `confirmations_ticks >= 60000` calls it mature.
        self.mine_for_ticks(node, naive_depth - get_next_block_tick_depth(node, act))
        assert_equal(get_next_block_tick_depth(node, act), naive_depth)
        assert_equal(get_block_confirmations_ticks(node, act), COINBASE_MATURITY_TICKS)

        # Which is exactly wrong: the span is 540 ticks short.
        assert_raises_rpc_error(-26, "bad-txns-premature-spend-of-coinbase",
                                node.sendrawtransaction, make_spend(act))

        # The neighbor one block older is genuinely mature, so the rule
        # discriminates rather than merely rejecting.
        assert get_next_block_tick_depth(node, act - 1) >= COINBASE_MATURITY_TICKS
        older_id = node.sendrawtransaction(make_spend(act - 1))
        assert_equal(node.getrawmempool(), [older_id])
        self.log.info("phase 2: conf_ticks=%d but span=%d is short; height %d accepted instead",
                      get_block_confirmations_ticks(node, act), get_next_block_tick_depth(node, act), act - 1)

        # Mine the remaining gap; the seam coinbase matures.
        self.mine_for_ticks(node, gap)
        assert_equal(get_next_block_tick_depth(node, act), COINBASE_MATURITY_TICKS)
        assert_equal(get_block_confirmations_ticks(node, act), COINBASE_MATURITY_TICKS + gap)
        assert_equal(set(node.getrawmempool()), set())

        seam_id = node.sendrawtransaction(make_spend(act))
        assert_equal(node.getrawmempool(), [seam_id])
        self.generate(node, 1)
        assert_equal(set(node.getrawmempool()), set())
        self.log.info("phase 2: matured at conf_ticks=%d, spent OK", COINBASE_MATURITY_TICKS + gap)

        # ------------------------------------------------------------------
        # Phase 3: uniform fast chain. The flat predicate is exact again --
        # conf_ticks == 60000 is precisely the boundary -- but the span covers
        # 1000 blocks rather than 100. Phase 2 showed the flat predicate is
        # unsafe across the seam; this shows it is tight off the seam.
        # ------------------------------------------------------------------
        assert_equal(self.block_interval, FAST_INTERVAL)
        # The window must lie entirely past the seam, or the span would not be
        # a clean multiple of the interval.
        assert uniform_boundary(node.getblockcount(), FAST_INTERVAL) > act
        check_uniform_boundary("phase 3", node.getblockcount(), FAST_INTERVAL)


if __name__ == '__main__':
    MempoolSpendCoinbaseTest().main()
