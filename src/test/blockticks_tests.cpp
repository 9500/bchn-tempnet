// Copyright (c) 2026-present The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/blockticks.h>
#include <node/blockheighttotickmapper.h>

#include <clientversion.h>
#include <random.h>
#include <streams.h>
#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <limits>
#include <string_view>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(blockticks_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(blockticks_class) {
    // Default c'tor -> 0
    BlockTicks bt;
    BOOST_CHECK_EQUAL(bt.value(), 0);
    BOOST_CHECK_EQUAL(bt.toSecs(), 0);
    // C'tor from int
    BlockTicks bt1{1};
    BOOST_CHECK_EQUAL(bt1.value(), 1);
    BOOST_CHECK_EQUAL(bt1.toSecs(), 1);
    // Comparison ops
    BOOST_CHECK(bt1 > bt);
    BOOST_CHECK(bt1 >= bt);
    BOOST_CHECK(bt < bt1);
    BOOST_CHECK(bt <= bt1);
    BOOST_CHECK(bt1 != bt);
    BlockTicks bt1_2{1};
    BOOST_CHECK(bt1 == bt1_2);
    BOOST_CHECK(bt1 >= bt1_2);
    BOOST_CHECK(bt1 <= bt1_2);
    // Assignment
    bt = BlockTicks{7};
    BOOST_CHECK_EQUAL(bt.value(), 7);
    BOOST_CHECK_EQUAL(bt.toSecs<double>(), 7.0);
    // Arithmetic
    BOOST_CHECK_EQUAL((BlockTicks{2} * 4).value(), 8);
    BOOST_CHECK_EQUAL((BlockTicks{2} + BlockTicks{4}).toSecs(), 6);
    BOOST_CHECK_EQUAL((BlockTicks{102} - BlockTicks{2}).value(), 100);
    BOOST_CHECK_EQUAL(BlockTicks{1020} / BlockTicks{2}, 510);
    // Arith assign
    bt = BlockTicks{10};
    bt -= BlockTicks{2};
    BOOST_CHECK_EQUAL(bt.value(), 8);
    bt += BlockTicks{2};
    BOOST_CHECK_EQUAL(bt.value(), 10);
    bt *= 2;
    BOOST_CHECK_EQUAL(bt.value(), 20);
}

