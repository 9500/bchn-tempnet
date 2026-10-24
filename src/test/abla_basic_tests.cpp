// Copyright (c) 2023-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/abla.h>

#include <random.h>
#include <serialize.h>
#include <streams.h>
#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <limits>
#include <string_view>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(abla_basic_tests, BasicTestingSetup)

namespace {

/// Reference ("oracle") for State::CalcLookaheadBlockSizeLimitWorstCase(): the original O(count) loop that function
/// used before it was replaced by a closed-form upper bound. Rolls the algorithm forward `count` blocks, every block
/// 100% full and every block with interval `interval`, and returns the limit of the last one.
uint64_t IteratedLookahead(const abla::Config &config, const abla::State &start, size_t count, BlockTicks interval,
                           bool disable2GBCap) {
    abla::State s{start};
    for (size_t i = 0; i < count; ++i) {
        const uint64_t maxSize = s.GetNextBlockSizeLimit(config, interval, disable2GBCap);
        s = s.NextBlockState(config, interval, maxSize);
    }
    return s.GetBlockSizeLimit(disable2GBCap);
}

/// The lookahead only feeds DoS / buffer-sizing limits (max p2p message size, compact block sanity checks), so
/// it may overestimate. This is the tolerance the tests hold it to over the windows the node actually uses.
constexpr size_t LOOKAHEAD_SLACK_MAX_COUNT = 2048; // == BLOCK_DOWNLOAD_WINDOW * 2, as used in validation.cpp
constexpr double LOOKAHEAD_MAX_REL_SLACK = 0.10;   // at most 10% above the exact iterated value

/// Compares the closed-form lookahead against the iterated oracle. Returns the closed-form value.
/// Always: never below the oracle; exactly equal for count <= 1; equal for fixed-size configs.
/// If `checkSlack`: additionally no more than LOOKAHEAD_MAX_REL_SLACK above the oracle.
uint64_t CheckLookaheadVsIterated(const abla::Config &config, const abla::State &start, size_t count,
                                  BlockTicks interval, bool disable2GBCap, bool checkSlack) {
    const uint64_t closed = start.CalcLookaheadBlockSizeLimitWorstCase(config, count, interval, disable2GBCap);
    const uint64_t iterated = IteratedLookahead(config, start, count, interval, disable2GBCap);
    const auto ctx = [&] {
        return strprintf("%s, state=%s, count=%u, interval=%u, disable2GBCap=%d: closed=%u iterated=%u",
                         config.ToString(), start.ToString(), count, interval.value(), disable2GBCap, closed, iterated);
    };
    BOOST_CHECK_MESSAGE(closed >= iterated, "lookahead below exact value: " + ctx());
    if (count <= 1 || config.IsFixedSize()) {
        BOOST_CHECK_MESSAGE(closed == iterated, "lookahead not exact: " + ctx());
    }
    if (checkSlack) {
        BOOST_CHECK_MESSAGE(double(closed) <= double(iterated) * (1.0 + LOOKAHEAD_MAX_REL_SLACK),
                            "lookahead slack too large: " + ctx());
    }
    return closed;
}

} // namespace

BOOST_AUTO_TEST_CASE(config_ctor) {
    const abla::Config config;

    // Test that default-constructed config is all 0's and is not valid
    BOOST_CHECK(!config.IsValid());
    BOOST_CHECK_EQUAL(config.epsilon0, 0);
    BOOST_CHECK_EQUAL(config.beta0, 0);
    BOOST_CHECK_EQUAL(config.gammaReciprocal, 0);
    BOOST_CHECK_EQUAL(config.zeta_xB7, 0);
    BOOST_CHECK_EQUAL(config.thetaReciprocal, 0);
    BOOST_CHECK_EQUAL(config.delta, 0);
    BOOST_CHECK_EQUAL(config.epsilonMax, 0);
    BOOST_CHECK_EQUAL(config.betaMax, 0);
}

