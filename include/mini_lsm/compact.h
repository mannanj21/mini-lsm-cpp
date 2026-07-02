#ifndef MINI_LSM_COMPACT_H
#define MINI_LSM_COMPACT_H

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>

namespace mini_lsm {

struct SimpleLeveledCompactionOptions {
    size_t size_ratio_percent{1};
    size_t level0_file_num_compaction_trigger{1};
    size_t max_levels{1};
};

struct LeveledCompactionOptions {
    size_t level_size_multiplier{10};
    size_t level0_file_num_compaction_trigger{1};
    size_t max_levels{4};
    size_t base_level_size_mb{10};
};

struct TieredCompactionOptions {
    size_t num_tiers{3};
    size_t max_size_amplification_percent{200};
    size_t size_ratio{1};
    size_t min_merge_width{2};
    std::optional<size_t> max_merge_width;
};

struct CompactionOptions {
    enum class Type {
        Leveled,
        Tiered,
        Simple,
        NoCompaction
    } type{Type::NoCompaction};

    std::variant<std::monostate, LeveledCompactionOptions, TieredCompactionOptions, SimpleLeveledCompactionOptions> options;

    static CompactionOptions leveled(LeveledCompactionOptions opts) {
        CompactionOptions co;
        co.type = Type::Leveled;
        co.options = std::move(opts);
        return co;
    }
    static CompactionOptions tiered(TieredCompactionOptions opts) {
        CompactionOptions co;
        co.type = Type::Tiered;
        co.options = std::move(opts);
        return co;
    }
    static CompactionOptions simple(SimpleLeveledCompactionOptions opts) {
        CompactionOptions co;
        co.type = Type::Simple;
        co.options = std::move(opts);
        return co;
    }
    static CompactionOptions no_compaction() {
        CompactionOptions co;
        co.type = Type::NoCompaction;
        co.options = std::monostate{};
        return co;
    }
};

struct SimpleLeveledCompactionTask {
    std::optional<size_t> upper_level;
    std::vector<size_t> upper_level_sst_ids;
    size_t lower_level{0};
    std::vector<size_t> lower_level_sst_ids;
    bool is_lower_level_bottom_level{false};
};

struct LeveledCompactionTask {
    std::optional<size_t> upper_level;
    std::vector<size_t> upper_level_sst_ids;
    size_t lower_level{0};
    std::vector<size_t> lower_level_sst_ids;
    bool is_lower_level_bottom_level{false};
};

struct TieredCompactionTask {
    std::vector<std::pair<size_t, std::vector<size_t>>> tiers;
    bool bottom_tier_included{false};
};

struct ForceFullCompactionTask {
    std::vector<size_t> l0_sstables;
    std::vector<size_t> l1_sstables;
};

struct CompactionTask {
    enum class Type {
        Leveled,
        Tiered,
        Simple,
        ForceFull
    } type{Type::Simple};

    std::variant<LeveledCompactionTask, TieredCompactionTask, SimpleLeveledCompactionTask, ForceFullCompactionTask> task;

    static CompactionTask simple(SimpleLeveledCompactionTask t) {
        CompactionTask ct;
        ct.type = Type::Simple;
        ct.task = std::move(t);
        return ct;
    }

    static CompactionTask leveled(LeveledCompactionTask t) {
        CompactionTask ct;
        ct.type = Type::Leveled;
        ct.task = std::move(t);
        return ct;
    }

    static CompactionTask tiered(TieredCompactionTask t) {
        CompactionTask ct;
        ct.type = Type::Tiered;
        ct.task = std::move(t);
        return ct;
    }

    static CompactionTask force_full(ForceFullCompactionTask t) {
        CompactionTask ct;
        ct.type = Type::ForceFull;
        ct.task = std::move(t);
        return ct;
    }