// Test basic functionality of class BlockHeightToTickMapper
BOOST_AUTO_TEST_CASE(mapper_tests_basic) {
    using HeightToIntervalPair = BlockHeightToTickMapper::HeightToIntervalPair;

    const auto ChkMsg0Interval = [](const auto &e) {
        return std::string_view{e.what()}.find("schedule may not contain intervals of size 0")
               != std::string_view::npos;
    };
    const auto ChkMsgBadHeight = [](const auto &e) {
        return std::string_view{e.what()}.find("height must be larger than all other heights")
        != std::string_view::npos;
    };
    // Ensure that the class c'tor disallows tick intervals that are 0
    BOOST_CHECK_EXCEPTION(BlockHeightToTickMapper({{HeightToIntervalPair{1, BlockTicks{0}},}}),
                          BlockHeightToTickMapper::BadArgs, ChkMsg0Interval);
    // default constructed value contains just 1 entry: 0, 600 (legacy default)
    BlockHeightToTickMapper def;
    BOOST_CHECK_EQUAL(def.Size(), 1);
    BOOST_CHECK(def.FirstEntry() == HeightToIntervalPair(0, BlockTicks{600}));
    BOOST_CHECK(def.FirstEntry() == def.LastEntry());
    BOOST_CHECK_THROW(def.PopLast(), BlockHeightToTickMapper::CannotPopLastElement); // this should fail for mappings of size == 1
    def.PushLast(HeightToIntervalPair(10, BlockTicks{60})); // this should work
    // pushing the same height again should fail
    BOOST_CHECK_EXCEPTION(def.PushLast(HeightToIntervalPair(10, BlockTicks{10})), BlockHeightToTickMapper::BadArgs,
                          ChkMsgBadHeight);
    // pushing an in-between entry should fail
    BOOST_CHECK_EXCEPTION(def.PushLast(HeightToIntervalPair(9, BlockTicks{10})), BlockHeightToTickMapper::BadArgs,
                          ChkMsgBadHeight);
    // pushing an entry with tick interval 0 should fail
    BOOST_CHECK_EXCEPTION(def.PushLast(HeightToIntervalPair(100, BlockTicks{0})), BlockHeightToTickMapper::BadArgs,
                          ChkMsg0Interval);
    // Now, test the default schedule after PushLast() to ensure it works as expected
    BOOST_CHECK_EQUAL(def.Size(), 2);
    BOOST_CHECK(def.FirstEntry() == HeightToIntervalPair(0, BlockTicks{600}));
    BOOST_CHECK(def.LastEntry() == HeightToIntervalPair(10, BlockTicks{60}));

    // Check HeightToTick and TickToHeight for the 2-entry table
    BOOST_CHECK(def.HeightToTick(0) == BlockTicks{0});
    BOOST_CHECK(def.HeightToTick(1) == BlockTicks{600});
    BOOST_CHECK(def.HeightToTick(9) == BlockTicks{9 * 600});
    BOOST_CHECK(def.HeightToTick(10) == BlockTicks{10 * 600});
    BOOST_CHECK(def.HeightToTick(11) == BlockTicks{10 * 600 + 60});
    BOOST_CHECK(def.HeightToTick(9'123'456) == BlockTicks{10 * 600 + 9'123'446LL * 60});
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{0}), 0);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{1}), 0);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{599}), 0);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{600}), 1);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{601}), 1);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{9*600 - 1}), 8);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{9*600}), 9);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{10*600 + 50*60 - 1}), 59);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{10*600 + 50*60}), 60);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{10*600 + 50*60 + 1}), 60);
    // Check TicksElapsed works as expected for the 2-entry table
    BOOST_CHECK(def.TicksElapsed(0, 0) == BlockTicks{0});
    BOOST_CHECK(def.TicksElapsed(0, 1) == BlockTicks{600});
    BOOST_CHECK(def.TicksElapsed(0, 2) == BlockTicks{1200});
    BOOST_CHECK(def.TicksElapsed(0, 9) == BlockTicks{600 * 9});
    BOOST_CHECK(def.TicksElapsed(1, 9) == BlockTicks{600 * 8});
    BOOST_CHECK(def.TicksElapsed(9, 99) == BlockTicks(600 * 1 + 60 * 89));
    BOOST_CHECK(def.TicksElapsed(99, 100) == BlockTicks(60));
    // Check GetBlockTickIntervalForHeight for the 2-entry table
    BOOST_CHECK(def.GetBlockTickIntervalForHeight(0) == BlockTicks{600});
    BOOST_CHECK(def.GetBlockTickIntervalForHeight(1) == BlockTicks{600});
    BOOST_CHECK(def.GetBlockTickIntervalForHeight(9) == BlockTicks{600});
    BOOST_CHECK(def.GetBlockTickIntervalForHeight(10) == BlockTicks{60});
    BOOST_CHECK(def.GetBlockTickIntervalForHeight(99) == BlockTicks{60});
    BOOST_CHECK(def.GetBlockTickIntervalForHeight(99'999'123) == BlockTicks{60});

    // And pop the newly added element from it, it should end up identical to the default-constructed value
    BOOST_CHECK(def.PopLast() == HeightToIntervalPair(10, BlockTicks{60}));
    BOOST_CHECK_EQUAL(def.Size(), 1);
    BOOST_CHECK(def.FirstEntry() == HeightToIntervalPair(0, BlockTicks{600}));
    BOOST_CHECK(def.FirstEntry() == def.LastEntry());
    BOOST_CHECK(def == BlockHeightToTickMapper{}); // check identity with default constructed for paranaoia

    // Check that doing TickToHeight and HeightToTick works if we are using the default-constructed value
    BOOST_CHECK(def.HeightToTick(0) == BlockTicks{0});
    BOOST_CHECK(def.HeightToTick(1) == BlockTicks{600});
    BOOST_CHECK(def.HeightToTick(9) == BlockTicks{9 * 600});
    BOOST_CHECK(def.HeightToTick(9'123'456) == BlockTicks{9'123'456LL * 600});
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{0}), 0);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{1}), 0);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{599}), 0);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{600}), 1);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{601}), 1);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{9*600 - 1}), 8);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{9*600}), 9);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{9*600 + 1}), 9);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{9'123'456LL*600 - 1}), 9'123'455);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{9'123'456LL*600}), 9'123'456);
    BOOST_CHECK_EQUAL(def.TickToHeight(BlockTicks{9'123'456LL*600 + 1}), 9'123'456);

    // Check TicksElapsed works as expected
    BOOST_CHECK(def.TicksElapsed(0, 0) == BlockTicks{0});
    BOOST_CHECK(def.TicksElapsed(0, 1) == BlockTicks{600});
    BOOST_CHECK(def.TicksElapsed(0, 2) == BlockTicks{1200});
    BOOST_CHECK(def.TicksElapsed(0, 9) == BlockTicks{600 * 9});
    BOOST_CHECK(def.TicksElapsed(1, 9) == BlockTicks{600 * 8});
    BOOST_CHECK(def.TicksElapsed(9, 1) == BlockTicks{600 * 8}); // Revers from/to (should still return the same interval)
    BOOST_CHECK(def.TicksElapsed(9, 99) == BlockTicks(600 * 90));
    BOOST_CHECK(def.TicksElapsed(9, 100) == BlockTicks(600 * 91));
    BOOST_CHECK(def.TicksElapsed(9, 101) == BlockTicks(600*92));
    BOOST_CHECK(def.TicksElapsed(0, 101) == BlockTicks(600*101));
    BOOST_CHECK(def.TicksElapsed(10, 1'020'001) == BlockTicks(600*1'019'991LL));

    // Check GetBlockTickIntervalForHeight
    BOOST_CHECK(def.GetBlockTickIntervalForHeight(0) == BlockTicks{600});
    BOOST_CHECK(def.GetBlockTickIntervalForHeight(1) == BlockTicks{600});
    BOOST_CHECK(def.GetBlockTickIntervalForHeight(99) == BlockTicks{600});
    BOOST_CHECK(def.GetBlockTickIntervalForHeight(99'999'123) == BlockTicks{600});
}

