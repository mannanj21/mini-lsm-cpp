#include <gtest/gtest.h>
#include <unistd.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include "mini_lsm/mem_table.h"
#include "mini_lsm/table.h"

using namespace mini_lsm;

TEST(MemTableTest, ConcurrentPutAndGet10Writers10Readers) {
    auto memtable = std::make_shared<MemTable>(0);
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;

    const int num_writers = 10;
    const int num_readers = 10;
    const int items_per_writer = 100;

    for (int w = 0; w < num_writers; ++w) {
        threads.emplace_back([memtable, &start, w]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < items_per_writer; ++i) {
                char kbuf[32];
                char vbuf[32];
                snprintf(kbuf, sizeof(kbuf), "w%02d_k%04d", w, i);
                snprintf(vbuf, sizeof(vbuf), "val_%02d_%04d", w, i);
                memtable->put(KeySlice(kbuf), KeySlice(vbuf));
            }
        });
    }

    for (int r = 0; r < num_readers; ++r) {
        threads.emplace_back([memtable, &start, r]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < 200; ++i) {
                char kbuf[32];
                snprintf(kbuf, sizeof(kbuf), "w%02d_k%04d", r % num_writers, i % items_per_writer);
                auto val = memtable->get(KeySlice(kbuf));
                (void)val;
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_FALSE(memtable->is_empty());
    EXPECT_GT(memtable->approximate_size(), 0);

    for (int w = 0; w < num_writers; ++w) {
        for (int i = 0; i < items_per_writer; ++i) {
            char kbuf[32];
            char vbuf[32];
            snprintf(kbuf, sizeof(kbuf), "w%02d_k%04d", w, i);
            snprintf(vbuf, sizeof(vbuf), "val_%02d_%04d", w, i);
            auto val = memtable->get(KeySlice(kbuf));
            ASSERT_TRUE(val.has_value());
            EXPECT_EQ(val.value(), std::string(vbuf));
        }
    }
}

TEST(MemTableIteratorTest, RangeScanningBoundaryConditions) {
    auto memtable = std::make_shared<MemTable>(1);
    memtable->put("key_10", "val_10");
    memtable->put("key_20", "val_20");
    memtable->put("key_30", "val_30");
    memtable->put("key_40", "val_40");
    memtable->put("key_50", "val_50");

    // 1. Unbounded scan
    {
        auto iter = memtable->scan(Bound::unbounded(), Bound::unbounded());
        std::vector<std::string> keys;
        while (iter.is_valid()) {
            std::string k(reinterpret_cast<const char*>(iter.key().raw_ref()), iter.key().len());
            keys.push_back(k);
            iter.next();
        }
        EXPECT_EQ(keys, (std::vector<std::string>{"key_10", "key_20", "key_30", "key_40", "key_50"}));
    }

    // 2. Inclusive lower, Inclusive upper [key_20, key_40]
    {
        auto iter = memtable->scan(Bound::included("key_20"), Bound::included("key_40"));
        std::vector<std::string> keys;
        while (iter.is_valid()) {
            std::string k(reinterpret_cast<const char*>(iter.key().raw_ref()), iter.key().len());
            keys.push_back(k);
            iter.next();
        }
        EXPECT_EQ(keys, (std::vector<std::string>{"key_20", "key_30", "key_40"}));
    }

    // 3. Exclusive lower, Exclusive upper (key_20, key_40)
    {
        auto iter = memtable->scan(Bound::excluded("key_20"), Bound::excluded("key_40"));
        std::vector<std::string> keys;
        while (iter.is_valid()) {
            std::string k(reinterpret_cast<const char*>(iter.key().raw_ref()), iter.key().len());
            keys.push_back(k);
            iter.next();
        }
        EXPECT_EQ(keys, (std::vector<std::string>{"key_30"}));
    }

    // 4. Empty range scan [key_25, key_26]
    {
        auto iter = memtable->scan(Bound::included("key_25"), Bound::included("key_26"));
        EXPECT_FALSE(iter.is_valid());
    }

    // 5. Scan past end [key_99, unbounded)
    {
        auto iter = memtable->scan(Bound::included("key_99"), Bound::unbounded());
        EXPECT_FALSE(iter.is_valid());
    }
}

TEST(MemTableTest, FlushProducesValidSstRoundTrip) {
    const std::string path = "test_memtable_flush_roundtrip.sst";
    ::unlink(path.c_str());

    auto memtable = std::make_shared<MemTable>(10);
    const int N = 60;
    for (int i = 0; i < N; ++i) {
        char kbuf[64];
        char vbuf[64];
        // Shared prefix to exercise prefix compression on the SST side
        snprintf(kbuf, sizeof(kbuf), "prefix_common_key_%03d", i);
        if (i == 15) {
            // Include a tombstone (empty string value represents deletion)
            memtable->put(KeySlice(kbuf), KeySlice(""));
        } else {
            snprintf(vbuf, sizeof(vbuf), "value_content_%03d", i);
            memtable->put(KeySlice(kbuf), KeySlice(vbuf));
        }
    }

    SsTableBuilder builder(32); // small block size guaranteeing multiple blocks
    memtable->flush(builder);

    auto table = builder.build_for_test(path);
    ASSERT_NE(table, nullptr);
    EXPECT_GT(table->num_blocks(), 1); // verify more than one block

    auto file = FileObject::open(path);
    auto reopened_table = SsTable::open(10, nullptr, std::move(file));
    ASSERT_NE(reopened_table, nullptr);

    auto sst_iter = SsTableIterator::create_and_seek_to_first(reopened_table);
    for (int i = 0; i < N; ++i) {
        ASSERT_TRUE(sst_iter.is_valid());
        char kbuf[64];
        snprintf(kbuf, sizeof(kbuf), "prefix_common_key_%03d", i);
        std::string ks(kbuf);
        EXPECT_EQ(sst_iter.key(), KeySlice(ks));
        if (i == 15) {
            EXPECT_EQ(sst_iter.value(), KeySlice(""));
        } else {
            char vbuf[64];
            snprintf(vbuf, sizeof(vbuf), "value_content_%03d", i);
            std::string vs(vbuf);
            EXPECT_EQ(sst_iter.value(), KeySlice(vs));
        }
        sst_iter.next();
    }
    EXPECT_FALSE(sst_iter.is_valid());

    ::unlink(path.c_str());
}
