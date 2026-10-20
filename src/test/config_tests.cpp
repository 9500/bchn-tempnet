// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2017-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <config.h>

#include <chainparams.h>
#include <consensus/consensus.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(config_tests, BasicTestingSetup)

static void CheckDefaultBlockSize(const Config &config) {
    BOOST_CHECK_EQUAL(config.GetDefaultConsensusBlockSize(legacy::POW_TARGET_SPACING_TICKS),
                      config.GetChainParams().GetConsensus().nDefaultConsensusBlockSize10Mins);
    BOOST_CHECK_EQUAL(config.GetDefaultConsensusBlockSize(upgrade2027::POW_TARGET_SPACING_TICKS),
                      config.GetChainParams().GetConsensus().nDefaultConsensusBlockSize10Mins / 10);
}

BOOST_AUTO_TEST_CASE(chain_params) {
    GlobalConfig config;

    // Global config is consistent with params.
    SelectParams(CBaseChainParams::MAIN);
    BOOST_CHECK_EQUAL(&Params(), &config.GetChainParams());
    CheckDefaultBlockSize(config);

    SelectParams(CBaseChainParams::TESTNET);
    BOOST_CHECK_EQUAL(&Params(), &config.GetChainParams());
    CheckDefaultBlockSize(config);

    SelectParams(CBaseChainParams::TESTNET4);
    BOOST_CHECK_EQUAL(&Params(), &config.GetChainParams());
    CheckDefaultBlockSize(config);

    SelectParams(CBaseChainParams::REGTEST);
    BOOST_CHECK_EQUAL(&Params(), &config.GetChainParams());
    CheckDefaultBlockSize(config);

    SelectParams(CBaseChainParams::SCALENET);
    BOOST_CHECK_EQUAL(&Params(), &config.GetChainParams());
    CheckDefaultBlockSize(config);

    SelectParams(CBaseChainParams::CHIPNET);
    BOOST_CHECK_EQUAL(&Params(), &config.GetChainParams());
    CheckDefaultBlockSize(config);
}

BOOST_AUTO_TEST_CASE(generated_block_size_percent) {
    const BlockTicks bt10 = legacy::POW_TARGET_SPACING_TICKS, bt1 = upgrade2027::POW_TARGET_SPACING_TICKS;

    for (const auto & ticks : {bt10, bt1}) {
        GlobalConfig config;
        BOOST_REQUIRE(ticks > BlockTicks{0});
        const uint64_t ratio = legacy::POW_TARGET_SPACING_TICKS / ticks;

        // Default constructed should be at the default consensus blocksize
        BOOST_CHECK_EQUAL(config.GetDefaultConsensusBlockSize(ticks), DEFAULT_CONSENSUS_BLOCK_SIZE / ratio);

        // Default to equal the max block size.
        BOOST_CHECK_EQUAL(config.GetDefaultConsensusBlockSize(ticks),
                          config.GetGeneratedBlockSize(ticks, DEFAULT_CONSENSUS_BLOCK_SIZE / ratio));

        // Out of range
        BOOST_CHECK(!config.SetGeneratedBlockSizePercent(-0.01));
        BOOST_CHECK_EQUAL(config.GetGeneratedBlockSize(ticks, std::nullopt), config.GetDefaultConsensusBlockSize(ticks));
        BOOST_CHECK(!config.SetGeneratedBlockSizePercent(100.1));
        BOOST_CHECK_EQUAL(config.GetGeneratedBlockSize(ticks, std::nullopt), config.GetDefaultConsensusBlockSize(ticks));

        BOOST_CHECK(config.SetGeneratedBlockSizePercent(0.0));
        BOOST_CHECK_EQUAL(config.GetGeneratedBlockSize(ticks, std::nullopt), 0);

        BOOST_CHECK(config.SetGeneratedBlockSizePercent(100.0));
        BOOST_CHECK_EQUAL(config.GetGeneratedBlockSize(ticks, std::nullopt), config.GetDefaultConsensusBlockSize(ticks));
        BOOST_CHECK_EQUAL(config.GetGeneratedBlockSize(ticks, 64 * ONE_MEGABYTE), 64 * ONE_MEGABYTE);

        // try various percentages and they should be what we expect
        for (double percent = 0.0; percent <= 100.0; percent += 0.1) {
            const uint64_t size_override = 64 * ONE_MEGABYTE;
            const uint64_t expected = config.GetDefaultConsensusBlockSize(ticks) * (percent / 100.0);
            const uint64_t expected_override = size_override * (percent / 100.0);
            BOOST_CHECK(config.SetGeneratedBlockSizePercent(percent));
            BOOST_CHECK_EQUAL(config.GetGeneratedBlockSize(ticks, std::nullopt), expected);
            BOOST_CHECK_EQUAL(config.GetGeneratedBlockSize(ticks, size_override), expected_override);
        }
    }
}

