#include <gtest/gtest.h>
#include "mini_lsm/compact.h"
#include "mini_lsm/lsm_storage.h"

using namespace mini_lsm;

TEST(TieredCompactionTest, SpaceAmplificationTrigger) {
    TieredCompactionOptions opts{3, 200, 100, 2, std::nullopt}; // 3 tiers, max space amp 200%, size ratio 100%, min width 2
    TieredCompactionController ctrl(opts);

    LsmStorageState state;
    // 3 tiers: T1 has 15 files, T2 has 10 files, T3 (bottom) has 10 files.
    // Sum of previous = 25. Space amp ratio = 25/10 * 100 = 250% >= 200%.
    state.levels = {
        {1, {101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115}},
        {2, {116, 117, 118, 119, 120, 121, 122, 123, 124, 125}},
        {3, {126, 127, 128, 129, 130, 131, 132, 133, 134, 135}}
    };

    auto task_opt = ctrl.generate_compaction_task(state);
    ASSERT_TRUE(task_opt.has_value());
    EXPECT_TRUE(task_opt->bottom_tier_included);
    EXPECT_EQ(task_opt->tiers.size(), 3);

    auto [new_state, removed] = ctrl.apply_compaction_result(state, *task_opt, {201, 202});
    EXPECT_EQ(new_state.levels.size(), 1);
    EXPECT_EQ(new_state.levels[0].first, 201);
    EXPECT_EQ(new_state.levels[0].second, (std::vector<size_t>{201, 202}));
    EXPECT_EQ(removed.size(), 35);
}

TEST(TieredCompactionTest, SizeRatioTrigger) {
    TieredCompactionOptions opts{3, 300, 100, 2, std::nullopt}; // size ratio 100% -> trigger > 2.0
    TieredCompactionController ctrl(opts);

    LsmStorageState state;
    // T1: 2 files. T2: 5 files. Ratio T2/T1 = 5/2 = 2.5 > 2.0. T3 (bottom): 20 files. Space amp = 7/20 = 35% < 300%.
    state.levels = {
        {1, {1, 2}},
        {2, {3, 4, 5, 6, 7}},
        {3, {8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27}}
    };

    auto task_opt = ctrl.generate_compaction_task(state);
    ASSERT_TRUE(task_opt.has_value());
    EXPECT_FALSE(task_opt->bottom_tier_included);
    ASSERT_EQ(task_opt->tiers.size(), 2);
    EXPECT_EQ(task_opt->tiers[0].first, 1);
    EXPECT_EQ(task_opt->tiers[1].first, 2);

    auto [new_state, removed] = ctrl.apply_compaction_result(state, *task_opt, {301});
    ASSERT_EQ(new_state.levels.size(), 2);
    EXPECT_EQ(new_state.levels[0].first, 301);
    EXPECT_EQ(new_state.levels[1].first, 3);
    EXPECT_EQ(removed, (std::vector<size_t>{1, 2, 3, 4, 5, 6, 7}));
}

TEST(TieredCompactionTest, InsufficientTiersNoTrigger) {
    TieredCompactionOptions opts{4, 200, 100, 2, std::nullopt}; // num_tiers = 4
    TieredCompactionController ctrl(opts);

    LsmStorageState state;
    state.levels = {
        {1, {1}},
        {2, {2, 3}}
    };

    auto task_opt = ctrl.generate_compaction_task(state);
    EXPECT_FALSE(task_opt.has_value());
}
