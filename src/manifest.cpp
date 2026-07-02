#include "mini_lsm/manifest.h"
#include "mini_lsm/endian.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdexcept>

namespace mini_lsm {

void to_json(nlohmann::json& j, const ManifestRecord& r) {
    switch (r.type) {
        case ManifestRecord::Type::Flush:
            j = nlohmann::json{{"Flush", r.flush_sst_id}};
            break;
        case ManifestRecord::Type::NewMemtable:
            j = nlohmann::json{{"NewMemtable", r.new_memtable_id}};
            break;
        case ManifestRecord::Type::Compaction:
            j = nlohmann::json{{"Compaction", nlohmann::json::array({r.compaction_task, r.compaction_output_sst_ids})}};
            break;
    }
}

void from_json(const nlohmann::json& j, ManifestRecord& r) {
    if (j.contains("Flush")) {
        r.type = ManifestRecord::Type::Flush;
        r.flush_sst_id = j.at("Flush").get<size_t>();
    } else if (j.contains("NewMemtable")) {
        r.type = ManifestRecord::Type::NewMemtable;
        r.new_memtable_id = j.at("NewMemtable").get<size_t>();
    } else if (j.contains("Compaction")) {
        r.type = ManifestRecord::Type::Compaction;
        auto arr = j.at("Compaction");
        r.compaction_task = arr.at(0).get<CompactionTask>();
        r.compaction_output_sst_ids = arr.at(1).get<std::vector<size_t>>();
    } else {
        throw std::runtime_error("Unknown ManifestRecord type in JSON");
    }
}

Manifest::Manifest(const std::string& path, int fd) : path_(path), fd_(fd) {}

Manifest::~Manifest() {
    std::lock_guard<std::mutex> lock(mu_);
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

std::variant<std::shared_ptr<Manifest>, std::string> Manifest::create(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return "Failed to create manifest file: " + path;
    }
    return std::shared_ptr<Manifest>(new Manifest(path, fd));
}

std::variant<std::pair<std::shared_ptr<Manifest>, std::vector<ManifestRecord>>, std::string> Manifest::recover(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        return "Failed to recover manifest file: " + path;
    }

    struct stat st;
    if (::fstat(fd, &st) < 0) {
        ::close(fd);
        return "Failed to stat manifest file: " + path;
    }

    std::vector<uint8_t> content(st.st_size);
    if (st.st_size > 0) {
        size_t total_read = 0;
        while (total_read < content.size()) {
            ssize_t ret = ::pread(fd, content.data() + total_read, content.size() - total_read, total_read);
            if (ret <= 0) {
                ::close(fd);
                return "Failed to read manifest file: " + path;
            }
            total_read += ret;
        }
    }

    std::vector<ManifestRecord> records;
    size_t offset = 0;
    while (offset < content.size()) {
        if (content.size() - offset < 8) {
            ::close(fd);
            return "Corrupted manifest: incomplete length header at EOF";
        }
        uint64_t len = read_u64_be(content.data() + offset);
        offset += 8;

        if (content.size() - offset < len + 4) {
            ::close(fd);
            return "Corrupted manifest: incomplete record payload/checksum at EOF";
        }

        uint32_t expected_checksum = read_u32_be(content.data() + offset + len);
        uint32_t actual_checksum = crc32_hash(content.data() + offset, len);
        if (expected_checksum != actual_checksum) {
            ::close(fd);
            return "Manifest record checksum mismatch";
        }

        std::string json_str(reinterpret_cast<const char*>(content.data() + offset), len);
        try {
            nlohmann::json j = nlohmann::json::parse(json_str);
            records.push_back(j.get<ManifestRecord>());
        } catch (const std::exception& e) {
            ::close(fd);
            return std::string("Failed to parse ManifestRecord JSON: ") + e.what();
        }

        offset += len + 4;
    }

    return std::make_pair(std::shared_ptr<Manifest>(new Manifest(path, fd)), std::move(records));
}

std::variant<std::monostate, std::string> Manifest::add_record(
    std::unique_lock<std::mutex>* /*state_lock_observer*/,
    const ManifestRecord& record) {
    return add_record_when_init(record);
}

std::variant<std::monostate, std::string> Manifest::add_record_when_init(const ManifestRecord& record) {
    std::lock_guard<std::mutex> lock(mu_);
    if (fd_ < 0) {
        return "Manifest file descriptor is closed";
    }

    std::string json_str;
    try {
        json_str = nlohmann::json(record).dump();
    } catch (const std::exception& e) {
        return std::string("Failed to serialize ManifestRecord: ") + e.what();
    }

    uint32_t checksum = crc32_hash(reinterpret_cast<const uint8_t*>(json_str.data()), json_str.size());

    std::vector<uint8_t> buf;
    put_u64_be(buf, static_cast<uint64_t>(json_str.size()));
    buf.insert(buf.end(), json_str.begin(), json_str.end());
    put_u32_be(buf, checksum);

    size_t written = 0;
    while (written < buf.size()) {
        ssize_t ret = ::write(fd_, buf.data() + written, buf.size() - written);
        if (ret <= 0) {
            return "Failed to write record to manifest file";
        }
        written += ret;
    }

    if (::fsync(fd_) < 0) {
        return "Failed to fsync manifest file";
    }

    return std::monostate{};
}

} // namespace mini_lsm