BOOST_AUTO_TEST_CASE(lookahead_guess) {
    GlobalConfig config;
    const auto ticks = legacy::POW_TARGET_SPACING_TICKS;

    BOOST_REQUIRE_EQUAL(config.GetDefaultConsensusBlockSize(ticks), DEFAULT_CONSENSUS_BLOCK_SIZE);

    size_t branch1{}, branch2{}, branch3{};
    for (uint64_t size = 0; size <= MAX_CONSENSUS_BLOCK_SIZE + ONE_MEGABYTE; size += ONE_MEGABYTE / 10) {
        config.NotifyMaxBlockSizeLookAheadGuessChanged(size);
        if (size <= config.GetDefaultConsensusBlockSize(ticks)) {
            // the max blocksize lookahead guess can never be smaller than the default consensus blocksize
            BOOST_CHECK_EQUAL(config.GetMaxBlockSizeLookAheadGuess(), config.GetDefaultConsensusBlockSize(ticks));
            ++branch1;
        } else if (size <= MAX_CONSENSUS_BLOCK_SIZE) {
            // however if it is set to larger, the lookahead guess should be verbatim what was set by
            // NotifyMaxBlockSizeLookAheadGuessChanged() above
            BOOST_CHECK_EQUAL(config.GetMaxBlockSizeLookAheadGuess(), size);
            ++branch2;
        } else {
            // except the lookahead guess should never exceed MAX_CONSENSUS_BLOCK_SIZE
            BOOST_CHECK_EQUAL(config.GetMaxBlockSizeLookAheadGuess(), MAX_CONSENSUS_BLOCK_SIZE);
            ++branch3;
        }
    }
    // Sanity check to ensure all 3 branches above were taken at least once
    BOOST_REQUIRE(branch1 && branch2 && branch3);
}

BOOST_AUTO_TEST_CASE(blocksize_override) {
    GlobalConfig config;
    const auto ticks = legacy::POW_TARGET_SPACING_TICKS;

    // paranoia: for the below test to be ok this must hold
    BOOST_REQUIRE(DEFAULT_CONSENSUS_BLOCK_SIZE != MAX_CONSENSUS_BLOCK_SIZE
                  && DEFAULT_CONSENSUS_BLOCK_SIZE != ONE_MEGABYTE + 1);

    // Default constructed should be at the default consensus blocksize
    BOOST_CHECK_EQUAL(config.GetDefaultConsensusBlockSize(ticks), DEFAULT_CONSENSUS_BLOCK_SIZE);

    // Disallow small values
    BOOST_CHECK(!config.SetBlockSizeOverride(1000));
    BOOST_CHECK(!config.SetBlockSizeOverride(ONE_MEGABYTE - 1));
    BOOST_CHECK(!config.SetBlockSizeOverride(ONE_MEGABYTE));
    // Disallow huge values
    BOOST_CHECK(!config.SetBlockSizeOverride(MAX_CONSENSUS_BLOCK_SIZE + 1));

    // Ensure nothing changed
    BOOST_CHECK_EQUAL(config.GetDefaultConsensusBlockSize(ticks), DEFAULT_CONSENSUS_BLOCK_SIZE);

    // Allow > ONE_MEGABYTE
    BOOST_CHECK(config.SetBlockSizeOverride(ONE_MEGABYTE + 1));
    BOOST_CHECK_EQUAL(config.GetDefaultConsensusBlockSize(ticks), ONE_MEGABYTE + 1);
    // Allow at limit
    BOOST_CHECK(config.SetBlockSizeOverride(MAX_CONSENSUS_BLOCK_SIZE));
    BOOST_CHECK_EQUAL(config.GetDefaultConsensusBlockSize(ticks), MAX_CONSENSUS_BLOCK_SIZE);

    // Clear and check again
    BOOST_CHECK(config.SetBlockSizeOverride(std::nullopt));
    BOOST_CHECK_EQUAL(config.GetDefaultConsensusBlockSize(ticks), DEFAULT_CONSENSUS_BLOCK_SIZE);
}

