#include <gtest/gtest.h>
#include <filesystem>
#include "mini_lsm/lsm_storage.h"

namespace mini_lsm {
namespace {

class LsmStorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "test_lsm_storage_dir";
        std::filesystem::remove_all(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::string test_dir_;
};

TEST_F(LsmStorageTest, BasicPutGetDelete) {
    auto opts = LsmStorageOptions::default_for_week1_test();
    auto db = MiniLsm::open(test_dir_, opts);

    db->put(KeySlice("key1"), KeySlice("val1"));
    db->put(KeySlice("key2"), KeySlice("val2"));

    EXPECT_EQ(db->get(KeySlice("key1")), "val1");
    EXPECT_EQ(db->get(KeySlice("key2")), "val2");
    EXPECT_EQ(db->get(KeySlice("key3")), "");

    db->del(KeySlice("key1"));
    EXPECT_EQ(db->get(KeySlice("key1")), "");
    EXPECT_EQ(db->get(KeySlice("key2")), "val2");
}

TEST_F(LsmStorageTest, ScanWithBounds) {
    auto opts = LsmStorageOptions::default_for_week1_test();
    auto db = MiniLsm::open(test_dir_, opts);

    db->put(KeySlice("a"), KeySlice("1"));
    db->put(KeySlice("b"), KeySlice("2"));
    db->put(KeySlice("c"), KeySlice("3"));
    db->put(KeySlice("d"), KeySlice("4"));

    auto iter = db->scan(Bound::included(KeySlice("b")), Bound::excluded(KeySlice("d")));
    ASSERT_TRUE(iter.is_valid());
    EXPECT_EQ(iter.key().to_string(), "b");
    EXPECT_EQ(iter.value().to_string(), "2");
    iter.next();
    ASSERT_TRUE(iter.is_valid());
    EXPECT_EQ(iter.key().to_string(), "c");
    EXPECT_EQ(iter.value().to_string(), "3");
    iter.next();
    EXPECT_FALSE(iter.is_valid());
}

TEST_F(LsmStorageTest, FreezeAndFlush) {
    auto opts = LsmStorageOptions::default_for_week1_test();
    auto db = MiniLsm::open(test_dir_, opts);

    db->put(KeySlice("k1"), KeySlice("v1"));
    db->inner->force_freeze_memtable();
    db->put(KeySlice("k2"), KeySlice("v2"));

    EXPECT_EQ(db->get(KeySlice("k1")), "v1");
    EXPECT_EQ(db->get(KeySlice("k2")), "v2");

    db->force_flush();
    EXPECT_EQ(db->inner->state->l0_sstables.size(), 1);
    EXPECT_EQ(db->get(KeySlice("k1")), "v1");
    EXPECT_EQ(db->get(KeySlice("k2")), "v2");
}

} // namespace
} // namespace mini_lsm