BOOST_AUTO_TEST_CASE(config_isvalid) {
    const abla::Config config = abla::Config::MakeDefault();
    const char *err = nullptr;

    BOOST_CHECK(config.IsValid(&err) && config.IsValid()); // test IsValid both with and without the err arg
    BOOST_CHECK_EQUAL(err, "");

    // Test default-constructed Config (all zeros) should be invalid
    BOOST_CHECK(!abla::Config{}.IsValid(&err) && !abla::Config{}.IsValid());
    BOOST_CHECK_NE(err, "");

    // Test IsValid on a bad Config: epsilon0 > epsilonMax
    auto bad = config;
    bad.epsilon0 = config.epsilonMax + 1u;
    BOOST_CHECK(!bad.IsValid(&err) && !bad.IsValid()); // test negative result from both forms of IsValid()
    BOOST_CHECK(std::string_view{err}.find("epsilonMax") != std::string_view::npos);
    bad = config;
    bad.epsilonMax = config.epsilon0 - 1u;
    BOOST_CHECK(!bad.IsValid(&err) && !bad.IsValid());
    BOOST_CHECK(std::string_view{err}.find("epsilonMax") != std::string_view::npos);

    // Test IsValid on a bad Config: beta0 > betaMax
    bad = config;
    bad.beta0 = config.betaMax + 1u;
    BOOST_CHECK(!bad.IsValid(&err) && !bad.IsValid());
    BOOST_CHECK(std::string_view{err}.find("betaMax") != std::string_view::npos);
    bad = config;
    bad.betaMax = config.beta0 - 1u;
    BOOST_CHECK(!bad.IsValid(&err) && !bad.IsValid());
    BOOST_CHECK(std::string_view{err}.find("betaMax") != std::string_view::npos);

    // Test IsValid on a bad Config: zeta_xB7 out of range
    bad = config;
    bad.zeta_xB7 = abla::MIN_ZETA_XB7 - 1u;
    BOOST_CHECK(!bad.IsValid(&err) && !bad.IsValid());
    BOOST_CHECK(std::string_view{err}.find("zeta") != std::string_view::npos);
    bad = config;
    bad.zeta_xB7 = abla::MAX_ZETA_XB7 + 1u;
    BOOST_CHECK(!bad.IsValid(&err) && !bad.IsValid());
    BOOST_CHECK(std::string_view{err}.find("zeta") != std::string_view::npos);

    // Test Isvalid on a bad Config: gammaReciprocal out of range
    bad = config;
    bad.gammaReciprocal = abla::MIN_GAMMA_RECIPROCAL - 1u;
    BOOST_CHECK(!bad.IsValid(&err) && !bad.IsValid());
    BOOST_CHECK(std::string_view{err}.find("gammaReciprocal") != std::string_view::npos);
    bad = config;
    bad.gammaReciprocal = abla::MAX_GAMMA_RECIPROCAL + 1u;
    BOOST_CHECK(!bad.IsValid(&err) && !bad.IsValid());
    BOOST_CHECK(std::string_view{err}.find("gammaReciprocal") != std::string_view::npos);

    // Test Isvalid on a bad Config: delta out of range
    bad = config;
    bad.delta = abla::MIN_DELTA - 1u;
    BOOST_CHECK(!bad.IsValid(&err) && !bad.IsValid());
    BOOST_CHECK(std::string_view{err}.find("delta") != std::string_view::npos);
    bad = config;
    bad.delta = abla::MAX_DELTA + 1u;
    BOOST_CHECK(!bad.IsValid(&err) && !bad.IsValid());
    BOOST_CHECK(std::string_view{err}.find("delta") != std::string_view::npos);

    // Test Isvalid on a bad Config: thetaReciprocal out of range
    bad = config;
    bad.thetaReciprocal = abla::MIN_THETA_RECIPROCAL - 1u;
    BOOST_CHECK(!bad.IsValid(&err) && !bad.IsValid());
    BOOST_CHECK(std::string_view{err}.find("thetaReciprocal") != std::string_view::npos);
    bad = config;
    bad.thetaReciprocal = abla::MAX_THETA_RECIPROCAL + 1u;
    BOOST_CHECK(!bad.IsValid(&err) && !bad.IsValid());
    BOOST_CHECK(std::string_view{err}.find("thetaReciprocal") != std::string_view::npos);

    // Test Isvalid on a bad Config: epsilon0 sanity check w.r.t. gammaReciprocal
    bad = config;
    bad.epsilon0 = config.gammaReciprocal * abla::B7 / (config.zeta_xB7 - abla::B7);
    // first ensure it's ok at the limit
    BOOST_CHECK(bad.IsValid(&err) && bad.IsValid());
    BOOST_CHECK_EQUAL(err, "");
    // next below the limit it should fail
    bad.epsilon0--;
    BOOST_CHECK(!bad.IsValid(&err) && !bad.IsValid());
    BOOST_CHECK(std::string_view{err}.find("epsilon0 sanity check") != std::string_view::npos);
}

BOOST_AUTO_TEST_CASE(config_setmax) {
    auto config = abla::Config::MakeDefault();

    BOOST_CHECK_NE(config.epsilonMax, 0u);
    BOOST_CHECK_NE(config.betaMax, 0u);

    config.epsilonMax = config.betaMax = 0u;
    BOOST_CHECK(!config.IsValid());

    // Test that SetMax works and does something (sets to some huge value)
    config.SetMax();
    BOOST_CHECK(config.IsValid());
    BOOST_CHECK_GT(config.epsilonMax, std::numeric_limits<uint32_t>::max());
    BOOST_CHECK_GT(config.betaMax, std::numeric_limits<uint32_t>::max());
}

