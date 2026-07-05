#include <gtest/gtest.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "mini_lsm/table.h"

using namespace mini_lsm;

TEST(CompressionTest, UncompressedRoundTrip) {
    const std::string path = "test_compress_none.sst";
    ::unlink(path.c_str());

    SsTableBuilder builder(64, CompressionType::None);
    for (int i = 0; i < 100; ++i) {
        char kbuf[32];
        char vbuf[64];
        snprintf(kbuf, sizeof(kbuf), "key_%05d", i);
        snprintf(vbuf, sizeof(vbuf), "value_uncompressed_%05d_data_payload", i);
        builder.add(KeySlice(kbuf), KeySlice(vbuf));
    }

    auto table = builder.build_for_test(path);
    ASSERT_NE(table, nullptr);
    EXPECT_GT(table->num_blocks(), 1);

    auto iter = SsTableIterator::create_and_seek_to_first(table);
    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(iter.is_valid());
        char kbuf[32];
        char vbuf[64];
        snprintf(kbuf, sizeof(kbuf), "key_%05d", i);
        snprintf(vbuf, sizeof(vbuf), "value_uncompressed_%05d_data_payload", i);
        EXPECT_EQ(iter.key(), KeySlice(kbuf));
        EXPECT_EQ(iter.value(), KeySlice(vbuf));
        iter.next();
    }
    EXPECT_FALSE(iter.is_valid());

    ::unlink(path.c_str());
}

TEST(CompressionTest, SnappyRoundTrip) {
    const std::string path_snappy = "test_compress_snappy.sst";
    const std::string path_none = "test_compress_ref.sst";
    ::unlink(path_snappy.c_str());
    ::unlink(path_none.c_str());

    // Build uncompressed reference to check size reduction
    SsTableBuilder builder_none(64, CompressionType::None);
    SsTableBuilder builder_snappy(64, CompressionType::Snappy);

    // Highly compressible data (repetitive values)
    for (int i = 0; i < 200; ++i) {
        char kbuf[32];
        char vbuf[256];
        snprintf(kbuf, sizeof(kbuf), "key_%05d", i);
        // Repeated string pattern is very compressible by Snappy
        snprintf(vbuf, sizeof(vbuf), "highly_compressible_payload_repeat_repeat_repeat_repeat_%05d", i);
        builder_none.add(KeySlice(kbuf), KeySlice(vbuf));
        builder_snappy.add(KeySlice(kbuf), KeySlice(vbuf));
    }

    auto table_none = builder_none.build_for_test(path_none);
    auto table_snappy = builder_snappy.build_for_test(path_snappy);
    ASSERT_NE(table_none, nullptr);
    ASSERT_NE(table_snappy, nullptr);

#if defined(MINI_LSM_SNAPPY) && MINI_LSM_SNAPPY
    // Verify Snappy actually reduced the file size on disk!
    EXPECT_LT(table_snappy->table_size(), table_none->table_size());
#endif

    // Verify all keys and values round-trip correctly via SsTableIterator
    auto iter = SsTableIterator::create_and_seek_to_first(table_snappy);
    for (int i = 0; i < 200; ++i) {
        ASSERT_TRUE(iter.is_valid());
        char kbuf[32];
        char vbuf[256];
        snprintf(kbuf, sizeof(kbuf), "key_%05d", i);
        snprintf(vbuf, sizeof(vbuf), "highly_compressible_payload_repeat_repeat_repeat_repeat_%05d", i);
        EXPECT_EQ(iter.key(), KeySlice(kbuf));
        EXPECT_EQ(iter.value(), KeySlice(vbuf));
        iter.next();
    }
    EXPECT_FALSE(iter.is_valid());

    ::unlink(path_snappy.c_str());
    ::unlink(path_none.c_str());
}

TEST(CompressionTest, MixedBlockFlags) {
    const std::string path = "test_compress_mixed.sst";
    ::unlink(path.c_str());

    // Start uncompressed
    SsTableBuilder builder(64, CompressionType::None);
    for (int i = 0; i < 50; ++i) {
        char kbuf[32];
        char vbuf[128];
        snprintf(kbuf, sizeof(kbuf), "key_%05d", i);
        snprintf(vbuf, sizeof(vbuf), "uncompressed_block_payload_%05d", i);
        builder.add(KeySlice(kbuf), KeySlice(vbuf));
    }

    // Switch to Snappy compression for subsequent blocks
    builder.set_compression(CompressionType::Snappy);
    for (int i = 50; i < 100; ++i) {
        char kbuf[32];
        char vbuf[128];
        snprintf(kbuf, sizeof(kbuf), "key_%05d", i);
        snprintf(vbuf, sizeof(vbuf), "snappy_compressed_block_payload_%05d", i);
        builder.add(KeySlice(kbuf), KeySlice(vbuf));
    }

    auto table = builder.build_for_test(path);
    ASSERT_NE(table, nullptr);
    EXPECT_GT(table->num_blocks(), 1);

    auto iter = SsTableIterator::create_and_seek_to_first(table);
    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(iter.is_valid());
        char kbuf[32];
        char vbuf[128];
        snprintf(kbuf, sizeof(kbuf), "key_%05d", i);
        if (i < 50) {
            snprintf(vbuf, sizeof(vbuf), "uncompressed_block_payload_%05d", i);
        } else {
            snprintf(vbuf, sizeof(vbuf), "snappy_compressed_block_payload_%05d", i);
        }
        EXPECT_EQ(iter.key(), KeySlice(kbuf));
        EXPECT_EQ(iter.value(), KeySlice(vbuf));
        iter.next();
    }
    EXPECT_FALSE(iter.is_valid());

    ::unlink(path.c_str());
}
