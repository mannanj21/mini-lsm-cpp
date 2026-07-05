#include "mini_lsm/compact.h"
#include "mini_lsm/lsm_storage.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace mini_lsm {

// JSON serializations
void to_json(nlohmann::json& j, const SimpleLeveledCompactionTask& t) {
    j = nlohmann::json{
        {"upper_level", t.upper_level ? nlohmann::json(*t.upper_level) : nlohmann::json(nullptr)},
        {"upper_level_sst_ids", t.upper_level_sst_ids},
        {"lower_level", t.lower_level},
        {"lower_level_sst_ids", t.lower_level_sst_ids},
        {"is_lower_level_bottom_level", t.is_lower_level_bottom_level}
    };
}

void from_json(const nlohmann::json& j, SimpleLeveledCompactionTask& t) {
    if (j.at("upper_level").is_null()) {
        t.upper_level = std::nullopt;
    } else {
        t.upper_level = j.at("upper_level").get<size_t>();
    }
    j.at("upper_level_sst_ids").get_to(t.upper_level_sst_ids);
    j.at("lower_level").get_to(t.lower_level);
    j.at("lower_level_sst_ids").get_to(t.lower_level_sst_ids);
    j.at("is_lower_level_bottom_level").get_to(t.is_lower_level_bottom_level);
}

void to_json(nlohmann::json& j, const LeveledCompactionTask& t) {
    j = nlohmann::json{
        {"upper_level", t.upper_level ? nlohmann::json(*t.upper_level) : nlohmann::json(nullptr)},
        {"upper_level_sst_ids", t.upper_level_sst_ids},
        {"lower_level", t.lower_level},
        {"lower_level_sst_ids", t.lower_level_sst_ids},
        {"is_lower_level_bottom_level", t.is_lower_level_bottom_level}
    };
}

void from_json(const nlohmann::json& j, LeveledCompactionTask& t) {
    if (j.at("upper_level").is_null()) {
        t.upper_level = std::nullopt;
    } else {
        t.upper_level = j.at("upper_level").get<size_t>();
    }
    j.at("upper_level_sst_ids").get_to(t.upper_level_sst_ids);
    j.at("lower_level").get_to(t.lower_level);
    j.at("lower_level_sst_ids").get_to(t.lower_level_sst_ids);
    j.at("is_lower_level_bottom_level").get_to(t.is_lower_level_bottom_level);
}

void to_json(nlohmann::json& j, const TieredCompactionTask& t) {
    j = nlohmann::json{
        {"tiers", t.tiers},
        {"bottom_tier_included", t.bottom_tier_included}
    };
}

void from_json(const nlohmann::json& j, TieredCompactionTask& t) {
    j.at("tiers").get_to(t.tiers);
    j.at("bottom_tier_included").get_to(t.bottom_tier_included);
}

void to_json(nlohmann::json& j, const ForceFullCompactionTask& t) {
    j = nlohmann::json{
        {"l0_sstables", t.l0_sstables},
        {"l1_sstables", t.l1_sstables}
    };
}

void from_json(const nlohmann::json& j, ForceFullCompactionTask& t) {
    j.at("l0_sstables").get_to(t.l0_sstables);
    j.at("l1_sstables").get_to(t.l1_sstables);
}

void to_json(nlohmann::json& j, const CompactionTask& t) {
    switch (t.type) {
        case CompactionTask::Type::Simple:
            j = nlohmann::json{{"Simple", std::get<SimpleLeveledCompactionTask>(t.task)}};
            break;
        case CompactionTask::Type::Leveled:
            j = nlohmann::json{{"Leveled", std::get<LeveledCompactionTask>(t.task)}};
            break;
        case CompactionTask::Type::Tiered:
            j = nlohmann::json{{"Tiered", std::get<TieredCompactionTask>(t.task)}};
            break;
        case CompactionTask::Type::ForceFull:
            j = nlohmann::json{{"ForceFullCompaction", std::get<ForceFullCompactionTask>(t.task)}};
            break;
    }
}