BOOST_AUTO_TEST_CASE(config_tostring) {
    abla::Config config;
    config.epsilon0 = 1;
    config.beta0 = 2;
    config.gammaReciprocal = 3;
    config.zeta_xB7 = 4;
    config.thetaReciprocal = 5;
    config.delta = 6;
    config.epsilonMax = 7;
    config.betaMax = 8;

    BOOST_CHECK_EQUAL(config.ToString(),
                      "abla::Config(epsilon0=1, beta0=2, gammaReciprocal=3, zeta_xB7=4, thetaReciprocal=5, delta=6,"
                      " epsilonMax=7, betaMax=8)");
}

BOOST_AUTO_TEST_CASE(state_ctors) {
    const abla::Config config = abla::Config::MakeDefault();

    // Test that default-constructed state is invalid and is all zeores (only really suitable for serialization)
    const abla::State state0;
    BOOST_CHECK(!state0.IsValid(config));
    BOOST_CHECK_EQUAL(state0.GetBlockSize(), 0u);
    BOOST_CHECK_EQUAL(state0.GetControlBlockSize(), 0u);
    BOOST_CHECK_EQUAL(state0.GetElasticBufferSize(), 0u);
    BOOST_CHECK(state0.GetBlockInterval() == legacy::POW_TARGET_SPACING_TICKS);

    // Non-default constructed State however is valid and has some default values from Config
    const abla::State state1(config, 12345u);
    BOOST_CHECK(state1.IsValid(config));
    BOOST_CHECK_EQUAL(state1.GetBlockSize(), 12345u);
    BOOST_CHECK_EQUAL(state1.GetControlBlockSize(), config.epsilon0);
    BOOST_CHECK_EQUAL(state1.GetElasticBufferSize(), config.beta0);
    BOOST_CHECK(state1.GetBlockInterval() == legacy::POW_TARGET_SPACING_TICKS);

    // Convenience c'tor, intentionally make an invalid state
    const abla::State state2(1, 2, 3, BlockTicks{4});
    BOOST_CHECK(!state2.IsValid(config));
    BOOST_CHECK_EQUAL(state2.GetBlockSize(), 1);
    BOOST_CHECK_EQUAL(state2.GetControlBlockSize(), 2);
    BOOST_CHECK_EQUAL(state2.GetElasticBufferSize(), 3);
    BOOST_CHECK(state2.GetBlockInterval() == BlockTicks{4});
}

BOOST_AUTO_TEST_CASE(state_getblocksizelimit) {
    const abla::Config config = abla::Config::MakeDefault();
    abla::State state(config, 12345u);
    const BlockTicks ticks = legacy::POW_TARGET_SPACING_TICKS;

    // Basic test -- value should be what we expect
    BOOST_REQUIRE(state.IsValid(config));
    BOOST_CHECK_EQUAL(state.GetBlockSizeLimit(false), state.GetControlBlockSize() + state.GetElasticBufferSize());
    BOOST_CHECK_EQUAL(state.GetBlockSizeLimit(true), state.GetControlBlockSize() + state.GetElasticBufferSize());

    // Try the test with the control state advanced close to the 2GB limit and test the `disable2GBCap` flag
    const uint64_t hugeCBS = config.epsilonMax / 3u, hugeEBS = config.betaMax / 3u;
    BOOST_REQUIRE_GT(hugeCBS, MAX_CONSENSUS_BLOCK_SIZE);
    BOOST_REQUIRE_GT(hugeEBS, MAX_CONSENSUS_BLOCK_SIZE);
    state = abla::State::FromTuple({6789u, hugeCBS, hugeEBS, ticks});
    BOOST_REQUIRE(state.IsValid(config));
    BOOST_CHECK_EQUAL(state.GetControlBlockSize(), hugeCBS);
    BOOST_CHECK_EQUAL(state.GetElasticBufferSize(), hugeEBS);
    BOOST_CHECK_GT(state.GetControlBlockSize() + state.GetElasticBufferSize(), MAX_CONSENSUS_BLOCK_SIZE);
    BOOST_CHECK(state.GetBlockInterval() == ticks);
    // test effect of the `disable2GBCap` flag (defaults to false but we verify that is the case)
    BOOST_CHECK_EQUAL(state.GetBlockSizeLimit(), MAX_CONSENSUS_BLOCK_SIZE);
    BOOST_CHECK_EQUAL(state.GetBlockSizeLimit(false), MAX_CONSENSUS_BLOCK_SIZE);
    BOOST_CHECK_EQUAL(state.GetBlockSizeLimit(true), state.GetControlBlockSize() + state.GetElasticBufferSize());
}

