#!/usr/bin/env python3
# Copyright (c) 2018-present The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the parckblock and unparkblock RPC calls."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    connect_nodes_bi,
    wait_until
)


class ParkedChainTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [["-noparkdeepreorg",
                            "-noautomaticunparking", "-whitelist=noban@127.0.0.1"], ["-maxreorgticks=-1"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    # There should only be one chaintip, which is expected_tip
    def only_valid_tip(self, expected_tip, other_tip_status=None):
        node = self.nodes[0]
        assert_equal(node.getbestblockhash(), expected_tip)
        for tip in node.getchaintips():
            if tip["hash"] == expected_tip:
                assert_equal(tip["status"], "active")
            else:
                assert_equal(tip["status"], other_tip_status)

    def run_test(self):
        def wait_for_tip(node, tip):
            def check_tip():
                return node.getbestblockhash() == tip
            wait_until(check_tip)

        node = self.nodes[0]
        parking_node = self.nodes[1]

        self.log.info("Test chain parking...")
        self.generate(node, 10)
        tip = node.getbestblockhash()
        self.generate(node, 1)
        block_to_park = node.getbestblockhash()
        self.generate(node, 10)
        parked_tip = node.getbestblockhash()

        # get parking_node caught up.
        # (probably not needed, but just in case parking can have race
        # condition like invalidateblock below)
        wait_for_tip(parking_node, parked_tip)

        # Let's park the chain.
        assert parked_tip != tip
        assert block_to_park != tip
        assert block_to_park != parked_tip
        node.parkblock(block_to_park)
        assert_equal(node.getbestblockhash(), tip)

        # When the chain is unparked, the node reorg into its original chain.
        node.unparkblock(parked_tip)
        assert_equal(node.getbestblockhash(), parked_tip)

        # Parking and then unparking a block should not change its validity,
        # and invaliding and reconsidering a block should not change its
        # parked state.  See the following test cases:
        self.log.info("Test invalidate, park, unpark, reconsider...")
        self.generate(node, 1)
        tip = node.getbestblockhash()
        self.generate(node, 1)
        bad_tip = node.getbestblockhash()
        # Generate an extra block to check that children are invalidated as
        # expected and do not produce dangling chaintips
        self.generate(node, 1)
        good_tip = node.getbestblockhash()

        # avoid race condition from parking_node requesting block when invalid
        wait_for_tip(parking_node, good_tip)

        node.invalidateblock(bad_tip)
        self.only_valid_tip(tip, other_tip_status="invalid")
        node.parkblock(bad_tip)
        self.only_valid_tip(tip, other_tip_status="invalid")
        node.unparkblock(bad_tip)
        self.only_valid_tip(tip, other_tip_status="invalid")
        node.reconsiderblock(bad_tip)
        self.only_valid_tip(good_tip)

        self.log.info("Test park, invalidate, reconsider, unpark")
        self.generate(node, 1)
        tip = node.getbestblockhash()
        self.generate(node, 1)
        bad_tip = node.getbestblockhash()
        self.generate(node, 1)
        good_tip = node.getbestblockhash()

        # avoid race condition from parking_node requesting block when invalid
        wait_for_tip(parking_node, good_tip)

        node.parkblock(bad_tip)
        self.only_valid_tip(tip, other_tip_status="parked")
        node.invalidateblock(bad_tip)
        # NOTE: Intuitively, other_tip_status would be "invalid", but because
        # only valid (unparked) chains are walked, child blocks' statuses are
        # not updated, so the "parked" state remains.
        self.only_valid_tip(tip, other_tip_status="parked")
        node.reconsiderblock(bad_tip)
        self.only_valid_tip(tip, other_tip_status="parked")
        node.unparkblock(bad_tip)
        self.only_valid_tip(good_tip)

        self.log.info("Test invalidate, park, reconsider, unpark...")
        self.generate(node, 1)
        tip = node.getbestblockhash()
        self.generate(node, 1)
        bad_tip = node.getbestblockhash()
        self.generate(node, 1)
        good_tip = node.getbestblockhash()

        # avoid race condition from parking_node requesting block when invalid
        wait_for_tip(parking_node, good_tip)

        node.invalidateblock(bad_tip)
        self.only_valid_tip(tip, other_tip_status="invalid")
        node.parkblock(bad_tip)
        self.only_valid_tip(tip, other_tip_status="invalid")
        node.reconsiderblock(bad_tip)
        self.only_valid_tip(tip, other_tip_status="parked")
        node.unparkblock(bad_tip)
        self.only_valid_tip(good_tip)

        self.log.info("Test park, invalidate, unpark, reconsider")
        self.generate(node, 1)
        tip = node.getbestblockhash()
        self.generate(node, 1)
        bad_tip = node.getbestblockhash()
        self.generate(node, 1)
        good_tip = node.getbestblockhash()

        # avoid race condition from parking_node requesting block when invalid
        wait_for_tip(parking_node, good_tip)

        node.parkblock(bad_tip)
        self.only_valid_tip(tip, other_tip_status="parked")
        node.invalidateblock(bad_tip)
        # NOTE: Intuitively, other_tip_status would be "invalid", but because
        # only valid (unparked) chains are walked, child blocks' statuses are
        # not updated, so the "parked" state remains.
        self.only_valid_tip(tip, other_tip_status="parked")
        node.unparkblock(bad_tip)
        self.only_valid_tip(tip, other_tip_status="invalid")
        node.reconsiderblock(bad_tip)
        self.only_valid_tip(good_tip)

        # To get ready for next testset, make sure both nodes are in sync.
        wait_for_tip(parking_node, good_tip)
        assert_equal(node.getbestblockhash(), parking_node.getbestblockhash())

        # Wait for node 1 to park the chain.
        def wait_for_parked_block(block):
            def check_block():
                for tip in parking_node.getchaintips():
                    if tip["hash"] == block:
                        assert tip["status"] != "active"
                        return tip["status"] == "parked"
                return False
            wait_until(check_block)

        def parking_penalty_blocks(depth_blocks, depth_ticks):
            """P, in units of one block's work, per the tick-based unpark ramp.

            P/D = (clamp(d,600,2400)/2400)^2, D being the defender's work since the fork.
            A chain at depth D + k stays parked for k <= P and is unparked by the next block.
            """
            clamped = min(max(depth_ticks, 600), 2400)
            return depth_blocks * clamped * clamped // (2400 * 2400)

        def check_reorg_protection(depth_ticks):
            # Depths are given in ticks, not blocks: the unpark penalty is tick-keyed, so at
            # 600 ticks 600/1200/1800/2400/3000/3600/60000 are the legacy 1/2/3/4/5/6/100
            # blocks, while at 60 ticks they are 10/20/30/40/50/60/1000. The one-block
            # boundary case arrives as `self.block_interval` ticks, since the parking *trigger*
            # is block-count keyed and must stay one block at either interval.
            depth = depth_ticks // self.block_interval
            extra_blocks = parking_penalty_blocks(depth, depth_ticks)
            self.log.info("Test deep reorg parking, {} ticks ({} blocks) deep".format(
                depth_ticks, depth))

            # Invalidate the tip on node 0, so it doesn't follow node 1.
            node.invalidateblock(node.getbestblockhash())
            # Mine block to create a fork of proper depth
            self.generatetoaddress(parking_node,
                                   nblocks=depth - 1,
                                   address=parking_node.getnewaddress(label='coinbase'))
            self.generatetoaddress(node,
                                   nblocks=depth,
                                   address=node.getnewaddress(label='coinbase'))
            # The chain is now inside the ramp's tolerance (D + P), so these blocks park.
            for i in range(extra_blocks):
                self.generate(node, 1)
                wait_for_parked_block(node.getbestblockhash())

            # One block past D + P, so the parking test no longer holds and the node reorgs.
            self.generate(node, 1)
            wait_until(lambda: parking_node.getbestblockhash()
                       == node.getbestblockhash())

        # Tick depths to exercise: the trigger boundary (one block, expressed in ticks) plus the
        # ramp anchors. Depths are in ticks because the penalty is tick-keyed, but the trigger is
        # block-count keyed, so the boundary case must scale with the interval while the anchors
        # must not. At 600 ticks this is the legacy 1/1/2/3/4/5/6/100 blocks; at 60 ticks it is
        # 1/10/20/30/40/50/60/1000 blocks.
        for depth_ticks in sorted({self.block_interval, 600, 1200, 1800, 2400, 3000, 3600, 60000}):
            check_reorg_protection(depth_ticks)

        # try deep reorg with a log check.
        with parking_node.assert_debug_log(["Park block"]):
            check_reorg_protection(1800)

        self.log.info(
            "Accepting many blocks at once (possibly out of order) should not park if there is no reorg.")
        # rewind one block to make a reorg that is shallow.
        node.invalidateblock(parking_node.getbestblockhash())
        # generate a ton of blocks at once.
        try:
            with parking_node.assert_debug_log(["Park block"]):
                self.generatetoaddress(node,
                                       nblocks=20,
                                       address=node.getnewaddress(label='coinbase'))
                wait_until(lambda: parking_node.getbestblockhash() ==
                           node.getbestblockhash())
        except AssertionError as exc:
            # good, we want an absence of "Park block" messages
            assert "does not partially match log" in exc.args[0]
        else:
            raise AssertionError("Parked block when there was no deep reorg")

        self.log.info("Test that unparking works when -parkdeepreorg=0")
        # Set the defender at the 2x lock-in depth (2400 ticks since the fork) and the
        # competitor at 3000 ticks, then walk the competitor up to exactly D + P (== 2D once
        # d >= 2400) and one interval past it. At 600 ticks this is the legacy 4/5 blocks,
        # +3, +1; at 60 ticks it is 40/50 blocks, +30, +1.
        node.invalidateblock(node.getbestblockhash())
        self.mine_for_ticks(parking_node, 2400 - self.block_interval)
        self.mine_for_ticks(node, 3000)
        wait_for_parked_block(node.getbestblockhash())
        # Restart the parking node without parkdeepreorg, preserving the arguments the harness
        # injected (restart_node replaces self.extra_args[1], and dropping
        # -upgrade2027activationtime would reload this chain at 600 ticks and reject it).
        restart_args = [a for a in self.extra_args[1] if not a.startswith('-parkdeepreorg')]
        self.restart_node(1, ["-parkdeepreorg=0"] + restart_args)
        parking_node = self.nodes[1]
        connect_nodes_bi(node, parking_node)
        # The other chain should still be marked 'parked'.
        wait_for_parked_block(node.getbestblockhash())
        # Reaching exactly D + P is not enough to unpark: the comparison is strict.
        self.mine_for_ticks(node, 1800)
        wait_for_parked_block(node.getbestblockhash())
        # Past D + P, the chain unparks.
        self.mine_for_ticks(node, 600)
        wait_until(lambda: parking_node.getbestblockhash() ==
                   node.getbestblockhash(), timeout=5)

        # Do not append tests after this point without restarting node again.
        # Parking node is no longer parking.


if __name__ == '__main__':
    ParkedChainTest().main()
