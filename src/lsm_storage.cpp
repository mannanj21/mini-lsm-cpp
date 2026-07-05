#include "mini_lsm/lsm_storage.h"
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace mini_lsm {

LsmStorageOptions LsmStorageOptions::default_for_week1_test() {
    LsmStorageOptions opts;
    opts.block_size = 4096;
    opts.target_sst_size = 2 << 20;
    opts.compaction_options = CompactionOptions::no_compaction();
    opts.enable_wal = false;
    opts.num_memtable_limit = 50;
    opts.serializable = false;
    return opts;
}

LsmStorageOptions LsmStorageOptions::default_for_week1_day6_test() {
    LsmStorageOptions opts;
    opts.block_size = 4096;
    opts.target_sst_size = 2 << 20;
    opts.compaction_options = CompactionOptions::no_compaction();
    opts.enable_wal = false;
    opts.num_memtable_limit = 2;
    opts.serializable = false;
    return opts;
}

LsmStorageOptions LsmStorageOptions::default_for_week2_test(CompactionOptions compaction_options) {
    LsmStorageOptions opts;
    opts.block_size = 4096;
    opts.target_sst_size = 1 << 20;
    opts.compaction_options = std::move(compaction_options);
    opts.enable_wal = false;
    opts.num_memtable_limit = 2;
    opts.serializable = false;
    return opts;
}

LsmStorageState LsmStorageState::create(const LsmStorageOptions& options) {
    LsmStorageState state;
    switch (options.compaction_options.type) {
        case CompactionOptions::Type::Leveled: {
            const auto& opts = std::get<LeveledCompactionOptions>(options.compaction_options.options);
            for (size_t l = 1; l <= opts.max_levels; ++l) {
                state.levels.push_back({l, {}});
            }
            break;
        }
        case CompactionOptions::Type::Simple: {
            const auto& opts = std::get<SimpleLeveledCompactionOptions>(options.compaction_options.options);
            for (size_t l = 1; l <= opts.max_levels; ++l) {
                state.levels.push_back({l, {}});
            }
            break;
        }
        case CompactionOptions::Type::Tiered:
            break;
        case CompactionOptions::Type::NoCompaction:
            state.levels.push_back({1, {}});
            break;
    }
    state.memtable = std::make_shared<MemTable>(0);
    return state;
}

LsmStorageInner::LsmStorageInner(LsmStorageOptions opts, std::string p)
    : path(std::move(p)),
      block_cache(std::make_shared<BlockCache>(1 << 20)),
      options(std::move(opts)),
      compaction_controller(CompactionController::create(options.compaction_options)) {
    state = std::make_shared<LsmStorageState>(LsmStorageState::create(options));
}

std::string LsmStorageInner::path_of_sst_val(size_t id) const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%05zu.sst", id);
    return (std::filesystem::path(path) / buf).string();
}

std::string LsmStorageInner::path_of_wal_val(size_t id) const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%05zu.wal", id);
    return (std::filesystem::path(path) / buf).string();
}

void LsmStorageInner::sync_dir() {
    if (!path.empty() && std::filesystem::exists(path)) {
        // POSIX directory sync if needed
    }
}

void LsmStorageInner::force_freeze_memtable(std::shared_mutex*) {
    std::unique_lock<std::mutex> guard(state_lock);
    size_t memtable_id = next_sst_id_val();
    std::shared_ptr<MemTable> new_memtable;
    if (options.enable_wal) {
        auto res = MemTable::create_with_wal(memtable_id, path_of_wal_val(memtable_id));
        if (std::holds_alternative<std::shared_ptr<MemTable>>(res)) {
            new_memtable = std::get<std::shared_ptr<MemTable>>(res);
        } else {
            new_memtable = std::make_shared<MemTable>(memtable_id);
        }
    } else {
        new_memtable = std::make_shared<MemTable>(memtable_id);
    }

    {
        std::unique_lock<std::shared_mutex> write_lock(state_mutex);
        auto next_state = std::make_shared<LsmStorageState>(*state);
        auto old_memtable = std::move(next_state->memtable);
        next_state->memtable = new_memtable;
        next_state->imm_memtables.insert(next_state->imm_memtables.begin(), old_memtable);
        state = next_state;
    }
    if (manifest) {
        manifest->add_record(&guard, ManifestRecord::new_memtable(memtable_id));
    }
    sync_dir();
}

