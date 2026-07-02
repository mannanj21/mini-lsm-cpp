#ifndef MINI_LSM_LSM_STORAGE_H
#define MINI_LSM_LSM_STORAGE_H

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include "mini_lsm/mem_table.h"
#include "mini_lsm/table.h"
#include "mini_lsm/compact.h"
#include "mini_lsm/manifest.h"
#include "mini_lsm/iterators.h"
#include "mini_lsm/concat_iterator.h"
#include "mini_lsm/lsm_iterator.h"
#include "mini_lsm/two_merge_iterator.h"
#include "mini_lsm/block_cache.h"

namespace mini_lsm {

struct LsmStorageOptions {
    size_t block_size{4096};
    size_t target_sst_size{2 << 20};
    size_t num_memtable_limit{50};
    CompactionOptions compaction_options{CompactionOptions::no_compaction()};
    bool enable_wal{false};
    bool serializable{false};

    static LsmStorageOptions default_for_week1_test();
    static LsmStorageOptions default_for_week1_day6_test();
    static LsmStorageOptions default_for_week2_test(CompactionOptions compaction_options);
};

struct LsmStorageState {
    std::shared_ptr<MemTable> memtable;
    std::vector<std::shared_ptr<MemTable>> imm_memtables;
    std::vector<size_t> l0_sstables;
    std::vector<std::pair<size_t, std::vector<size_t>>> levels;
    std::unordered_map<size_t, std::shared_ptr<SsTable>> sstables;

    static LsmStorageState create(const LsmStorageOptions& options);
};

class LsmStorageInner : public std::enable_shared_from_this<LsmStorageInner> {
public:
    mutable std::shared_mutex state_mutex;
    std::shared_ptr<LsmStorageState> state;
    std::mutex state_lock;
    std::string path;
    std::shared_ptr<BlockCache> block_cache;
    std::atomic<size_t> next_sst_id{1};
    LsmStorageOptions options;
    CompactionController compaction_controller;
    std::shared_ptr<Manifest> manifest;

    explicit LsmStorageInner(LsmStorageOptions options, std::string path);

    static std::shared_ptr<LsmStorageInner> open(const std::string& path, LsmStorageOptions options);

    size_t next_sst_id_val() { return next_sst_id.fetch_add(1); }
    std::string path_of_sst_val(size_t id) const;
    std::string path_of_wal_val(size_t id) const;
    void sync_dir();

    // Compaction methods (implemented in src/compact.cpp)
    std::vector<std::shared_ptr<SsTable>> compact_generate_sst_from_iter(
        StorageIterator& iter, bool compact_to_bottom_level);
    std::vector<std::shared_ptr<SsTable>> compact(const CompactionTask& task);
    void force_full_compaction();
    bool trigger_compaction();
    bool trigger_flush();
    void force_flush_next_imm_memtable();

    std::unique_ptr<std::thread> spawn_compaction_thread(std::atomic<bool>& stop_flag);
    std::unique_ptr<std::thread> spawn_flush_thread(std::atomic<bool>& stop_flag);

    // Read/Write operations (implemented in src/lsm_storage.cpp for Step 22)
    void put(KeySlice key, KeySlice value);
    void del(KeySlice key);
    std::string get(KeySlice key) const;
    FusedIterator scan(Bound lower, Bound upper) const;
    void sync();
    void force_freeze_memtable(std::shared_mutex* mutex_ptr = nullptr);
};

class MiniLsm {
public:
    std::shared_ptr<LsmStorageInner> inner;
    std::unique_ptr<std::thread> flush_thread;
    std::unique_ptr<std::thread> compaction_thread;
    std::atomic<bool> stop_flag{false};

    explicit MiniLsm(std::shared_ptr<LsmStorageInner> inner);
    ~MiniLsm();

    static std::unique_ptr<MiniLsm> open(const std::string& path, LsmStorageOptions options);
    void close();
    void sync() { inner->sync(); }

    void put(KeySlice key, KeySlice value) { inner->put(key, value); }
    void del(KeySlice key) { inner->del(key); }
    std::string get(KeySlice key) const { return inner->get(key); }
    FusedIterator scan(Bound lower, Bound upper) const { return inner->scan(lower, upper); }
    void force_flush() { inner->force_flush_next_imm_memtable(); }
    void force_full_compaction() { inner->force_full_compaction(); }
};

} // namespace mini_lsm

#endif // MINI_LSM_LSM_STORAGE_H
