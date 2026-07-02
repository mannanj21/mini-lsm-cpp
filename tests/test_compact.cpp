#include <gtest/gtest.h>
#include "mini_lsm/compact.h"
#include "mini_lsm/lsm_storage.h"
#include <chrono>
#include <filesystem>
#include <thread>

using namespace mini_lsm;

class CompactionOrchestrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "test_compaction_dir_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        std::filesystem::remove_all(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::string test_dir_;
};

TEST_F(CompactionOrchestrationTest, EndToEndSimpleLeveledCompaction) {
    LsmStorageOptions opts;
    opts.block_size = 64;
    opts.target_sst_size = 1024;
    opts.num_memtable_limit = 10;

    SimpleLeveledCompactionOptions sl_opts;
    sl_opts.level0_file_num_compaction_trigger = 2;
    sl_opts.max_levels = 1;
    sl_opts.size_ratio_percent = 1;
    opts.compaction_options = CompactionOptions::simple(sl_opts);

    auto inner = LsmStorageInner::open(test_dir_, opts);

    // Write first SSTable into L0
    inner->put("k1", "v1_old");
    inner->put("k2", "v2_old");
    inner->put("k3", "v3");
    inner->force_flush_next_imm_memtable(); // Wait, first put data to memtable then freeze and flush
    // Since put adds to memtable, let's freeze it
    inner->force_freeze_memtable();
    inner->force_flush_next_imm_memtable();

    {
        std::shared_lock<std::shared_mutex> lock(inner->state_mutex);
        EXPECT_EQ(inner->state->l0_sstables.size(), 1);
    }

    // Write second SSTable into L0 with updates and shadow tombstone
    inner->put("k1", "v1_new");
    inner->del("k2"); // Tombstone for k2
    inner->put("k4", "v4");
    inner->force_freeze_memtable();
    inner->force_flush_next_imm_memtable();

    {
        std::shared_lock<std::shared_mutex> lock(inner->state_mutex);
        EXPECT_EQ(inner->state->l0_sstables.size(), 2);
    }

    // Trigger compaction (should trigger L0 -> L1 compaction)
    bool compacted = inner->trigger_compaction();
    EXPECT_TRUE(compacted);

    {
        std::shared_lock<std::shared_mutex> lock(inner->state_mutex);
        EXPECT_TRUE(inner->state->l0_sstables.empty());
        ASSERT_FALSE(inner->state->levels.empty());
        EXPECT_FALSE(inner->state->levels[0].second.empty());
    }

    // Check data correctness
    EXPECT_EQ(inner->get("k1"), "v1_new");
    EXPECT_EQ(inner->get("k2"), ""); // Tombstone dropped on bottom level or returns empty
    EXPECT_EQ(inner->get("k3"), "v3");
    EXPECT_EQ(inner->get("k4"), "v4");
}

TEST_F(CompactionOrchestrationTest, ForceFullCompactionMergeAndTombstoneDrop) {
    LsmStorageOptions opts = LsmStorageOptions::default_for_week1_test();
    opts.block_size = 64;
    opts.target_sst_size = 1024;
    auto inner = LsmStorageInner::open(test_dir_, opts);

    inner->put("a", "alpha");
    inner->put("b", "beta");
    inner->force_freeze_memtable();
    inner->force_flush_next_imm_memtable();

    inner->put("b", "beta_v2");
    inner->del("a");
    inner->put("c", "charlie");
    inner->force_freeze_memtable();
    inner->force_flush_next_imm_memtable();

    EXPECT_EQ(inner->get("a"), "");
    EXPECT_EQ(inner->get("b"), "beta_v2");
    EXPECT_EQ(inner->get("c"), "charlie");

    inner->force_full_compaction();

    {
        std::shared_lock<std::shared_mutex> lock(inner->state_mutex);
        EXPECT_TRUE(inner->state->l0_sstables.empty());
        ASSERT_FALSE(inner->state->levels.empty());
        EXPECT_EQ(inner->state->levels[0].second.size(), 1);
    }

    EXPECT_EQ(inner->get("a"), "");
    EXPECT_EQ(inner->get("b"), "beta_v2");
    EXPECT_EQ(inner->get("c"), "charlie");
}

TEST_F(CompactionOrchestrationTest, BackgroundThreadsExecution) {
    LsmStorageOptions opts;
    opts.block_size = 64;
    opts.target_sst_size = 1024;
    opts.num_memtable_limit = 1;

    SimpleLeveledCompactionOptions sl_opts;
    sl_opts.level0_file_num_compaction_trigger = 2;
    sl_opts.max_levels = 1;
    sl_opts.size_ratio_percent = 1;
    opts.compaction_options = CompactionOptions::simple(sl_opts);

    auto lsm = MiniLsm::open(test_dir_, opts);
    lsm->put("bg1", "val1");
    lsm->put("bg2", "val2");
    lsm->inner->force_freeze_memtable();
    lsm->put("bg3", "val3");
    lsm->inner->force_freeze_memtable();

    auto start = std::chrono::steady_clock::now();
    bool compacted_to_l1 = false;
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1500)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::shared_lock<std::shared_mutex> lock(lsm->inner->state_mutex);
        if (!lsm->inner->state->levels.empty() && !lsm->inner->state->levels[0].second.empty()) {
            compacted_to_l1 = true;
            break;
        }
    }
    EXPECT_TRUE(compacted_to_l1);
    EXPECT_EQ(lsm->get("bg1"), "val1");
    EXPECT_EQ(lsm->get("bg2"), "val2");
    EXPECT_EQ(lsm->get("bg3"), "val3");
}
