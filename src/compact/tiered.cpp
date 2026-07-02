#include "mini_lsm/compact.h"
#include "mini_lsm/lsm_storage.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <unordered_map>

namespace mini_lsm {

TieredCompactionController::TieredCompactionController(TieredCompactionOptions options)
    : options_(std::move(options)) {}

std::optional<TieredCompactionTask> TieredCompactionController::generate_compaction_task(
    const LsmStorageState& snapshot) const {
    assert(snapshot.l0_sstables.empty() && "should not add l0 ssts in tiered compaction");
    if (snapshot.levels.size() < options_.num_tiers) {
        return std::nullopt;
    }

    // Compaction triggered by space amplification ratio
    size_t size = 0;
    for (size_t id = 0; id < snapshot.levels.size() - 1; ++id) {
        size += snapshot.levels[id].second.size();
    }
    double space_amp_ratio = (static_cast<double>(size) / static_cast<double>(snapshot.levels.back().second.size())) * 100.0;
    if (space_amp_ratio >= static_cast<double>(options_.max_size_amplification_percent)) {
        return TieredCompactionTask{snapshot.levels, true};
    }

    double size_ratio_trigger = (100.0 + static_cast<double>(options_.size_ratio)) / 100.0;
    // Compaction triggered by size ratio
    size = 0;
    for (size_t id = 0; id < snapshot.levels.size() - 1; ++id) {
        size += snapshot.levels[id].second.size();
        size_t next_level_size = snapshot.levels[id + 1].second.size();
        double current_size_ratio = static_cast<double>(next_level_size) / static_cast<double>(size);
        if (current_size_ratio > size_ratio_trigger && id + 1 >= options_.min_merge_width) {
            std::vector<std::pair<size_t, std::vector<size_t>>> tiers(
                snapshot.levels.begin(), snapshot.levels.begin() + id + 1);
            return TieredCompactionTask{std::move(tiers), false};
        }
    }

    // Trying to reduce sorted runs without respecting size ratio
    size_t num_tiers_to_take = std::min(snapshot.levels.size(), options_.max_merge_width.value_or(std::numeric_limits<size_t>::max()));
    std::vector<std::pair<size_t, std::vector<size_t>>> tiers(
        snapshot.levels.begin(), snapshot.levels.begin() + num_tiers_to_take);
    return TieredCompactionTask{
        std::move(tiers),
        snapshot.levels.size() >= num_tiers_to_take
    };
}

std::pair<LsmStorageState, std::vector<size_t>> TieredCompactionController::apply_compaction_result(
    const LsmStorageState& snapshot,
    const TieredCompactionTask& task,
    const std::vector<size_t>& output) const {
    assert(snapshot.l0_sstables.empty() && "should not add l0 ssts in tiered compaction");
    LsmStorageState new_state = snapshot;

    std::unordered_map<size_t, std::vector<size_t>> tier_to_remove;
    for (const auto& [x, y] : task.tiers) {
        tier_to_remove[x] = y;
    }

    std::vector<std::pair<size_t, std::vector<size_t>>> levels;
    bool new_tier_added = false;
    std::vector<size_t> files_to_remove;

    for (const auto& [tier_id, files] : snapshot.levels) {
        auto it = tier_to_remove.find(tier_id);
        if (it != tier_to_remove.end()) {
            assert(it->second == files && "file changed after issuing compaction task");
            files_to_remove.insert(files_to_remove.end(), it->second.begin(), it->second.end());
            tier_to_remove.erase(it);
        } else {
            levels.push_back({tier_id, files});
        }
        if (tier_to_remove.empty() && !new_tier_added) {
            new_tier_added = true;
            levels.push_back({output.empty() ? 0 : output[0], output});
        }
    }
    assert(tier_to_remove.empty() && "some tiers not found");

    new_state.levels = std::move(levels);
    return {std::move(new_state), std::move(files_to_remove)};
}

} // namespace mini_lsm
