#include <gtest/gtest.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "mini_lsm/table.h"

using namespace mini_lsm;

TEST(SsTableBuilderTest, MultiBlockLayoutAndChecksums) {
    const std::string path = "test_sst_step8.sst";
    ::unlink(path.c_str());

    SsTableBuilder builder(32);
    for (int i = 0; i < 20; ++i) {
        char kbuf[16];
        char vbuf[16];
        snprintf(kbuf, sizeof(kbuf), "key_%03d", i);
        snprintf(vbuf, sizeof(vbuf), "value_%03d", i);
        builder.add(KeySlice(kbuf), KeySlice(vbuf));
    }

    auto table = builder.build_for_test(path);
    ASSERT_NE(table, nullptr);
    EXPECT_GT(table->num_blocks(), 1);

    EXPECT_EQ(table->first_key.as_key_slice(), KeySlice("key_000"));
    EXPECT_EQ(table->last_key.as_key_slice(), KeySlice("key_019"));

    for (size_t idx = 0; idx < table->num_blocks(); ++idx) {
        auto blk = table->read_block(idx);
        ASSERT_NE(blk, nullptr);
        EXPECT_FALSE(blk->offsets.empty());
    }

    ASSERT_NE(table->bloom, nullptr);
    for (int i = 0; i < 20; ++i) {
        char kbuf[16];
        snprintf(kbuf, sizeof(kbuf), "key_%03d", i);
        uint32_t h = farmhash_fingerprint32(KeySlice(kbuf));
        EXPECT_TRUE(table->bloom->may_contain(h));
    }

    ::unlink(path.c_str());
}

TEST(SsTableIteratorTest, EndToEndSeekAndIterateWithBloomRejection) {
    const std::string path = "test_sst_step9.sst";
    ::unlink(path.c_str());

    SsTableBuilder builder(64);
    const int N = 100;
    for (int i = 0; i < N; ++i) {
        char kbuf[32];
        char vbuf[32];
        snprintf(kbuf, sizeof(kbuf), "key_%04d", i * 5);
        snprintf(vbuf, sizeof(vbuf), "val_%04d", i * 5);
        builder.add(KeySlice(kbuf), KeySlice(vbuf));
    }

    auto built_table = builder.build_for_test(path);
    auto file = FileObject::open(path);
    auto table = SsTable::open(1, nullptr, std::move(file));
    ASSERT_NE(table, nullptr);
    EXPECT_GT(table->num_blocks(), 1);

    // Verify all entries via iterator
    auto iter = SsTableIterator::create_and_seek_to_first(table);
    for (int i = 0; i < N; ++i) {
        ASSERT_TRUE(iter.is_valid());
        char kbuf[32];
        char vbuf[32];
        snprintf(kbuf, sizeof(kbuf), "key_%04d", i * 5);
        snprintf(vbuf, sizeof(vbuf), "val_%04d", i * 5);
        EXPECT_EQ(iter.key(), KeySlice(kbuf));
        EXPECT_EQ(iter.value(), KeySlice(vbuf));
        iter.next();
    }
    EXPECT_FALSE(iter.is_valid());

    // Test seek_to_key exact match
    auto iter_exact = SsTableIterator::create_and_seek_to_key(table, KeySlice("key_0025"));
    ASSERT_TRUE(iter_exact.is_valid());
    EXPECT_EQ(iter_exact.key(), KeySlice("key_0025"));
    EXPECT_EQ(iter_exact.value(), KeySlice("val_0025"));

    // Test seek_to_key between entries (e.g. seek key_0022 -> should land on key_0025)
    auto iter_mid = SsTableIterator::create_and_seek_to_key(table, KeySlice("key_0022"));
    ASSERT_TRUE(iter_mid.is_valid());
    EXPECT_EQ(iter_mid.key(), KeySlice("key_0025"));

    // Test seek past end
    auto iter_end = SsTableIterator::create_and_seek_to_key(table, KeySlice("z_past_end"));
    EXPECT_FALSE(iter_end.is_valid());

    // Verify bloom filter rejects missing keys without reading blocks
    ASSERT_NE(table->bloom, nullptr);
    int rejected_missing = 0;
    for (int i = 0; i < 50; ++i) {
        std::string missing = "missing_key_" + std::to_string(i);
        uint32_t h = farmhash_fingerprint32(KeySlice(missing));
        if (!table->bloom->may_contain(h)) {
            rejected_missing++;
        }
    }
    EXPECT_GT(rejected_missing, 0); // bloom filter successfully rejects missing keys

    ::unlink(path.c_str());
}
