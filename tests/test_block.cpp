#include <gtest/gtest.h>
#include <zlib.h>
#include <memory>
#include "mini_lsm/key.h"
#include "mini_lsm/endian.h"
#include "mini_lsm/block.h"

using namespace mini_lsm;

TEST(KeyTest, BasicOperationsAndComparisons) {
    KeyVec k1 = KeyVec::from_vec({'a', 'b', 'c'});
    KeyVec k2 = KeyVec::from_vec({'a', 'b', 'd'});
    KeyVec k1_dup = KeyVec::from_vec({'a', 'b', 'c'});

    EXPECT_EQ(k1.len(), 3);
    EXPECT_FALSE(k1.is_empty());
    EXPECT_EQ(k1, k1_dup);
    EXPECT_LT(k1, k2);
    EXPECT_GT(k2, k1);

    KeySlice s1 = k1.as_key_slice();
    KeySlice s2 = k2.as_key_slice();
    EXPECT_EQ(s1, k1);
    EXPECT_LT(s1, s2);
}

TEST(EndianTest, BigEndianSerialization) {
    std::vector<uint8_t> buf;
    put_u16_be(buf, 0x1234);
    put_u32_be(buf, 0xAABBCCDD);
    put_u64_be(buf, 0x1122334455667788ULL);

    ASSERT_EQ(buf.size(), 2 + 4 + 8);
    EXPECT_EQ(buf[0], 0x12);
    EXPECT_EQ(buf[1], 0x34);
    EXPECT_EQ(buf[2], 0xAA);
    EXPECT_EQ(buf[5], 0xDD);

    EXPECT_EQ(read_u16_be(&buf[0]), 0x1234);
    EXPECT_EQ(read_u32_be(&buf[2]), 0xAABBCCDD);
    EXPECT_EQ(read_u64_be(&buf[6]), 0x1122334455667788ULL);

    std::vector<uint8_t> direct(14, 0);
    write_u16_be(&direct[0], 0x1234);
    write_u32_be(&direct[2], 0xAABBCCDD);
    write_u64_be(&direct[6], 0x1122334455667788ULL);
    EXPECT_EQ(buf, direct);
}

TEST(Crc32Test, KnownAnswerIEEE) {
    const char* data = "123456789";
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(data), 9);
    EXPECT_EQ(crc, 0xCBF43926UL);
}

TEST(BlockBuilderTest, PrefixCompressionAndLayout) {
    BlockBuilder builder(1024);
    EXPECT_TRUE(builder.is_empty());

    EXPECT_TRUE(builder.add(KeySlice("aaaa", 4), KeySlice("v1", 2)));
    EXPECT_TRUE(builder.add(KeySlice("aaab", 4), KeySlice("v2", 2)));
    EXPECT_TRUE(builder.add(KeySlice("aaac", 4), KeySlice("v3", 2)));
    EXPECT_FALSE(builder.is_empty());

    Block block = builder.build();
    EXPECT_EQ(block.offsets.size(), 3);
    EXPECT_EQ(block.offsets[0], 0);

    // Verify first entry: overlap=0, key_len=4, "aaaa", val_len=2, "v1" (total 2+2+4+2+2 = 12 bytes)
    EXPECT_EQ(read_u16_be(&block.data[0]), 0);
    EXPECT_EQ(read_u16_be(&block.data[2]), 4);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(&block.data[4]), 4), "aaaa");
    EXPECT_EQ(read_u16_be(&block.data[8]), 2);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(&block.data[10]), 2), "v1");

    // Verify second entry: overlap=3 ("aaa"), key_len=1 ("b"), val_len=2, "v2" (total 2+2+1+2+2 = 9 bytes)
    size_t off1 = block.offsets[1];
    EXPECT_EQ(off1, 12);
    EXPECT_EQ(read_u16_be(&block.data[off1]), 3);
    EXPECT_EQ(read_u16_be(&block.data[off1 + 2]), 1);
    EXPECT_EQ(block.data[off1 + 4], 'b');
    EXPECT_EQ(read_u16_be(&block.data[off1 + 5]), 2);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(&block.data[off1 + 7]), 2), "v2");

    std::vector<uint8_t> encoded = block.encode();
    Block decoded = Block::decode(encoded);
    EXPECT_EQ(decoded.data, block.data);
    EXPECT_EQ(decoded.offsets, block.offsets);
}

TEST(BlockIteratorTest, RoundTripAndSeek) {
    BlockBuilder builder(1024);
    builder.add(KeySlice("apple", 5), KeySlice("v1", 2));
    builder.add(KeySlice("banana", 6), KeySlice("v2", 2));
    builder.add(KeySlice("cat", 3), KeySlice("v3", 2));
    builder.add(KeySlice("dog", 3), KeySlice("v4", 2));

    std::vector<uint8_t> encoded = builder.build().encode();
    auto block = std::make_shared<Block>(Block::decode(encoded));

    auto iter = BlockIterator::create_and_seek_to_first(block);
    ASSERT_TRUE(iter.is_valid());
    EXPECT_EQ(iter.key(), KeySlice("apple", 5));
    EXPECT_EQ(iter.value(), KeySlice("v1", 2));

    iter.next();
    ASSERT_TRUE(iter.is_valid());
    EXPECT_EQ(iter.key(), KeySlice("banana", 6));
    EXPECT_EQ(iter.value(), KeySlice("v2", 2));

    iter.next();
    ASSERT_TRUE(iter.is_valid());
    EXPECT_EQ(iter.key(), KeySlice("cat", 3));
    EXPECT_EQ(iter.value(), KeySlice("v3", 2));

    iter.next();
    ASSERT_TRUE(iter.is_valid());
    EXPECT_EQ(iter.key(), KeySlice("dog", 3));
    EXPECT_EQ(iter.value(), KeySlice("v4", 2));

    iter.next();
    EXPECT_FALSE(iter.is_valid());

    // Test seek_to_key
    iter.seek_to_key(KeySlice("ball", 4)); // should land on "banana"
    ASSERT_TRUE(iter.is_valid());
    EXPECT_EQ(iter.key(), KeySlice("banana", 6));

    iter.seek_to_key(KeySlice("dog", 3));
    ASSERT_TRUE(iter.is_valid());
    EXPECT_EQ(iter.key(), KeySlice("dog", 3));

    iter.seek_to_key(KeySlice("zebra", 5));
    EXPECT_FALSE(iter.is_valid());
}

TEST(BlockIteratorTest, SharedPrefixRisk12) {
    BlockBuilder builder(1024);
    builder.add(KeySlice("key_prefix_alpha", 16), KeySlice("val1", 4));
    builder.add(KeySlice("key_prefix_beta", 15), KeySlice("val2", 4));
    builder.add(KeySlice("key_prefix_gamma", 16), KeySlice("val3", 4));

    std::vector<uint8_t> encoded = builder.build().encode();
    auto block = std::make_shared<Block>(Block::decode(encoded));

    auto iter = BlockIterator::create_and_seek_to_first(block);
    ASSERT_TRUE(iter.is_valid());
    EXPECT_EQ(iter.key(), KeySlice("key_prefix_alpha", 16));

    iter.next();
    ASSERT_TRUE(iter.is_valid());
    EXPECT_EQ(iter.key(), KeySlice("key_prefix_beta", 15));

    iter.next();
    ASSERT_TRUE(iter.is_valid());
    EXPECT_EQ(iter.key(), KeySlice("key_prefix_gamma", 16));
}