BOOST_AUTO_TEST_CASE(state_algo_blocksize_exceeds_max_block_size) {
    // Try a corner case: the state's blockSize exceeds what the algo expects (it should clamp the size used internally)
    const abla::Config config = abla::Config::MakeDefault();
    const BlockTicks ticks = legacy::POW_TARGET_SPACING_TICKS;
    const uint64_t hugeCBS = config.epsilonMax / 3u, hugeEBS = config.betaMax / 3u;
    const auto state = abla::State::FromTuple({6789u, hugeCBS, hugeEBS, ticks});
    BOOST_REQUIRE(state.IsValid(config));
    for (const bool disable2GBCap : {false, true}) {
        // test at max
        const uint64_t delta = 1'000'000'000u;
        const uint64_t exceedsMax = state.GetBlockSizeLimit(disable2GBCap) + delta;
        const auto overSaturated = abla::State::FromTuple({exceedsMax, hugeCBS, hugeEBS, ticks});
        BOOST_REQUIRE(overSaturated.IsValid(config));
        if (disable2GBCap) {
            BOOST_CHECK_EQUAL(exceedsMax,
                              delta + overSaturated.GetControlBlockSize() + overSaturated.GetElasticBufferSize());
        } else {
            BOOST_CHECK_EQUAL(exceedsMax,
                              delta + MAX_CONSENSUS_BLOCK_SIZE);
        }
        const auto saturated = abla::State::FromTuple({exceedsMax - delta, hugeCBS, hugeEBS, ticks});
        BOOST_CHECK(overSaturated != saturated);
        const auto osNext = overSaturated.NextBlockState(config, ticks, 123u);
        const auto sNext = saturated.NextBlockState(config, ticks, 123u);
        if (disable2GBCap){
            BOOST_CHECK(osNext == sNext);
        } else {
            BOOST_CHECK(osNext != sNext);
        }
        BOOST_CHECK_EQUAL(overSaturated.GetNextBlockSizeLimit(config, ticks, disable2GBCap),
                          saturated.GetNextBlockSizeLimit(config, ticks, disable2GBCap));
        BOOST_CHECK_EQUAL(sNext.GetBlockSizeLimit(disable2GBCap),
                          saturated.GetNextBlockSizeLimit(config, ticks, disable2GBCap));
        BOOST_CHECK_EQUAL(osNext.GetBlockSizeLimit(disable2GBCap),
                          overSaturated.GetNextBlockSizeLimit(config, ticks, disable2GBCap));

        // Test that abuse of algo with "overLimit" blockSize values such as u64max leads to "clamping" of the
        // blockSize used internally for advancing the state, and no overflows occur, and it's the same as if just the
        // maxBlockSize were used.
        const auto u64max = std::numeric_limits<uint64_t>::max();
        for (const uint64_t overLimit : {(hugeCBS + hugeEBS) * 2, (hugeCBS + hugeEBS) * 3, u64max}) {
            auto mstate = abla::State::FromTuple({overLimit, hugeCBS, hugeEBS, ticks});
            BOOST_REQUIRE(mstate.IsValid(config));
            const auto N = 2048;
            const auto lookaheadLimit = mstate.CalcLookaheadBlockSizeLimitWorstCase(config, N, abla::BASE_BLOCK_INTERVAL, disable2GBCap);
            const auto iteratedLimit = IteratedLookahead(config, mstate, N, abla::BASE_BLOCK_INTERVAL, disable2GBCap);
            for (size_t i = 0; i < N; ++i) {
                // this forces overLimit to the algo for all in-between states
                mstate = mstate.NextBlockState(config, ticks, overLimit);
            }
            // end result should still be the same as if the max block size per state was used, not overLimit
            BOOST_CHECK_EQUAL(iteratedLimit, mstate.GetBlockSizeLimit(disable2GBCap));
            // the closed-form lookahead is an upper bound on that
            BOOST_CHECK_GE(lookaheadLimit, iteratedLimit);
        }
    }
}

BOOST_AUTO_TEST_CASE(state_isvalid) {
    const abla::Config config = abla::Config::MakeDefault();
    const char *err = nullptr;
    const BlockTicks ticks = legacy::POW_TARGET_SPACING_TICKS;

    abla::State state(config, 80);

    BOOST_CHECK(state.IsValid(config, &err));
    BOOST_CHECK_EQUAL(err, "");

    // Invalid due to controlBlockSize < config.epsilon0
    const auto badCBS = config.epsilon0 - 1;
    state = abla::State::FromTuple({80, badCBS, config.beta0, ticks});
    BOOST_CHECK_EQUAL(state.GetControlBlockSize(), badCBS);
    BOOST_CHECK(!state.IsValid(config, &err));
    BOOST_CHECK(std::string_view{err}.find("invalid controlBlockSize state") != std::string_view::npos);
    err = "";

    // Invalid due to controlBlockSize > config.epsilonMax
    const auto badCBS2 = config.epsilonMax + 1;
    state = abla::State::FromTuple({80, badCBS2, config.beta0, ticks});
    BOOST_CHECK_EQUAL(state.GetControlBlockSize(), badCBS2);
    BOOST_CHECK(!state.IsValid(config, &err));
    BOOST_CHECK(std::string_view{err}.find("invalid controlBlockSize state") != std::string_view::npos);
    err = "";

    // Invalid due to elasticBufferSize < config.beta0
    const auto badEBS = config.beta0 - 1;
    state = abla::State::FromTuple({80, config.epsilon0, badEBS, ticks});
    BOOST_CHECK_EQUAL(state.GetElasticBufferSize(), badEBS);
    BOOST_CHECK(!state.IsValid(config, &err));
    BOOST_CHECK(std::string_view{err}.find("invalid elasticBufferSize state") != std::string_view::npos);
    err = "";

    // Invalid due to elasticBufferSize > config.betaMax
    const auto badEBS2 = config.betaMax + 1;
    state = abla::State::FromTuple({80, config.epsilon0, badEBS2, ticks});
    BOOST_CHECK_EQUAL(state.GetElasticBufferSize(), badEBS2);
    BOOST_CHECK(!state.IsValid(config, &err));
    BOOST_CHECK(std::string_view{err}.find("invalid elasticBufferSize state") != std::string_view::npos);
    err = "";

    // Invalid due to out-of-range ticks
    const BlockTicks one = BlockTicks{1};
    for (const BlockTicks &badTicks : {BlockTicks{0}, abla::MIN_BLOCK_INTERVAL - one, abla::MAX_BLOCK_INTERVAL + one}) {
        state = abla::State::FromTuple({80, config.epsilon0, config.beta0, badTicks});
        BOOST_CHECK(state.GetBlockInterval() == badTicks);
        BOOST_CHECK(!state.IsValid(config, &err));
        BOOST_CHECK(std::string_view{err}.find("invalid blockInterval state") != std::string_view::npos);
        err = "";
    }
}