void from_json(const nlohmann::json& j, CompactionTask& t) {
    if (j.contains("Simple")) {
        t.type = CompactionTask::Type::Simple;
        t.task = j.at("Simple").get<SimpleLeveledCompactionTask>();
    } else if (j.contains("Leveled")) {
        t.type = CompactionTask::Type::Leveled;
        t.task = j.at("Leveled").get<LeveledCompactionTask>();
    } else if (j.contains("Tiered")) {
        t.type = CompactionTask::Type::Tiered;
        t.task = j.at("Tiered").get<TieredCompactionTask>();
    } else if (j.contains("ForceFullCompaction")) {
        t.type = CompactionTask::Type::ForceFull;
        t.task = j.at("ForceFullCompaction").get<ForceFullCompactionTask>();
    } else {
        throw std::runtime_error("Unknown CompactionTask type in JSON");
    }
}

// CompactionController implementation
CompactionController CompactionController::create(const CompactionOptions& options) {
    switch (options.type) {
        case CompactionOptions::Type::Leveled:
            return CompactionController::leveled(LeveledCompactionController(std::get<LeveledCompactionOptions>(options.options)));
        case CompactionOptions::Type::Tiered:
            return CompactionController::tiered(TieredCompactionController(std::get<TieredCompactionOptions>(options.options)));
        case CompactionOptions::Type::Simple:
            return CompactionController::simple(SimpleLeveledCompactionController(std::get<SimpleLeveledCompactionOptions>(options.options)));
        case CompactionOptions::Type::NoCompaction:
            return CompactionController::no_compaction();
    }
    return CompactionController::no_compaction();
}

std::optional<CompactionTask> CompactionController::generate_compaction_task(const LsmStorageState& snapshot) const {
    switch (type) {
        case Type::Leveled: {
            auto t = std::get<LeveledCompactionController>(ctrl).generate_compaction_task(snapshot);
            if (t) return CompactionTask::leveled(std::move(*t));
            return std::nullopt;
        }
        case Type::Tiered: {
            auto t = std::get<TieredCompactionController>(ctrl).generate_compaction_task(snapshot);
            if (t) return CompactionTask::tiered(std::move(*t));
            return std::nullopt;
        }
        case Type::Simple: {
            auto t = std::get<SimpleLeveledCompactionController>(ctrl).generate_compaction_task(snapshot);
            if (t) return CompactionTask::simple(std::move(*t));
            return std::nullopt;
        }
        case Type::NoCompaction:
            return std::nullopt;
    }
    return std::nullopt;
}

std::pair<LsmStorageState, std::vector<size_t>> CompactionController::apply_compaction_result(
    const LsmStorageState& snapshot,
    const CompactionTask& task,
    const std::vector<size_t>& output,
    bool in_recovery) const {
    if (task.type == CompactionTask::Type::ForceFull) {
        LsmStorageState next_state = snapshot;
        const auto& fft = std::get<ForceFullCompactionTask>(task.task);
        std::vector<size_t> files_to_remove;
        for (size_t id : fft.l0_sstables) {
            files_to_remove.push_back(id);
            next_state.sstables.erase(id);
        }
        for (size_t id : fft.l1_sstables) {
            files_to_remove.push_back(id);
            next_state.sstables.erase(id);
        }
        if (!next_state.levels.empty()) {
            next_state.levels[0].second = output;
        } else {
            next_state.levels.push_back({1, output});
        }
        std::vector<size_t> new_l0;
        for (size_t x : next_state.l0_sstables) {
            if (std::find(fft.l0_sstables.begin(), fft.l0_sstables.end(), x) == fft.l0_sstables.end()) {
                new_l0.push_back(x);
            }
        }
        next_state.l0_sstables = std::move(new_l0);
        return {next_state, files_to_remove};
    }
    switch (type) {
        case Type::Leveled:
            return std::get<LeveledCompactionController>(ctrl).apply_compaction_result(
                snapshot, std::get<LeveledCompactionTask>(task.task), output, in_recovery);
        case Type::Tiered:
            return std::get<TieredCompactionController>(ctrl).apply_compaction_result(
                snapshot, std::get<TieredCompactionTask>(task.task), output);
        case Type::Simple:
            return std::get<SimpleLeveledCompactionController>(ctrl).apply_compaction_result(
                snapshot, std::get<SimpleLeveledCompactionTask>(task.task), output);
        case Type::NoCompaction:
            throw std::runtime_error("NoCompaction controller cannot apply result");
    }
    throw std::runtime_error("Unknown controller type");
}

