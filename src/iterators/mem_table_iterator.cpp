#include "mini_lsm/mem_table.h"
#include <cassert>

namespace mini_lsm {

MemTableIterator::MemTableIterator(std::shared_ptr<const MemTable> table)
    : table_(table), idx_(0) {
    if (table_) {
        std::shared_lock<std::shared_mutex> lock(table_->mu_);
        for (const auto& entry : table_->map_) {
            snapshot_.push_back(entry);
        }
    }
}

MemTableIterator::MemTableIterator(std::shared_ptr<const MemTable> table, Bound lower, Bound upper)
    : table_(table), idx_(0) {
    if (!table_) {
        return;
    }
    std::shared_lock<std::shared_mutex> lock(table_->mu_);
    auto it = table_->map_.begin();

    if (lower.type == BoundType::Included) {
        std::string l(reinterpret_cast<const char*>(lower.key.raw_ref()), lower.key.len());
        it = table_->map_.lower_bound(l);
    } else if (lower.type == BoundType::Excluded) {
        std::string l(reinterpret_cast<const char*>(lower.key.raw_ref()), lower.key.len());
        it = table_->map_.upper_bound(l);
    }

    for (; it != table_->map_.end(); ++it) {
        KeySlice k(it->first);
        if (upper.type == BoundType::Included) {
            if (k > upper.key) {
                break;
            }
        } else if (upper.type == BoundType::Excluded) {
            if (k >= upper.key) {
                break;
            }
        }
        snapshot_.push_back(*it);
    }
}

KeySlice MemTableIterator::key() const {
    assert(is_valid());
    return KeySlice(snapshot_[idx_].first);
}

KeySlice MemTableIterator::value() const {
    assert(is_valid());
    return KeySlice(snapshot_[idx_].second);
}

bool MemTableIterator::is_valid() const {
    return idx_ < snapshot_.size();
}

void MemTableIterator::next() {
    if (is_valid()) {
        idx_++;
    }
}

} // namespace mini_lsm
