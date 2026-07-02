#include "mini_lsm/compact.h"
#include "mini_lsm/lsm_storage.h"
#include <cassert>
#include <iostream>
#include <unordered_set>

namespace mini_lsm {

SimpleLeveledCompactionController::SimpleLeveledCompactionController(SimpleLeveledCompactionOptions options)
    : options_(std::move(options)) {}

std::optional<SimpleLeveledCompactionTask> SimpleLeveledCompactionController::generate_compaction_task(
    const LsmStorageState& snapshot) const {
    if (options_.max_levels == 0) {
        return std::nullopt;
    }

    std::vector<size_t> level_sizes;
    level_sizes.push_back(snapshot.l0_sstables.size());
    for (const auto& [lvl, files] : snapshot.levels) {
        level_sizes.push_back(files.size());
    }

    // Check L0 to L1 trigger
    if (snapshot.l0_sstables.size() >= options_.level0_file_num_compaction_trigger) {
        SimpleLeveledCompactionTask task;
        task.upper_level = std::nullopt;
        task.upper_level_sst_ids = snapshot.l0_sstables;
        task.lower_level = 1;
        task.lower_level_sst_ids = snapshot.levels.empty() ? std::vector<size_t>{} : snapshot.levels[0].second;
        task.is_lower_level_bottom_level = (1 == options_.max_levels);
        return task;
    }

    // Check size ratio trigger for levels 1 through max_levels - 1
    for (size_t i = 1; i < options_.max_levels; ++i) {
        size_t lower_level = i + 1;
        if (i >= level_sizes.size() || lower_level >= level_sizes.size()) {
            continue;
        }
        if (level_sizes[i] == 0) {
            continue;
        }
        double size_ratio = static_cast<double>(level_sizes[lower_level]) / static_cast<double>(level_sizes[i]);
        if (size_ratio < static_cast<double>(options_.size_ratio_percent) / 100.0) {
            SimpleLeveledCompactionTask task;
            task.upper_level = i;
            task.upper_level_sst_ids = snapshot.levels[i - 1].second;
            task.lower_level = lower_level;
            task.lower_level_sst_ids = snapshot.levels[lower_level - 1].second;
            task.is_lower_level_bottom_level = (lower_level == options_.max_levels);
            return task;
        }
    }

    return std::nullopt;
}

std::pair<LsmStorageState, std::vector<size_t>> SimpleLeveledCompactionController::apply_compaction_result(
    const LsmStorageState& snapshot,
    const SimpleLeveledCompactionTask& task,
    const std::vector<size_t>& output) const {
    LsmStorageState new_state = snapshot;
    std::vector<size_t> files_to_remove;

    if (task.upper_level.has_value()) {
        size_t upper_level = *task.upper_level;
        assert(task.upper_level_sst_ids == new_state.levels[upper_level - 1].second);
        files_to_remove.insert(files_to_remove.end(),
                               new_state.levels[upper_level - 1].second.begin(),
                               new_state.levels[upper_level - 1].second.end());
        new_state.levels[upper_level - 1].second.clear();
    } else {
        files_to_remove.insert(files_to_remove.end(),
                               task.upper_level_sst_ids.begin(),
                               task.upper_level_sst_ids.end());
        std::unordered_set<size_t> l0_ssts_compacted(task.upper_level_sst_ids.begin(), task.upper_level_sst_ids.end());
        std::vector<size_t> new_l0;
        for (size_t id : new_state.l0_sstables) {
            if (l0_ssts_compacted.find(id) == l0_ssts_compacted.end()) {
                new_l0.push_back(id);
            } else {
                l0_ssts_compacted.erase(id);
            }
        }
        assert(l0_ssts_compacted.empty());
        new_state.l0_sstables = std::move(new_l0);
    }

    assert(task.lower_level_sst_ids == new_state.levels[task.lower_level - 1].second);
    files_to_remove.insert(files_to_remove.end(),
                           new_state.levels[task.lower_level - 1].second.begin(),
                           new_state.levels[task.lower_level - 1].second.end());
    new_state.levels[task.lower_level - 1].second = output;

    return {std::move(new_state), std::move(files_to_remove)};
}

} // namespace mini_lsm
