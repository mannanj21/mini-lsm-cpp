#include <gtest/gtest.h>
#include <unistd.h>
#include <memory>
#include <string>
#include <vector>
#include "mini_lsm/iterators.h"
#include "mini_lsm/lsm_iterator.h"
#include "mini_lsm/mem_table.h"
#include "mini_lsm/table.h"

using namespace mini_lsm;

TEST(MergeIteratorTest, OverlappingRangesWithShadowDeletions) {
    auto m1 = std::make_shared<MemTable>(1);
    m1->put("k1", "v1_new");
    m1->put("k2", ""); // Tombstone deletion shadowing k2
    m1->put("k4", "v4_new");

    auto m2 = std::make_shared<MemTable>(2);
    m2->put("k2", "v2_mid");
    m2->put("k3", "v3_mid");

    auto m3 = std::make_shared<MemTable>(3);
    m3->put("k1", "v1_old");
    m3->put("k3", "v3_old");
    m3->put("k5", "v5_old");

    std::vector<MemTableIterator> iters;
    iters.push_back(m1->scan(Bound::unbounded(), Bound::unbounded()));
    iters.push_back(m2->scan(Bound::unbounded(), Bound::unbounded()));
    iters.push_back(m3->scan(Bound::unbounded(), Bound::unbounded()));

    auto merge_iter = MergeIterator<MemTableIterator>::create(std::move(iters));

    ASSERT_TRUE(merge_iter.is_valid());
    EXPECT_EQ(merge_iter.key(), KeySlice("k1"));
    EXPECT_EQ(merge_iter.value(), KeySlice("v1_new"));

    merge_iter.next();
    ASSERT_TRUE(merge_iter.is_valid());
    EXPECT_EQ(merge_iter.key(), KeySlice("k2"));
    EXPECT_EQ(merge_iter.value(), KeySlice(""));

    merge_iter.next();
    ASSERT_TRUE(merge_iter.is_valid());
    EXPECT_EQ(merge_iter.key(), KeySlice("k3"));
    EXPECT_EQ(merge_iter.value(), KeySlice("v3_mid"));

    merge_iter.next();
    ASSERT_TRUE(merge_iter.is_valid());
    EXPECT_EQ(merge_iter.key(), KeySlice("k4"));
    EXPECT_EQ(merge_iter.value(), KeySlice("v4_new"));

    merge_iter.next();
    ASSERT_TRUE(merge_iter.is_valid());
    EXPECT_EQ(merge_iter.key(), KeySlice("k5"));
    EXPECT_EQ(merge_iter.value(), KeySlice("v5_old"));

    merge_iter.next();
    EXPECT_FALSE(merge_iter.is_valid());
}

TEST(TwoMergeIteratorTest, MemTableAndSSTableShadowing) {
    const std::string path = "test_two_merge.sst";
    ::unlink(path.c_str());

    SsTableBuilder builder(64);
    builder.add(KeySlice("a"), KeySlice("sst_a"));
    builder.add(KeySlice("b"), KeySlice("sst_b"));
    builder.add(KeySlice("c"), KeySlice("sst_c"));
    auto sst = builder.build_for_test(path);

    auto mem = std::make_shared<MemTable>(1);
    mem->put("b", "mem_b_new");
    mem->put("d", "mem_d");

    auto mem_iter = mem->scan(Bound::unbounded(), Bound::unbounded());
    auto sst_iter = SsTableIterator::create_and_seek_to_first(sst);

    auto two_merge = TwoMergeIterator<MemTableIterator, SsTableIterator>::create(std::move(mem_iter), std::move(sst_iter));

    ASSERT_TRUE(two_merge.is_valid());
    EXPECT_EQ(two_merge.key(), KeySlice("a"));
    EXPECT_EQ(two_merge.value(), KeySlice("sst_a"));

    two_merge.next();
    ASSERT_TRUE(two_merge.is_valid());
    EXPECT_EQ(two_merge.key(), KeySlice("b"));
    EXPECT_EQ(two_merge.value(), KeySlice("mem_b_new"));

    two_merge.next();
    ASSERT_TRUE(two_merge.is_valid());
    EXPECT_EQ(two_merge.key(), KeySlice("c"));
    EXPECT_EQ(two_merge.value(), KeySlice("sst_c"));

    two_merge.next();
    ASSERT_TRUE(two_merge.is_valid());
    EXPECT_EQ(two_merge.key(), KeySlice("d"));
    EXPECT_EQ(two_merge.value(), KeySlice("mem_d"));

    two_merge.next();
    EXPECT_FALSE(two_merge.is_valid());

    ::unlink(path.c_str());
}

