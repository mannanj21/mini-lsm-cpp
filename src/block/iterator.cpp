#include "mini_lsm/block.h"
#include "mini_lsm/endian.h"
#include <cassert>

namespace mini_lsm {

BlockIterator::BlockIterator()
    : block_(nullptr), idx_(0) {}

BlockIterator::BlockIterator(std::shared_ptr<Block> block)
    : block_(std::move(block)), idx_(0) {
    if (block_ && !block_->offsets.empty()) {
        const uint8_t* ptr = block_->data.data();
        uint16_t overlap = read_u16_be(ptr);
        (void)overlap;
        uint16_t key_len = read_u16_be(ptr + 2);
        first_key_.clear();
        first_key_.append(ptr + 4, key_len);
    }
}

BlockIterator BlockIterator::create_and_seek_to_first(std::shared_ptr<Block> block) {
    BlockIterator iter(std::move(block));
    iter.seek_to_first();
    return iter;
}

BlockIterator BlockIterator::create_and_seek_to_key(std::shared_ptr<Block> block, KeySlice target) {
    BlockIterator iter(std::move(block));
    iter.seek_to_key(target);
    return iter;
}

KeySlice BlockIterator::key() const {
    assert(!key_.is_empty());
    return key_.as_key_slice();
}

KeySlice BlockIterator::value() const {
    assert(!key_.is_empty());
    return KeySlice(block_->data.data() + value_start_, value_len_);
}

bool BlockIterator::is_valid() const {
    return !key_.is_empty();
}

void BlockIterator::next() {
    idx_++;
    seek_to(idx_);
}

void BlockIterator::seek_to_first() {
    seek_to(0);
}

void BlockIterator::seek_to(size_t idx) {
    if (!block_ || idx >= block_->offsets.size()) {
        key_.clear();
        value_start_ = 0;
        value_len_ = 0;
        return;
    }
    idx_ = idx;
    seek_to_offset();
}

void BlockIterator::seek_to_offset() {
    size_t offset = block_->offsets[idx_];
    const uint8_t* ptr = block_->data.data() + offset;
    uint16_t overlap_len = read_u16_be(ptr);
    uint16_t key_len = read_u16_be(ptr + 2);
    ptr += 4;
    key_.clear();
    key_.append(first_key_.raw_ref(), overlap_len);
    key_.append(ptr, key_len);
    ptr += key_len;
    uint16_t val_len = read_u16_be(ptr);
    value_start_ = static_cast<size_t>((ptr + 2) - block_->data.data());
    value_len_ = val_len;
}

void BlockIterator::seek_to_key(KeySlice target) {
    if (!block_) return;
    size_t low = 0;
    size_t high = block_->offsets.size();
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        seek_to(mid);
        assert(is_valid());
        int cmp = key().compare(target);
        if (cmp < 0) {
            low = mid + 1;
        } else if (cmp > 0) {
            high = mid;
        } else {
            return;
        }
    }
    seek_to(low);
}

} // namespace mini_lsm