void LsmStorageInner::force_flush_next_imm_memtable() {
    std::unique_lock<std::mutex> guard(state_lock);
    std::shared_ptr<MemTable> flush_memtable;
    {
        std::shared_lock<std::shared_mutex> lock(state_mutex);
        if (state->imm_memtables.empty()) {
            return;
        }
        flush_memtable = state->imm_memtables.back();
    }

    SsTableBuilder builder(options.block_size, options.compression);
    flush_memtable->flush(builder);
    size_t sst_id = flush_memtable->id();
    auto sst = builder.build(sst_id, block_cache, path_of_sst_val(sst_id));

    {
        std::unique_lock<std::shared_mutex> write_lock(state_mutex);
        auto next_state = std::make_shared<LsmStorageState>(*state);
        next_state->imm_memtables.pop_back();
        if (compaction_controller.flush_to_l0()) {
            next_state->l0_sstables.insert(next_state->l0_sstables.begin(), sst_id);
        } else {
            next_state->levels.insert(next_state->levels.begin(), {sst_id, {sst_id}});
        }
        next_state->sstables[sst_id] = sst;
        state = next_state;
    }
    if (manifest) {
        manifest->add_record(&guard, ManifestRecord::flush(sst_id));
    }
    sync_dir();
}

void LsmStorageInner::put(KeySlice key, KeySlice value) {
    std::shared_lock<std::shared_mutex> lock(state_mutex);
    state->memtable->put(key, value);
    if (state->memtable->approximate_size() >= options.target_sst_size) {
        lock.unlock();
        force_freeze_memtable();
    }
}

void LsmStorageInner::del(KeySlice key) {
    put(key, KeySlice());
}

std::string LsmStorageInner::get(KeySlice key) const {
    std::shared_lock<std::shared_mutex> lock(state_mutex);
    if (auto v = state->memtable->get(key)) {
        if (v->empty()) return "";
        return *v;
    }
    for (const auto& imm : state->imm_memtables) {
        if (auto v = imm->get(key)) {
            if (v->empty()) return "";
            return *v;
        }
    }

    auto keep_table = [&](KeySlice k, const SsTable& table) {
        if (k >= table.first_key.as_key_slice() && k <= table.last_key.as_key_slice()) {
            if (table.bloom) {
                if (table.bloom->may_contain(farmhash_fingerprint32(k))) {
                    return true;
                }
            } else {
                return true;
            }
        }
        return false;
    };

    std::vector<SsTableIterator> l0_iters;
    for (size_t id : state->l0_sstables) {
        auto it = state->sstables.find(id);
        if (it != state->sstables.end() && it->second) {
            if (keep_table(key, *it->second)) {
                l0_iters.push_back(SsTableIterator::create_and_seek_to_key(it->second, key));
            }
        }
    }
    auto l0_merge = MergeIterator<SsTableIterator>::create(std::move(l0_iters));

    std::vector<SstConcatIterator> level_iters;
    for (const auto& [_, level_sst_ids] : state->levels) {
        std::vector<std::shared_ptr<SsTable>> level_ssts;
        for (size_t id : level_sst_ids) {
            auto it = state->sstables.find(id);
            if (it != state->sstables.end() && it->second) {
                if (keep_table(key, *it->second)) {
                    level_ssts.push_back(it->second);
                }
            }
        }
        level_iters.push_back(SstConcatIterator::create_and_seek_to_key(std::move(level_ssts), key));
    }
    auto level_merge = MergeIterator<SstConcatIterator>::create(std::move(level_iters));

    auto iter = TwoMergeIterator<MergeIterator<SsTableIterator>, MergeIterator<SstConcatIterator>>::create(
        std::move(l0_merge), std::move(level_merge));

    if (iter.is_valid() && iter.key() == key && !iter.value().is_empty()) {
        return std::string(reinterpret_cast<const char*>(iter.value().raw_ref()), iter.value().len());
    }
    return "";
}

void LsmStorageInner::sync() {
    std::shared_lock<std::shared_mutex> lock(state_mutex);
    state->memtable->sync_wal();
}

