#include <gtest/gtest.h>
#include <memory>
#include "mini_lsm/block_cache.h"

using namespace mini_lsm;

TEST(BlockCacheTest, LRUEvictionAndMRUBump) {
    const size_t C = 3;
    BlockCache cache(C);

    auto blk1 = std::make_shared<Block>();
    auto blk2 = std::make_shared<Block>();
    auto blk3 = std::make_shared<Block>();
    auto blk4 = std::make_shared<Block>();

    // Insert 3 blocks (capacity C)
    cache.insert(1, 10, blk1);
    cache.insert(1, 20, blk2);
    cache.insert(1, 30, blk3);
    EXPECT_EQ(cache.size(), 3);

    // Verify `get` bumps item to MRU: get blk1 (1, 10) so it becomes MRU. Order from MRU to LRU is now: blk1, blk3, blk2.
    auto retrieved = cache.get(1, 10);
    EXPECT_EQ(retrieved, blk1);

    // Insert C+1 (blk4). Since blk2 (1, 20) is now LRU, blk2 should be evicted.
    cache.insert(1, 40, blk4);
    EXPECT_EQ(cache.size(), 3);

    EXPECT_NE(cache.get(1, 10), nullptr); // blk1 still present
    EXPECT_NE(cache.get(1, 30), nullptr); // blk3 still present
    EXPECT_NE(cache.get(1, 40), nullptr); // blk4 still present
    EXPECT_EQ(cache.get(1, 20), nullptr); // blk2 evicted!
}