BOOST_AUTO_TEST_CASE(state_tostring) {
    const abla::Config config = abla::Config::MakeDefault();
    const abla::State state(config, 80u);

    BOOST_CHECK_EQUAL(state.ToString(),
                      "abla::State(blockSize=80, controlBlockSize=16000000, elasticBufferSize=16000000, blockInterval=600)");
}

BOOST_AUTO_TEST_CASE(state_tuple) {
    const auto state = abla::State::FromTuple({1, 2, 3, BlockTicks{4}});
    BOOST_CHECK_EQUAL(state.GetBlockSize(), 1);
    BOOST_CHECK_EQUAL(state.GetControlBlockSize(), 2);
    BOOST_CHECK_EQUAL(state.GetElasticBufferSize(), 3);
    BOOST_CHECK(state.GetBlockInterval() == BlockTicks{4});

    const auto tup = state.ToTuple();
    BOOST_CHECK(tup == std::tuple(1, 2, 3, BlockTicks{4}));
}

BOOST_AUTO_TEST_CASE(state_ser) {
    FastRandomContext rng;
    const abla::Config config = abla::Config::MakeDefault();

    for (size_t i = 0; i < 10; ++i) {
        const uint64_t randomSize = rng.rand64(),
                       randomCBS = rng.randrange(config.epsilonMax),
                       randomEBS = rng.randrange(config.betaMax);
        const BlockTicks ticks = abla::MIN_BLOCK_INTERVAL
                                 + BlockTicks{rng.randrange((abla::MAX_BLOCK_INTERVAL - abla::MIN_BLOCK_INTERVAL).value() + 1u)};
        const abla::State state = abla::State::FromTuple({randomSize, randomCBS, randomEBS, ticks});

        BOOST_REQUIRE(state.IsValid(config));
        BOOST_REQUIRE_EQUAL(state.GetBlockSize(), randomSize);
        BOOST_REQUIRE_EQUAL(state.GetControlBlockSize(), randomCBS);
        BOOST_REQUIRE_EQUAL(state.GetElasticBufferSize(), randomEBS);

        // test ser/deser cycle -- note that we only ser/unser StateBase, without the ticks (which live in CBlockIndex)
        std::vector<uint8_t> v1;
        CVectorWriter(SER_DISK, INIT_PROTO_VERSION, v1, 0) << state;
        BOOST_CHECK(!v1.empty());
        abla::State state2; // default constructed should be all 0's
        BOOST_CHECK_EQUAL(state2.GetBlockSize(), 0);
        BOOST_CHECK_EQUAL(state2.GetControlBlockSize(), 0);
        BOOST_CHECK_EQUAL(state2.GetElasticBufferSize(), 0);
        // after unser, state2 will equal state only for the serialized values
        VectorReader(SER_DISK, INIT_PROTO_VERSION, v1, 0) >> state2;
        BOOST_CHECK_EQUAL(state2.GetBlockSize(), randomSize);
        BOOST_CHECK_EQUAL(state2.GetControlBlockSize(), randomCBS);
        BOOST_CHECK_EQUAL(state2.GetElasticBufferSize(), randomEBS);
        // but for the block interval it will get the default value
        BOOST_CHECK(state2.GetBlockInterval() == abla::BASE_BLOCK_INTERVAL);
    }
}