bool CompactionController::flush_to_l0() const {
    return type == Type::Leveled || type == Type::Simple || type == Type::NoCompaction;
}

// LsmStorageInner Compaction methods
std::vector<std::shared_ptr<SsTable>> LsmStorageInner::compact_generate_sst_from_iter(
    StorageIterator& iter, bool compact_to_bottom_level) {
    std::unique_ptr<SsTableBuilder> builder = nullptr;
    std::vector<std::shared_ptr<SsTable>> new_sst;

    while (iter.is_valid()) {
        if (!builder) {
            builder = std::make_unique<SsTableBuilder>(options.block_size, options.compression);
        }
        if (compact_to_bottom_level) {
            if (!iter.value().is_empty()) {
                builder->add(iter.key(), iter.value());
            }
        } else {
            builder->add(iter.key(), iter.value());
        }
        iter.next();

        if (builder && !builder->is_empty() && builder->estimated_size() >= options.target_sst_size) {
            size_t sst_id = next_sst_id_val();
            auto old_builder = std::move(builder);
            builder = nullptr;
            auto sst = old_builder->build(sst_id, block_cache, path_of_sst_val(sst_id));
            new_sst.push_back(std::move(sst));
        }
    }
    if (builder && !builder->is_empty()) {
        size_t sst_id = next_sst_id_val();
        auto sst = builder->build(sst_id, block_cache, path_of_sst_val(sst_id));
        new_sst.push_back(std::move(sst));
    }
    return new_sst;
}

