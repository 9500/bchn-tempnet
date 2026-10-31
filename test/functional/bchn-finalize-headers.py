#!/usr/bin/env python3
# Copyright (c) 2020-present The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test processing of -finalizeheaders and -finalizeheaderspenalty

Setup: two nodes, node0 + node1, not connected to each other.

Node0 will be used to test behavior of receiving headers with header finalization enabled.
Node1 will test with header finalization disabled.

We have one P2PInterface connection to node0 called node_with_finalheaders,
and one to node1 called node_without_finalheaders.

We use a -finalizeheaderspenalty of 50 so that two below-finalized headers
are needed for node 0 to disconnect us.

Blocks are created so that the chains contain a finalized block.
Then, headers for blocks that would replace the finalized one, and deeper,
are created, and it is checked that the node on which headers are checked
against the finalized block, will disconnect the submitter for misbehavior.
"""

import time

from test_framework.blocktools import (
    create_block,
    create_coinbase,
    get_next_block_interval,
)
from test_framework.messages import (
    CBlockHeader,
    msg_headers,
)
from test_framework.p2p import P2PInterface
from test_framework.cdefs import DEFAULT_MAX_REORG_TICKS
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

def mine_header(prevblockhash, coinbase, timestamp):
    # Create a valid block and return its header
    block = create_block(int("0x" + prevblockhash, 0), coinbase, timestamp)
    block.solve()
    return CBlockHeader(block)


class FinalizedHeadersTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        # use a finalization delay of 0
        common_extra_args = ["-finalizationdelay=0", "-noparkdeepreorg"]
        self.extra_args = [common_extra_args + ["-finalizeheaders=1",
                                                "-finalizeheaderspenalty=50"],
                           common_extra_args + ["-finalizeheaders=0"]]

    def setup_network(self):
        self.setup_nodes()

    def run_test(self):
        # Setup the p2p connections
        # node_with_finalheaders connects to node0
        node_with_finalheaders = self.nodes[0].add_p2p_connection(P2PInterface())
        # node_without_finalheaders connects to node1
        node_without_finalheaders = self.nodes[1].add_p2p_connection(P2PInterface())

        genesis_hash = [n.getbestblockhash() for n in self.nodes]
        assert_equal(genesis_hash[0], genesis_hash[1])

        assert_equal(self.nodes[0].getblockcount(), 0)
        assert_equal(self.nodes[1].getblockcount(), 0)

        # Have nodes mine enough blocks to get them to finalize. The gate is
        # 6000 ticks, so the number of blocks differs by the interval ratio.
        # One extra legacy interval of margin keeps the finalized block off
        # genesis, so there is a parent to build the below-finalized header on.
        for n in self.nodes:
            self.mine_for_ticks(n, DEFAULT_MAX_REORG_TICKS + get_next_block_interval(n))

        def finalized_height(node):
            fin = node.getfinalizedblockhash()
            assert fin != "", "expected a finalized block"
            return node.getblockheader(fin)["height"]

        # Both nodes run the same consensus against the same number of blocks,
        # so the gate lands at the same height on each even though the chains
        # themselves (timestamps, hashes) differ.
        tip_height = self.nodes[0].getblockcount()
        fin_height = finalized_height(self.nodes[0])
        assert_equal(self.nodes[1].getblockcount(), tip_height)
        assert_equal(finalized_height(self.nodes[1]), fin_height)

        # The finalized block must have a parent, and at least one
        # non-finalized block must sit above it, or the loop below is empty.
        assert fin_height >= 1
        assert tip_height > fin_height + 1

        def construct_header_for(node, height, time_stamp):
            parent_hash = node.getblockhash(height - 1)
            return mine_header(parent_hash, create_coinbase(height), time_stamp)

        # For both nodes:
        # Replacement headers for block from tip down to last
        # non-finalized block should be accepted.
        block_time = int(time.time())
        # Derived from the gate rather than a block constant: at 600 ticks this
        # is 9 iterations, at 60 ticks 99, both ending one above the finalized
        # block.
        for height in range(tip_height - 1, fin_height, -1):
            # Create a header for node 0 and submit it
            headers_message = msg_headers()
            headers_message.headers.append(construct_header_for(self.nodes[0],
                                                                height,
                                                                block_time))
            node_with_finalheaders.send_and_ping(headers_message)

            # Create a header for node 1 and submit it
            headers_message = msg_headers()
            headers_message.headers.append(construct_header_for(self.nodes[1],
                                                                height,
                                                                block_time))
            node_without_finalheaders.send_and_ping(headers_message)

            # Both nodes remain connected in this loop because
            # the new headers do not attempt to replace the finalized block
            assert node_with_finalheaders.is_connected
            assert node_without_finalheaders.is_connected

        # Now, headers that would replace the finalized block...
        # The header-finalizing node should reject the deeper header
        # and get a DoS score of 50 while the non-header-finalizing node
        # will accept the header.
        headers_message = msg_headers()
        headers_message.headers.append(construct_header_for(self.nodes[0],
                                                            fin_height,
                                                            block_time))
        # Node 0 has not yet been disconnected, but it got a rejection logged and penalized
        expected_header_rejection_msg = ["peer=0 (0 -> 50) reason: bad-header-finalization", ]
        with self.nodes[0].assert_debug_log(expected_msgs=expected_header_rejection_msg, timeout=10):
            node_with_finalheaders.send_and_ping(headers_message)
            # The long sleep below is for GitLab CI.
            # On local modern test machines a sleep of 1 second worked
            # very reliably.
            time.sleep(4)
        assert node_with_finalheaders.is_connected

        headers_message = msg_headers()
        headers_message.headers.append(construct_header_for(self.nodes[1],
                                                            fin_height,
                                                            block_time))
        node_without_finalheaders.send_message(headers_message)
        time.sleep(1)
        assert node_without_finalheaders.is_connected

        # Now, one more header on both...
        # The header-finalizing node should disconnect while the
        # non-header-finalizing node will accept the header.
        headers_message = msg_headers()
        headers_message.headers.append(construct_header_for(self.nodes[0],
                                                            fin_height,
                                                            block_time))
        # Node 0 should disconnect when we send again
        expected_header_rejection_msg = ["peer=0 (50 -> 100) reason: bad-header-finalization", ]
        with self.nodes[0].assert_debug_log(expected_msgs=expected_header_rejection_msg, timeout=10):
            node_with_finalheaders.send_message(headers_message)
            # Again, a long sleep below only for GitLab CI.
            time.sleep(4)
        assert not node_with_finalheaders.is_connected

        headers_message = msg_headers()
        headers_message.headers.append(construct_header_for(self.nodes[1],
                                                            fin_height,
                                                            block_time))
        node_without_finalheaders.send_message(headers_message)
        time.sleep(1)
        assert node_without_finalheaders.is_connected


if __name__ == '__main__':
    FinalizedHeadersTest().main()