// Test that the "fixed size" configuration for the ABLA EBAA behaves as expected.
BOOST_AUTO_TEST_CASE(feature_fixedsize) {
    const auto defBlkSz = DEFAULT_CONSENSUS_BLOCK_SIZE;
    const abla::Config confNormal = abla::Config::MakeDefault(defBlkSz, /* fixedSize = */ false);
    const abla::Config confFixed  = abla::Config::MakeDefault(defBlkSz, /* fixedSize = */ true);
    BOOST_REQUIRE(confNormal.IsValid());
    BOOST_REQUIRE(confFixed.IsValid());
    BOOST_CHECK( ! confNormal.IsFixedSize());
    BOOST_CHECK(confFixed.IsFixedSize());

    const abla::State stateNormal(confNormal, 0), stateFixed(confFixed, 0);
    BOOST_REQUIRE(stateNormal.IsValid(confNormal));
    BOOST_REQUIRE(stateFixed.IsValid(confFixed));

    // Both fixed and dynamic configs start off with defBlkSz as the limit
    BOOST_CHECK_EQUAL(stateNormal.GetBlockSizeLimit(), defBlkSz);
    BOOST_CHECK_EQUAL(stateFixed.GetBlockSizeLimit(), defBlkSz);

    // However, the normal (dynamic) config will grow if blocks are full
    BOOST_CHECK_GT(stateNormal.CalcLookaheadBlockSizeLimitWorstCase(confNormal, 2048), defBlkSz);
    // But the fixed config never grows and stays at default
    BOOST_CHECK_EQUAL(stateFixed.CalcLookaheadBlockSizeLimitWorstCase(confFixed, 2048), defBlkSz);

    BOOST_CHECK_GT(stateNormal.CalcLookaheadBlockSizeLimitWorstCase(confNormal, 2048),
                   stateFixed.CalcLookaheadBlockSizeLimitWorstCase(confFixed, 2048));
}


// CalcLookaheadBlockSizeLimitWorstCase() is a closed-form upper bound (CHIP-2025-03); compare it against the original
// iterated loop over the chain configurations, a range of states, intervals and counts.
BOOST_AUTO_TEST_CASE(lookahead_closed_form_vs_iterated) {
    struct NamedConfig { const char *name; abla::Config config; };
    const NamedConfig configs[] = {
        {"32MB dynamic (mainnet, regtest)", abla::Config::MakeDefault(32'000'000, false)},
        {"2MB dynamic (chipnet)",           abla::Config::MakeDefault(2'000'000, false)},
        {"256MB dynamic (scalenet)",        abla::Config::MakeDefault(256'000'000, false)},
        {"32MB fixed (testnet3)",           abla::Config::MakeDefault(32'000'000, true)},
        {"2MB fixed (testnet4)",            abla::Config::MakeDefault(2'000'000, true)},
    };
    const BlockTicks intervals[] = {BlockTicks{600}, BlockTicks{60}, BlockTicks{1}};
    const size_t counts[] = {0, 1, 2, 3, 12, 144, 1024, 2048};

    for (const auto &[name, config] : configs) {
        BOOST_TEST_MESSAGE(strprintf("Config: %s", name));
        BOOST_REQUIRE(config.IsValid());

        // A variety of starting states
        std::vector<abla::State> starts;
        for (const BlockTicks &own : {BlockTicks{600}, BlockTicks{60}}) {
            const abla::State floor(0, config.epsilon0, config.beta0, own);
            starts.push_back(floor);                                                      // empty tip block
            starts.push_back(abla::State::FromTuple({floor.GetBlockSizeLimit(true), config.epsilon0, config.beta0, own})); // full tip block
            starts.push_back(abla::State::FromTuple({std::numeric_limits<uint64_t>::max(), config.epsilon0, config.beta0, own})); // over-limit size
        }
        {
            // grown state: 5000 full blocks at 600, then a mixed tail at 60 with assorted sizes
            abla::State s(config, 0);
            for (int i = 0; i < 5000; ++i) s = s.NextBlockState(config, BlockTicks{600}, s.GetNextBlockSizeLimit(config, BlockTicks{600}, true));
            starts.push_back(s);
            FastRandomContext rng(uint256S("ab1a"));
            for (int i = 0; i < 500; ++i) {
                const uint64_t lim = s.GetNextBlockSizeLimit(config, BlockTicks{60}, true);
                s = s.NextBlockState(config, BlockTicks{60}, rng.randrange(lim + 1));
            }
            starts.push_back(s);
        }
        if (!config.IsFixedSize()) {
            // huge and fully saturated states (exercise the epsilonMax / betaMax clips and the 2GB cap)
            starts.push_back(abla::State::FromTuple({6789u, config.epsilonMax / 3u, config.betaMax / 3u, BlockTicks{600}}));
            starts.push_back(abla::State::FromTuple({config.epsilonMax + config.betaMax, config.epsilonMax, config.betaMax, BlockTicks{600}}));
            starts.push_back(abla::State::FromTuple({0, config.epsilonMax - 1000u, config.betaMax - 1000u, BlockTicks{60}}));
        }

        for (const auto &start : starts) {
            BOOST_REQUIRE(start.IsValid(config));
            for (const BlockTicks &interval : intervals) {
                for (const size_t count : counts) {
                    for (const bool disable2GBCap : {false, true}) {
                        CheckLookaheadVsIterated(config, start, count, interval, disable2GBCap,
                                                 count <= LOOKAHEAD_SLACK_MAX_COUNT);
                    }
                }
            }
        }

        // The call site in validation.cpp: 2048 blocks at the default (600) interval, default 2GB cap.
        // From the initial state this must still be at least the initial limit, and exact for fixed-size configs.
        const abla::State initial(config, 0);
        const uint64_t lookahead = CheckLookaheadVsIterated(config, initial, 2048, abla::BASE_BLOCK_INTERVAL, false, true);
        BOOST_TEST_MESSAGE(strprintf("  2048-block lookahead from initial state: closed=%u iterated=%u", lookahead,
                                     IteratedLookahead(config, initial, 2048, abla::BASE_BLOCK_INTERVAL, false)));
        if (config.IsFixedSize()) {
            BOOST_CHECK_EQUAL(lookahead, initial.GetBlockSizeLimit());
        } else {
            BOOST_CHECK_GT(lookahead, initial.GetBlockSizeLimit());
        }
    }
}