std::vector<std::shared_ptr<SsTable>> LsmStorageInner::compact(const CompactionTask& task) {
    LsmStorageState snapshot;
    {
        std::shared_lock<std::shared_mutex> lock(state_mutex);
        snapshot = *state;
    }

    switch (task.type) {
        case CompactionTask::Type::ForceFull: {
            const auto& t = std::get<ForceFullCompactionTask>(task.task);
            std::vector<SsTableIterator> l0_iters;
            for (size_t id : t.l0_sstables) {
                auto it = snapshot.sstables.find(id);
                if (it != snapshot.sstables.end() && it->second) {
                    l0_iters.push_back(SsTableIterator::create_and_seek_to_first(it->second));
                }
            }
            std::vector<std::shared_ptr<SsTable>> l1_ssts;
            for (size_t id : t.l1_sstables) {
                auto it = snapshot.sstables.find(id);
                if (it != snapshot.sstables.end() && it->second) {
                    l1_ssts.push_back(it->second);
                }
            }
            auto merge_l0 = MergeIterator<SsTableIterator>::create(std::move(l0_iters));
            auto concat_l1 = SstConcatIterator::create_and_seek_to_first(std::move(l1_ssts));
            auto two_merge = TwoMergeIterator<MergeIterator<SsTableIterator>, SstConcatIterator>::create(
                std::move(merge_l0), std::move(concat_l1));
            return compact_generate_sst_from_iter(two_merge, task.compact_to_bottom_level());
        }
        case CompactionTask::Type::Simple:
        case CompactionTask::Type::Leveled: {
            std::optional<size_t> upper_level;
            std::vector<size_t> upper_level_sst_ids;
            std::vector<size_t> lower_level_sst_ids;
            if (task.type == CompactionTask::Type::Simple) {
                const auto& t = std::get<SimpleLeveledCompactionTask>(task.task);
                upper_level = t.upper_level;
                upper_level_sst_ids = t.upper_level_sst_ids;
                lower_level_sst_ids = t.lower_level_sst_ids;
            } else {
                const auto& t = std::get<LeveledCompactionTask>(task.task);
                upper_level = t.upper_level;
                upper_level_sst_ids = t.upper_level_sst_ids;
                lower_level_sst_ids = t.lower_level_sst_ids;
            }

            std::vector<std::shared_ptr<SsTable>> lower_ssts;
            for (size_t id : lower_level_sst_ids) {
                auto it = snapshot.sstables.find(id);
                if (it != snapshot.sstables.end() && it->second) {
                    lower_ssts.push_back(it->second);
                }
            }
            auto concat_lower = SstConcatIterator::create_and_seek_to_first(std::move(lower_ssts));

            if (upper_level.has_value()) {
                std::vector<std::shared_ptr<SsTable>> upper_ssts;
                for (size_t id : upper_level_sst_ids) {
                    auto it = snapshot.sstables.find(id);
                    if (it != snapshot.sstables.end() && it->second) {
                        upper_ssts.push_back(it->second);
                    }
                }
                auto concat_upper = SstConcatIterator::create_and_seek_to_first(std::move(upper_ssts));
                auto two_merge = TwoMergeIterator<SstConcatIterator, SstConcatIterator>::create(
                    std::move(concat_upper), std::move(concat_lower));
                return compact_generate_sst_from_iter(two_merge, task.compact_to_bottom_level());
            } else {
                std::vector<SsTableIterator> l0_iters;
                for (size_t id : upper_level_sst_ids) {
                    auto it = snapshot.sstables.find(id);
                    if (it != snapshot.sstables.end() && it->second) {
                        l0_iters.push_back(SsTableIterator::create_and_seek_to_first(it->second));
                    }
                }
                auto merge_upper = MergeIterator<SsTableIterator>::create(std::move(l0_iters));
                auto two_merge = TwoMergeIterator<MergeIterator<SsTableIterator>, SstConcatIterator>::create(
                    std::move(merge_upper), std::move(concat_lower));
                return compact_generate_sst_from_iter(two_merge, task.compact_to_bottom_level());
            }
        }
        case CompactionTask::Type::Tiered: {
            const auto& t = std::get<TieredCompactionTask>(task.task);
            std::vector<SstConcatIterator> tier_iters;
            for (const auto& [_, tier_ids] : t.tiers) {
                std::vector<std::shared_ptr<SsTable>> ssts;
                for (size_t id : tier_ids) {
                    auto it = snapshot.sstables.find(id);
                    if (it != snapshot.sstables.end() && it->second) {
                        ssts.push_back(it->second);
                    }
                }
                tier_iters.push_back(SstConcatIterator::create_and_seek_to_first(std::move(ssts)));
            }
            auto merge_iter = MergeIterator<SstConcatIterator>::create(std::move(tier_iters));
            return compact_generate_sst_from_iter(merge_iter, task.compact_to_bottom_level());
        }
    }
    return {};
}

void LsmStorageInner::force_full_compaction() {
    if (options.compaction_options.type != CompactionOptions::Type::NoCompaction) {
        throw std::runtime_error("full compaction can only be called when compaction is not enabled");
    }

    LsmStorageState snapshot;
    {
        std::shared_lock<std::shared_mutex> lock(state_mutex);
        snapshot = *state;
    }

    std::vector<size_t> l0_sstables = snapshot.l0_sstables;
    std::vector<size_t> l1_sstables = snapshot.levels.empty() ? std::vector<size_t>{} : snapshot.levels[0].second;

    ForceFullCompactionTask fft{l0_sstables, l1_sstables};
    CompactionTask task = CompactionTask::force_full(std::move(fft));

    auto sstables = compact(task);
    std::vector<size_t> ids;

    {
        std::unique_lock<std::mutex> state_lock_guard(state_lock);
        std::shared_ptr<LsmStorageState> next_state = std::make_shared<LsmStorageState>(*state);

        for (size_t sst : l0_sstables) {
            next_state->sstables.erase(sst);
        }
        for (size_t sst : l1_sstables) {
            next_state->sstables.erase(sst);
        }

        for (const auto& new_sst : sstables) {
            ids.push_back(new_sst->id);
            next_state->sstables[new_sst->id] = new_sst;
        }

        if (!next_state->levels.empty()) {
            next_state->levels[0].second = ids;
        } else {
            next_state->levels.push_back({1, ids});
        }

        std::vector<size_t> new_l0;
        for (size_t x : next_state->l0_sstables) {
            if (std::find(l0_sstables.begin(), l0_sstables.end(), x) == l0_sstables.end()) {
                new_l0.push_back(x);
            }
        }
        next_state->l0_sstables = std::move(new_l0);

        {
            std::unique_lock<std::shared_mutex> write_lock(state_mutex);
            state = next_state;
        }
        sync_dir();
        if (manifest) {
            manifest->add_record(&state_lock_guard, ManifestRecord::compaction(task, ids));
        }
    }

    for (size_t sst : l0_sstables) {
        std::remove(path_of_sst_val(sst).c_str());
    }
    for (size_t sst : l1_sstables) {
        std::remove(path_of_sst_val(sst).c_str());
    }
}