BOOST_AUTO_TEST_CASE(tx_broadcast_interval) {
    GlobalConfig config;
    // check that default constructed config has this value defined and defaulted
    BOOST_CHECK_EQUAL(config.GetInvBroadcastInterval(), DEFAULT_INV_BROADCAST_INTERVAL);
    // set it
    BOOST_CHECK(config.SetInvBroadcastInterval(DEFAULT_INV_BROADCAST_INTERVAL * 2));
    // check that it took effect
    BOOST_CHECK_EQUAL(config.GetInvBroadcastInterval(), DEFAULT_INV_BROADCAST_INTERVAL * 2);
    // set to max
    BOOST_CHECK(config.SetInvBroadcastInterval(MAX_INV_BROADCAST_INTERVAL));
    BOOST_CHECK_EQUAL(config.GetInvBroadcastInterval(), MAX_INV_BROADCAST_INTERVAL);
    // beyond max should fail, but first reset to some known value
    BOOST_CHECK(config.SetInvBroadcastInterval(42));
    BOOST_CHECK_EQUAL(config.GetInvBroadcastInterval(), 42);
    // ensure fails
    BOOST_CHECK(!config.SetInvBroadcastInterval(MAX_INV_BROADCAST_INTERVAL + 1));
    // ensure previous value is preserved
    BOOST_CHECK_EQUAL(config.GetInvBroadcastInterval(), 42);
}

BOOST_AUTO_TEST_CASE(tx_broadcast_rate) {
    GlobalConfig config;
    const BlockTicks bt10 = legacy::POW_TARGET_SPACING_TICKS,
                     bt1 = upgrade2027::POW_TARGET_SPACING_TICKS;
    BOOST_REQUIRE(bt1 * 10 == bt10);
    // Check that default constructed config has this value defined and defaulted
    BOOST_CHECK_EQUAL(config.GetInvBroadcastRatePerMbForBlockInterval(bt10),
                      DEFAULT_INV_BROADCAST_RATE_PER_MB_PER_10_MIN_BLOCK);
    // Check that the value is scaled inversely proportional to the interval
    BOOST_CHECK_EQUAL(config.GetInvBroadcastRatePerMbForBlockInterval(bt1),
                      10 * DEFAULT_INV_BROADCAST_RATE_PER_MB_PER_10_MIN_BLOCK);
    // set it
    BOOST_CHECK(config.SetInvBroadcastRatePerMbFor10MinBlocks(101));
    // check that it took effect
    BOOST_CHECK_EQUAL(config.GetInvBroadcastRatePerMbForBlockInterval(bt10), 101);
    BOOST_CHECK_EQUAL(config.GetInvBroadcastRatePerMbForBlockInterval(bt1), 1010);
    // set to max
    BOOST_CHECK(config.SetInvBroadcastRatePerMbFor10MinBlocks(MAX_INV_BROADCAST_RATE_PER_MB_PER_10_MIN_BLOCK));
    BOOST_CHECK_EQUAL(config.GetInvBroadcastRatePerMbForBlockInterval(bt10), MAX_INV_BROADCAST_RATE_PER_MB_PER_10_MIN_BLOCK);
    BOOST_CHECK_EQUAL(config.GetInvBroadcastRatePerMbForBlockInterval(bt1), 10 * MAX_INV_BROADCAST_RATE_PER_MB_PER_10_MIN_BLOCK);
    // beyond max should fail, but first reset to some known value
    BOOST_CHECK(config.SetInvBroadcastRatePerMbFor10MinBlocks(42));
    BOOST_CHECK_EQUAL(config.GetInvBroadcastRatePerMbForBlockInterval(bt10), 42);
    BOOST_CHECK_EQUAL(config.GetInvBroadcastRatePerMbForBlockInterval(bt1), 420);
    // ensure fails
    BOOST_CHECK(!config.SetInvBroadcastRatePerMbFor10MinBlocks(MAX_INV_BROADCAST_RATE_PER_MB_PER_10_MIN_BLOCK + 1));
    // ensure previous value is preserved
    BOOST_CHECK_EQUAL(config.GetInvBroadcastRatePerMbForBlockInterval(bt10), 42);
    BOOST_CHECK_EQUAL(config.GetInvBroadcastRatePerMbForBlockInterval(bt1), 420);
}

BOOST_AUTO_TEST_SUITE_END()