TEST(SstConcatIteratorTest, IterateAcross3SequentialSSTablesAndSeek) {
    const std::string p1 = "concat_1.sst";
    const std::string p2 = "concat_2.sst";
    const std::string p3 = "concat_3.sst";
    ::unlink(p1.c_str());
    ::unlink(p2.c_str());
    ::unlink(p3.c_str());

    SsTableBuilder b1(64);
    b1.add("k1", "v1");
    b1.add("k2", "v2");
    auto s1 = b1.build_for_test(p1);

    SsTableBuilder b2(64);
    b2.add("k3", "v3");
    b2.add("k4", "v4");
    auto s2 = b2.build_for_test(p2);

    SsTableBuilder b3(64);
    b3.add("k5", "v5");
    b3.add("k6", "v6");
    auto s3 = b3.build_for_test(p3);

    std::vector<std::shared_ptr<SsTable>> tables = {s1, s2, s3};

    // 1. Full sequential iteration across all 3 tables
    {
        auto iter = SstConcatIterator::create_and_seek_to_first(tables);
        std::vector<std::string> keys;
        while (iter.is_valid()) {
            std::string k(reinterpret_cast<const char*>(iter.key().raw_ref()), iter.key().len());
            keys.push_back(k);
            iter.next();
        }
        EXPECT_EQ(keys, (std::vector<std::string>{"k1", "k2", "k3", "k4", "k5", "k6"}));
    }

    // 2. Seek to boundary of table 2 (k3)
    {
        auto iter = SstConcatIterator::create_and_seek_to_key(tables, KeySlice("k3"));
        ASSERT_TRUE(iter.is_valid());
        EXPECT_EQ(iter.key(), KeySlice("k3"));
        iter.next();
        ASSERT_TRUE(iter.is_valid());
        EXPECT_EQ(iter.key(), KeySlice("k4"));
    }

    // 3. Seek to key between table 1 and table 2 (k2_5) -> should jump to k3
    {
        auto iter = SstConcatIterator::create_and_seek_to_key(tables, KeySlice("k2_5"));
        ASSERT_TRUE(iter.is_valid());
        EXPECT_EQ(iter.key(), KeySlice("k3"));
    }

    // 4. Seek to start of table 3 (k5)
    {
        auto iter = SstConcatIterator::create_and_seek_to_key(tables, KeySlice("k5"));
        ASSERT_TRUE(iter.is_valid());
        EXPECT_EQ(iter.key(), KeySlice("k5"));
    }

    // 5. Seek past all tables (k7)
    {
        auto iter = SstConcatIterator::create_and_seek_to_key(tables, KeySlice("k7"));
        EXPECT_FALSE(iter.is_valid());
    }

    ::unlink(p1.c_str());
    ::unlink(p2.c_str());
    ::unlink(p3.c_str());
}

TEST(LsmIteratorTest, TombstoneSkippingAndBoundChecking) {
    auto m1 = std::make_shared<MemTable>(1);
    m1->put("key1", "val1_new");
    m1->put("key2", ""); // Tombstone deletion shadowing key2
    m1->put("key4", ""); // Tombstone deletion shadowing key4
    m1->put("key5", "val5_new");

    auto m2 = std::make_shared<MemTable>(2);
    m2->put("key2", "val2_old");
    m2->put("key3", "val3_old");
    m2->put("key4", "val4_old");
    m2->put("key6", "val6_old");

    // 1. Unbounded scan with tombstone skipping (Risk #7)
    {
        auto iter1 = m1->scan(Bound::unbounded(), Bound::unbounded());
        auto iter2 = m2->scan(Bound::unbounded(), Bound::unbounded());
        auto two_merge = TwoMergeIterator<MemTableIterator, MemTableIterator>::create(std::move(iter1), std::move(iter2));
        auto lsm_iter = LsmIterator::create(std::move(two_merge), Bound::unbounded());

        std::vector<std::string> keys;
        std::vector<std::string> vals;
        while (lsm_iter.is_valid()) {
            std::string k(reinterpret_cast<const char*>(lsm_iter.key().raw_ref()), lsm_iter.key().len());
            std::string v(reinterpret_cast<const char*>(lsm_iter.value().raw_ref()), lsm_iter.value().len());
            keys.push_back(k);
            vals.push_back(v);
            lsm_iter.next();
        }
        EXPECT_EQ(keys, (std::vector<std::string>{"key1", "key3", "key5", "key6"}));
        EXPECT_EQ(vals, (std::vector<std::string>{"val1_new", "val3_old", "val5_new", "val6_old"}));
    }

    // 2. Bounded scan: Included bound <= key5
    {
        auto iter1 = m1->scan(Bound::unbounded(), Bound::unbounded());
        auto iter2 = m2->scan(Bound::unbounded(), Bound::unbounded());
        auto two_merge = TwoMergeIterator<MemTableIterator, MemTableIterator>::create(std::move(iter1), std::move(iter2));
        auto lsm_iter = LsmIterator::create(std::move(two_merge), Bound::included("key5"));

        std::vector<std::string> keys;
        while (lsm_iter.is_valid()) {
            std::string k(reinterpret_cast<const char*>(lsm_iter.key().raw_ref()), lsm_iter.key().len());
            keys.push_back(k);
            lsm_iter.next();
        }
        EXPECT_EQ(keys, (std::vector<std::string>{"key1", "key3", "key5"}));
    }

    // 3. Bounded scan: Excluded bound < key5
    {
        auto iter1 = m1->scan(Bound::unbounded(), Bound::unbounded());
        auto iter2 = m2->scan(Bound::unbounded(), Bound::unbounded());
        auto two_merge = TwoMergeIterator<MemTableIterator, MemTableIterator>::create(std::move(iter1), std::move(iter2));
        auto lsm_iter = LsmIterator::create(std::move(two_merge), Bound::excluded("key5"));

        std::vector<std::string> keys;
        while (lsm_iter.is_valid()) {
            std::string k(reinterpret_cast<const char*>(lsm_iter.key().raw_ref()), lsm_iter.key().len());
            keys.push_back(k);
            lsm_iter.next();
        }
        EXPECT_EQ(keys, (std::vector<std::string>{"key1", "key3"}));
    }
}

TEST(FusedIteratorTest, FusedIteratorBehavior) {
    auto m = std::make_shared<MemTable>(1);
    m->put("a", "1");
    auto iter = m->scan(Bound::unbounded(), Bound::unbounded());
    auto fused = FusedIterator::create(std::move(iter));

    ASSERT_TRUE(fused.is_valid());
    EXPECT_EQ(fused.key(), KeySlice("a"));
    fused.next();
    EXPECT_FALSE(fused.is_valid());

    // Calling next on an already invalid fused iterator should not panic or change validity
    fused.next();
    EXPECT_FALSE(fused.is_valid());
}