bool LsmStorageInner::trigger_compaction() {
    LsmStorageState snapshot;
    {
        std::shared_lock<std::shared_mutex> lock(state_mutex);
        snapshot = *state;
    }

    auto task_opt = compaction_controller.generate_compaction_task(snapshot);
    if (!task_opt.has_value()) {
        return false;
    }

    auto sstables = compact(*task_opt);
    std::vector<size_t> output;
    for (const auto& sst : sstables) {
        output.push_back(sst->id);
    }

    std::vector<std::shared_ptr<SsTable>> ssts_to_remove;
    {
        std::unique_lock<std::mutex> state_lock_guard(state_lock);
        std::shared_ptr<LsmStorageState> next_state = std::make_shared<LsmStorageState>(*state);

        std::vector<size_t> new_sst_ids;
        for (const auto& file_to_add : sstables) {
            new_sst_ids.push_back(file_to_add->id);
            next_state->sstables[file_to_add->id] = file_to_add;
        }

        auto [applied_state, files_to_remove] = compaction_controller.apply_compaction_result(
            *next_state, *task_opt, output, false);
        *next_state = std::move(applied_state);

        for (size_t file_id : files_to_remove) {
            auto it = next_state->sstables.find(file_id);
            if (it != next_state->sstables.end()) {
                ssts_to_remove.push_back(it->second);
                next_state->sstables.erase(it);
            }
        }

        {
            std::unique_lock<std::shared_mutex> write_lock(state_mutex);
            state = next_state;
        }
        sync_dir();
        if (manifest) {
            manifest->add_record(&state_lock_guard, ManifestRecord::compaction(*task_opt, new_sst_ids));
        }
    }

    for (const auto& sst : ssts_to_remove) {
        std::remove(path_of_sst_val(sst->id).c_str());
    }
    sync_dir();
    return true;
}

std::unique_ptr<std::thread> LsmStorageInner::spawn_compaction_thread(std::atomic<bool>& stop_flag) {
    if (options.compaction_options.type == CompactionOptions::Type::NoCompaction) {
        return nullptr;
    }
    auto self = shared_from_this();
    return std::make_unique<std::thread>([self, &stop_flag]() {
        while (!stop_flag.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (stop_flag.load()) break;
            try {
                self->trigger_compaction();
            } catch (const std::exception& e) {
                std::cerr << "compaction failed: " << e.what() << std::endl;
            }
        }
    });
}

bool LsmStorageInner::trigger_flush() {
    bool res = false;
    {
        std::shared_lock<std::shared_mutex> lock(state_mutex);
        res = state->imm_memtables.size() >= options.num_memtable_limit;
    }
    if (res) {
        force_flush_next_imm_memtable();
        return true;
    }
    return false;
}

std::unique_ptr<std::thread> LsmStorageInner::spawn_flush_thread(std::atomic<bool>& stop_flag) {
    auto self = shared_from_this();
    return std::make_unique<std::thread>([self, &stop_flag]() {
        while (!stop_flag.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (stop_flag.load()) break;
            try {
                self->trigger_flush();
            } catch (const std::exception& e) {
                std::cerr << "flush failed: " << e.what() << std::endl;
            }
        }
    });
}

} // namespace mini_lsm
