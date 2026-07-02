#include "mini_lsm/block.h"
#include "mini_lsm/endian.h"
#include <cassert>

namespace mini_lsm {

std::vector<uint8_t> Block::encode() const {
    std::vector<uint8_t> buf = data;
    for (uint16_t offset : offsets) {
        put_u16_be(buf, offset);
    }
    put_u16_be(buf, static_cast<uint16_t>(offsets.size()));
    return buf;
}

Block Block::decode(const uint8_t* raw, size_t len) {
    assert(len >= 2);
    uint16_t num_entries = read_u16_be(raw + len - 2);
    size_t offsets_len = static_cast<size_t>(num_entries) * 2;
    size_t data_end = len - 2 - offsets_len;
    assert(data_end <= len - 2);

    Block block;
    block.data.assign(raw, raw + data_end);
    block.offsets.reserve(num_entries);
    const uint8_t* offsets_ptr = raw + data_end;
    for (uint16_t i = 0; i < num_entries; ++i) {
        block.offsets.push_back(read_u16_be(offsets_ptr + i * 2));
    }
    return block;
}

static size_t compute_overlap(KeySlice first_key, KeySlice key) {
    size_t i = 0;
    while (i < first_key.len() && i < key.len()) {
        if (first_key.raw_ref()[i] != key.raw_ref()[i]) {
            break;
        }
        i++;
    }
    return i;
}

BlockBuilder::BlockBuilder(size_t block_size)
    : block_size_(block_size) {}

bool BlockBuilder::add(KeySlice key, const uint8_t* value_ptr, size_t value_len) {
    assert(!key.is_empty());
    size_t estimated = 2 + offsets_.size() * 2 + data_.size();
    if (estimated + key.len() + value_len + 2 * 3 > block_size_ && !is_empty()) {
        return false;
    }
    offsets_.push_back(static_cast<uint16_t>(data_.size()));
    size_t overlap = compute_overlap(first_key_.as_key_slice(), key);
    put_u16_be(data_, static_cast<uint16_t>(overlap));
    put_u16_be(data_, static_cast<uint16_t>(key.len() - overlap));
    data_.insert(data_.end(), key.raw_ref() + overlap, key.raw_ref() + key.len());
    put_u16_be(data_, static_cast<uint16_t>(value_len));
    data_.insert(data_.end(), value_ptr, value_ptr + value_len);

    if (first_key_.is_empty()) {
        first_key_ = key.to_key_vec();
    }
    return true;
}

bool BlockBuilder::is_empty() const {
    return offsets_.empty();
}

Block BlockBuilder::build() {
    assert(!is_empty());
    Block block;
    block.data = std::move(data_);
    block.offsets = std::move(offsets_);
    return block;
}

} // namespace mini_lsm
