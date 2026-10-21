// Copyright (c) 2021-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <config.h>
#include <node/blockstorage.h>
#include <test/check_assert.h>
#include <test/setup_common.h>
#include <test/util.h>
#include <txmempool.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <cassert>
#include <utility>

BOOST_FIXTURE_TEST_SUITE(testlib_tests, BasicTestingSetup)

// Test the test suite facility for trapping assert() failures (see test/check_assert.h and test/check_assert.cpp)
BOOST_AUTO_TEST_CASE(check_assert_test) {
    if (CheckAssert([]{assert(false);}) == CheckAssertResult::Unsupported) {
        BOOST_WARN_MESSAGE(false, "Unable to test CheckAssert, not supported on this platform; skipping.");
        return; // skip this test for unsupported platforms
    }
    BOOST_CHECK(CheckAssert([]{ assert(!"any message"); })
                == CheckAssertResult::AssertEncountered);
    BOOST_CHECK(CheckAssert([]{ assert(!"wrong message"); }, "right message")
                == CheckAssertResult::AssertEncounteredWrongMessage);
    BOOST_CHECK(CheckAssert([]{ assert(!"right message"); }, "right message")
                == CheckAssertResult::AssertEncountered);
    BOOST_CHECK(CheckAssert([]{}, "ignored message")
                == CheckAssertResult::NoAssertEncountered);
    BOOST_CHECK(CheckAssert([]{ BOOST_CHECK(false); })
                == CheckAssertResult::NoAssertEncountered);

    // check that the macro works
    BCHN_CHECK_ASSERT(assert(!"expected message"), "expected message");
}

BOOST_FIXTURE_TEST_CASE(test_MatureCurrentTip, TestChainSetup) {
    const auto &config = ::GetConfig();
    const auto *tip =::ChainActive().Tip();
    BOOST_REQUIRE(tip);

    const CScript spk = CScript() << OP_2DROP << OP_TRUE;
    const auto & [outpt, amount] = MineBlock(config, spk);
    tip = ::ChainActive().Next(tip);
    const auto *savedTip = tip;

    // Attempt to spend the new block's coinbase, should fail due to immature coinbase
    const CTransactionRef spendTx = [&]{
        CMutableTransaction mtx;
        mtx.vin.emplace_back(outpt, CScript() << GetRandHash() << GetRandHash()); // ensure tx is big enough
        mtx.vout.emplace_back(amount - 1000 * SATOSHI, spk);
        return MakeTransactionRef(std::move(mtx));
    }();
    bool missingInputs = false;
    CValidationState state;
    bool res = WITH_LOCK(cs_main, return AcceptToMemoryPool(config, g_mempool, state, spendTx, &missingInputs, true, Amount::zero(), false));
    BOOST_CHECK(!res);
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-premature-spend-of-coinbase");
    BOOST_CHECK(!missingInputs);
    BOOST_CHECK(!g_mempool.exists(spendTx->GetId()));

    // Now mature the current tip; this should create just enough blocks that mempool spend of tip is possible.
    const auto nblocks = MatureCurrentTip(config, &spk);
    BOOST_CHECK(nblocks == 99 || nblocks == 999); // 10-min blocktime or 1-min blocktime
    tip = ::ChainActive().Tip();
    BOOST_CHECK_EQUAL(tip->nHeight - savedTip->nHeight, nblocks);
    // We should have created enough blocks so that mempool spends are now possible (they are checked against the NEXT block's ticks)
    BOOST_CHECK((tip->GetBlockTicks() + tip->GetTickDeltaForNextBlock()) - savedTip->GetBlockTicks() == kCoinbaseMaturityTicks);

    // Spending to mempool should work
    state = {};
    res = WITH_LOCK(cs_main, return AcceptToMemoryPool(config, g_mempool, state, spendTx, &missingInputs, true, Amount::zero(), false));
    BOOST_CHECK(res);
    BOOST_CHECK(state.IsValid());
    BOOST_CHECK(g_mempool.exists(spendTx->GetId()));

    // Spending to mempool for next block's coinbase should *not* work, however
    const CTransactionRef spendTx2 = [&] {
        const auto *nextBlock = ::ChainActive().Next(savedTip);
        BOOST_REQUIRE(nextBlock);
        CBlock block;
        BOOST_REQUIRE(ReadBlockFromDisk(block, nextBlock, config.GetChainParams().GetConsensus()));
        const auto &cbtx = block.vtx.at(0);
        CMutableTransaction mtx;
        mtx.vin.emplace_back(cbtx->GetId(), 0, CScript() << GetRandHash() << GetRandHash()); // ensure tx is big enough
        mtx.vout.emplace_back(cbtx->vout.at(0).nValue - 1000 * SATOSHI, spk);
        return MakeTransactionRef(std::move(mtx));
    }();
    state = {};
    missingInputs = false;
    res = WITH_LOCK(cs_main, return AcceptToMemoryPool(config, g_mempool, state, spendTx2, &missingInputs, true, Amount::zero(), false));
    BOOST_CHECK(!res);
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-premature-spend-of-coinbase");
    BOOST_CHECK(!missingInputs);
    BOOST_CHECK(!g_mempool.exists(spendTx2->GetId()));

    // Mining 1 block should now allow the above spendTx2 to be spent
    MineBlock(config, spk);
    state = {};
    res = WITH_LOCK(cs_main, return AcceptToMemoryPool(config, g_mempool, state, spendTx2, &missingInputs, true, Amount::zero(), false));
    BOOST_CHECK(res);
    BOOST_CHECK(state.IsValid());
    BOOST_CHECK(g_mempool.exists(spendTx2->GetId()));
}

BOOST_AUTO_TEST_SUITE_END()
