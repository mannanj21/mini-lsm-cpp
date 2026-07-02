#include <gtest/gtest.h>
#include "mini_lsm/compact.h"
#include "mini_lsm/lsm_storage.h"

using namespace mini_lsm;

TEST(LeveledCompactionTest, L0CompactionTrigger) {
    LeveledCompactionOptions opts{10, 2, 3, 2}; // multiplier 10, l0 trigger 2, max levels 3, base level size 2MB
    LeveledCompactionController ctrl(opts);

    LsmStorageState state;
    state.l0_sstables = {1, 2};
    state.levels = {{1, {3}}, {2, {4, 5}}, {3, {6, 7}}};

    auto task_opt = ctrl.generate_compaction_task(state);
    ASSERT_TRUE(task_opt.has_value());
    EXPECT_FALSE(task_opt->upper_level.has_value());
    EXPECT_EQ(task_opt->upper_level_sst_ids, (std::vector<size_t>{1, 2}));
    EXPECT_EQ(task_opt->lower_level, 3); // base level is 3 since real sizes are 0 (< 2MB)
    EXPECT_TRUE(task_opt->is_lower_level_bottom_level);

    auto [new_state, removed] = ctrl.apply_compaction_result(state, *task_opt, {10, 11});
    EXPECT_TRUE(new_state.l0_sstables.empty());
    EXPECT_EQ(new_state.levels[2].second, (std::vector<size_t>{6, 7, 10, 11}));
}

TEST(LeveledCompactionTest, NoCompactionTriggered) {
    LeveledCompactionOptions opts{10, 4, 3, 2}; // l0 trigger 4
    LeveledCompactionController ctrl(opts);

    LsmStorageState state;
    state.l0_sstables = {1};
    state.levels = {{1, {}}, {2, {}}, {3, {}}};

    auto task_opt = ctrl.generate_compaction_task(state);
    EXPECT_FALSE(task_opt.has_value());
}
