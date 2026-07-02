#include "mini_lsm/table.h"
#include "mini_lsm/block_cache.h"
#include "mini_lsm/endian.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace mini_lsm {

FileObject::FileObject(int fd, uint64_t sz)
    : fd_ptr_(new int(fd), [](int* p) {
          if (p && *p >= 0) {
              ::close(*p);
          }
          delete p;
      }),
      size_(sz) {}

FileObject FileObject::create(const std::string& path, const std::vector<uint8_t>& data) {
    int fd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        throw std::runtime_error("Failed to create file: " + path);
    }
    size_t written = 0;
    while (written < data.size()) {
        ssize_t ret = ::write(fd, data.data() + written, data.size() - written);
        if (ret < 0) {
            ::close(fd);
            throw std::runtime_error("Failed to write data to file: " + path);
        }
        written += ret;
    }
    ::close(fd);
    return open(path);
}

FileObject FileObject::open(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    struct stat st;
    if (::fstat(fd, &st) < 0) {
        ::close(fd);
        throw std::runtime_error("Failed to stat file: " + path);
    }
    return FileObject(fd, static_cast<uint64_t>(st.st_size));
}

void FileObject::read_exact_at(uint8_t* buf, uint64_t len, uint64_t offset) const {
    int file_fd = this->fd();
    if (file_fd < 0) {
        throw std::runtime_error("FileObject not open");
    }
    uint64_t read_bytes = 0;
    while (read_bytes < len) {
        ssize_t ret = ::pread(file_fd, buf + read_bytes, len - read_bytes, offset + read_bytes);
        if (ret < 0) {
            throw std::runtime_error("pread failed");
        }
        if (ret == 0) {
            throw std::runtime_error("Unexpected EOF during pread");
        }
        read_bytes += ret;
    }
}

std::vector<uint8_t> FileObject::read(uint64_t offset, uint64_t len) const {
    std::vector<uint8_t> res(len);
    if (len > 0) {
        read_exact_at(res.data(), len, offset);
    }
    return res;
}

uint64_t FileObject::size() const {
    return size_;
}

int FileObject::fd() const {
    return fd_ptr_ ? *fd_ptr_ : -1;
}

void BlockMeta::encode_block_meta(const std::vector<BlockMeta>& meta, std::vector<uint8_t>& buf) {
    size_t orig_len = buf.size();
    put_u32_be(buf, static_cast<uint32_t>(meta.size()));
    for (const auto& m : meta) {
        put_u32_be(buf, static_cast<uint32_t>(m.offset));
        put_u16_be(buf, static_cast<uint16_t>(m.first_key.len()));
        buf.insert(buf.end(), m.first_key.raw_ref(), m.first_key.raw_ref() + m.first_key.len());
        put_u16_be(buf, static_cast<uint16_t>(m.last_key.len()));
        buf.insert(buf.end(), m.last_key.raw_ref(), m.last_key.raw_ref() + m.last_key.len());
    }
    uint32_t checksum = crc32_hash(buf.data() + orig_len + 4, buf.size() - orig_len - 4);
    put_u32_be(buf, checksum);
}

std::vector<BlockMeta> BlockMeta::decode_block_meta(const uint8_t* raw, size_t len) {
    assert(len >= 8);
    uint32_t num = read_u32_be(raw);
    uint32_t expected_checksum = read_u32_be(raw + len - 4);
    uint32_t actual_checksum = crc32_hash(raw + 4, len - 8);
    if (expected_checksum != actual_checksum) {
        throw std::runtime_error("meta checksum mismatched");
    }
    std::vector<BlockMeta> meta;
    const uint8_t* ptr = raw + 4;
    for (uint32_t i = 0; i < num; ++i) {
        uint32_t offset = read_u32_be(ptr);
        ptr += 4;
        uint16_t fk_len = read_u16_be(ptr);
        ptr += 2;
        KeyVec fk = KeyVec::from_slice(ptr, fk_len);
        ptr += fk_len;
        uint16_t lk_len = read_u16_be(ptr);
        ptr += 2;
        KeyVec lk = KeyVec::from_slice(ptr, lk_len);
        ptr += lk_len;
        meta.push_back(BlockMeta{offset, std::move(fk), std::move(lk)});
    }
    return meta;
}

