#ifndef MINI_LSM_BLOCK_CACHE_H
#define MINI_LSM_BLOCK_CACHE_H

#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include "mini_lsm/block.h"

namespace mini_lsm {

class BlockCache {
public:
    explicit BlockCache(size_t capacity);

    std::shared_ptr<Block> get(size_t sst_id, size_t block_idx);
    void insert(size_t sst_id, size_t block_idx, std::shared_ptr<Block> block);

    template <typename Func>
    std::shared_ptr<Block> try_get_with(size_t sst_id, size_t block_idx, Func loader) {
        if (auto blk = get(sst_id, block_idx)) {
            return blk;
        }
        auto blk = loader();
        if (blk) {
            insert(sst_id, block_idx, blk);
        }
        return blk;
    }

    size_t size() const;
    size_t capacity() const { return capacity_; }

private:
    using Key = std::pair<size_t, size_t>;
    struct PairHash {
        size_t operator()(const Key& k) const {
            return std::hash<size_t>()(k.first) ^ (std::hash<size_t>()(k.second) + 0x9e3779b9 + (std::hash<size_t>()(k.first) << 6) + (std::hash<size_t>()(k.first) >> 2));
        }
    };

    using ListEntry = std::pair<Key, std::shared_ptr<Block>>;
    using List = std::list<ListEntry>;

    size_t capacity_;
    mutable std::mutex mu_;
    List list_;
    std::unordered_map<Key, List::iterator, PairHash> map_;
};

} // namespace mini_lsm

#endif // MINI_LSM_BLOCK_CACHE_H
