#include "mini_lsm/wal.h"
#include "mini_lsm/endian.h"
#include <unistd.h>
#include <sys/types.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

namespace mini_lsm {

Wal::Wal(const std::string& path, FILE* fp) : path_(path), fp_(fp) {}

Wal::~Wal() {
    std::lock_guard<std::mutex> lock(mu_);
    if (fp_) {
        std::fflush(fp_);
        std::fclose(fp_);
        fp_ = nullptr;
    }
}

std::variant<std::shared_ptr<Wal>, std::string> Wal::create(const std::string& path) {
    FILE* fp = std::fopen(path.c_str(), "wb+");
    if (!fp) {
        return "Failed to create WAL file: " + path;
    }
    return std::shared_ptr<Wal>(new Wal(path, fp));
}

std::variant<std::shared_ptr<Wal>, std::string> Wal::recover(
    const std::string& path,
    std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& recovered_records)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        // If file doesn't exist, create new
        return create(path);
    }

    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    size_t pos = 0;
    while (pos < buf.size()) {
        if (buf.size() - pos < 2) {
            std::cerr << "WAL recovery warning: partial frame header at offset " << pos << ", skipping/truncating.\n";
            break;
        }
        uint16_t key_len = read_u16_be(&buf[pos]);
        if (buf.size() - pos < 2 + static_cast<size_t>(key_len) + 2) {
            std::cerr << "WAL recovery warning: partial frame key at offset " << pos << ", skipping/truncating.\n";
            break;
        }
        uint16_t val_len = read_u16_be(&buf[pos + 2 + key_len]);
        size_t frame_content_len = 2 + static_cast<size_t>(key_len) + 2 + static_cast<size_t>(val_len);
        if (buf.size() - pos < frame_content_len + 4) {
            std::cerr << "WAL recovery warning: partial frame value/crc at offset " << pos << ", skipping/truncating.\n";
            break;
        }

        uint32_t computed_crc = crc32_hash(&buf[pos], frame_content_len);
        uint32_t expected_crc = read_u32_be(&buf[pos + frame_content_len]);
        if (computed_crc != expected_crc) {
            std::cerr << "WAL recovery warning: CRC32 mismatch at offset " << pos << " (expected " 
                      << expected_crc << ", got " << computed_crc << "), skipping/truncating.\n";
            break;
        }

        std::vector<uint8_t> key(&buf[pos + 2], &buf[pos + 2 + key_len]);
        std::vector<uint8_t> val(&buf[pos + 2 + key_len + 2], &buf[pos + 2 + key_len + 2 + val_len]);
        recovered_records.emplace_back(std::move(key), std::move(val));

        pos += frame_content_len + 4;
    }

    // Truncate file to valid intact length `pos` per Risk #2
    if (pos != buf.size()) {
        ::truncate(path.c_str(), pos);
    }

    FILE* fp = std::fopen(path.c_str(), "ab+");
    if (!fp) {
        return "Failed to open WAL for append after recovery: " + path;
    }
    return std::shared_ptr<Wal>(new Wal(path, fp));
}

std::variant<std::monostate, std::string> Wal::put(KeySlice key, KeySlice value) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!fp_) {
        return "WAL file pointer is null";
    }

    std::vector<uint8_t> frame;
    put_u16_be(frame, static_cast<uint16_t>(key.len()));
    frame.insert(frame.end(), key.raw_ref(), key.raw_ref() + key.len());
    put_u16_be(frame, static_cast<uint16_t>(value.len()));
    frame.insert(frame.end(), value.raw_ref(), value.raw_ref() + value.len());

    uint32_t crc = crc32_hash(frame.data(), frame.size());
    put_u32_be(frame, crc);

    size_t written = std::fwrite(frame.data(), 1, frame.size(), fp_);
    if (written != frame.size()) {
        return "Failed to write frame to WAL";
    }
    return std::monostate{};
}

std::variant<std::monostate, std::string> Wal::put_batch(const std::vector<std::pair<KeySlice, KeySlice>>& batch) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!fp_) {
        return "WAL file pointer is null";
    }

    std::vector<uint8_t> buffer;
    for (const auto& [key, value] : batch) {
        size_t start_idx = buffer.size();
        put_u16_be(buffer, static_cast<uint16_t>(key.len()));
        buffer.insert(buffer.end(), key.raw_ref(), key.raw_ref() + key.len());
        put_u16_be(buffer, static_cast<uint16_t>(value.len()));
        buffer.insert(buffer.end(), value.raw_ref(), value.raw_ref() + value.len());
        uint32_t crc = crc32_hash(&buffer[start_idx], buffer.size() - start_idx);
        put_u32_be(buffer, crc);
    }

    size_t written = std::fwrite(buffer.data(), 1, buffer.size(), fp_);
    if (written != buffer.size()) {
        return "Failed to write batch to WAL";
    }
    return std::monostate{};
}

std::variant<std::monostate, std::string> Wal::sync() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!fp_) {
        return "WAL file pointer is null";
    }
    if (std::fflush(fp_) != 0) {
        return "Failed to flush WAL buffer";
    }
    int fd = fileno(fp_);
    if (fd >= 0) {
        ::fsync(fd);
    }
    return std::monostate{};
}

} // namespace mini_lsm
