#include <gtest/gtest.h>
#include <filesystem>
#include <cstdio>
#include <map>
#include <string>
#include "mini_lsm/lsm_storage.h"

namespace mini_lsm {
namespace {

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "test_integration_dir";
        std::filesystem::remove_all(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::string test_dir_;
};

TEST_F(IntegrationTest, FullLifecycle1000KeysSurvival) {
    auto opts = LsmStorageOptions::default_for_week1_test();
    opts.enable_wal = true;

    std::map<std::string, std::string> expected;

    // Open DB and put 1000 keys
    {
        auto db = MiniLsm::open(test_dir_, opts);
        for (int i = 0; i < 1000; ++i) {
            char k[32], v[32];
            std::snprintf(k, sizeof(k), "key_%04d", i);
            std::snprintf(v, sizeof(v), "val_%04d", i);
            db->put(KeySlice(k), KeySlice(v));
            expected[k] = v;

            // Periodically freeze/flush to create multiple SSTables/memtables
            if (i % 200 == 0 && i > 0) {
                db->inner->force_freeze_memtable();
            }
        }

        // Perform some overwrites
        for (int i = 100; i < 200; ++i) {
            char k[32], v[32];
            std::snprintf(k, sizeof(k), "key_%04d", i);
            std::snprintf(v, sizeof(v), "val_new_%04d", i);
            db->put(KeySlice(k), KeySlice(v));
            expected[k] = v;
        }

        // Perform some deletes
        for (int i = 500; i < 550; ++i) {
            char k[32];
            std::snprintf(k, sizeof(k), "key_%04d", i);
            db->del(KeySlice(k));
            expected.erase(k);
        }

        // Scan and verify before flush/compaction
        auto iter = db->scan(Bound::unbounded(), Bound::unbounded());
        for (const auto& [k, v] : expected) {
            ASSERT_TRUE(iter.is_valid()) << "Expected valid iterator at key " << k;
            EXPECT_EQ(iter.key().to_string(), k);
            EXPECT_EQ(iter.value().to_string(), v);
            iter.next();
        }
        EXPECT_FALSE(iter.is_valid());

        // Force flush and compaction
        while (!db->inner->state->imm_memtables.empty()) {
            db->force_flush();
        }
        db->force_full_compaction();

        // Verify again before closing
        for (const auto& [k, v] : expected) {
            EXPECT_EQ(db->get(KeySlice(k)), v) << "Key failed: " << k;
        }

        // Clean close
        db->close();
    }

    // Reopen DB and verify all data survived
    {
        auto db = MiniLsm::open(test_dir_, opts);
        for (const auto& [k, v] : expected) {
            EXPECT_EQ(db->get(KeySlice(k)), v) << "Key did not survive reopen: " << k;
        }

        // Scan across reopened DB
        auto iter = db->scan(Bound::unbounded(), Bound::unbounded());
        for (const auto& [k, v] : expected) {
            ASSERT_TRUE(iter.is_valid());
            EXPECT_EQ(iter.key().to_string(), k);
            EXPECT_EQ(iter.value().to_string(), v);
            iter.next();
        }
        EXPECT_FALSE(iter.is_valid());

        db->close();
    }
}

} // namespace
} // namespace mini_lsm
