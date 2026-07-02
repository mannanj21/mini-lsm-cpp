#include <gtest/gtest.h>
#include "mini_lsm/compact.h"
#include "mini_lsm/lsm_storage.h"

using namespace mini_lsm;

TEST(SimpleLeveledTest, L0CompactionTrigger) {
    SimpleLeveledCompactionOptions opts{200, 2, 3}; // ratio 200%, l0 trigger 2, max levels 3
    SimpleLeveledCompactionController ctrl(opts);

    LsmStorageState state;
    state.l0_sstables = {1, 2};
    state.levels = {{1, {3}}, {2, {4, 5}}, {3, {6, 7, 8, 9}}};

    auto task_opt = ctrl.generate_compaction_task(state);
    ASSERT_TRUE(task_opt.has_value());
    EXPECT_FALSE(task_opt->upper_level.has_value());
    EXPECT_EQ(task_opt->upper_level_sst_ids, (std::vector<size_t>{1, 2}));
    EXPECT_EQ(task_opt->lower_level, 1);
    EXPECT_EQ(task_opt->lower_level_sst_ids, (std::vector<size_t>{3}));
    EXPECT_FALSE(task_opt->is_lower_level_bottom_level);

    auto [new_state, removed] = ctrl.apply_compaction_result(state, *task_opt, {10, 11});
    EXPECT_TRUE(new_state.l0_sstables.empty());
    EXPECT_EQ(new_state.levels[0].second, (std::vector<size_t>{10, 11}));
    EXPECT_EQ(removed, (std::vector<size_t>{1, 2, 3}));
}

TEST(SimpleLeveledTest, SizeRatioCompactionTrigger) {
    SimpleLeveledCompactionOptions opts{200, 4, 3}; // ratio 200%, l0 trigger 4, max levels 3
    SimpleLeveledCompactionController ctrl(opts);

    LsmStorageState state;
    state.l0_sstables = {1}; // 1 file, < 4 trigger
    // L1: 2 files, L2: 2 files (ratio 2/2 = 1.0 < 2.0 -> triggers L1 to L2)
    state.levels = {{1, {2, 3}}, {2, {4, 5}}, {3, {6, 7, 8, 9, 10}}};

    auto task_opt = ctrl.generate_compaction_task(state);
    ASSERT_TRUE(task_opt.has_value());
    EXPECT_TRUE(task_opt->upper_level.has_value());
    EXPECT_EQ(task_opt->upper_level, 1);
    EXPECT_EQ(task_opt->upper_level_sst_ids, (std::vector<size_t>{2, 3}));
    EXPECT_EQ(task_opt->lower_level, 2);
    EXPECT_EQ(task_opt->lower_level_sst_ids, (std::vector<size_t>{4, 5}));
    EXPECT_FALSE(task_opt->is_lower_level_bottom_level);

    auto [new_state, removed] = ctrl.apply_compaction_result(state, *task_opt, {11, 12, 13});
    EXPECT_EQ(new_state.l0_sstables, (std::vector<size_t>{1}));
    EXPECT_TRUE(new_state.levels[0].second.empty());
    EXPECT_EQ(new_state.levels[1].second, (std::vector<size_t>{11, 12, 13}));
    EXPECT_EQ(removed, (std::vector<size_t>{2, 3, 4, 5}));
}

TEST(SimpleLeveledTest, NoCompactionTriggered) {
    SimpleLeveledCompactionOptions opts{200, 4, 3};
    SimpleLeveledCompactionController ctrl(opts);

    LsmStorageState state;
    state.l0_sstables = {1};
    // L1: 1 file, L2: 3 files (3/1 = 3.0 >= 2.0), L3: 7 files (7/3 = 2.33 >= 2.0)
    state.levels = {{1, {2}}, {2, {3, 4, 5}}, {3, {6, 7, 8, 9, 10, 11, 12}}};

    auto task_opt = ctrl.generate_compaction_task(state);
    EXPECT_FALSE(task_opt.has_value());
}