FusedIterator LsmStorageInner::scan(Bound lower, Bound upper) const {
    std::shared_ptr<LsmStorageState> snapshot;
    {
        std::shared_lock<std::shared_mutex> lock(state_mutex);
        snapshot = state;
    }

    std::vector<MemTableIterator> memtable_iters;
    memtable_iters.push_back(snapshot->memtable->scan(lower, upper));
    for (const auto& mem : snapshot->imm_memtables) {
        memtable_iters.push_back(mem->scan(lower, upper));
    }
    auto memtable_iter = MergeIterator<MemTableIterator>::create(std::move(memtable_iters));

    auto range_overlap = [](const Bound& l, const Bound& u, KeySlice first, KeySlice last) {
        if (l.type == BoundType::Included && l.key > last) return false;
        if (l.type == BoundType::Excluded && l.key >= last) return false;
        if (u.type == BoundType::Included && u.key < first) return false;
        if (u.type == BoundType::Excluded && u.key <= first) return false;
        return true;
    };

    std::vector<SsTableIterator> table_iters;
    for (size_t table_id : snapshot->l0_sstables) {
        auto it = snapshot->sstables.find(table_id);
        if (it != snapshot->sstables.end() && it->second) {
            auto table = it->second;
            if (range_overlap(lower, upper, table->first_key.as_key_slice(), table->last_key.as_key_slice())) {
                SsTableIterator iter;
                switch (lower.type) {
                    case BoundType::Included:
                        iter = SsTableIterator::create_and_seek_to_key(table, lower.key);
                        break;
                    case BoundType::Excluded:
                        iter = SsTableIterator::create_and_seek_to_key(table, lower.key);
                        if (iter.is_valid() && iter.key() == lower.key) {
                            iter.next();
                        }
                        break;
                    case BoundType::Unbounded:
                        iter = SsTableIterator::create_and_seek_to_first(table);
                        break;
                }
                table_iters.push_back(std::move(iter));
            }
        }
    }
    auto l0_iter = MergeIterator<SsTableIterator>::create(std::move(table_iters));

    std::vector<SstConcatIterator> level_iters;
    for (const auto& [_, level_sst_ids] : snapshot->levels) {
        std::vector<std::shared_ptr<SsTable>> level_ssts;
        for (size_t id : level_sst_ids) {
            auto it = snapshot->sstables.find(id);
            if (it != snapshot->sstables.end() && it->second) {
                auto table = it->second;
                if (range_overlap(lower, upper, table->first_key.as_key_slice(), table->last_key.as_key_slice())) {
                    level_ssts.push_back(table);
                }
            }
        }
        if (!level_ssts.empty()) {
            SstConcatIterator iter;
            switch (lower.type) {
                case BoundType::Included:
                    iter = SstConcatIterator::create_and_seek_to_key(std::move(level_ssts), lower.key);
                    break;
                case BoundType::Excluded:
                    iter = SstConcatIterator::create_and_seek_to_key(std::move(level_ssts), lower.key);
                    if (iter.is_valid() && iter.key() == lower.key) {
                        iter.next();
                    }
                    break;
                case BoundType::Unbounded:
                    iter = SstConcatIterator::create_and_seek_to_first(std::move(level_ssts));
                    break;
            }
            level_iters.push_back(std::move(iter));
        } else {
            level_iters.push_back(SstConcatIterator());
        }
    }
    auto level_iter = MergeIterator<SstConcatIterator>::create(std::move(level_iters));

    auto two_merge_1 = TwoMergeIterator<MergeIterator<MemTableIterator>, MergeIterator<SsTableIterator>>::create(
        std::move(memtable_iter), std::move(l0_iter));
    auto two_merge_2 = TwoMergeIterator<decltype(two_merge_1), MergeIterator<SstConcatIterator>>::create(
        std::move(two_merge_1), std::move(level_iter));

    auto lsm_iter = LsmIterator::create(std::move(two_merge_2), upper);
    return FusedIterator::create(std::move(lsm_iter));
}

