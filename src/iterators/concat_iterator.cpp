#include "mini_lsm/iterators.h"
#include "mini_lsm/table.h"
#include <cassert>

namespace mini_lsm {

void SstConcatIterator::check_sst_valid() {
    for (const auto& sst : sstables_) {
        assert(sst->first_key.as_slice() <= sst->last_key.as_slice());
    }
    if (!sstables_.empty()) {
        for (size_t i = 0; i + 1 < sstables_.size(); ++i) {
            assert(sstables_[i]->last_key.as_slice() < sstables_[i + 1]->first_key.as_slice());
        }
    }
}

void SstConcatIterator::move_until_valid() {
    while (current_.has_value()) {
        if (current_->is_valid()) {
            break;
        }
        if (next_sst_idx_ >= sstables_.size()) {
            current_.reset();
        } else {
            current_ = SsTableIterator::create_and_seek_to_first(sstables_[next_sst_idx_]);
            next_sst_idx_++;
        }
    }
}

SstConcatIterator SstConcatIterator::create_and_seek_to_first(std::vector<std::shared_ptr<SsTable>> sstables) {
    SstConcatIterator iter;
    iter.sstables_ = std::move(sstables);
    iter.check_sst_valid();
    if (iter.sstables_.empty()) {
        iter.next_sst_idx_ = 0;
        return iter;
    }
    iter.current_ = SsTableIterator::create_and_seek_to_first(iter.sstables_[0]);
    iter.next_sst_idx_ = 1;
    iter.move_until_valid();
    return iter;
}

SstConcatIterator SstConcatIterator::create_and_seek_to_key(std::vector<std::shared_ptr<SsTable>> sstables, KeySlice key) {
    SstConcatIterator iter;
    iter.sstables_ = std::move(sstables);
    iter.check_sst_valid();

    // Find partition point: first index where sstables[idx]->first_key > key
    size_t part = 0;
    while (part < iter.sstables_.size() && iter.sstables_[part]->first_key.as_slice() <= key) {
        part++;
    }
    size_t idx = part > 0 ? part - 1 : 0;
    if (idx >= iter.sstables_.size()) {
        iter.next_sst_idx_ = iter.sstables_.size();
        return iter;
    }

    iter.current_ = SsTableIterator::create_and_seek_to_key(iter.sstables_[idx], key);
    iter.next_sst_idx_ = idx + 1;
    iter.move_until_valid();
    return iter;
}

KeySlice SstConcatIterator::key() const {
    assert(is_valid());
    return current_->key();
}

KeySlice SstConcatIterator::value() const {
    assert(is_valid());
    return current_->value();
}

bool SstConcatIterator::is_valid() const {
    return current_.has_value() && current_->is_valid();
}

void SstConcatIterator::next() {
    assert(is_valid());
    current_->next();
    move_until_valid();
}

} // namespace mini_lsm
