#include "mini_lsm/compact.h"
#include "mini_lsm/lsm_storage.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <unordered_set>

namespace mini_lsm {

LeveledCompactionController::LeveledCompactionController(LeveledCompactionOptions options)
    : options_(std::move(options)) {}

std::vector<size_t> LeveledCompactionController::find_overlapping_ssts(
    const LsmStorageState& snapshot,
    const std::vector<size_t>& sst_ids,
    size_t in_level) const {
    bool first = true;
    KeyVec begin_key;
    KeyVec end_key;
    for (size_t id : sst_ids) {
        auto it = snapshot.sstables.find(id);
        if (it != snapshot.sstables.end() && it->second) {
            const auto& sst = it->second;
            if (first || sst->first_key < begin_key) {
                begin_key = sst->first_key;
            }
            if (first || sst->last_key > end_key) {
                end_key = sst->last_key;
            }
            first = false;
        }
    }
    if (first) {
        return {};
    }
    std::vector<size_t> overlap_ssts;
    if (in_level == 0 || in_level > snapshot.levels.size()) {
        return overlap_ssts;
    }
    for (size_t sst_id : snapshot.levels[in_level - 1].second) {
        auto it = snapshot.sstables.find(sst_id);
        if (it != snapshot.sstables.end() && it->second) {
            const auto& sst = it->second;
            const KeyVec& first_k = sst->first_key;
            const KeyVec& last_k = sst->last_key;
            if (!(last_k < begin_key || first_k > end_key)) {
                overlap_ssts.push_back(sst_id);
            }
        }
    }
    return overlap_ssts;
}

std::optional<LeveledCompactionTask> LeveledCompactionController::generate_compaction_task(
    const LsmStorageState& snapshot) const {
    std::vector<size_t> target_level_size(options_.max_levels, 0);
    std::vector<size_t> real_level_size(options_.max_levels, 0);
    size_t base_level = options_.max_levels;

    for (size_t i = 0; i < options_.max_levels && i < snapshot.levels.size(); ++i) {
        size_t sum_bytes = 0;
        for (size_t id : snapshot.levels[i].second) {
            auto it = snapshot.sstables.find(id);
            if (it != snapshot.sstables.end() && it->second) {
                sum_bytes += it->second->table_size();
            }
        }
        real_level_size[i] = sum_bytes;
    }

    size_t base_level_size_bytes = options_.base_level_size_mb * 1024 * 1024;
    if (options_.max_levels > 0) {
        target_level_size[options_.max_levels - 1] = std::max(real_level_size[options_.max_levels - 1], base_level_size_bytes);
        for (size_t i = options_.max_levels - 1; i-- > 0;) {
            size_t next_level_size = target_level_size[i + 1];
            size_t this_level_size = next_level_size / options_.level_size_multiplier;
            if (next_level_size > base_level_size_bytes) {
                target_level_size[i] = this_level_size;
            }
            if (target_level_size[i] > 0) {
                base_level = i + 1;
            }
        }
    }

    // Flush L0 SST is top priority
    if (snapshot.l0_sstables.size() >= options_.level0_file_num_compaction_trigger) {
        LeveledCompactionTask task;
        task.upper_level = std::nullopt;
        task.upper_level_sst_ids = snapshot.l0_sstables;
        task.lower_level = base_level;
        task.lower_level_sst_ids = find_overlapping_ssts(snapshot, snapshot.l0_sstables, base_level);
        task.is_lower_level_bottom_level = (base_level == options_.max_levels);
        return task;
    }

    std::vector<std::pair<double, size_t>> priorities;
    for (size_t level = 0; level < options_.max_levels; ++level) {
        if (target_level_size[level] > 0) {
            double prio = static_cast<double>(real_level_size[level]) / static_cast<double>(target_level_size[level]);
            if (prio > 1.0) {
                priorities.push_back({prio, level + 1});
            }
        }
    }

    if (priorities.empty()) {
        return std::nullopt;
    }

    std::sort(priorities.begin(), priorities.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
    });

    size_t level = priorities.front().second;
    if (level == 0 || level > snapshot.levels.size() || snapshot.levels[level - 1].second.empty()) {
        return std::nullopt;
    }

    size_t selected_sst = *std::min_element(snapshot.levels[level - 1].second.begin(), snapshot.levels[level - 1].second.end());
    LeveledCompactionTask task;
    task.upper_level = level;
    task.upper_level_sst_ids = {selected_sst};
    task.lower_level = level + 1;
    task.lower_level_sst_ids = find_overlapping_ssts(snapshot, {selected_sst}, level + 1);
    task.is_lower_level_bottom_level = (level + 1 == options_.max_levels);
    return task;
}

std::pair<LsmStorageState, std::vector<size_t>> LeveledCompactionController::apply_compaction_result(
    const LsmStorageState& snapshot,
    const LeveledCompactionTask& task,
    const std::vector<size_t>& output,
    bool in_recovery) const {
    LsmStorageState new_state = snapshot;
    std::vector<size_t> files_to_remove;

    std::unordered_set<size_t> upper_set(task.upper_level_sst_ids.begin(), task.upper_level_sst_ids.end());
    std::unordered_set<size_t> lower_set(task.lower_level_sst_ids.begin(), task.lower_level_sst_ids.end());

    if (task.upper_level.has_value()) {
        size_t upper_level = *task.upper_level;
        std::vector<size_t> new_upper;
        for (size_t x : new_state.levels[upper_level - 1].second) {
            if (upper_set.find(x) != upper_set.end()) {
                upper_set.erase(x);
            } else {
                new_upper.push_back(x);
            }
        }
        assert(upper_set.empty());
        new_state.levels[upper_level - 1].second = std::move(new_upper);
    } else {
        std::vector<size_t> new_l0;
        for (size_t x : new_state.l0_sstables) {
            if (upper_set.find(x) != upper_set.end()) {
                upper_set.erase(x);
            } else {
                new_l0.push_back(x);
            }
        }
        assert(upper_set.empty());
        new_state.l0_sstables = std::move(new_l0);
    }

    files_to_remove.insert(files_to_remove.end(), task.upper_level_sst_ids.begin(), task.upper_level_sst_ids.end());
    files_to_remove.insert(files_to_remove.end(), task.lower_level_sst_ids.begin(), task.lower_level_sst_ids.end());

    std::vector<size_t> new_lower;
    for (size_t x : new_state.levels[task.lower_level - 1].second) {
        if (lower_set.find(x) != lower_set.end()) {
            lower_set.erase(x);
        } else {
            new_lower.push_back(x);
        }
    }
    assert(lower_set.empty());
    new_lower.insert(new_lower.end(), output.begin(), output.end());

    if (!in_recovery) {
        std::sort(new_lower.begin(), new_lower.end(), [&](size_t x, size_t y) {
            auto it_x = snapshot.sstables.find(x);
            auto it_y = snapshot.sstables.find(y);
            if (it_x != snapshot.sstables.end() && it_y != snapshot.sstables.end() && it_x->second && it_y->second) {
                return it_x->second->first_key < it_y->second->first_key;
            }
            return x < y;
        });
    }

    new_state.levels[task.lower_level - 1].second = std::move(new_lower);
    return {std::move(new_state), std::move(files_to_remove)};
}

} // namespace mini_lsm
