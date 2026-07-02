#ifndef MINI_LSM_TABLE_H
#define MINI_LSM_TABLE_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "mini_lsm/key.h"
#include "mini_lsm/block.h"

namespace mini_lsm {

class BlockCache;

struct Bloom {
    std::vector<uint8_t> filter;
    uint8_t k{0};

    static size_t bloom_bits_per_key(size_t entries, double false_positive_rate);
    static Bloom build_from_key_hashes(const std::vector<uint32_t>& hashes, size_t bits_per_key);
    bool may_contain(uint32_t h) const;

    void encode(std::vector<uint8_t>& buf) const;
    static Bloom decode(const uint8_t* raw, size_t len);
};

uint32_t farmhash_fingerprint32(KeySlice key);

struct BlockMeta {
    size_t offset;
    KeyVec first_key;
    KeyVec last_key;

    static void encode_block_meta(const std::vector<BlockMeta>& meta, std::vector<uint8_t>& buf);
    static std::vector<BlockMeta> decode_block_meta(const uint8_t* raw, size_t len);
};

class FileObject {
public:
    FileObject() = default;
    static FileObject create(const std::string& path, const std::vector<uint8_t>& data);
    static FileObject open(const std::string& path);

    void read_exact_at(uint8_t* buf, uint64_t len, uint64_t offset) const;
    std::vector<uint8_t> read(uint64_t offset, uint64_t len) const;
    uint64_t size() const;
    int fd() const;

private:
    explicit FileObject(int fd, uint64_t sz);
    std::shared_ptr<int> fd_ptr_;
    uint64_t size_{0};
};

struct SsTable {
    size_t id;
    FileObject file;
    KeyVec first_key;
    KeyVec last_key;
    std::vector<BlockMeta> block_meta;
    size_t block_meta_offset;
    std::shared_ptr<BlockCache> block_cache;
    std::shared_ptr<Bloom> bloom;
    size_t max_ts{0};

    static std::shared_ptr<SsTable> open(size_t id, std::shared_ptr<BlockCache> block_cache, FileObject file);
    std::shared_ptr<Block> read_block(size_t block_idx);
    std::shared_ptr<Block> read_block_cached(size_t block_idx);
    size_t find_block_idx(KeySlice key);
    size_t num_blocks() const { return block_meta.size(); }
    uint64_t table_size() const { return file.size(); }
};

class SsTableBuilder {
public:
    explicit SsTableBuilder(size_t block_size);

    void add(KeySlice key, KeySlice value);
    void add(KeySlice key, const uint8_t* value_ptr, size_t value_len) {
        add(key, KeySlice(value_ptr, value_len));
    }
    size_t estimated_size() const;
    bool is_empty() const { return meta_.empty() && builder_.is_empty(); }
    std::shared_ptr<SsTable> build(size_t id, std::shared_ptr<BlockCache> block_cache, const std::string& path);
    std::shared_ptr<SsTable> build_for_test(const std::string& path);

private:
    void finish_block();

    BlockBuilder builder_;
    KeyVec first_key_;
    KeyVec last_key_;
    std::vector<uint8_t> data_;
    std::vector<BlockMeta> meta_;
    size_t block_size_;
    std::vector<uint32_t> key_hashes_;
};

class SsTableIterator {
public:
    SsTableIterator();
    static SsTableIterator create_and_seek_to_first(std::shared_ptr<SsTable> table);
    static SsTableIterator create_and_seek_to_key(std::shared_ptr<SsTable> table, KeySlice key);

    KeySlice key() const;
    KeySlice value() const;
    bool is_valid() const;
    void next();

private:
    void seek_to_first();
    void seek_to_key(KeySlice key);
    void seek_to_idx(size_t idx);

    std::shared_ptr<SsTable> table_;
    BlockIterator blk_iter_;
    size_t blk_idx_{0};
};

} // namespace mini_lsm

#endif // MINI_LSM_TABLE_H