// Counts at and past the closed form's radius (count - 1 >= D/G, D = 600 * B7 * gammaReciprocal,
// G = interval * (zeta_xB7 - B7)), where it falls back to epsilonMax + betaMax. Only "never below" is
// checked here; the slack is not meant to be small this far out.
BOOST_AUTO_TEST_CASE(lookahead_closed_form_radius) {
    // mainnet-like config: D/G == 75876 at interval 600
    {
        const auto config = abla::Config::MakeDefault(32'000'000, false);
        const uint64_t DoverG = 600 * abla::B7 * config.gammaReciprocal / (600 * (config.zeta_xB7 - abla::B7));
        BOOST_REQUIRE_EQUAL(DoverG, 75876u);
        const abla::State start(config, 0);
        for (const size_t count : {size_t(DoverG - 1), size_t(DoverG), size_t(DoverG + 1), size_t(DoverG + 2)}) {
            for (const bool disable2GBCap : {false, true}) {
                const uint64_t closed = CheckLookaheadVsIterated(config, start, count, abla::BASE_BLOCK_INTERVAL,
                                                                 disable2GBCap, false);
                if (count - 1 >= DoverG) {
                    // fallback: the trivial maximum (capped at 2GB if the cap is in effect)
                    const uint64_t sat = config.epsilonMax + config.betaMax;
                    BOOST_CHECK_EQUAL(closed, disable2GBCap ? sat : MAX_CONSENSUS_BLOCK_SIZE);
                }
            }
        }
    }
    // the most aggressive allowed config (min gammaReciprocal, max zeta): D/G == 9484 at interval 600
    {
        abla::Config config = abla::Config::MakeDefault(32'000'000, false);
        config.gammaReciprocal = abla::MIN_GAMMA_RECIPROCAL;
        config.zeta_xB7 = abla::MAX_ZETA_XB7;
        config.SetMax();
        BOOST_REQUIRE(config.IsValid());
        const abla::State start(config, 0);
        for (const size_t count : {size_t(2), size_t(1000), size_t(9483), size_t(9484), size_t(9485), size_t(9486), size_t(20000)}) {
            for (const BlockTicks &interval : {BlockTicks{600}, BlockTicks{60}}) {
                for (const bool disable2GBCap : {false, true}) {
                    CheckLookaheadVsIterated(config, start, count, interval, disable2GBCap, false);
                }
            }
        }
    }
}