// Test functionality of class BlockHeightToTickMapper when using a larger, multi-entry schedule
BOOST_AUTO_TEST_CASE(mapper_tests_larger_schedule) {
    using HeightToIntervalPair = BlockHeightToTickMapper::HeightToIntervalPair;

    const std::vector<HeightToIntervalPair> sched_unsorted{{
        // Gets auto-generated: {0, BlockTicks{600}},
        {1000, BlockTicks{150}},
        {20'000, BlockTicks{60}},
        {100, BlockTicks{300}},
        {10'000, BlockTicks{90}},
    }};

    auto sched_sorted = sched_unsorted;
    std::sort(sched_sorted.begin(), sched_sorted.end());

    BlockHeightToTickMapper m(sched_unsorted);

    BOOST_CHECK_EQUAL(m.Size(), 5);
    // first entry for height 0 was auto-genned for us
    BOOST_CHECK(m.FirstEntry() == HeightToIntervalPair(0, BlockTicks{600}));
    BOOST_CHECK(m.LastEntry() == sched_sorted.back());

    // Check HeightToTick works as expected
    BOOST_CHECK(m.HeightToTick(0) == BlockTicks{0});
    BOOST_CHECK(m.HeightToTick(1) == BlockTicks{600});
    BOOST_CHECK(m.HeightToTick(50) == BlockTicks{30'000});
    BOOST_CHECK(m.HeightToTick(100) == BlockTicks{60'000});
    BOOST_CHECK(m.HeightToTick(101) == BlockTicks{60'300});
    BOOST_CHECK(m.HeightToTick(102) == BlockTicks{60'600});
    BOOST_CHECK(m.HeightToTick(150) == BlockTicks{75'000});
    BOOST_CHECK(m.HeightToTick(200) == BlockTicks{90'000});
    BOOST_CHECK(m.HeightToTick(250) == BlockTicks{105'000});
    BOOST_CHECK(m.HeightToTick(750) == BlockTicks{255'000});
    BOOST_CHECK(m.HeightToTick(999) == BlockTicks{329'700});
    BOOST_CHECK(m.HeightToTick(1000) == BlockTicks{330'000});
    BOOST_CHECK(m.HeightToTick(1001) == BlockTicks{330'000 + 150});
    BOOST_CHECK(m.HeightToTick(1002) == BlockTicks{330'000 + 150*2});
    BOOST_CHECK(m.HeightToTick(1003) == BlockTicks{330'000 + 150*3});
    BOOST_CHECK(m.HeightToTick(9999) == BlockTicks{330'000 + 150*8999});
    BOOST_CHECK(m.HeightToTick(10'000) == BlockTicks{330'000 + 150*9000});
    BOOST_CHECK(m.HeightToTick(10'001) == BlockTicks{330'000 + 150*9000 + 90});
    BOOST_CHECK(m.HeightToTick(10'002) == BlockTicks{330'000 + 150*9000 + 90*2});
    BOOST_CHECK(m.HeightToTick(10'003) == BlockTicks{330'000 + 150*9000 + 90*3});
    BOOST_CHECK(m.HeightToTick(10'103) == BlockTicks{330'000 + 150*9000 + 90*103});
    BOOST_CHECK(m.HeightToTick(15'000) == BlockTicks{330'000 + 150*9000 + 90*5'000});
    BOOST_CHECK(m.HeightToTick(19'000) == BlockTicks{330'000 + 150*9000 + 90*9'000});
    BOOST_CHECK(m.HeightToTick(19'998) == BlockTicks{330'000 + 150*9000 + 90*9'998});
    BOOST_CHECK(m.HeightToTick(19'999) == BlockTicks{330'000 + 150*9000 + 90*9'999});
    BOOST_CHECK(m.HeightToTick(20'000) == BlockTicks{330'000 + 150*9000 + 90*10'000});
    BOOST_CHECK(m.HeightToTick(20'001) == BlockTicks{330'000 + 150*9000 + 90*10'000 + 60});
    BOOST_CHECK(m.HeightToTick(20'002) == BlockTicks{330'000 + 150*9000 + 90*10'000 + 60*2});
    BOOST_CHECK(m.HeightToTick(99'999) == BlockTicks{330'000 + 150*9000 + 90*10'000 + 60*79'999});
    BOOST_CHECK(m.HeightToTick(1'099'999) == BlockTicks{330'000 + 150*9000 + 90*10'000 + 60*1'079'999});

    // Check TickToHeight works as expected
    BOOST_CHECK(m.TickToHeight(BlockTicks{0}) == 0);
    BOOST_CHECK(m.TickToHeight(BlockTicks{1}) == 0);
    BOOST_CHECK(m.TickToHeight(BlockTicks{599}) == 0);
    BOOST_CHECK(m.TickToHeight(BlockTicks{600}) == 1);
    BOOST_CHECK(m.TickToHeight(BlockTicks{601}) == 1);
    BOOST_CHECK(m.TickToHeight(BlockTicks{1199}) == 1);
    BOOST_CHECK(m.TickToHeight(BlockTicks{1200}) == 2);
    BOOST_CHECK(m.TickToHeight(BlockTicks{11999}) == 19);
    BOOST_CHECK(m.TickToHeight(BlockTicks{12000}) == 20);
    BOOST_CHECK(m.TickToHeight(BlockTicks{12001}) == 20);
    BOOST_CHECK(m.TickToHeight(BlockTicks{59'399}) == 98);
    BOOST_CHECK(m.TickToHeight(BlockTicks{59'400}) == 99);
    BOOST_CHECK(m.TickToHeight(BlockTicks{59'999}) == 99);
    BOOST_CHECK(m.TickToHeight(BlockTicks{60'000}) == 100);
    BOOST_CHECK(m.TickToHeight(BlockTicks{60'299}) == 100);
    BOOST_CHECK(m.TickToHeight(BlockTicks{60'300}) == 101);
    BOOST_CHECK(m.TickToHeight(BlockTicks{60'301}) == 101);
    BOOST_CHECK(m.TickToHeight(BlockTicks{60'599}) == 101);
    BOOST_CHECK(m.TickToHeight(BlockTicks{60'600}) == 102);
    BOOST_CHECK(m.TickToHeight(BlockTicks{74'999}) == 149);
    BOOST_CHECK(m.TickToHeight(BlockTicks{75'000}) == 150);
    BOOST_CHECK(m.TickToHeight(BlockTicks{75'001}) == 150);
    BOOST_CHECK(m.TickToHeight(BlockTicks{89'999}) == 199);
    BOOST_CHECK(m.TickToHeight(BlockTicks{90'000}) == 200);
    BOOST_CHECK(m.TickToHeight(BlockTicks{90'001}) == 200);
    BOOST_CHECK(m.TickToHeight(BlockTicks{330'000 - 1}) == 999);
    BOOST_CHECK(m.TickToHeight(BlockTicks{330'000}) == 1'000);
    BOOST_CHECK(m.TickToHeight(BlockTicks{330'000 + 1}) == 1'000);
    BOOST_CHECK(m.TickToHeight(BlockTicks{330'000 + 150*9000 - 1}) == 9'999);
    BOOST_CHECK(m.TickToHeight(BlockTicks{330'000 + 150*9000}) == 10'000);
    BOOST_CHECK(m.TickToHeight(BlockTicks{330'000 + 150*9000 + 1}) == 10'000);
    BOOST_CHECK(m.TickToHeight(BlockTicks{330'000 + 150*9000 + 90*9'999}) == 19'999);
    BOOST_CHECK(m.TickToHeight(BlockTicks{330'000 + 150*9000 + 90*9'999 + 1}) == 19'999);
    BOOST_CHECK(m.TickToHeight(BlockTicks{330'000 + 150*9000 + 90*10'000 - 1}) == 19'999);
    BOOST_CHECK(m.TickToHeight(BlockTicks{330'000 + 150*9000 + 90*10'000}) == 20'000);
    BOOST_CHECK(m.TickToHeight(BlockTicks{330'000 + 150*9000 + 90*10'000 + 1}) == 20'000);
    BOOST_CHECK(m.TickToHeight(BlockTicks{330'000 + 150*9000 + 90*10'000 + 59}) == 20'000);
    BOOST_CHECK(m.TickToHeight(BlockTicks{330'000 + 150*9000 + 90*10'000 + 60}) == 20'001);
    BOOST_CHECK(m.TickToHeight(BlockTicks{330'000 + 150*9000 + 90*10'000 + 60 + 1}) == 20'001);

    // Check TicksElapsed works as expected
    BOOST_CHECK(m.TicksElapsed(0, 0) == BlockTicks{0});
    BOOST_CHECK(m.TicksElapsed(0, 1) == BlockTicks{600});
    BOOST_CHECK(m.TicksElapsed(0, 2) == BlockTicks{1200});
    BOOST_CHECK(m.TicksElapsed(0, 9) == BlockTicks{600 * 9});
    BOOST_CHECK(m.TicksElapsed(1, 9) == BlockTicks{600 * 8});
    BOOST_CHECK(m.TicksElapsed(9, 1) == BlockTicks{600 * 8}); // Revers from/to (should still return the same interval)
    BOOST_CHECK(m.TicksElapsed(9, 99) == BlockTicks(600 * 90));
    BOOST_CHECK(m.TicksElapsed(9, 100) == BlockTicks(600 * 91));
    BOOST_CHECK(m.TicksElapsed(9, 101) == BlockTicks(600*91 + 300*1));
    BOOST_CHECK(m.TicksElapsed(0, 101) == BlockTicks(600*100 + 300*1));
    BOOST_CHECK(m.TicksElapsed(10, 500) == BlockTicks(600*90 + 300*400));
    BOOST_CHECK(m.TicksElapsed(10, 999) == BlockTicks(600*90 + 300*899));
    BOOST_CHECK(m.TicksElapsed(10, 1000) == BlockTicks(600*90 + 300*900));
    BOOST_CHECK(m.TicksElapsed(10, 1001) == BlockTicks(600*90 + 300*900 + 150*1));
    BOOST_CHECK(m.TicksElapsed(10, 1234) == BlockTicks(600*90 + 300*900 + 150*234));
    BOOST_CHECK(m.TicksElapsed(10, 9'999) == BlockTicks(600*90 + 300*900 + 150*8'999));
    BOOST_CHECK(m.TicksElapsed(10, 10'000) == BlockTicks(600*90 + 300*900 + 150*9'000));
    BOOST_CHECK(m.TicksElapsed(10, 10'001) == BlockTicks(600*90 + 300*900 + 150*9'000 + 90*1));
    BOOST_CHECK(m.TicksElapsed(1'123, 10'001) == BlockTicks(150*8'877 + 90*1));
    BOOST_CHECK(m.TicksElapsed(10, 20'000) == BlockTicks(600*90 + 300*900 + 150*9'000 + 90*10'000));
    BOOST_CHECK(m.TicksElapsed(10, 20'001) == BlockTicks(600*90 + 300*900 + 150*9'000 + 90*10'000 + 60*1));
    BOOST_CHECK(m.TicksElapsed(10, 1'020'001) == BlockTicks(600*90 + 300*900 + 150*9'000 + 90*10'000 + 60*1'000'001));

    // Check GetBlockTickIntervalForHeight
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(0) == BlockTicks{600});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(1) == BlockTicks{600});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(99) == BlockTicks{600});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(100) == BlockTicks{300});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(101) == BlockTicks{300});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(999) == BlockTicks{300});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(1'000) == BlockTicks{150});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(1'001) == BlockTicks{150});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(9'999) == BlockTicks{150});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(10'000) == BlockTicks{90});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(10'001) == BlockTicks{90});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(10'123) == BlockTicks{90});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(19'999) == BlockTicks{90});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(20'000) == BlockTicks{60});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(20'001) == BlockTicks{60});
    BOOST_CHECK(m.GetBlockTickIntervalForHeight(99'123'456) == BlockTicks{60});
}

BOOST_AUTO_TEST_SUITE_END()
