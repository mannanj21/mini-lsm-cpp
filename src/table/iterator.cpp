#include "mini_lsm/table.h"
#include <cassert>

namespace mini_lsm {

SsTableIterator::SsTableIterator()
    : table_(nullptr), blk_idx_(0) {}

SsTableIterator SsTableIterator::create_and_seek_to_first(std::shared_ptr<SsTable> table) {
    SsTableIterator iter;
    iter.table_ = std::move(table);
    iter.seek_to_first();
    return iter;
}

SsTableIterator SsTableIterator::create_and_seek_to_key(std::shared_ptr<SsTable> table, KeySlice key) {
    SsTableIterator iter;
    iter.table_ = std::move(table);
    iter.seek_to_key(key);
    return iter;
}

KeySlice SsTableIterator::key() const {
    assert(is_valid());
    return blk_iter_.key();
}

KeySlice SsTableIterator::value() const {
    assert(is_valid());
    return blk_iter_.value();
}

bool SsTableIterator::is_valid() const {
    return blk_iter_.is_valid();
}

void SsTableIterator::next() {
    blk_iter_.next();
    if (!blk_iter_.is_valid()) {
        blk_idx_++;
        if (blk_idx_ < table_->num_blocks()) {
            seek_to_idx(blk_idx_);
        }
    }
}

void SsTableIterator::seek_to_first() {
    if (!table_ || table_->num_blocks() == 0) {
        blk_iter_ = BlockIterator();
        return;
    }
    blk_idx_ = 0;
    auto blk = table_->read_block_cached(0);
    blk_iter_ = BlockIterator::create_and_seek_to_first(blk);
}

void SsTableIterator::seek_to_key(KeySlice key) {
    if (!table_ || table_->num_blocks() == 0) {
        blk_iter_ = BlockIterator();
        return;
    }
    blk_idx_ = table_->find_block_idx(key);
    auto blk = table_->read_block_cached(blk_idx_);
    blk_iter_ = BlockIterator::create_and_seek_to_key(blk, key);
    if (!blk_iter_.is_valid()) {
        blk_idx_++;
        if (blk_idx_ < table_->num_blocks()) {
            blk = table_->read_block_cached(blk_idx_);
            blk_iter_ = BlockIterator::create_and_seek_to_first(blk);
        }
    }
}

void SsTableIterator::seek_to_idx(size_t idx) {
    if (!table_ || idx >= table_->num_blocks()) {
        blk_iter_ = BlockIterator();
        return;
    }
    blk_idx_ = idx;
    auto blk = table_->read_block_cached(idx);
    blk_iter_ = BlockIterator::create_and_seek_to_first(blk);
}

} // namespace mini_lsm