// Random valid configs, states, intervals and counts: the closed form must never be below the iterated value.
BOOST_AUTO_TEST_CASE(lookahead_closed_form_random) {
    FastRandomContext rng(uint256S("ab1a1004"));
    size_t nChecked = 0;
    for (int ci = 0; ci < 200; ++ci) {
        abla::Config config;
        config.zeta_xB7 = abla::MIN_ZETA_XB7 + rng.randrange(abla::MAX_ZETA_XB7 - abla::MIN_ZETA_XB7 + 1);
        config.gammaReciprocal = abla::MIN_GAMMA_RECIPROCAL
                                 + rng.randrange(abla::MAX_GAMMA_RECIPROCAL - abla::MIN_GAMMA_RECIPROCAL + 1);
        config.thetaReciprocal = abla::MIN_THETA_RECIPROCAL
                                 + rng.randrange(abla::MAX_THETA_RECIPROCAL - abla::MIN_THETA_RECIPROCAL + 1);
        config.delta = 1 + rng.randrange(abla::MAX_DELTA);
        const uint64_t minEps0 = (config.gammaReciprocal * abla::B7 + (config.zeta_xB7 - abla::B7) - 1)
                                 / (config.zeta_xB7 - abla::B7);
        config.epsilon0 = minEps0 + rng.randrange(ci % 2 ? 1'000 : 100'000'000);
        config.beta0 = 1 + rng.randrange(ci % 2 ? 1'000 : 100'000'000);
        config.SetMax();
        if (!config.IsValid()) continue;
        for (int si = 0; si < 25; ++si) {
            const BlockTicks own{1 + rng.randrange(600)};
            const uint64_t eps = config.epsilon0 + rng.randrange(si % 2 ? 1'000'000 : 1'000'000'000'000ull);
            const uint64_t beta = config.beta0 + rng.randrange(si % 3 ? 1'000 : 1'000'000'000'000ull);
            const auto start = abla::State::FromTuple({rng.rand64() % (3 * (eps + beta)), eps, beta, own});
            if (!start.IsValid(config)) continue;
            const BlockTicks interval{1 + rng.randrange(si % 2 ? 8 : 600)};
            const size_t count = si % 10 == 0 ? rng.randrange(3000) : rng.randrange(6);
            for (const bool disable2GBCap : {false, true}) {
                CheckLookaheadVsIterated(config, start, count, interval, disable2GBCap, false);
                ++nChecked;
            }
        }
    }
    BOOST_TEST_MESSAGE(strprintf("Checked %u random lookaheads", nChecked));
    BOOST_CHECK_GT(nChecked, 1000u);
}

// Lookahead vectors from CHIP-2025-03 (tests/abla/abla_ticks.json, "lookaheads"; mainnet config, 2GB cap disabled).
// "expected" is the closed form's value, "legacyExact" the iterated value. Rows of kind "download" (a tick window
// counted in base-interval blocks, ceil(window / 600)) map onto count = ceil(window / 600) at interval 600; rows of
// kind "constant" use the state's own interval.
BOOST_AUTO_TEST_CASE(lookahead_closed_form_chip_vectors) {
    const auto config = abla::Config::MakeDefault(32'000'000, false);
    BOOST_REQUIRE_EQUAL(config.epsilonMax, 2837960626724546304ull);
    BOOST_REQUIRE_EQUAL(config.betaMax, 9459868755748488064ull);
    struct Row {
        const char *name;
        uint64_t blockSize, eps, beta, ownInterval;
        size_t count;
        uint64_t interval, expected, legacyExact;
    };
    const Row rows[] = {
        {"lookahead_const_1", 32000000ull, 16000000ull, 16000000ull, 600, 1, 600, 32001889ull, 32001889ull},
        {"lookahead_const_2", 32000000ull, 16000000ull, 16000000ull, 600, 2, 600, 32004208ull, 32003778ull},
        {"lookahead_const_10_60", 3200000ull, 16000000ull, 16000000ull, 60, 10, 60, 3200227ull, 3200189ull},
        {"lookahead_download_600_full600", 32000000ull, 16000000ull, 16000000ull, 600, 1, 600, 32001889ull, 32001889ull},
        {"lookahead_download_1200_full600", 32000000ull, 16000000ull, 16000000ull, 600, 2, 600, 32004208ull, 32003778ull},
        {"lookahead_download_7200_full600", 32000000ull, 16000000ull, 16000000ull, 600, 12, 600, 32027408ull, 32022662ull},
        {"lookahead_download_600_floor600", 0ull, 16000000ull, 16000000ull, 600, 1, 600, 32000000ull, 32000000ull},
        {"lookahead_download_600_full60", 3200000ull, 16000000ull, 16000000ull, 60, 1, 600, 32000189ull, 32000189ull},
        {"lookahead_download_1200_full60", 3200000ull, 16000000ull, 16000000ull, 60, 2, 600, 32002508ull, 32002078ull},
        {"lookahead_download_max_window_saturated", 12297829382473034368ull, 2837960626724546304ull, 9459868755748488064ull, 600, 144, 600, 12297829382473034368ull, 12297829382473034368ull},
        {"lookahead_download_guard_saturated", 12297829382473034368ull, 2837960626724546304ull, 9459868755748488064ull, 600, 75877, 600, 12297829382473034368ull, 12297829382473034368ull},
    };
    for (const Row &r : rows) {
        BOOST_TEST_MESSAGE(strprintf("CHIP vector: %s", r.name));
        const auto start = abla::State::FromTuple({r.blockSize, r.eps, r.beta, BlockTicks{r.ownInterval}});
        BOOST_REQUIRE(start.IsValid(config));
        BOOST_CHECK_EQUAL(start.CalcLookaheadBlockSizeLimitWorstCase(config, r.count, BlockTicks{r.interval}, true), r.expected);
        BOOST_CHECK_EQUAL(IteratedLookahead(config, start, r.count, BlockTicks{r.interval}, true), r.legacyExact);
    }
}

/**
 * Note: Rest of coverage for ABLA::State (such as state advancement, lookahead, etc) is in abla_test_vectors.cpp.
 **/

BOOST_AUTO_TEST_SUITE_END()