std::shared_ptr<LsmStorageInner> LsmStorageInner::open(const std::string& path, LsmStorageOptions options) {
    std::filesystem::create_directories(path);
    auto inner = std::make_shared<LsmStorageInner>(options, path);
    auto manifest_path = (std::filesystem::path(path) / "MANIFEST").string();
    size_t next_id = 1;

    if (!std::filesystem::exists(manifest_path)) {
        if (options.enable_wal) {
            auto wal_path = inner->path_of_wal_val(inner->state->memtable->id());
            auto mem_res = MemTable::create_with_wal(inner->state->memtable->id(), wal_path);
            if (std::holds_alternative<std::shared_ptr<MemTable>>(mem_res)) {
                inner->state->memtable = std::get<std::shared_ptr<MemTable>>(mem_res);
            }
        }
        auto m_res = Manifest::create(manifest_path);
        if (std::holds_alternative<std::shared_ptr<Manifest>>(m_res)) {
            inner->manifest = std::get<std::shared_ptr<Manifest>>(m_res);
            inner->manifest->add_record_when_init(ManifestRecord::new_memtable(inner->state->memtable->id()));
        }
    } else {
        auto rec_res = Manifest::recover(manifest_path);
        if (std::holds_alternative<std::pair<std::shared_ptr<Manifest>, std::vector<ManifestRecord>>>(rec_res)) {
            auto& [m, records] = std::get<std::pair<std::shared_ptr<Manifest>, std::vector<ManifestRecord>>>(rec_res);
            inner->manifest = m;
            std::set<size_t> memtables;
            for (const auto& rec : records) {
                switch (rec.type) {
                    case ManifestRecord::Type::Flush: {
                        memtables.erase(rec.flush_sst_id);
                        if (inner->compaction_controller.flush_to_l0()) {
                            inner->state->l0_sstables.insert(inner->state->l0_sstables.begin(), rec.flush_sst_id);
                        } else {
                            inner->state->levels.insert(inner->state->levels.begin(), {rec.flush_sst_id, {rec.flush_sst_id}});
                        }
                        next_id = std::max(next_id, rec.flush_sst_id);
                        break;
                    }
                    case ManifestRecord::Type::NewMemtable: {
                        next_id = std::max(next_id, rec.new_memtable_id);
                        memtables.insert(rec.new_memtable_id);
                        break;
                    }
                    case ManifestRecord::Type::Compaction: {
                        auto [new_state, _] = inner->compaction_controller.apply_compaction_result(
                            *inner->state, rec.compaction_task, rec.compaction_output_sst_ids, true);
                        *inner->state = new_state;
                        for (size_t out_id : rec.compaction_output_sst_ids) {
                            next_id = std::max(next_id, out_id);
                        }
                        break;
                    }
                }
            }
            for (size_t sst_id : inner->state->l0_sstables) {
                auto sst_path = inner->path_of_sst_val(sst_id);
                auto sst = SsTable::open(sst_id, inner->block_cache, FileObject::open(sst_path));
                inner->state->sstables[sst_id] = sst;
            }
            for (const auto& [_, sst_ids] : inner->state->levels) {
                for (size_t sst_id : sst_ids) {
                    auto sst_path = inner->path_of_sst_val(sst_id);
                    auto sst = SsTable::open(sst_id, inner->block_cache, FileObject::open(sst_path));
                    inner->state->sstables[sst_id] = sst;
                }
            }
            next_id++;
            if (options.enable_wal) {
                for (size_t id : memtables) {
                    auto wal_path = inner->path_of_wal_val(id);
                    auto mem_res = MemTable::recover_from_wal(id, wal_path);
                    if (std::holds_alternative<std::shared_ptr<MemTable>>(mem_res)) {
                        auto mem = std::get<std::shared_ptr<MemTable>>(mem_res);
                        if (!mem->is_empty()) {
                            inner->state->imm_memtables.insert(inner->state->imm_memtables.begin(), mem);
                        }
                    }
                }
                auto wal_path = inner->path_of_wal_val(next_id);
                auto mem_res = MemTable::create_with_wal(next_id, wal_path);
                if (std::holds_alternative<std::shared_ptr<MemTable>>(mem_res)) {
                    inner->state->memtable = std::get<std::shared_ptr<MemTable>>(mem_res);
                } else {
                    inner->state->memtable = std::make_shared<MemTable>(next_id);
                }
            } else {
                inner->state->memtable = std::make_shared<MemTable>(next_id);
            }
            inner->next_sst_id.store(next_id + 1);
            if (inner->manifest) {
                inner->manifest->add_record_when_init(ManifestRecord::new_memtable(inner->state->memtable->id()));
            }
        }
    }
    return inner;
}

MiniLsm::MiniLsm(std::shared_ptr<LsmStorageInner> i) : inner(std::move(i)) {
    compaction_thread = inner->spawn_compaction_thread(stop_flag);
    flush_thread = inner->spawn_flush_thread(stop_flag);
}

void MiniLsm::close() {
    stop_flag.store(true);
    if (compaction_thread && compaction_thread->joinable()) {
        compaction_thread->join();
    }
    if (flush_thread && flush_thread->joinable()) {
        flush_thread->join();
    }

    if (inner->options.enable_wal) {
        inner->sync();
        inner->sync_dir();
        return;
    }

    if (!inner->state->memtable->is_empty()) {
        inner->force_freeze_memtable();
    }

    while (!inner->state->imm_memtables.empty()) {
        inner->force_flush_next_imm_memtable();
    }
    inner->sync_dir();
}

MiniLsm::~MiniLsm() {
    close();
}

std::unique_ptr<MiniLsm> MiniLsm::open(const std::string& path, LsmStorageOptions options) {
    auto inner = LsmStorageInner::open(path, options);
    return std::make_unique<MiniLsm>(std::move(inner));
}

} // namespace mini_lsm
