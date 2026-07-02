#include "mini_lsm/lsm_iterator.h"

namespace mini_lsm {

LsmIterator::LsmIterator(std::unique_ptr<StorageIterator> inner, Bound end_bound)
    : inner_(std::move(inner)), end_bound_(std::move(end_bound)) {
    if (inner_ && inner_->is_valid()) {
        is_valid_ = true;
        match_end_bound();
        move_to_non_delete();
    } else {
        is_valid_ = false;
    }
}

KeySlice LsmIterator::key() const {
    assert(is_valid_);
    return inner_->key();
}

KeySlice LsmIterator::value() const {
    assert(is_valid_);
    return inner_->value();
}

bool LsmIterator::is_valid() const {
    return is_valid_;
}

void LsmIterator::next() {
    if (!is_valid_) return;
    next_inner();
    move_to_non_delete();
}

void LsmIterator::next_inner() {
    inner_->next();
    if (!inner_->is_valid()) {
        is_valid_ = false;
        return;
    }
    match_end_bound();
}

void LsmIterator::match_end_bound() {
    if (!is_valid_) return;
    switch (end_bound_.type) {
        case BoundType::Unbounded:
            break;
        case BoundType::Included:
            if (inner_->key() > end_bound_.key) {
                is_valid_ = false;
            }
            break;
        case BoundType::Excluded:
            if (inner_->key() >= end_bound_.key) {
                is_valid_ = false;
            }
            break;
    }
}

void LsmIterator::move_to_non_delete() {
    while (is_valid_ && inner_->value().is_empty()) {
        next_inner();
    }
}

} // namespace mini_lsm