std::shared_ptr<SsTable> SsTable::open(size_t id, std::shared_ptr<BlockCache> block_cache, FileObject file) {
    uint64_t file_len = file.size();
    auto raw_bloom_offset = file.read(file_len - 4, 4);
    uint32_t bloom_offset = read_u32_be(raw_bloom_offset.data());
    auto raw_bloom = file.read(bloom_offset, file_len - 4 - bloom_offset);
    auto bloom = std::make_shared<Bloom>(Bloom::decode(raw_bloom.data(), raw_bloom.size()));

    auto raw_meta_offset = file.read(bloom_offset - 4, 4);
    uint32_t block_meta_offset = read_u32_be(raw_meta_offset.data());
    auto raw_meta = file.read(block_meta_offset, bloom_offset - 4 - block_meta_offset);
    auto block_meta = BlockMeta::decode_block_meta(raw_meta.data(), raw_meta.size());

    auto table = std::make_shared<SsTable>();
    table->id = id;
    table->file = std::move(file);
    table->first_key = block_meta.front().first_key;
    table->last_key = block_meta.back().last_key;
    table->block_meta = std::move(block_meta);
    table->block_meta_offset = block_meta_offset;
    table->block_cache = std::move(block_cache);
    table->bloom = std::move(bloom);
    table->max_ts = 0;
    return table;
}

std::shared_ptr<Block> SsTable::read_block(size_t block_idx) {
    assert(block_idx < block_meta.size());
    size_t offset = block_meta[block_idx].offset;
    size_t offset_end = (block_idx + 1 < block_meta.size()) ? block_meta[block_idx + 1].offset : block_meta_offset;
    size_t block_len = offset_end - offset - 4;
    auto data_with_chksum = file.read(offset, offset_end - offset);
    uint32_t expected_checksum = read_u32_be(data_with_chksum.data() + block_len);
    uint32_t actual_checksum = crc32_hash(data_with_chksum.data(), block_len);
    if (expected_checksum != actual_checksum) {
        throw std::runtime_error("block checksum mismatched");
    }
    return std::make_shared<Block>(Block::decode(data_with_chksum.data(), block_len));
}

std::shared_ptr<Block> SsTable::read_block_cached(size_t block_idx) {
    if (block_cache) {
        auto loader = [this, block_idx]() { return read_block(block_idx); };
        return block_cache->try_get_with(id, block_idx, loader);
    }
    return read_block(block_idx);
}

size_t SsTable::find_block_idx(KeySlice key) {
    auto it = std::upper_bound(block_meta.begin(), block_meta.end(), key,
        [](KeySlice k, const BlockMeta& meta) {
            return k < meta.first_key.as_key_slice();
        });
    size_t idx = std::distance(block_meta.begin(), it);
    return idx > 0 ? idx - 1 : 0;
}

SsTableBuilder::SsTableBuilder(size_t block_size)
    : builder_(block_size), block_size_(block_size) {}

void SsTableBuilder::add(KeySlice key, KeySlice value) {
    if (first_key_.is_empty()) {
        first_key_ = KeyVec::from_slice(key.raw_ref(), key.len());
    }
    key_hashes_.push_back(farmhash_fingerprint32(key));

    if (builder_.add(key, value)) {
        last_key_ = KeyVec::from_slice(key.raw_ref(), key.len());
        return;
    }

    finish_block();

    assert(builder_.add(key, value));
    first_key_ = KeyVec::from_slice(key.raw_ref(), key.len());
    last_key_ = KeyVec::from_slice(key.raw_ref(), key.len());
}

size_t SsTableBuilder::estimated_size() const {
    return data_.size();
}

void SsTableBuilder::finish_block() {
    if (builder_.is_empty()) {
        return;
    }
    BlockBuilder new_builder(block_size_);
    std::swap(builder_, new_builder);
    auto encoded_block = new_builder.build().encode();

    meta_.push_back(BlockMeta{
        data_.size(),
        std::move(first_key_),
        std::move(last_key_)
    });

    uint32_t checksum = crc32_hash(encoded_block.data(), encoded_block.size());
    data_.insert(data_.end(), encoded_block.begin(), encoded_block.end());
    put_u32_be(data_, checksum);
}

std::shared_ptr<SsTable> SsTableBuilder::build(size_t id, std::shared_ptr<BlockCache> block_cache, const std::string& path) {
    finish_block();
    std::vector<uint8_t> buf = data_;
    size_t meta_offset = buf.size();
    BlockMeta::encode_block_meta(meta_, buf);
    put_u32_be(buf, static_cast<uint32_t>(meta_offset));

    Bloom bloom = Bloom::build_from_key_hashes(key_hashes_, Bloom::bloom_bits_per_key(key_hashes_.size(), 0.01));
    size_t bloom_offset = buf.size();
    bloom.encode(buf);
    put_u32_be(buf, static_cast<uint32_t>(bloom_offset));

    FileObject file = FileObject::create(path, buf);
    return SsTable::open(id, std::move(block_cache), std::move(file));
}

std::shared_ptr<SsTable> SsTableBuilder::build_for_test(const std::string& path) {
    return build(0, nullptr, path);
}

} // namespace mini_lsm
