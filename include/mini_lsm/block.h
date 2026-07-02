#ifndef MINI_LSM_BLOCK_H
#define MINI_LSM_BLOCK_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include "mini_lsm/key.h"

namespace mini_lsm {

struct Block {
    std::vector<uint8_t> data;
    std::vector<uint16_t> offsets;

    std::vector<uint8_t> encode() const;
    static Block decode(const uint8_t* raw, size_t len);
    static Block decode(const std::vector<uint8_t>& raw) {
        return decode(raw.data(), raw.size());
    }
};

class BlockBuilder {
public:
    explicit BlockBuilder(size_t block_size);

    bool add(KeySlice key, const uint8_t* value_ptr, size_t value_len);
    bool add(KeySlice key, KeySlice value) {
        return add(key, value.raw_ref(), value.len());
    }

    bool is_empty() const;
    Block build();

private:
    std::vector<uint16_t> offsets_;
    std::vector<uint8_t> data_;
    size_t block_size_;
    KeyVec first_key_;
};

class BlockIterator {
public:
    BlockIterator();
    explicit BlockIterator(std::shared_ptr<Block> block);

    static BlockIterator create_and_seek_to_first(std::shared_ptr<Block> block);
    static BlockIterator create_and_seek_to_key(std::shared_ptr<Block> block, KeySlice key);

    KeySlice key() const;
    KeySlice value() const;
    bool is_valid() const;
    void next();
    void seek_to_first();
    void seek_to_key(KeySlice key);
    void seek_to(size_t idx);

private:
    void seek_to_offset();

    std::shared_ptr<Block> block_;
    KeyVec key_;
    size_t value_start_{0};
    size_t value_len_{0};
    size_t idx_{0};
    KeyVec first_key_;
};

} // namespace mini_lsm

#endif // MINI_LSM_BLOCK_H