    bool compact_to_bottom_level() const {
        switch (type) {
            case Type::ForceFull: return true;
            case Type::Leveled: return std::get<LeveledCompactionTask>(task).is_lower_level_bottom_level;
            case Type::Simple: return std::get<SimpleLeveledCompactionTask>(task).is_lower_level_bottom_level;
            case Type::Tiered: return std::get<TieredCompactionTask>(task).bottom_tier_included;
        }
        return false;
    }
};

void to_json(nlohmann::json& j, const SimpleLeveledCompactionTask& t);
void from_json(const nlohmann::json& j, SimpleLeveledCompactionTask& t);
void to_json(nlohmann::json& j, const LeveledCompactionTask& t);
void from_json(const nlohmann::json& j, LeveledCompactionTask& t);
void to_json(nlohmann::json& j, const TieredCompactionTask& t);
void from_json(const nlohmann::json& j, TieredCompactionTask& t);
void to_json(nlohmann::json& j, const ForceFullCompactionTask& t);
void from_json(const nlohmann::json& j, ForceFullCompactionTask& t);
void to_json(nlohmann::json& j, const CompactionTask& t);
void from_json(const nlohmann::json& j, CompactionTask& t);

struct LsmStorageState;

class SimpleLeveledCompactionController {
public:
    explicit SimpleLeveledCompactionController(SimpleLeveledCompactionOptions options);

    std::optional<SimpleLeveledCompactionTask> generate_compaction_task(
        const LsmStorageState& snapshot) const;

    std::pair<LsmStorageState, std::vector<size_t>> apply_compaction_result(
        const LsmStorageState& snapshot,
        const SimpleLeveledCompactionTask& task,
        const std::vector<size_t>& output) const;

private:
    SimpleLeveledCompactionOptions options_;
};

class TieredCompactionController {
public:
    explicit TieredCompactionController(TieredCompactionOptions options);

    std::optional<TieredCompactionTask> generate_compaction_task(
        const LsmStorageState& snapshot) const;

    std::pair<LsmStorageState, std::vector<size_t>> apply_compaction_result(
        const LsmStorageState& snapshot,
        const TieredCompactionTask& task,
        const std::vector<size_t>& output) const;

private:
    TieredCompactionOptions options_;
};

class LeveledCompactionController {
public:
    explicit LeveledCompactionController(LeveledCompactionOptions options);

    std::optional<LeveledCompactionTask> generate_compaction_task(
        const LsmStorageState& snapshot) const;

    std::pair<LsmStorageState, std::vector<size_t>> apply_compaction_result(
        const LsmStorageState& snapshot,
        const LeveledCompactionTask& task,
        const std::vector<size_t>& output,
        bool in_recovery = false) const;

    std::vector<size_t> find_overlapping_ssts(
        const LsmStorageState& snapshot,
        const std::vector<size_t>& sst_ids,
        size_t in_level) const;

private:
    LeveledCompactionOptions options_;
};

class CompactionController {
public:
    enum class Type {
        Leveled,
        Tiered,
        Simple,
        NoCompaction
    } type{Type::NoCompaction};

    std::variant<std::monostate, LeveledCompactionController, TieredCompactionController, SimpleLeveledCompactionController> ctrl;

    static CompactionController leveled(LeveledCompactionController c) {
        CompactionController cc;
        cc.type = Type::Leveled;
        cc.ctrl = std::move(c);
        return cc;
    }
    static CompactionController tiered(TieredCompactionController c) {
        CompactionController cc;
        cc.type = Type::Tiered;
        cc.ctrl = std::move(c);
        return cc;
    }
    static CompactionController simple(SimpleLeveledCompactionController c) {
        CompactionController cc;
        cc.type = Type::Simple;
        cc.ctrl = std::move(c);
        return cc;
    }
    static CompactionController no_compaction() {
        CompactionController cc;
        cc.type = Type::NoCompaction;
        cc.ctrl = std::monostate{};
        return cc;
    }

    static CompactionController create(const CompactionOptions& options);

    std::optional<CompactionTask> generate_compaction_task(const LsmStorageState& snapshot) const;

    std::pair<LsmStorageState, std::vector<size_t>> apply_compaction_result(
        const LsmStorageState& snapshot,
        const CompactionTask& task,
        const std::vector<size_t>& output,
        bool in_recovery = false) const;

    bool flush_to_l0() const;
};

} // namespace mini_lsm

#endif // MINI_LSM_COMPACT_H
