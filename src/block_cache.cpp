#include "mini_lsm/block_cache.h"

namespace mini_lsm {

BlockCache::BlockCache(size_t capacity)
    : capacity_(capacity) {}

std::shared_ptr<Block> BlockCache::get(size_t sst_id, size_t block_idx) {
    std::lock_guard<std::mutex> lock(mu_);
    Key key{sst_id, block_idx};
    auto it = map_.find(key);
    if (it == map_.end()) {
        return nullptr;
    }
    list_.splice(list_.begin(), list_, it->second);
    return it->second->second;
}

void BlockCache::insert(size_t sst_id, size_t block_idx, std::shared_ptr<Block> block) {
    if (capacity_ == 0 || !block) {
        return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    Key key{sst_id, block_idx};
    auto it = map_.find(key);
    if (it != map_.end()) {
        it->second->second = std::move(block);
        list_.splice(list_.begin(), list_, it->second);
        return;
    }
    if (map_.size() >= capacity_) {
        auto last = list_.back();
        map_.erase(last.first);
        list_.pop_back();
    }
    list_.push_front({key, std::move(block)});
    map_[key] = list_.begin();
}

size_t BlockCache::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return map_.size();
}

} // namespace mini_lsm
