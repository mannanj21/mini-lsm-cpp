#ifndef MINI_LSM_MANIFEST_H
#define MINI_LSM_MANIFEST_H

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>
#include "mini_lsm/compact.h"

namespace mini_lsm {

struct ManifestRecord {
    enum class Type {
        Flush,
        NewMemtable,
        Compaction
    } type{Type::Flush};

    size_t flush_sst_id{0};
    size_t new_memtable_id{0};
    CompactionTask compaction_task;
    std::vector<size_t> compaction_output_sst_ids;

    static ManifestRecord flush(size_t sst_id) {
        ManifestRecord r;
        r.type = Type::Flush;
        r.flush_sst_id = sst_id;
        return r;
    }

    static ManifestRecord new_memtable(size_t mem_id) {
        ManifestRecord r;
        r.type = Type::NewMemtable;
        r.new_memtable_id = mem_id;
        return r;
    }

    static ManifestRecord compaction(CompactionTask task, std::vector<size_t> output_sst_ids) {
        ManifestRecord r;
        r.type = Type::Compaction;
        r.compaction_task = std::move(task);
        r.compaction_output_sst_ids = std::move(output_sst_ids);
        return r;
    }
};

void to_json(nlohmann::json& j, const ManifestRecord& r);
void from_json(const nlohmann::json& j, ManifestRecord& r);

class Manifest {
public:
    ~Manifest();

    static std::variant<std::shared_ptr<Manifest>, std::string> create(const std::string& path);
    static std::variant<std::pair<std::shared_ptr<Manifest>, std::vector<ManifestRecord>>, std::string> recover(const std::string& path);

    std::variant<std::monostate, std::string> add_record(
        std::unique_lock<std::mutex>* state_lock_observer,
        const ManifestRecord& record);

    std::variant<std::monostate, std::string> add_record_when_init(const ManifestRecord& record);

private:
    Manifest(const std::string& path, int fd);

    std::string path_;
    int fd_{-1};
    std::mutex mu_;
};

} // namespace mini_lsm

#endif // MINI_LSM_MANIFEST_H
