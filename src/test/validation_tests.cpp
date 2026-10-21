// Copyright (c) 2011-2019 The Bitcoin Core developers
// Copyright (c) 2017-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>

#include <chain.h>
#include <chainparams.h>
#include <clientversion.h>
#include <config.h>
#include <consensus/consensus.h>
#include <net.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <util/system.h>

#include <test/setup_common.h>

#include <boost/signals2/signal.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstdio>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(validation_tests, TestingSetup)

static void TestBlockSubsidyHalvings(const Consensus::Params &consensusParams) {
    int maxHalvings = 64;
    Amount nInitialSubsidy = 50 * COIN;

    CBlockIndex dummyPrev;
    dummyPrev.SetTickDeltaForNextBlock(legacy::POW_TARGET_SPACING_TICKS);
    // for height == 0
    Amount nPreviousSubsidy = 2 * nInitialSubsidy;
    BOOST_CHECK_EQUAL(nPreviousSubsidy, 2 * nInitialSubsidy);
    for (int nHalvings = 0; nHalvings < maxHalvings; nHalvings++) {
        const BlockTicks ticks = nHalvings * consensusParams.subsidyHalvingInterval;
        dummyPrev.SetBlockTicks(ticks);
        Amount nSubsidy = GetBlockSubsidy(&dummyPrev, consensusParams);
        BOOST_CHECK(nSubsidy <= nInitialSubsidy);
        BOOST_CHECK_EQUAL(nSubsidy, nPreviousSubsidy / 2);
        nPreviousSubsidy = nSubsidy;
    }
    dummyPrev.SetBlockTicks(maxHalvings * consensusParams.subsidyHalvingInterval);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(&dummyPrev, consensusParams),
                      Amount::zero());
}

static void TestBlockSubsidyHalvings(int nSubsidyHalvingInterval) {
    Consensus::Params consensusParams;
    consensusParams.subsidyHalvingInterval = nSubsidyHalvingInterval * legacy::POW_TARGET_SPACING_TICKS;
    TestBlockSubsidyHalvings(consensusParams);
}

BOOST_AUTO_TEST_CASE(block_subsidy_test) {
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    // As in main
    TestBlockSubsidyHalvings(chainParams->GetConsensus());
    // As in regtest
    TestBlockSubsidyHalvings(150);
    // Just another interval
    TestBlockSubsidyHalvings(1000);
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test_10_min_blocks) {
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    BOOST_REQUIRE(0 == chainParams->GetConsensus().subsidyHalvingInterval / legacy::POW_TARGET_SPACING_TICKS % 1000);
    CBlockIndex dummyPrev;
    dummyPrev.SetTickDeltaForNextBlock(legacy::POW_TARGET_SPACING_TICKS);
    Amount nSum = Amount::zero();
    for (int nHeight = 0; nHeight < 14000000; nHeight += 1000) {
        dummyPrev.SetBlockTicks(nHeight * legacy::POW_TARGET_SPACING_TICKS);
        Amount nSubsidy = GetBlockSubsidy(&dummyPrev, chainParams->GetConsensus());
        BOOST_CHECK(nSubsidy <= 50 * COIN);
        nSum += 1000 * nSubsidy;
        BOOST_CHECK(MoneyRange(nSum));
    }
    BOOST_CHECK_EQUAL(nSum, int64_t(2099999997690000LL) * SATOSHI);
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test_1_min_blocks) {
    // Note that in the 1-minute block regime, we come up short 0.2499 BCH (due to int64_t truncation at the end).
    // This test ensures the behavior of GetBlockSubsidy() matches the spec, and that we are 0.2499 BCH short of
    // what we would have been at 10-minute blocks.
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    BOOST_REQUIRE(0 == chainParams->GetConsensus().subsidyHalvingInterval / upgrade2027::POW_TARGET_SPACING_TICKS % 10'000);
    CBlockIndex dummyPrev;
    dummyPrev.SetTickDeltaForNextBlock(upgrade2027::POW_TARGET_SPACING_TICKS);
    const BlockTicks end = 14'000'000 * legacy::POW_TARGET_SPACING_TICKS; // end is same tick count of 14 million 10-min blocks
    Amount nSum = Amount::zero();
    // We do this 10,000 1-min blocks at a time (same as 1000 10-min blocks at a time)
    for (BlockTicks ticks{0}; ticks < end; ticks += 10'000 * upgrade2027::POW_TARGET_SPACING_TICKS) {
        dummyPrev.SetBlockTicks(ticks);
        Amount nSubsidy = GetBlockSubsidy(&dummyPrev, chainParams->GetConsensus());
        BOOST_CHECK(nSubsidy <= 50 * COIN);
        nSum += 10'000 * nSubsidy;
        BOOST_CHECK(MoneyRange(nSum));
    }
    BOOST_CHECK_EQUAL(nSum, int64_t(2099999972700000LL) * SATOSHI);
}

static CBlock makeLargeDummyBlock(const size_t num_tx) {
    CBlock block;
    block.vtx.reserve(num_tx);

    for (size_t i = 0; i < num_tx; i++) {
        block.vtx.push_back(MakeTransactionRef());
    }
    return block;
}

/**
 * Test that LoadExternalBlockFile works with the buffer size set below the
 * size of a large block. Currently, LoadExternalBlockFile has the buffer size
 * for CBufferedFile set to 2 * MAX_TX_SIZE. Test with a value of
 * 10 * MAX_TX_SIZE.
 */
BOOST_AUTO_TEST_CASE(validation_load_external_block_file) {
    fs::path tmpfile_name =
        SetDataDir("validation_load_external_block_file") / "block.dat";

    FILE *fp = fopen(tmpfile_name.string().c_str(), "wb+");

    BOOST_CHECK(fp != nullptr);

    const Config &config = GetConfig();
    const CChainParams &chainparams = config.GetChainParams();

    // serialization format is:
    // message start magic, size of block, block

    size_t nwritten = fwrite(std::begin(chainparams.DiskMagic()),
                             CMessageHeader::MESSAGE_START_SIZE, 1, fp);

    BOOST_CHECK_EQUAL(nwritten, 1UL);

    const auto &empty_tx = CTransaction::null;
    size_t empty_tx_size = GetSerializeSize(empty_tx, CLIENT_VERSION);

    size_t num_tx = (10 * MAX_TX_SIZE) / empty_tx_size;

    CBlock block = makeLargeDummyBlock(num_tx);

    BOOST_CHECK(GetSerializeSize(block, CLIENT_VERSION) > 2 * MAX_TX_SIZE);

    unsigned int size = GetSerializeSize(block, CLIENT_VERSION);
    {
        CAutoFile outs(fp, SER_DISK, CLIENT_VERSION);
        outs << size;
        outs << block;
        outs.release();
    }

    fseek(fp, 0, SEEK_SET);
    BOOST_CHECK_NO_THROW({ LoadExternalBlockFile(config, fp, 0); });
}

BOOST_AUTO_TEST_SUITE_END()
